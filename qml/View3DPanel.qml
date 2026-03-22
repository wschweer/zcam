//=============================================================================
//  wcam
//
//  Copyright (C) 2025/2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

import QtCore
import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import QtQuick.Controls.Material
import QtQuick.Layouts
// import ZCam

Item {
    id: panel
    visible: true
    focus: true

    signal positionChanged(real x, real y)
    property bool perspectiveCamera: false

    View3D {
        id: view3D
        anchors.fill: parent
        focus: true
        camera: panel.perspectiveCamera ? camera2 : camera1
        renderMode: View3D.Offscreen

        environment: SceneEnvironment {
//            clearColor: ZCam.backgroundColor
            backgroundMode: SceneEnvironment.Color
            antialiasingQuality: SceneEnvironment.VeryHigh
            }
        Settings {
//            property alias scale: root.scale
//            property alias rotation: root.eulerRotation
            }
        OrthographicCamera {
                id: camera1
                position: Qt.vector3d(0, 0, 1000)
                clipNear: 0.1
                clipFar: 10000;
                }
        PerspectiveCamera {
                id: camera2
                position: Qt.vector3d(0, 0, 1000)
                clipNear: 0.1       // zero does not work for perspective
                clipFar: 10000;
                }
        DirectionalLight {
            eulerRotation.x: -30
            eulerRotation.y: -70
            }

        DirectionalLight {
            eulerRotation.x: 20
            eulerRotation.y: 40
            }
        Node {
            id: root
            TestCube {}
            }
    }
    SpaceMouse {
        onRotate: v => {
            var r = root.eulerRotation;
            r.x -= v.x;
            r.y += v.y;
            r.z -= v.z;
        }
        onTranslate: v => {
            var mag = 0.3

            let delta = Qt.vector3d(0, 0, 0);
            delta.x   = (v.x / mag) * 10.0;
            delta.y   = (-v.y / mag) * 10.0;
            delta.z   = v.z * -0.1;

            let velocity = Qt.vector3d(0, 0, 0);
            // X Movement
            let xDirection = root.right;
            velocity = velocity.plus(Qt.vector3d(xDirection.x * delta.x, xDirection.y * delta.x, xDirection.z * delta.x));

            // Y Movement
            let yDirection = root.up;
            velocity = velocity.plus(Qt.vector3d(yDirection.x * delta.y, yDirection.y * delta.y, yDirection.z * delta.y));

//            console.log("velocity "+velocity)
            root.position = root.position.plus(velocity);

            root.scale = root.scale.times(1.0 + delta.z);
        }
    }

    component TButton : Button  {
        hoverEnabled: true
        flat: true
        icon.color: "transparent"
        icon.width: ZCam.style.iconSize
        icon.height: ZCam.style.iconSize
        }

    component RButton : Button  {
        id: button
        hoverEnabled: true
        flat: true
        checkable: true
        autoExclusive: true
        icon.color: "transparent"
        icon.width: ZCam.style.iconSize
        icon.height: ZCam.style.iconSize
        background: Rectangle {
            border.width: 2
            border.color: "blue"
            anchors.fill: parent
            visible: button.checked
            color: "transparent"
            radius: 5
            }
        }

    //-----------------------------------------------------
    //  MouseArea
    //-----------------------------------------------------

    MouseArea  {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        property real xSpeed: 0.05
        property real ySpeed: 0.05
        property vector2d lastPos: Qt.vector2d(0, 0)
        property vector3d eLastPos: Qt.vector3d(0, 0, 0)
        property vector3d pos3d: Qt.vector3d(0, 0, 0)       // current position on xy plane
        property real frameDelta: 10;
        property Node curNode: null
        acceptedButtons: Qt.AllButtons

        function pan(delta) {
            // Zugriff auf die aktuell aktive Kamera im View3D
            var cam = view3D.camera;
            var up  = cam.up;
            var right = cam.right;
            var unitsPerPixel = 1.0;

            // --- UNTERSCHEIDUNG DER KAMERA-TYPEN ---

            // Prüfen, ob es eine PerspectiveCamera ist (hat 'fieldOfView')
            if (cam.fieldOfView !== undefined) {
                var fovRad = cam.fieldOfView * (Math.PI / 180);

                // Berechnung der sichtbaren Höhe auf der aktuellen Tiefe der Kamera
                // (Annahme: wir pannen auf der Ebene z=0, Distanz ist also cam.z)
                // Falls die Kamera rotiert ist, ist cam.z eine Annäherung, für perfektes Panning
                // auf einer schiefen Ebene bräuchte man Raycasting. Für Standard-Pan reicht cam.z.
                var distance = cam.z;

                // Schutz vor negativen Distanzen (falls Kamera hinter dem Objekt ist)
                distance = Math.abs(distance);

                var viewHeightAtDepth = 2 * distance * Math.tan(fovRad / 2);

                unitsPerPixel = viewHeightAtDepth / panel.height;
                }
            // Andernfalls: OrthographicCamera
            else {
                // Bei Ortho bestimmt nur der 'scale' (Zoom) das Verhältnis.
                // Standard: scale 1.0 => 1 Pixel = 1 Unit
                // Wir nehmen cam.scale oder 1.0 als Fallback, falls undefined
                var camScale = (cam.scale) ? cam.scale.y : 1.0;

                // Vermeidung von Division durch Null
                if (camScale === 0) camScale = 0.001;

                unitsPerPixel = 1.0 / camScale;
                }

            // --- BEWEGUNG AUSFÜHREN ---

            // WICHTIG: delta.y ist positiv nach unten (Screen),
            // aber im 3D-Raum ist Y oft positiv nach oben.
            // Um das Bild "unter der Maus zu greifen", bewegen wir die Kamera entgegengesetzt.

            var moveX = -delta.x * unitsPerPixel;
            var moveY =  delta.y * unitsPerPixel;
            var moveVec = right.times(moveX).plus(up.times(moveY));

            // Falls du stattdessen die KAMERA bewegen willst (klassisches Panning):
            cam.position = cam.position.minus(moveVec);
            }

        onWheel: (mouse) => {
            if (mouse.modifiers != Qt.ControlModifier)
                return;

            // view3D.pick() liefert die Position in Szene-Koordinaten (Parent-Space von root).
            // Um den Punkt unter dem Cursor beim Zoomen stabil zu halten, gilt:
            //
            //   Szene-Pos eines lokalen Punktes L:
            //     S = root.position + Rotation(root.scale * L)
            //
            //   Nach Skalierung mit Faktor sd soll S gleich bleiben:
            //     root.position_new + sd * (S - root.position) = S
            //   →  root.position_new = root.position + (1 - sd) * (S - root.position)
            //
            //   Die Rotation kürzt sich heraus → Formel gilt für beliebige root.eulerRotation.

            // Cursor-Position in Szene-Koordinaten ermitteln.
            // Primär: pick() gegen das Grid-Modell (exakt).
            // Fallback: screenToScene() projiziert auf die XY-Ebene (z=0 in Root-Local-Space).

            var cursorScenePos;
//            var result = view3D.pick(mouse.x, mouse.y, grid);
//            if (result && result.hitType !== PickResult.Null) {
//                cursorScenePos = result.position;
//                }
//            else {
                var localPos = screenToScene(mouse.x, mouse.y);
                if (!localPos)
                    return;
                // screenToScene liefert Root-lokale Koordinaten → in Szene-Koordinaten umrechnen
                cursorScenePos = root.mapPositionToScene(localPos);
//                }

            var sd = (mouse.angleDelta.y > 0.0) ? 1.2 : 0.8;
            root.scale    = root.scale.times(sd);
            // Korrektur: root.position so verschieben, dass cursorScenePos auf dem Bildschirm bleibt
            root.position = root.position.plus(cursorScenePos.minus(root.position).times(1.0 - sd));
            }

        onPressed: (mouse) => {
            panel.focus = true
            lastPos  = Qt.vector2d(mouse.x, mouse.y)
            eLastPos = screenToScene(mouse.x, mouse.y)
            if (!curNode) {
                curNode  = pickModel(mouse.x, mouse.y)
//                ZCam.mousePress(curNode ? curNode.element : null, mouse.buttons, mouse.modifiers, eLastPos.x, eLastPos.y)
                }
            else {
//                ZCam.mousePress(null, mouse.buttons, mouse.modifiers, eLastPos.x, eLastPos.y)
//                curNode = base.searchBase(ZCam.hoverModel)
//                console.log("=====curNode=="+curNode+"===="+ZCam.hoverModel);
                }
            }

        onDoubleClicked: (mouse) => {
//            console.log("double click");
            var m = pickModel(mouse.x, mouse.y)
//            if (m && m.element)
//                ZCam.doubleClick(m.element);
            }

        onReleased: (mouse) => {
//            ZCam.mouseRelease();
//            if (ZCam.currentTool != "line")
//                curNode = null
            }

        function pickModel(x, y) {
            var list = view3D.pickAll(x, y);
            var area = null
            var hit = null
            var pickLevel = 0
            for (const result of list) {
                if (result.hitType == PickResult.Model) {
                    if (result.objectHit.element) {
                        //
                        // look for the smallest model
                        //
                        var l = result.objectHit.element.pickLevel
                        var b = result.objectHit.bounds
                        var a = (b.maximum.x - b.minimum.x) * (b.maximum.y - b.minimum.y);
                        if (l > pickLevel || (l == pickLevel && a != 0 && (a < area))) {
                            hit = result.objectHit
                            area = a;
                            pickLevel = l;
                            }
                        }
                    }
                }
            return hit
            }

        //---------------------------------------------------------
        //   screenToScene
        //  returns the scene position for mouse position x/y
        //  projected on the xy plane
        //---------------------------------------------------------

        function screenToScene(x, y) {
            let normX = x / view3D.width;
            let normY = y / view3D.height;
            let nearPos = root.mapPositionFromScene(view3D.camera.mapFromViewport(Qt.vector3d(normX, normY, 0)));
            let farPos =  root.mapPositionFromScene(view3D.camera.mapFromViewport(Qt.vector3d(normX, normY, 1)));

            let direction = farPos.minus(nearPos).normalized();
            if (Math.abs(direction.z) > 0.0001) {
                let t = (0 - nearPos.z) / direction.z;
                let r = nearPos.plus(direction.times(t));
                return r;
                }
            console.log("overflow: no screen position");
            return null
            }

        onPositionChanged: (mouse) => {
            pos3d = screenToScene(mouse.x, mouse.y);
            panel.positionChanged(pos3d.x, pos3d.y)
//            var result     = view3D.pick(mouse.x, mouse.y, grid);
            var currentPos = Qt.vector2d(mouse.x, mouse.y);
            var delta      = Qt.vector2d(lastPos.x - currentPos.x, lastPos.y - currentPos.y);

            if ((mouse.buttons == Qt.RightButton) && (mouse.modifiers == Qt.NoModifier)) {
                //*************
                //  rotate
                //*************
                var rotationVector = root.eulerRotation;

                // rotate x
                var rotateX = -delta.x * mouseArea.xSpeed * frameDelta;
                rotationVector.y += rotateX;

                // rotate y
                var rotateY = delta.y * -mouseArea.ySpeed * frameDelta;
                rotationVector.x += rotateY;
                root.setEulerRotation(rotationVector);
                lastPos = currentPos;
                }
            else if ((mouse.buttons == Qt.MiddleButton) && (mouse.modifiers == Qt.NoModifier)) {
                //*************
                //  pan
                //*************
                pan(delta)
                lastPos = currentPos;
                }
            else if ((mouse.buttons == Qt.LeftButton) && (mouse.modifiers == Qt.NoModifier)) {
                //*******************************
                //  drag model curNode
                //*******************************
                if (curNode) {
                    var eDelta = pos3d.minus(eLastPos);
                    eLastPos = pos3d
//                    ZCam.dragged(curNode.element, eDelta, mouse.modifiers);
                    }
                }
            else {
                // in "line" editmode the handle can be dragged without any mouse button pressed:
/*
                if (ZCam.currentTool == "line" && curNode) {
                    var eDelta = pos3d.minus(eLastPos);
                    //if (mouse.modifiers == Qt.ShiftModifier)

                    eLastPos = pos3d
                    ZCam.dragged(curNode.element, eDelta, mouse.modifiers);
                    }
                else {
                    //*************
                    //  hover
                    //*************
                    var m = pickModel(mouse.x, mouse.y)
                    ZCam.hover(m ? m.element : null);
                    }
*/
                }
            }
        }

    ToolRow {
        id: toolRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 10
        anchors.leftMargin: 10

        TButton {
            icon.source: "qrc:////icons/view-top.svg"
            onClicked: { root.eulerRotation = Qt.vector3d(0, 0, 0) }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-bottom.svg"
            onClicked: { root.eulerRotation = Qt.vector3d(180, 0, 0) }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-front.svg"
            onClicked: { root.eulerRotation = Qt.vector3d(90, 0, 0) }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-rear.svg"
            onClicked: { root.eulerRotation = Qt.vector3d(-90, 0, 0) }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-left.svg"
            onClicked: { root.eulerRotation = Qt.vector3d(0, 90, 0) }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-right.svg"
            onClicked: { root.eulerRotation = Qt.vector3d(0, -90, 0) }
            z: 1
            }
        RButton {
            id: iCamera
            checkable: true
            checked: true
            icon.source: "qrc:////icons/view-isometric.svg"
            onClicked: {
                panel.perspectiveCamera = false;
                pCamera.checked = false
                }
            ToolTip.text: qsTr("Isometric Projection")
            ToolTip.delay: 1000
            ToolTip.timeout: 4000
            ToolTip.visible: hovered
            z: 1
            }
        RButton {
            id: pCamera
            checkable: true
            checked: false
            icon.source: "qrc:////icons/view-perspective.svg"
            onClicked: {
                panel.perspectiveCamera = true
                iCamera.checked = false
                }
            ToolTip.text: qsTr("Perspective Projection")
            ToolTip.delay: 1000
            ToolTip.timeout: 4000
            ToolTip.visible: hovered
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-fullscreen.svg"
            onClicked: {
                root.eulerRotation = Qt.vector3d(0, 0, 0)
                root.scale         = Qt.vector3d(5.0, 5.0, 5.0)
                root.position      = Qt.vector3d(0.0, 0.0, 0.0)
                }
            z: 1
            }
        }

/*
    ToolColumn {
        anchors.top: parent.top
        anchors.topMargin: toolRow.height + 20
        anchors.leftMargin: 10
        anchors.left: parent.left

        RButton {
            icon.source: "qrc:////icons/select.svg"
            checked: ZCam.currentTool == "pointer"
            onCheckedChanged: if (checked) ZCam.currentTool = "pointer";
            z: 1
            }
        RButton {
            icon.source: "qrc:////icons/Draft_Line.svg"
            checked: ZCam.currentTool == "line"
            onCheckedChanged: if (checked) ZCam.currentTool = "line";
            z: 1
            }
        RButton {
            icon.source: "qrc:////icons/Draft_Polygon.svg"
            checked: ZCam.currentTool == "polygon"
            onCheckedChanged: if (checked) ZCam.currentTool = "polygon";
            }
        RButton {
            icon.source: "qrc:////icons/Draft_Rectangle.svg"
            checked: ZCam.currentTool == "rectangle"
            onCheckedChanged: if (checked) ZCam.currentTool = "rectangle";
            }
        RButton {
            icon.source: "qrc:////icons/Draft_Circle.svg"
            checked: ZCam.currentTool == "circle"
            onCheckedChanged: if (checked) ZCam.currentTool = "circle";
            }
        RButton {
            icon.source: "qrc:////icons/Draft_Text.svg"
            checked: ZCam.currentTool == "text"
            onCheckedChanged: if (checked) ZCam.currentTool = "text";
            }
        }
*/
    }
