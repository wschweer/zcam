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
    // Overlay visibility is driven by the project's CameraElement (show property).
    property var _cameraElement: (ZCam.project && ZCam.project.cameraElement) ? ZCam.project.cameraElement : null
    property bool cameraOverlayVisible: _cameraElement ? _cameraElement.show : false

    // Polygon drawing state
    property var _drawingPolygon: null   // the Polygon being drawn (null when idle)

    // Text editing state
    property var _editingText: null      // the Text element being edited (null when idle)

    // Lasso selection state
    property bool lassoActive: false     // true while Ctrl-drag is in progress
    property var lassoPoints: []        // list of QVector3D in scene (root) coords
    property var lassoScreenPoints: []  // list of Qt.vector2d in screen coords for overlay
    property int lassoPointCount: 0     // incremented on each point add (triggers binding updates)

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

    //─────────────────────────────────────────────────────────────
    //  Snap reference-point marker
    //    A small cross rendered at the element's reference point
    //    (element origin, 0,0 in local coords) while a drag with
    //    grid snap is active.  Two thin #Cube models form the
    //    horizontal and vertical bars.
    //
    //    Visibility is driven by ZCam.snapDragActive, a flag that
    //    is set once at startElementDrag() and cleared once at
    //    endElementDrag() — it never toggles in the middle of a drag
    //    (unlike the per-axis snap flags that can switch on/off as
    //    the cursor crosses grid lines), so the cross cannot flicker
    //    or disappear mid-drag.
    //
    //    The position comes from ZCam.snapRefPos, which is derived
    //    from the exact same parent-local pos value that is assigned
    //    to the element during the drag (see ZCam::dragged).
    //    Like the vertex handles, the marker lives inside root so
    //    its position uses root-space coordinates directly.
    //─────────────────────────────────────────────────────────────

    Node {
        id: snapMarker
        parent: root
        visible: ZCam.snapDragActive
        position: ZCam.snapRefPos

        // Scale compensation factor for a constant on-screen size
        // regardless of the canvas zoom (root.scale), analogous to
        // the vertex handles.  Shared by both bars.
        property vector3d unitScale: {
            var rs = root.scale
            var f = rs.x !== 0 ? 1.0 / rs.x : 1.0
            return Qt.vector3d(f, f, f)
            }

        // Thin horizontal bar at the reference point.
        Model {
            source: "#Cube"
            pickable: false
            scale: snapMarker.unitScale.times(Qt.vector3d(0.06, 0.006, 0.006))
            materials: [
                PrincipledMaterial {
                    cullMode: PrincipledMaterial.NoCulling
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: Qt.rgba(1.0, 0.4, 0.0, 1.0) // orange
                }
            ]
        }
        // Thin vertical bar at the reference point.
        Model {
            source: "#Cube"
            pickable: false
            scale: snapMarker.unitScale.times(Qt.vector3d(0.006, 0.06, 0.006))
            materials: [
                PrincipledMaterial {
                    cullMode: PrincipledMaterial.NoCulling
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: Qt.rgba(1.0, 0.4, 0.0, 1.0) // orange
                }
            ]
        }
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

    function screenToScene(x, y) {
        return mouseArea.screenToScene(x, y);
        }

    //=========================================================
    //  Grid viewport update
    //    Computes the currently visible scene region in local
    //    coordinates and pushes it to the Grid element so the
    //    grid geometry is rebuilt to cover the full visible
    //    area at the current zoom/pan state.
    //=========================================================
    function updateGridViewport() {
        var grid = ZCam.project ? ZCam.project.gridElement : null;
        if (!grid)
            return;

        // Map the four corners of the viewport to scene (root) coordinates.
        var tl = screenToScene(0, 0);
        var tr = screenToScene(panel.width, 0);
        var bl = screenToScene(0, panel.height);
        var br = screenToScene(panel.width, panel.height);
        if (!tl || !tr || !bl || !br)
            return;

        var left   = Math.min(tl.x, tr.x, bl.x, br.x);
        var right  = Math.max(tl.x, tr.x, bl.x, br.x);
        var top    = Math.min(tl.y, tr.y, bl.y, br.y);
        var bottom = Math.max(tl.y, tr.y, bl.y, br.y);

        // Compute the camera view direction in local (root) coordinates.
        // Derive it EXACTLY like screenToScene(): take two points on the
        // centre view ray (near / far) and build their difference.  This
        // is the true camera direction (in root coordinates) and works
        // for both orthographic AND perspective projection.  Rotating the
        // view rotates this vector, so the billboard orientation in
        // Grid::setViewport() always gets the real angle.
        // (The old approximation "cam position normalised" pointed toward
        // the root origin instead — after a rotation that vector still
        // had a dominant Z component, so the grid kept being billboarded
        // nearly flat and the lines appeared thicker and thicker.)
        var cam = view3D.camera;
        var nearC = root.mapPositionFromScene(cam.mapFromViewport(Qt.vector3d(0.5, 0.5, 0)));
        var farC  = root.mapPositionFromScene(cam.mapFromViewport(Qt.vector3d(0.5, 0.5, 1)));
        var viewDir = farC.minus(nearC);
        if (viewDir.length() < 1e-9)
            viewDir = Qt.vector3d(0, 0, -1);   // degenerate fallback: straight down
        else
            viewDir = viewDir.normalized();

        // Pass the REAL panel size in pixels so the grid can compute
        // the exact mm-per-pixel ratio — the hardcoded nominal canvas
        // width of 1000 px made the freshly-built 3D canvas look
        // different from the state after the first refresh whenever
        // the actual layout deviated from that guess.
        grid.setViewport(left, top, right, bottom, viewDir, panel.width, panel.height);
        }

    //=========================================================
    //  Quaternion helpers
    //    The SpaceMouse rotates the world node (root) relative
    //    to the CAMERA, so the scene always turns the way the
    //    knob is pushed — no matter where the camera currently
    //    looks.  A camera-space rotation is applied in world
    //    space as   q' = qCam ⊗ q ⊗ qCam⁻¹.
    //=========================================================
    function qMul(a, b) {
        return Qt.quaternion(a.scalar * b.scalar - a.x * b.x - a.y * b.y - a.z * b.z, a.scalar * b.x + a.x * b.scalar + a.y * b.z - a.z * b.y, a.scalar * b.y - a.x * b.z + a.y * b.scalar + a.z * b.x, a.scalar * b.z + a.x * b.y - a.y * b.x + a.z * b.scalar);
        }

    // Rotate vector v by quaternion q:  v' = q · (0,v) · q⁻¹
    function qRotVec(q, v) {
        var qv3 = qMul(qMul(q, Qt.quaternion(0, v.x, v.y, v.z)), Qt.quaternion(q.scalar, -q.x, -q.y, -q.z));
        return Qt.vector3d(qv3.x, qv3.y, qv3.z);
        }

    // Quaternion for `deg` degrees around (normalized) axis.
    function qAxisAngle(axis, deg) {
        var half = deg * Math.PI / 360.0;
        var s = Math.sin(half);
        return Qt.quaternion(Math.cos(half), axis.x * s, axis.y * s, axis.z * s);
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
            clearColor: ZCam.config ? ZCam.config.canvasBG : Material.color(Material.BlueGrey, Material.Shade500)
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
                if (lassoActive) {
                    lassoActive = false;
                    lassoPoints = [];
                    lassoScreenPoints = [];
                    lassoPointCount = 0;
                    event.accepted = true;
                    return;
                    }
                // If a lasso selection is active, Escape clears it.
                if (ZCam.selectedElements && ZCam.selectedElements.length > 0) {
                    ZCam.clearSelection();
                    event.accepted = true;
                    return;
                    }
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

            //─────────────────────────────────────────────────────────────
            //  Camera overlay
            //    Live video from the project's CameraElement, shown on the
            //    XY plane.  The element drives the transform (pos / rot /
            //    overlaySize) and the trapezoid (keystone) correction
            //    (trapezX / trapezY).  The mesh is a 1×1 quad whose corner
            //    positions are sheared by the trapezoid properties.
            //─────────────────────────────────────────────────────────────
            Node {
                id: cameraOverlayNode
                visible: panel.cameraOverlayVisible
                position: panel._cameraElement ? panel._cameraElement.pos : Qt.vector3d(0, 0, 0)
                eulerRotation: panel._cameraElement ? panel._cameraElement.rot : Qt.vector3d(0, 0, 0)

                Model {
                    id: cameraOverlay
                    pickable: false
                    geometry: CameraOverlayGeometry {
                        // Unit quad (1×1, centred at origin) sheared by the
                        // trapezoid correction properties:
                        //   x' = x + trapezX * y
                        //   y' = y + trapezY * x
                        trapezX: panel._cameraElement ? panel._cameraElement.trapezX : 0.0
                        trapezY: panel._cameraElement ? panel._cameraElement.trapezY : 0.0
                        }
                    scale: panel._cameraElement ? Qt.vector3d(panel._cameraElement.overlaySize.x, panel._cameraElement.overlaySize.y, 1) : Qt.vector3d(100, 100, 1)
                    materials: [
                        PrincipledMaterial {
                            cullMode: PrincipledMaterial.NoCulling
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: "#ffffff"
                            // Use the material's opacity property (not the
                            // baseColor alpha channel) to control transparency.
                            // PrincipledMaterial only enables alpha blending
                            // when its opacity property is < 1.0; the baseColor
                            // alpha is ignored in opaque render mode.
                            opacity: panel._cameraElement ? panel._cameraElement.opacity : 1.0
                            // Use the camera element's live video as the
                            // diffuse map so the video frame is displayed
                            // on the overlay quad.
                            baseColorMap: Texture {
                                id: cameraTexture
                                textureData: CameraTextureData {
                                    camera: panel._cameraElement
                                    }
                                }
                            }
                    ]
                    }
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
            // The snap marker is bound to ZCam.snapRefPos / snapActive and
            // updates itself — nothing to clean up here.
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
        updateGridViewport();
        }

    // Reset the 3D camera when a new project is created.
    // This covers newProject(), createMaterialTest() and
    // createGalvoTest() — all emit projectCreated() via
    // ZCam::endNewProject().
    Connections {
        target: ZCam
        function onProjectCreated() {
            resetCamera();
            updateGridViewport();
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
        // Initialize the grid to cover the default viewport.
        Qt.callLater(updateGridViewport);
        }

    SpaceMouse {
        // Per-axis sensitivity, configurable in Config → SpaceMouse.
        // The raw device values are only roughly ±0.7 at full
        // deflection, so they are amplified to make moving, zooming
        // and rotating feel responsive.
        property real sensPanX: ZCam.config ? ZCam.config.smPanX : 4.0
        property real sensPanY: ZCam.config ? ZCam.config.smPanY : 4.0
        property real sensZoom: ZCam.config ? ZCam.config.smZoom : 12.0
        property real sensPitch: ZCam.config ? ZCam.config.smPitch : 1.0
        property real sensYaw: ZCam.config ? ZCam.config.smYaw : 1.0
        property real sensRoll: ZCam.config ? ZCam.config.smRoll : 1.0
        // All motions are interpreted in CAMERA space: pushing the knob
        // left always moves the content to the left on screen, twisting
        // the knob always rolls the view, etc. — independent of how the
        // world node (root) is currently rotated.
        onRotate: v => {
            // Camera axes expressed in world coordinates (cameras sit at
            // world identity, looking down -Z).
            var right = Qt.vector3d(1, 0, 0);
            var up = Qt.vector3d(0, 1, 0);
            var fwd = Qt.vector3d(0, 0, -1);

            // Axis (world space) and angle for each SpaceMouse component:
            //   tilt  (v.x, up/down) → pitch around camera right
            //   twist (v.z)          → roll  around camera forward
            //   turn  (v.y, l/r)     → yaw   around camera up
            // The signs match the previous behaviour in the home view
            // (yaw around +up, roll around +z), only the axes are now
            // fixed to the camera instead of the rotated object.
            var q = qMul(qAxisAngle(right, -v.x * sensPitch), qAxisAngle(fwd, v.z * sensRoll));
            q = qMul(q, qAxisAngle(up, v.y * sensYaw));

            // Rotate root.position as well so the node spins around its
            // origin instead of orbiting the world origin.
            var pos = qRotVec(q, root.position);
            var rot = qMul(q, root.rotation);
            root.rotation = rot;
            root.position = pos;
            updateGridViewport();
            }
        onTranslate: v => {
            // Camera-space offset.  The cameras sit at world identity,
            // so the camera axes ARE the world axes and the offset can
            // be applied directly — independent of how the world node
            // (root) is currently rotated:
            //   knob right/left   → +x / -x  (content right/left)
            //   knob up           → +y       (content up)
            //   knob pushed away  → +z       (content toward the
            //                                   viewer → zoom in)
            // The root scale is applied so the content keeps moving
            // proportionally to the zoom factor like before; the
            // sensPan*/sensZoom factors amplify the raw device values.
            var s = root.scale;
            var world = Qt.vector3d(v.x * s.x * sensPanX, -v.y * s.y * sensPanY, -v.z * s.z * sensZoom);
            root.position = root.position.plus(world);
            updateGridViewport();
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
        // Drag threshold: accumulate scene-space movement until it
        // exceeds config.dragThreshold before actually moving the
        // element.  This prevents accidental micro-moves when the user
        // just clicks (without intending to drag).
        property vector3d _dragAccum: Qt.vector3d(0, 0, 0)
        property bool _dragThresholdMet: false
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
        //   Ctrl+wheel   → element scaling (large step, ~20%)
        //   plain wheel  → canvas zoom    (normal step, ~10%)
        //   Shift+wheel  → element scaling (fine step, ~2%)
        property real wheelBigStep: 1.2
        property real wheelStep: 1.1
        property real wheelSmallStep: 1.02

        onWheel: mouse => {
            // Determine the scale delta based on modifier:
            //   Ctrl+wheel  → bigStep    (element scaling, large step)
            //   plain wheel → step       (canvas zoom, normal step)
            //   Shift+wheel → smallStep  (element scaling, fine step)
            var sd;
            if (mouse.modifiers & Qt.ControlModifier)
                sd = (mouse.angleDelta.y > 0.0) ? wheelBigStep : (1.0 / wheelBigStep);
            else if (mouse.modifiers & Qt.ShiftModifier)
                sd = (mouse.angleDelta.y > 0.0) ? wheelSmallStep : (1.0 / wheelSmallStep);
            else
                sd = (mouse.angleDelta.y > 0.0) ? wheelStep : (1.0 / wheelStep);

            // Ctrl+wheel or Shift+wheel scales a visible, draggable, selected
            // element.  Ctrl uses a large step, Shift uses a fine step.
            // Only elements that are visible on the canvas (Text, Polygon,
            // Ellipse, Rectangle etc.) and actually shown (show == true and
            // ancestors visible) are eligible for element scaling.
            //
            // The scale center is the current mouse position in scene
            // coordinates, analogous to how the canvas zoom keeps the
            // cursor position fixed.
            if (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) {
                var el = ZCam.currentElement;
                if (el && el.visible() && el.show && el.ancestorsShow && el.draggable()) {
                    var pivot = screenToScene(mouse.x, mouse.y);
                    if (!pivot)
                        return;
                    var scaleFactor = Qt.vector3d(sd, sd, sd);
                    ZCam.startElementDrag(el);
                    ZCam.scaled(el, scaleFactor, mouse.modifiers, pivot);
                    ZCam.endElementDrag();
                    return;
                    }
                }

            // Plain wheel — zoom the 3D view.
            var cursorScenePos;
            var localPos = screenToScene(mouse.x, mouse.y);
            if (!localPos)
                return;
            cursorScenePos = root.mapPositionToScene(localPos);

            root.scale = root.scale.times(sd);
            root.position = root.position.plus(cursorScenePos.minus(root.position).times(1.0 - sd));

            // Rebuild the grid to cover the new visible area.
            updateGridViewport();
            }

        onPressed: mouse => {
            panel.focus = true;
            lastPos = Qt.vector2d(mouse.x, mouse.y);
            eLastPos = screenToScene(mouse.x, mouse.y);
            // Reset drag threshold state on every press.
            _dragAccum = Qt.vector3d(0, 0, 0);
            _dragThresholdMet = false;
            // Ctrl+Left-drag starts a lasso selection.
            if (mouse.button == Qt.LeftButton && (mouse.modifiers & Qt.ControlModifier)) {
                lassoActive = true;
                lassoPoints = [Qt.vector3d(eLastPos.x, eLastPos.y, eLastPos.z)];
                lassoScreenPoints = [Qt.vector2d(mouse.x, mouse.y)];
                lassoPointCount = 1;
                return;
                }
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
                    if (!curNode.element.draggable())
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
                    if (el.draggable() && !hasSegSel)
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
                if (curNode && curNode.element && !curNode.element.draggable())
                    curNode = null;
                // Show context menu when an element or a group of
                // elements is under the cursor.
                if (ZCam.currentElement || (ZCam.selectedElements && ZCam.selectedElements.length > 0))
                    canvasMenu.popup(mouse.x, mouse.y);
                } else {
                if (!curNode)
                    curNode = pickModel(mouse.x, mouse.y);
                if (curNode && curNode.element && !curNode.element.draggable())
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
            // Finish lasso selection
            if (lassoActive) {
                lassoActive = false;
                ZCam.lassoSelect(lassoPoints);
                lassoPoints = [];
                lassoScreenPoints = [];
                lassoPointCount = 0;
                return;
                }
            if (vertexDragHandle) {
                ZCam.endVertexDrag(vertexDragHandle._poly, vertexDragHandle._vertexIndex);
                vertexDragHandle = null;
                }
            ZCam.endElementDrag();
            if (ZCam.currentTool != "rectangle" && ZCam.currentTool != "polygon" && ZCam.currentTool != "circle")
                curNode = null;
            }

        function pickModel(x, y) {
            // The coordinates come from the panel-level MouseArea and
            // are relative to the panel, not to the View3D — convert
            // them into View3D-local coordinates before any picking.
            var posInView = mouseArea.mapToItem(view3D, x, y);
            var vx = posInView.x;
            var vy = posInView.y;
            // Try Qt Quick 3D's own raycast picker first: it works in
            // exactly the same coordinate space as the rendering, so
            // there is no risk of a coordinate-frame mismatch.  Walk
            // the results (nearest first) and return the first element
            // hit that is a real Element3d with an actual pickable
            // object (not a helper node like the grid background).
            let results = view3D.pickAll(vx, vy);
            // Walk the hits (nearest first).  PickResult.element is
            // always null for QML-instantiated Models, so read the
            // Element3d from the picked Model's own "element" property
            // (set by ProjectTree.addElement).
            for (let i = 0; i < results.length; ++i) {
                var obj = results[i].objectHit;
                // Walk up the QML parent chain until a Model with an
                // "element" property is found — child helper Models
                // (edge lines, bboxOverlay) live inside the real
                // element Model.
                while (obj && obj.element === undefined)
                    obj = obj.parent;
                if (!obj || obj.element === null || obj.element === undefined)
                    continue;
                var el = obj.element;
                if (el.show)
                    return { element: el, objectHit: results[i].objectHit, bounds: results[i].bounds };
                }
            ZCam.logLine("pickAll: " + results.length + " hits");
            if (results.length === 0)
                ZCam.logLine("pickAll -> no usable element (0 hits), falling back to pickAt");
            // Fallback: C++ ray-based 3D-bbox picking.
            var el2 = ZCam.pickAt(view3D, root, vx, vy);
            if (!el2)
                return null;
            return { element: el2, objectHit: null, bounds: null };
            }

        // Picking variant used to start a left-button drag.
        // Uses the same ray-based approach as pickModel().  The
        // Group-drag-handle special case (return the selected Group
        // itself when clicking inside its box) is intentionally not
        // re-implemented here — ray-based picking on 3D elements
        // already returns the innermost draggable element, which is
        // the correct drag target for the 3D viewport.
        function pickDragTarget(x, y) {
            return pickModel(x, y);
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

            // Lasso drag: accumulate points (only if moved enough to avoid clutter)
            if (lassoActive && pos3d) {
                var lastScreen = lassoScreenPoints[lassoScreenPoints.length - 1];
                var dx = mouse.x - lastScreen.x;
                var dy = mouse.y - lastScreen.y;
                if (dx * dx + dy * dy >= 9) {  // 3px minimum
                    // Copy the vector3d value explicitly — pushing the property
                    // directly would store a reference and all entries would
                    // end up with the same value.
                    lassoPoints.push(Qt.vector3d(pos3d.x, pos3d.y, pos3d.z));
                    lassoScreenPoints.push(Qt.vector2d(mouse.x, mouse.y));
                    lassoPointCount = lassoPointCount + 1;
                    }
                lastPos = currentPos;
                return;
                }

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
                updateGridViewport();
                } else if ((mouse.buttons == Qt.MiddleButton) && (mouse.modifiers == Qt.NoModifier)) {
                pan(delta);
                lastPos = currentPos;
                updateGridViewport();
                } else if ((mouse.buttons == Qt.LeftButton) && (mouse.modifiers == Qt.NoModifier)) {
                if (vertexDragHandle) {
                    // The handle is a child of root, so pos3d is already
                    // in the same coordinate space as the handle.
                    eLastPos = pos3d;
                    ZCam.dragVertexTo(vertexDragHandle._poly, vertexDragHandle._vertexIndex, pos3d);
                    } else if (curNode) {
                    var eDelta = pos3d.minus(eLastPos);
                    eLastPos = pos3d;
                    // Apply drag threshold: accumulate scene-space
                    // movement until the total displacement exceeds
                    // config.dragThreshold (in mm).  Only then start
                    // actually moving the element.  This prevents
                    // accidental micro-moves on a simple click.
                    if (!_dragThresholdMet) {
                        _dragAccum = Qt.vector3d(_dragAccum.x + eDelta.x, _dragAccum.y + eDelta.y, _dragAccum.z + eDelta.z);
                        var threshold = ZCam.config ? ZCam.config.dragThreshold : 0.5;
                        if (_dragAccum.length() < threshold)
                            return;
                        // Threshold met — emit the accumulated delta
                        // as a single move, then switch to live-drag.
                        _dragThresholdMet = true;
                        ZCam.dragged(curNode.element, _dragAccum, mouse.modifiers);
                        _dragAccum = Qt.vector3d(0, 0, 0);
                    } else {
                        ZCam.dragged(curNode.element, eDelta, mouse.modifiers);
                    }
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

    // Lasso selection overlay — drawn as a semi-transparent polygon
    // outline on top of the 3D canvas while Ctrl-drag is active.
    Canvas {
        id: lassoOverlay
        anchors.fill: parent
        z: 50
        visible: panel.lassoActive && panel.lassoPointCount > 1
        onWidthChanged: requestPaint();
        onHeightChanged: requestPaint();

        // Repaint whenever a new lasso point is added.
        Connections {
            target: panel
            function onLassoPointCountChanged() { lassoOverlay.requestPaint(); }
            function onLassoActiveChanged() { if (!panel.lassoActive) lassoOverlay.requestPaint(); }
        }

        onPaint: {
            var ctx = getContext('2d');
            ctx.reset();
            ctx.clearRect(0, 0, width, height);
            var pts = panel.lassoScreenPoints;
            if (pts.length < 2)
                return;
            ctx.beginPath();
            ctx.moveTo(pts[0].x, pts[0].y);
            for (var i = 1; i < pts.length; ++i)
                ctx.lineTo(pts[i].x, pts[i].y);
            // Close the path back to start
            ctx.lineTo(pts[0].x, pts[0].y);
            ctx.closePath();
            // Fill with semi-transparent teal
            ctx.fillStyle = Qt.rgba(0.0, 0.5, 0.5, 0.15);
            ctx.fill();
            // Stroke with teal outline
            ctx.strokeStyle = Qt.rgba(0.0, 0.8, 0.8, 0.9);
            ctx.lineWidth = 2;
            ctx.stroke();
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
            updateGridViewport();
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
                updateGridViewport();
                }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-bottom.svg"
            onClicked: {
                root.eulerRotation = Qt.vector3d(180, 0, 0);
                updateGridViewport();
                }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-front.svg"
            onClicked: {
                root.eulerRotation = Qt.vector3d(-90, 0, 0);
                updateGridViewport();
                }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-rear.svg"
            onClicked: {
                root.eulerRotation = Qt.vector3d(-90, 180, 0);
                updateGridViewport();
                }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-left.svg"
            onClicked: {
                root.eulerRotation = Qt.vector3d(-90, 90, 0);
                updateGridViewport();
                }
            z: 1
            }
        TButton {
            icon.source: "qrc:////icons/view-right.svg"
            onClicked: {
                root.eulerRotation = Qt.vector3d(-90, -90, 0);
                updateGridViewport();
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

    //-----------------------------------------------------
    //  Context menu for the 3D canvas
    //    Appears on right-click when an element or a group of
    //    elements is under the cursor.  The "Group" entry is
    //    enabled only when two or more draggable elements are
    //    selected (lasso multi-selection).
    //-----------------------------------------------------
    Menu {
        id: canvasMenu
        Material.theme: Material.Dark
        MenuItem {
            text: qsTr("Group")
            // Enabled only when two or more elements are selected.
            enabled: ZCam.selectedElements && ZCam.selectedElements.length >= 2
            onTriggered: {
                ZCam.groupSelectedElements();
                }
            }
        MenuItem {
            text: qsTr("Combine")
            // Enabled only when two or more elements are selected.
            // The C++ side filters further to only Polygon elements
            // on the same tree level.
            enabled: ZCam.selectedElements && ZCam.selectedElements.length >= 2
            onTriggered: {
                ZCam.combineSelectedPolygons();
                }
            }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Center on Workspace")
            enabled: ZCam.currentElement !== null
            onTriggered: {
                ZCam.centerOnWorkspace(ZCam.currentElement);
                }
            }
        MenuItem {
            text: qsTr("Delete")
            enabled: ZCam.currentElement !== null
            onTriggered: {
                ZCam.deleteCurrentElement();
                }
            }
        }
    }
