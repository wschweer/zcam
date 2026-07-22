//=============================================================================
//  ZCam
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

import QtCore
import QtQuick
import QtQuick3D
import QtQuick.Controls
import QtQuick.Controls.Material
import ZCam

Item {
    id: panel
    visible: true
    focus: true

    Material.theme: Material.Dark
    Material.accent: Material.Teal

    signal positionChanged(real x, real y)
    property bool perspectiveCamera: false

    // Polygon drawing state
    property var _drawingPolygon: null   // the Polygon being drawn (null when idle)

    // Text editing state
    property var _editingText: null      // the Text element being edited (null when idle)

    // Forward key events to the text element being edited.
    // Returns true if the event was consumed.
    function handleTextKey(event) {
        if (!_editingText)
            return false;
        if (event.key === Qt.Key_Escape) {
            _editingText.setEditing(false);
            _editingText = null;
            ZCam.currentTool = "pointer";
            event.accepted = true;
            return true;
            }
        // Navigation and editing keys
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Up || event.key === Qt.Key_Down || event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace || event.key === Qt.Key_Return) {
            _editingText.keyEvent(event.key, event.modifiers, "");
            event.accepted = true;
            return true;
            }
        // Printable character
        if (event.text && event.text.length > 0 && event.key !== Qt.Key_Backspace) {
            _editingText.keyEvent(event.key, event.modifiers, event.text);
            event.accepted = true;
            return true;
            }
        return false;
        }

    //─────────────────────────────────────────────────────────────
    //  Vertex handle management
    //─────────────────────────────────────────────────────────────

    // The component for a single vertex handle sphere.
    // When the mouse hovers over a handle, _hovered is set to true
    // and the baseColor becomes lighter (brighter orange/yellow).
    // Bezier control-point handles are rendered in dark red.
    Component {
        id: handleComponent
        Model {
            property int _vertexIndex: -1
            property var _poly: null
            property bool _hovered: false
            property bool _isControlPoint: false
            // Only show handles when the associated element is visible.
            visible: _poly ? (_poly.show && _poly.ancestorsShow) : false
            source: "#Sphere"
            // Compensate for root.scale so handles keep a constant
            // on-screen size regardless of scene zoom level.
            scale: {
                var s = ZCam.config ? ZCam.config.handleSize : 0.02;
                var rs = root.scale;
                var sx = rs.x !== 0 ? s / rs.x : s;
                var sy = rs.y !== 0 ? s / rs.y : s;
                var sz = rs.z !== 0 ? s / rs.z : s;
                return Qt.vector3d(sx, sy, sz);
                }
            pickable: true
            materials: [
                PrincipledMaterial {
                    cullMode: PrincipledMaterial.NoCulling
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: _isControlPoint ? (_hovered ? Qt.rgba(0.9, 0.3, 0.3, 1.0)   // lighter red (hovered control point)
                        : Qt.rgba(0.6, 0.0, 0.0, 1.0))  // dark red (control point)
                    : (_hovered ? Qt.rgba(1.0, 0.75, 0.3, 1.0)   // lighter orange (hovered)
                        : Qt.rgba(1.0, 0.4, 0.0, 1.0))  // orange (normal)
                    }
            ]
            }
        }

    // Tracks the current polygon and its handles.
    property var _handlePolygon: null
    property var _handleList: []
    property var _hoveredHandle: null   // currently hovered vertex handle Model

    // Rebuild all vertex handles.  Called when the current element
    // changes (selection) or when the scene graph is rebuilt.
    function rebuildVertexHandles() {
        // destroy old handles
        for (var i = 0; i < _handleList.length; ++i) {
            if (_handleList[i])
                _handleList[i].destroy();
            }
        _handleList = [];
        _handlePolygon = null;
        _hoveredHandle = null;

        var poly = ZCam.currentElement;
        if (!poly || !poly.hasHandles)
            return;

        // Defer to allow ProjectTree to rebuild the scene graph first
        Qt.callLater(doRebuildVertexHandles);
        }

    function doRebuildVertexHandles() {
        var poly = ZCam.currentElement;
        if (!poly || !poly.hasHandles)
            return;
        _handlePolygon = poly;

        var n = poly.vertexCount();
        for (var idx = 0; idx < n; ++idx) {
            if (!poly.isVertex(idx))
                continue;
            var p = poly.vertexWorldPos(idx);
            var handle = handleComponent.createObject(root, {
                "position": Qt.vector3d(p.x, p.y, p.z),
                "_vertexIndex": idx,
                "_poly": poly,
                "_isControlPoint": poly.isControlPoint(idx)
                });
            _handleList.push(handle);
            }
        }

    // Update handle positions without rebuilding.  Called during
    // drag and undo/redo when vertexRevision changes.
    function updateVertexHandlePositions() {
        if (!_handlePolygon)
            return;
        var poly = _handlePolygon;
        for (var i = 0; i < _handleList.length; ++i) {
            var h = _handleList[i];
            if (!h)
                continue;
            var p = poly.vertexWorldPos(h._vertexIndex);
            if (p)
                h.position = Qt.vector3d(p.x, p.y, p.z);
            }
        }

    // Update handle hover state.  Called from MouseArea onPositionChanged
    // when no button is pressed (pure hover).  Highlights the handle
    // currently under the mouse cursor.
    function updateHandleHover(x, y) {
        var newHover = mouseArea.pickVertexHandle(x, y);
        if (newHover === _hoveredHandle)
            return;
        if (_hoveredHandle)
            _hoveredHandle._hovered = false;
        if (newHover)
            newHover._hovered = true;
        _hoveredHandle = newHover;
        }

    // Finish the current polygon drawing session.
    // Closes the polygon and resets drawing state.
    function finishPolygonDrawing() {
        if (!_drawingPolygon)
            return;
        _drawingPolygon.finishDrawing();
        _drawingPolygon = null;
        rebuildVertexHandles();
        }

    // Convert a world (root) position to local polygon coordinates.
    // For new polygons, pos is in root space and scale/rot are identity,
    // so local = world - pos.
    function worldToPolygonLocal(poly, worldPos) {
        return Qt.vector3d(worldPos.x - poly.pos.x, worldPos.y - poly.pos.y, 0);
        }

    //=========================================================
    //  Background View3D — renders only the Grid element so
    //  the grid is always rendered behind all other 3D geometry
    //  regardless of z-stacking within the main scene.
    //=========================================================
    View3D {
        id: backgroundView
        anchors.fill: parent

        camera: panel.perspectiveCamera ? bgCameraPerspective : bgCameraOrtho

        environment: SceneEnvironment {
            clearColor: Material.color(Material.BlueGrey, Material.Shade500)
            backgroundMode: SceneEnvironment.Color
            antialiasingQuality: SceneEnvironment.VeryHigh
            }

        OrthographicCamera {
            id: bgCameraOrtho
            // Bind directly to the main camera so the background
            // (grid) layer is always perfectly in sync with the
            // interactive scene — including during initialization
            // when Settings restores the persisted camera position.
            // Using a binding instead of Connections avoids the
            // timing issue where the Connections target (camera1)
            // does not exist yet when the background View3D is
            // created, causing the initial positionChanged signal
            // to be missed.
            position: camera1.position
            clipNear: 0.1
            clipFar: 10000
            }
        PerspectiveCamera {
            id: bgCameraPerspective
            // See comment above for bgCameraOrtho.
            position: camera2.position
            clipNear: 0.1
            clipFar: 10000
            }

        Node {
            id: bgRoot
            // Mirror the main root node transform so the grid
            // stays in sync with the scene camera.
            position: root.position
            eulerRotation: root.eulerRotation
            scale: root.scale

            // Grid model — only visible when a Grid element exists
            // and config.showGrid is true.
            GridShape {
                id: gridShape
                visible: ZCam.project && ZCam.project.gridElement && ZCam.config.showGrid && ZCam.project.gridElement.show
                element: ZCam.project ? ZCam.project.gridElement : null
                }
            }

        // Background cameras are kept in sync with the main cameras
        // via direct property bindings (position: camera1.position /
        // position: camera2.position) declared on the camera objects
        // above.  This replaces the previous Connections-based
        // approach which missed the initial sync when Settings
        // restored camera positions during component initialization.
        }

    View3D {
        id: view3D
        anchors.fill: parent
        focus: true
        camera: panel.perspectiveCamera ? camera2 : camera1
        renderMode: View3D.Offscreen

        // Handle Delete key directly on the View3D because the
        // offscreen render mode can bypass the QML Shortcut event
        // filter installed on the window.
        Keys.onPressed: function (event) {
            // If we are editing a text element, forward all key events
            // to the text element first.
            if (panel._editingText) {
                if (panel.handleTextKey(event))
                    return;
                }
            if (event.key === Qt.Key_Delete) {
                ZCam.deleteCurrentElement();
                event.accepted = true;
                }
            if (event.key === Qt.Key_Escape) {
                finishPolygonDrawing();
                ZCam.currentTool = "pointer";
                event.accepted = true;
                }
            if (event.key === Qt.Key_P) {
                ZCam.centerOnWorkspace(ZCam.currentElement);
                event.accepted = true;
                }
            }

        environment: SceneEnvironment {
            clearColor: "transparent"
            backgroundMode: SceneEnvironment.Transparent
            antialiasingQuality: SceneEnvironment.VeryHigh
            }
        Settings {
            id: viewSettings
            category: "View3D"
            property alias scale: root.scale
            property alias rootPosition: root.position
            property alias projection: panel.perspectiveCamera
            property alias position1: camera1.position
            property alias position2: camera2.position
            property vector3d rotation
            Component.onCompleted: root.eulerRotation = rotation
            }
        OrthographicCamera {
            id: camera1
            position: Qt.vector3d(0, 0, 1000)
            clipNear: 0.1
            clipFar: 10000
            }
        PerspectiveCamera {
            id: camera2
            position: Qt.vector3d(0, 0, 1000)
            clipNear: 0.1
            clipFar: 10000
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
            onEulerRotationChanged: viewSettings.rotation = eulerRotation
            ProjectTree {
                id: projectTree
                }
            }
        }

    // Rebuild handles when the current element changes.
    Connections {
        target: ZCam
        function onCurrentElementChanged() {
            // If a text element was being edited and the selection moved
            // to a different element, exit editing mode so the cursor
            // and editing bounding box disappear.
            if (panel._editingText && panel._editingText !== ZCam.currentElement) {
                panel._editingText.setEditing(false);
                panel._editingText = null;
                ZCam.currentTool = "pointer";
                }
            rebuildVertexHandles();
            }
        }

    // Rebuild handles when the handle size changes in config.
    Connections {
        target: ZCam.config
        function onHandleSizeChanged() {
            rebuildVertexHandles();
            }
        ignoreUnknownSignals: true
        }

    // Update handle positions when vertex revision changes (drag/undo).
    // Use _handlePolygon which is a var so QML resolves the signal dynamically.
    Connections {
        target: _handlePolygon
        function onVertexRevisionChanged() {
            updateVertexHandlePositions();
            }
        ignoreUnknownSignals: true
        }

    // Reset the 3D canvas camera and root node to their default
    // positions.  Called whenever a new project is created (including
    // Material-Test and Galvo-Test) so the user starts with a clean
    // view instead of inheriting the previous project's zoom/pan/rotation.
    function resetCamera() {
        root.eulerRotation = Qt.vector3d(0, 0, 0);
        root.scale = Qt.vector3d(5.0, 5.0, 5.0);
        root.position = Qt.vector3d(0.0, 0.0, 0.0);
        // Center the workspace of the current machine on the canvas.
        // The workspace center in local coords is (maxTravel.x/2, maxTravel.y/2).
        // Because root applies a scale, the center in world (camera) coords is
        // (cx * root.scale.x, cy * root.scale.y).  We move the camera to that
        // point so the center appears at screen center.
        // If no machine is present, the camera is reset to (0, 0, 1000).
        var machine = (ZCam.project && ZCam.project.machine) ? ZCam.project.machine : null;
        if (machine) {
            var travel = machine.maxTravel;
            var cx = travel.x / 2.0;
            var cy = travel.y / 2.0;
            camera1.position = Qt.vector3d(cx * root.scale.x, cy * root.scale.y, 1000);
            camera2.position = Qt.vector3d(cx * root.scale.x, cy * root.scale.y, 1000);
            } else {
            camera1.position = Qt.vector3d(0, 0, 1000);
            camera2.position = Qt.vector3d(0, 0, 1000);
            }
        // Persist the reset values into Settings so they survive
        // the next application restart.
        viewSettings.rotation = Qt.vector3d(0, 0, 0);
        }

    // Reset the 3D camera when a new project is created.
    // This covers newProject(), createMaterialTest() and
    // createGalvoTest() — all emit projectCreated() via
    // ZCam::endNewProject().
    Connections {
        target: ZCam
        function onProjectCreated() {
            resetCamera();
            }
        }

    Component.onCompleted: {
        rebuildVertexHandles();
        // Sync camera button states with the persisted perspectiveCamera value.
        // The Settings component restores perspectiveCamera before this
        // handler runs (inner Component.onCompleted runs first), so the
        // value is already correct at this point.
        iCamera.checked = !panel.perspectiveCamera;
        pCamera.checked = panel.perspectiveCamera;
        }

    SpaceMouse {
        onRotate: v => {
            var r = root.eulerRotation;
            r.x -= v.x;
            r.y += v.y;
            r.z -= v.z;
            root.eulerRotation = r;
            }
        onTranslate: v => {
            var mag = 0.3;

            let delta = Qt.vector3d(0, 0, 0);
            delta.x = (v.x / mag) * 10.0;
            delta.y = (-v.y / mag) * 10.0;
            delta.z = v.z * -0.1;

            let velocity = Qt.vector3d(0, 0, 0);
            let xDirection = root.right;
            velocity = velocity.plus(Qt.vector3d(xDirection.x * delta.x, xDirection.y * delta.x, xDirection.z * delta.x));

            let yDirection = root.up;
            velocity = velocity.plus(Qt.vector3d(yDirection.x * delta.y, yDirection.y * delta.y, yDirection.z * delta.y));

            root.position = root.position.plus(velocity);
            root.scale = root.scale.times(1.0 + delta.z);
            }
        }

    component TButton: Button {
        hoverEnabled: true
        flat: true
        icon.color: "transparent"
        icon.width: ZCam.config.iconSize
        icon.height: ZCam.config.iconSize
        }

    component RButton: Button {
        id: button
        hoverEnabled: true
        flat: true
        checkable: true
        autoExclusive: true
        icon.color: "transparent"
        icon.width: ZCam.config.iconSize
        icon.height: ZCam.config.iconSize
        background: Rectangle {
            border.width: 2
            border.color: Material.accentColor
            anchors.fill: parent
            visible: button.checked
            color: "transparent"
            radius: 5
            }
        }

    //-----------------------------------------------------
    //  MouseArea
    //-----------------------------------------------------

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        property real xSpeed: 0.05
        property real ySpeed: 0.05
        property vector2d lastPos: Qt.vector2d(0, 0)
        property vector3d eLastPos: Qt.vector3d(0, 0, 0)
        property vector3d pos3d: Qt.vector3d(0, 0, 0)
        property real frameDelta: 10
        property var curNode: null
        property variant vertexDragHandle: null
        acceptedButtons: Qt.AllButtons

        function pan(delta) {
            var cam = view3D.camera;
            var up = cam.up;
            var right = cam.right;
            var unitsPerPixel = 1.0;

            if (cam.fieldOfView !== undefined) {
                var fovRad = cam.fieldOfView * (Math.PI / 180);
                var distance = cam.z;
                distance = Math.abs(distance);
                var viewHeightAtDepth = 2 * distance * Math.tan(fovRad / 2);
                unitsPerPixel = viewHeightAtDepth / panel.height;
                } else {
                var camScale = (cam.scale) ? cam.scale.y : 1.0;
                if (camScale === 0)
                    camScale = 0.001;
                unitsPerPixel = 1.0 / camScale;
                }

            var moveX = -delta.x * unitsPerPixel;
            var moveY = delta.y * unitsPerPixel;
            var moveVec = right.times(moveX).plus(up.times(moveY));
            cam.position = cam.position.minus(moveVec);
            }

        // Step sizes for mouse-wheel scaling/zooming:
        //   Ctrl+wheel  → bigStep    (large step, ~20%)
        //   plain wheel → step       (normal step, ~10%)
        //   Shift+wheel → smallStep  (fine step, ~2%)
        property real wheelBigStep: 1.2
        property real wheelStep: 1.1
        property real wheelSmallStep: 1.02

        onWheel: mouse => {
            // Determine the scale delta based on modifier:
            //   Ctrl+wheel  → bigStep    (large step)
            //   plain wheel → step       (normal step)
            //   Shift+wheel → smallStep  (fine step)
            var sd;
            if (mouse.modifiers & Qt.ControlModifier)
                sd = (mouse.angleDelta.y > 0.0) ? wheelBigStep : (1.0 / wheelBigStep);
            else if (mouse.modifiers & Qt.ShiftModifier)
                sd = (mouse.angleDelta.y > 0.0) ? wheelSmallStep : (1.0 / wheelSmallStep);
            else
                sd = (mouse.angleDelta.y > 0.0) ? wheelStep : (1.0 / wheelStep);

            // If a visible canvas element is selected, scale the element
            // instead of the 3D view.  Only elements that are visible on
            // the canvas (Text, Polygon, Ellipse, Rectangle etc.) and
            // actually shown (show == true and ancestors visible) are
            // eligible for element scaling.
            //
            // The scale center is the current mouse position in scene
            // coordinates, analogous to how the canvas zoom keeps the
            // cursor position fixed.
            var el = ZCam.currentElement;
            if (el && el.visible() && el.show && el.ancestorsShow && el.draggable) {
                var pivot = screenToScene(mouse.x, mouse.y);
                if (!pivot)
                    return;
                var scaleFactor = Qt.vector3d(sd, sd, sd);
                ZCam.startElementDrag(el);
                ZCam.scaled(el, scaleFactor, mouse.modifiers, pivot);
                ZCam.endElementDrag();
                return;
                }

            // No suitable element selected — zoom the 3D view.
            var cursorScenePos;
            var localPos = screenToScene(mouse.x, mouse.y);
            if (!localPos)
                return;
            cursorScenePos = root.mapPositionToScene(localPos);

            root.scale = root.scale.times(sd);
            root.position = root.position.plus(cursorScenePos.minus(root.position).times(1.0 - sd));
            }

        onPressed: mouse => {
            panel.focus = true;
            lastPos = Qt.vector2d(mouse.x, mouse.y);
            eLastPos = screenToScene(mouse.x, mouse.y);
            if (mouse.button == Qt.LeftButton) {
                // If a text element is being edited, a click inside
                // the text bounding box moves the cursor to that
                // position. A click outside exits editing mode.
                if (panel._editingText && eLastPos) {
                    if (panel._editingText.setCursorPositionFromWorld(eLastPos)) {
                        view3D.forceActiveFocus();
                        return;
                        }
                    // Click was outside the text bounding box — exit editing
                    panel._editingText.setEditing(false);
                    panel._editingText = null;
                    ZCam.currentTool = "pointer";
                    }
                // When actively drawing a polygon, skip handle
                // picking — clicks continue the polygon, not drag
                // the preview handle.
                if (!_drawingPolygon) {
                    // First check if a vertex handle was picked.
                    var handle = pickVertexHandle(mouse.x, mouse.y);
                    if (handle) {
                        vertexDragHandle = handle;
                        ZCam.startVertexDrag(handle._poly, handle._vertexIndex);
                        return;
                        }
                    }
                // Rectangle tool: create a 0×0 rectangle at the click
                // position and immediately start dragging the
                // bottom-right handle (index 1) so the user can
                // pull the rectangle to the desired size.
                if (ZCam.currentTool == "rectangle") {
                    var newRect = ZCam.createRectangle(eLastPos.x, eLastPos.y);
                    if (newRect) {
                        // Wait for handles to be rebuilt by the
                        // currentElementChanged connection, then
                        // start dragging handle index 1.
                        Qt.callLater(function () {
                            // Find the handle for vertex index 1.
                            for (var i = 0; i < _handleList.length; ++i) {
                                var h = _handleList[i];
                                if (h && h._vertexIndex === 1) {
                                    vertexDragHandle = h;
                                    ZCam.startVertexDrag(newRect, 1);
                                    return;
                                    }
                                }
                            });
                        }
                    return;
                    }
                // Ellipse (circle) tool: create a 0×0 ellipse at the click
                // position and immediately start dragging the
                // bottom-right handle (index 1) so the user can
                // pull the ellipse to the desired size.
                if (ZCam.currentTool == "circle") {
                    var newEll = ZCam.createEllipse(eLastPos.x, eLastPos.y);
                    if (newEll) {
                        Qt.callLater(function () {
                            for (var i = 0; i < _handleList.length; ++i) {
                                var h = _handleList[i];
                                if (h && h._vertexIndex === 1) {
                                    vertexDragHandle = h;
                                    ZCam.startVertexDrag(newEll, 1);
                                    return;
                                    }
                                }
                            });
                        }
                    return;
                    }
                // Polygon tool: interactive polygon drawing.
                // First click starts a new polygon; subsequent
                // clicks add segments; right-click or Escape
                // finishes the polygon.
                if (ZCam.currentTool == "polygon") {
                    if (!_drawingPolygon) {
                        // Start a new polygon at the click position.
                        var newPoly = ZCam.createPolygon(eLastPos.x, eLastPos.y);
                        if (newPoly) {
                            _drawingPolygon = newPoly;
                            var lp = worldToPolygonLocal(newPoly, eLastPos);
                            _drawingPolygon.startDrawing(Qt.vector2d(lp.x, lp.y));
                            rebuildVertexHandles();
                            }
                        } else {
                        // Continue: fix current segment, start new one.
                        var lp2 = worldToPolygonLocal(_drawingPolygon, eLastPos);
                        _drawingPolygon.continueDrawing(Qt.vector2d(lp2.x, lp2.y));
                        rebuildVertexHandles();
                        }
                    return;
                    }
                // Text tool: create a new Text element at the click
                // position and immediately enter editing mode.
                if (ZCam.currentTool == "text") {
                    var newText = ZCam.createText(eLastPos.x, eLastPos.y);
                    if (newText) {
                        _editingText = newText;
                        newText.setEditing(true);
                        view3D.forceActiveFocus();
                        }
                    return;
                    }
                // Pick a new element under the cursor.
                // NOTE: ZCam.currentElement is NOT set here — it is
                // set by ZCam.mousePress() below.  This allows
                // mousePress to distinguish between a first click
                // (select the element, show bounding box) and a
                // second click on an already-selected polygon
                // (select a segment instead).
                // For drag purposes, if the current element is a Group
                // and the click falls inside its world bounding box,
                // the Group is returned so the visible selection box
                // acts as a drag handle for the whole group.
                curNode = pickDragTarget(mouse.x, mouse.y);
                if (curNode && curNode.element) {
                    if (!curNode.element.draggable)
                        curNode = null;
                    } else {
                    // Clicked on empty space — clear selection.
                    // mousePress(null) below will call
                    // setCurrentElement(null) which also clears
                    // any segment selection on the old element.
                    }
                ZCam.mousePress(curNode ? curNode.element : null, mouse.buttons, mouse.modifiers, eLastPos.x, eLastPos.y);
                // After mousePress (which may have selected a segment
                // or set currentElement), rebuild handles to reflect
                // the new selection state.
                var el = ZCam.currentElement;
                if (el) {
                    rebuildVertexHandles();
                    // Only start element drag if no segment is selected
                    // (otherwise the user is doing segment-level editing).
                    var hasSegSel = false;
                    try {
                        hasSegSel = el.selectedSegment >= 0;
                        } catch (e) {}
                    if (el.draggable && !hasSegSel)
                        ZCam.startElementDrag(el);
                    }
                } else if (mouse.button == Qt.RightButton) {
                // Right-click finishes the current polygon drawing.
                if (_drawingPolygon) {
                    finishPolygonDrawing();
                    return;
                    }
                if (!curNode)
                    curNode = pickModel(mouse.x, mouse.y);
                if (curNode && curNode.element && !curNode.element.draggable)
                    curNode = null;
                } else {
                if (!curNode)
                    curNode = pickModel(mouse.x, mouse.y);
                if (curNode && curNode.element && !curNode.element.draggable)
                    curNode = null;
                }
            }

        onDoubleClicked: mouse => {
            var m = pickModel(mouse.x, mouse.y);
            if (m && m.element) {
                // Double-click on a Text element enters edit mode
                // and switches to the text tool.
                if (m.element.typeName() === "text") {
                    // If a different text is being edited, exit that first
                    if (panel._editingText && panel._editingText !== m.element) {
                        panel._editingText.setEditing(false);
                        panel._editingText = null;
                        }
                    ZCam.currentElement = m.element;
                    panel._editingText = m.element;
                    m.element.setEditing(true);
                    ZCam.currentTool = "text";
                    view3D.forceActiveFocus();
                    }
                else
                    ZCam.doubleClick(m.element);
                }
            }

        onReleased: mouse => {
            if (vertexDragHandle) {
                ZCam.endVertexDrag(vertexDragHandle._poly, vertexDragHandle._vertexIndex);
                vertexDragHandle = null;
                }
            ZCam.endElementDrag();
            if (ZCam.currentTool != "rectangle" && ZCam.currentTool != "polygon" && ZCam.currentTool != "circle")
                curNode = null;
            }

        function pickModel(x, y) {
            // Custom picking: use C++ bounding-box-based picking
            // instead of Qt Quick 3D's built-in pickAll.
            // The C++ side traverses the element tree and returns
            // the innermost (smallest area) visible element whose
            // world bounding box contains the screen-to-scene point.
            var scenePos = screenToScene(x, y);
            if (!scenePos)
                return null;
            var el = ZCam.pickElement(scenePos.x, scenePos.y);
            if (!el)
                return null;
            // Return a pseudo-result object with .element so the
            // callers that expect { element: ... } still work.
            return { element: el, objectHit: null, bounds: null };
            }

        // Picking variant used to start a left-button drag.
        // When the currently selected element is a Group and the click
        // lies inside the Group's world bounding box, return the Group
        // itself so the visible selection bounding box acts as a drag
        // handle for the whole group (and its children).
        function pickDragTarget(x, y) {
            var scenePos = screenToScene(x, y);
            if (!scenePos)
                return null;
            var el = ZCam.pickDragTarget(scenePos.x, scenePos.y);
            if (!el)
                return null;
            return { element: el, objectHit: null, bounds: null };
            }

        // Returns the vertex handle Model under x/y, or null.
        function pickVertexHandle(x, y) {
            var list = view3D.pickAll(x, y);
            for (var i = 0; i < list.length; ++i) {
                var result = list[i];
                if (result.hitType == PickResult.Model) {
                    var obj = result.objectHit;
                    if (obj._vertexIndex !== undefined) {
                        return obj;
                        }
                    }
                }
            return null;
            }

        function screenToScene(x, y) {
            let normX = x / view3D.width;
            let normY = y / view3D.height;
            let nearPos = root.mapPositionFromScene(view3D.camera.mapFromViewport(Qt.vector3d(normX, normY, 0)));
            let farPos = root.mapPositionFromScene(view3D.camera.mapFromViewport(Qt.vector3d(normX, normY, 1)));

            let direction = farPos.minus(nearPos).normalized();
            if (Math.abs(direction.z) > 0.0001) {
                let t = (0 - nearPos.z) / direction.z;
                let r = nearPos.plus(direction.times(t));
                return r;
                }
            console.log("overflow: no screen position");
            return null;
            }

        onPositionChanged: mouse => {
            pos3d = screenToScene(mouse.x, mouse.y);
            panel.positionChanged(pos3d.x, pos3d.y);
            var currentPos = Qt.vector2d(mouse.x, mouse.y);
            var delta = Qt.vector2d(lastPos.x - currentPos.x, lastPos.y - currentPos.y);

            // Update polygon preview when drawing (no button pressed = hover).
            if (_drawingPolygon && pos3d) {
                var lp = worldToPolygonLocal(_drawingPolygon, pos3d);
                _drawingPolygon.updatePreview(Qt.vector2d(lp.x, lp.y));
                }

            if ((mouse.buttons == Qt.RightButton) && (mouse.modifiers == Qt.NoModifier)) {
                if (curNode) {
                    var rotSpeed = 0.05 * frameDelta;
                    var dRot = Qt.vector3d(delta.y * -mouseArea.ySpeed * rotSpeed, -delta.x * mouseArea.xSpeed * rotSpeed, 0);
                    ZCam.rotated(curNode.element, dRot, mouse.modifiers);
                    } else {
                    var rotationVector = root.eulerRotation;
                    var rotateX = -delta.x * mouseArea.xSpeed * frameDelta;
                    rotationVector.y += rotateX;
                    var rotateY = delta.y * -mouseArea.ySpeed * frameDelta;
                    rotationVector.x += rotateY;
                    root.setEulerRotation(rotationVector);
                    }
                lastPos = currentPos;
                } else if ((mouse.buttons == Qt.MiddleButton) && (mouse.modifiers == Qt.NoModifier)) {
                pan(delta);
                lastPos = currentPos;
                } else if ((mouse.buttons == Qt.LeftButton) && (mouse.modifiers == Qt.NoModifier)) {
                if (vertexDragHandle) {
                    // The handle is a child of root, so pos3d is already
                    // in the same coordinate space as the handle.
                    eLastPos = pos3d;
                    ZCam.dragVertexTo(vertexDragHandle._poly, vertexDragHandle._vertexIndex, pos3d);
                    } else if (curNode) {
                    var eDelta = pos3d.minus(eLastPos);
                    eLastPos = pos3d;
                    ZCam.dragged(curNode.element, eDelta, mouse.modifiers);
                    }
                } else {
                // No button pressed — update handle hover highlight.
                updateHandleHover(mouse.x, mouse.y);
                var m = pickModel(mouse.x, mouse.y);
                ZCam.hover(m ? m.element : null);
                }
            }
        }

    //-----------------------------------------------------
    //  Drag & Drop area for SVG / DXF / DWG import
    //-----------------------------------------------------

    DropArea {
        id: dropArea
        anchors.fill: parent
        z: 100  // above the 3D view and mouse area

        property bool _hasImportDrop: false
        property string _pendingImportPath: ""   // file being dragged

        // Accept drags that contain at least one supported file.
        // We evaluate this in onEntered and onPositionChanged so
        // that the highlight overlay appears only for valid drops.
        function containsImportable(urls) {
            for (var i = 0; i < urls.length; ++i) {
                var path = urls[i].toString().toLowerCase();
                if (path.endsWith(".svg") || path.endsWith(".dxf") || path.endsWith(".dwg"))
                    return true;
                }
            return false;
            }

        // Check if the drag source is a MediaArtworkPanel tile
        function isArtworkDrag(drag) {
            if (!drag.source)
                return false;
            // Walk up the parent hierarchy to find MediaArtworkPanel root
            var p = drag.source;
            while (p) {
                if (p.selectedFilePath !== undefined && p.selectedFilePath !== "" )
                    return true;
                p = p.parent;
                }
            return false;
            }

        function getArtworkPath(drag) {
            var p = drag.source;
            while (p) {
                if (p.selectedFilePath !== undefined && p.selectedFilePath !== "")
                    return p.selectedFilePath;
                p = p.parent;
                }
            return "";
            }

        // Extract the first importable file path from the drop (either
        // from urls or from an artwork panel drag source).
        function getImportPath(drop) {
            if (drop.urls && drop.urls.length > 0) {
                for (var i = 0; i < drop.urls.length; ++i) {
                    var path = drop.urls[i].toString();
                    var lower = path.toLowerCase();
                    if (lower.endsWith(".svg") || lower.endsWith(".dxf") || lower.endsWith(".dwg")) {
                        if (path.startsWith("file://"))
                            path = path.substring("file://".length);
                        return path;
                        }
                    }
                }
            else if (isArtworkDrag(drop)) {
                return getArtworkPath(drop);
                }
            return "";
            }

        onEntered: drop => {
            if (drop.urls && drop.urls.length > 0)
                _hasImportDrop = containsImportable(drop.urls);
            else
                _hasImportDrop = isArtworkDrag(drop);
            drop.accepted = _hasImportDrop;
            // Start drag preview for SVG or DXF files
            if (_hasImportDrop) {
                _pendingImportPath = getImportPath(drop);
                if (_pendingImportPath !== "") {
                    var lower = _pendingImportPath.toLowerCase();
                    if (lower.endsWith(".svg"))
                        ZCam.startSvgDrag(_pendingImportPath);
                    else if (lower.endsWith(".dxf") || lower.endsWith(".dwg")) {
                        // Start DXF drag preview using the same geometry mechanism
                        ZCam.startDxfDrag(_pendingImportPath);
                        }
                    }
                }
            }

        onPositionChanged: drop => {
            if (drop.urls && drop.urls.length > 0)
                _hasImportDrop = containsImportable(drop.urls);
            else
                _hasImportDrop = isArtworkDrag(drop);
            drop.accepted = _hasImportDrop;
            // Update the preview box position to follow the mouse
            if (_hasImportDrop && _pendingImportPath !== "") {
                var scenePos = mouseArea.screenToScene(drop.x, drop.y);
                if (scenePos)
                    svgDragPreview.position = Qt.vector3d(scenePos.x, scenePos.y, 0);
                }
            }

        onDropped: drop => {
            _hasImportDrop = false;
            var imported = false;
            // Get the drop position in scene coordinates
            var dropPos = mouseArea.screenToScene(drop.x, drop.y);
            if (drop.urls && drop.urls.length > 0) {
                for (var i = 0; i < drop.urls.length; ++i) {
                    var path = drop.urls[i].toString();
                    // Strip "file://" prefix to get a local path.
                    if (path.startsWith("file://"))
                        path = path.substring("file://".length);
                    var lower = path.toLowerCase();
                    if (lower.endsWith(".svg")) {
                        if (dropPos)
                            ZCam.importSvgAt(path, dropPos.x, dropPos.y);
                        else
                            ZCam.importFile(path);
                        imported = true;
                        }
                    else if (lower.endsWith(".dxf") || lower.endsWith(".dwg")) {
                        if (dropPos)
                            ZCam.importDxfAt(path, dropPos.x, dropPos.y);
                        else
                            ZCam.importFile(path);
                        imported = true;
                        }
                    }
                }
            else if (isArtworkDrag(drop)) {
                var artworkPath = getArtworkPath(drop);
                if (artworkPath !== "") {
                    var artLower = artworkPath.toLowerCase();
                    if (artLower.endsWith(".svg") && dropPos)
                        ZCam.importSvgAt(artworkPath, dropPos.x, dropPos.y);
                    else if ((artLower.endsWith(".dxf") || artLower.endsWith(".dwg")) && dropPos)
                        ZCam.importDxfAt(artworkPath, dropPos.x, dropPos.y);
                    else
                        ZCam.importFile(artworkPath);
                    imported = true;
                    }
                }
            // End drag preview
            _pendingImportPath = "";
            ZCam.endSvgDrag();
            drop.accepted = imported;
            }

        onExited: {
            _hasImportDrop = false;
            _pendingImportPath = "";
            ZCam.endSvgDrag();
            }
        }

    // Visual feedback overlay shown while dragging supported files over the canvas.
    Rectangle {
        id: dropOverlay
        anchors.fill: parent
        z: 99  // below the DropArea so it doesn't block drop events
        color: Material.color(Material.Teal, Material.Shade700)
        opacity: 0.25
        border.width: 4
        border.color: Material.color(Material.Teal, Material.Shade200)
        radius: 8
        visible: dropArea._hasImportDrop

        Label {
            anchors.centerIn: parent
            text: qsTr("Drop SVG / DXF to import")
            font.pixelSize: 24
            font.bold: true
            color: "white"
            style: Text.Raised
            styleColor: Material.color(Material.Teal, Material.Shade900)
            }
        }

    // SVG drag-preview bounding box rendered in the 3D scene.
    // The geometry is a rectangle outline created by ZCam::startSvgDrag()
    // from the SVG's path data.  Its position is updated in
    // DropArea::onPositionChanged to follow the mouse on the XY plane.
    Model {
        id: svgDragPreview
        parent: root
        visible: ZCam.dragPreviewGeometry !== null
        geometry: ZCam.dragPreviewGeometry
        pickable: false
        position: Qt.vector3d(0, 0, 0)
        materials: [
            PrincipledMaterial {
                cullMode: PrincipledMaterial.NoCulling
                lineWidth: 2
                lighting: PrincipledMaterial.NoLighting
                baseColor: Qt.rgba(0.0, 1.0, 0.5, 1.0)  // teal-green outline
            }
        ]
    }

    NavigationCube {
        id: navCube
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 10
        anchors.rightMargin: 10
        z: 10

        sceneRotation: root.eulerRotation

        onViewRequested: rotation => {
            root.eulerRotation = rotation;
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
            onClicked: {
                root.eulerRotation = Qt.vector3d(0, 0, 0);
                }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-bottom.svg"
            onClicked: {
                root.eulerRotation = Qt.vector3d(180, 0, 0);
                }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-front.svg"
            onClicked: {
                root.eulerRotation = Qt.vector3d(-90, 0, 0);
                }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-rear.svg"
            onClicked: {
                root.eulerRotation = Qt.vector3d(-90, 180, 0);
                }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-left.svg"
            onClicked: {
                root.eulerRotation = Qt.vector3d(-90, 90, 0);
                }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-right.svg"
            onClicked: {
                root.eulerRotation = Qt.vector3d(-90, -90, 0);
                }
            z: 1
            }
        RButton {
            id: iCamera
            checkable: true
            checked: true
            icon.source: "qrc:////icons/view-isometric.svg"
            onClicked: {
                panel.perspectiveCamera = false;
                pCamera.checked = false;
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
                panel.perspectiveCamera = true;
                iCamera.checked = false;
                }
            ToolTip.text: qsTr("Perspective Projection")
            ToolTip.delay: 1000
            ToolTip.timeout: 4000
            ToolTip.visible: hovered
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-fullscreen.svg"
            onClicked: resetCamera()
            z: 1
            }
        }

    ToolColumn {
        anchors.top: parent.top
        anchors.topMargin: toolRow.height + 20
        anchors.leftMargin: 10
        anchors.left: parent.left

        RButton {
            icon.source: "qrc:////icons/select.svg"
            checked: ZCam.currentTool == "pointer"
            onCheckedChanged: if (checked)
                ZCam.currentTool = "pointer"
            z: 1
            }
        RButton {
            icon.source: "qrc:////icons/Draft_Polygon.svg"
            checked: ZCam.currentTool == "polygon"
            onCheckedChanged: if (checked)
                ZCam.currentTool = "polygon"
            }
        RButton {
            icon.source: "qrc:////icons/Draft_Rectangle.svg"
            checked: ZCam.currentTool == "rectangle"
            onCheckedChanged: if (checked)
                ZCam.currentTool = "rectangle"
            }
        RButton {
            icon.source: "qrc:////icons/Draft_Circle.svg"
            checked: ZCam.currentTool == "circle"
            onCheckedChanged: if (checked)
                ZCam.currentTool = "circle"
            }
        RButton {
            icon.source: "qrc:////icons/Draft_Text.svg"
            checked: ZCam.currentTool == "text"
            onCheckedChanged: if (checked)
                ZCam.currentTool = "text"
            }
        }
    }
