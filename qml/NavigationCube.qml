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

import QtQuick
import QtQuick3D
import QtQuick.Controls.Material
import ZCam

// ─────────────────────────────────────────────────────────────────
//  NavigationCube — FreeCAD-style 3D navigation cube
//
//  Uses a single TruncatedCubeGeometry (C++ custom geometry) that
//  produces a proper truncated-cube mesh: 6 octagonal main faces +
//  8 triangular corner faces, each as a named subset.
//
//  Clicking a face snaps to the corresponding standard or isometric
//  view.  Picking is done via pickSubset() which returns the hit
//  subset index; we map that back to a face name → rotation.
// ─────────────────────────────────────────────────────────────────

Item {
    id: root

    signal viewRequested(vector3d rotation)
    property vector3d sceneRotation: Qt.vector3d(0, 0, 0)

    width: cubeSize
    height: cubeSize
    property int cubeSize: 230

    // ── Visual constants ──────────────────────────────────────────
    readonly property color faceColor: "#81d4fa"       // Light Blue 300
    readonly property color faceHoverColor: "#b3e5fc"  // Light Blue 200 (lighter)
    readonly property color faceBorderColor: "#4fc3f7" // Light Blue 400
    readonly property color cornerColor: "#29b6f6"     // Light Blue 400
    readonly property color cornerHoverColor: "#81d4fa"// Light Blue 300 (lighter)
    //    readonly property color textColor: "#ffeb00"       // bright yellow
    readonly property color textColor: "#000000"       // bright yellow
    readonly property real camDist: 260

    // ── Subset index → face name (must match C++ order) ───────────
    //   0-5: top, bottom, front, rear, right, left
    //   6-13: tfr, tfl, trr, trl, bfr, bfl, brr, brl
    readonly property var faceNames: ["top", "bottom", "front", "rear", "right", "left", "tfr", "tfl", "trr", "trl", "bfr", "bfl", "brr", "brl"]
    property string hoveredFace: ""

    // ── Face name → view rotation ─────────────────────────────────
    function rotationForFace(name) {
        switch (name) {
        case "top":
            return Qt.vector3d(0, 0, 0);
        case "bottom":
            return Qt.vector3d(180, 0, 0);
        case "front":
            return Qt.vector3d(-90, 0, 0);
        case "rear":
            return Qt.vector3d(-90, 180, 0);
        case "left":
            return Qt.vector3d(-90, 90, 0);
        case "right":
            return Qt.vector3d(-90, -90, 0);
        // Isometric corner views (Z-up approximation)
        case "tfr":
            return Qt.vector3d(-55, 0, -45); // top-front-right
        case "tfl":
            return Qt.vector3d(-55, 0, 45); // top-front-left
        case "trr":
            return Qt.vector3d(-55, 0, -135); // top-rear-right
        case "trl":
            return Qt.vector3d(-55, 0, 135); // top-rear-left
        case "bfr":
            return Qt.vector3d(55, 0, -45); // bottom-front-right
        case "bfl":
            return Qt.vector3d(55, 0, 45); // bottom-front-left
        case "brr":
            return Qt.vector3d(55, 0, -135); // bottom-rear-right
        case "brl":
            return Qt.vector3d(55, 0, 135); // bottom-rear-left
        default:
            return null;
            }
        }

    // ── Text label textures for the 6 main faces ──────────────────
    //   Each texture is rendered as a Qt Quick Rectangle with Text,
    //   then used as baseColorMap on the corresponding subset material.
    function faceTexture(label, scaleFactor) {
        return Qt.createComponent("about:blank"); // placeholder
        }

    // ── 3D viewport ───────────────────────────────────────────────
    View3D {
        id: navView
        anchors.fill: parent
        renderMode: View3D.Offscreen

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingQuality: SceneEnvironment.High
            }

        PerspectiveCamera {
            position: Qt.vector3d(0, 0, root.camDist)
            clipNear: 1
            clipFar: root.camDist * 10
            }

        DirectionalLight {
            eulerRotation.x: -30
            eulerRotation.y: -70
            brightness: 1.0
            }
        DirectionalLight {
            eulerRotation.x: 30
            eulerRotation.y: 70
            brightness: 0.5
            }

        Node {
            id: cubeNode
            eulerRotation: root.sceneRotation

            // ── The truncated cube ─────────────────────────────────
            Model {
                id: cubeModel
                pickable: true
                geometry: TruncatedCubeGeometry {
                    halfSize: 50
                    truncation: 18
                    }

                // One material per subset (14 total).
                // Indices 0-5: main faces with text labels.
                // Indices 6-13: corner faces, plain color.
                materials: [
                    // 0: top
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "top" ? root.faceHoverColor : root.faceColor
                        roughness: 0.7
                        baseColorMap: Texture {
                            sourceItem: Rectangle {
                                width: 512
                                height: 512
                                color: root.hoveredFace === "top" ? root.faceHoverColor : root.faceColor
                                border.color: root.faceBorderColor
                                border.width: 4
                                radius: 8
                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("Top")
                                    color: root.textColor
                                    font.pixelSize: 150
                                    font.bold: true
                                    }
                                }
                            }
                        },
                    // 1: bottom
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "bottom" ? root.faceHoverColor : root.faceColor
                        roughness: 0.7
                        baseColorMap: Texture {
                            sourceItem: Rectangle {
                                width: 512
                                height: 512
                                color: root.hoveredFace === "bottom" ? root.faceHoverColor : root.faceColor
                                border.color: root.faceBorderColor
                                border.width: 4
                                radius: 8
                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("Bottom")
                                    color: root.textColor
                                    font.pixelSize: 110
                                    font.bold: true
                                    }
                                }
                            }
                        },
                    // 2: front
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "front" ? root.faceHoverColor : root.faceColor
                        roughness: 0.7
                        baseColorMap: Texture {
                            sourceItem: Rectangle {
                                width: 512
                                height: 512
                                color: root.hoveredFace === "front" ? root.faceHoverColor : root.faceColor
                                border.color: root.faceBorderColor
                                border.width: 4
                                radius: 8
                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("Front")
                                    color: root.textColor
                                    font.pixelSize: 130
                                    font.bold: true
                                    }
                                }
                            }
                        },
                    // 3: rear
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "rear" ? root.faceHoverColor : root.faceColor
                        roughness: 0.7
                        baseColorMap: Texture {
                            sourceItem: Rectangle {
                                width: 512
                                height: 512
                                color: root.hoveredFace === "rear" ? root.faceHoverColor : root.faceColor
                                border.color: root.faceBorderColor
                                border.width: 4
                                radius: 8
                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("Rear")
                                    color: root.textColor
                                    font.pixelSize: 130
                                    font.bold: true
                                    }
                                }
                            }
                        },
                    // 4: right
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "right" ? root.faceHoverColor : root.faceColor
                        roughness: 0.7
                        baseColorMap: Texture {
                            sourceItem: Rectangle {
                                width: 512
                                height: 512
                                color: root.hoveredFace === "right" ? root.faceHoverColor : root.faceColor
                                border.color: root.faceBorderColor
                                border.width: 4
                                radius: 8
                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("Right")
                                    color: root.textColor
                                    font.pixelSize: 130
                                    font.bold: true
                                    }
                                }
                            }
                        },
                    // 5: left
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "left" ? root.faceHoverColor : root.faceColor
                        roughness: 0.7
                        baseColorMap: Texture {
                            sourceItem: Rectangle {
                                width: 512
                                height: 512
                                color: root.hoveredFace === "left" ? root.faceHoverColor : root.faceColor
                                border.color: root.faceBorderColor
                                border.width: 4
                                radius: 8
                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("Left")
                                    color: root.textColor
                                    font.pixelSize: 130
                                    font.bold: true
                                    }
                                }
                            }
                        },
                    // 6-13: corner faces (triangles)
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "tfr" ? root.cornerHoverColor : root.cornerColor
                        roughness: 0.5
                        },
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "tfl" ? root.cornerHoverColor : root.cornerColor
                        roughness: 0.5
                        },
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "trr" ? root.cornerHoverColor : root.cornerColor
                        roughness: 0.5
                        },
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "trl" ? root.cornerHoverColor : root.cornerColor
                        roughness: 0.5
                        },
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "bfr" ? root.cornerHoverColor : root.cornerColor
                        roughness: 0.5
                        },
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "bfl" ? root.cornerHoverColor : root.cornerColor
                        roughness: 0.5
                        },
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "brr" ? root.cornerHoverColor : root.cornerColor
                        roughness: 0.5
                        },
                    PrincipledMaterial {
                        baseColor: root.hoveredFace === "brl" ? root.cornerHoverColor : root.cornerColor
                        roughness: 0.5
                        }
                ]
                }
            }
        }

    // ── Mouse handling ────────────────────────────────────────────
    function faceAt(x, y) {
        var result = navView.pick(x, y);
        if (!result || !result.objectHit)
            return "";

        // ── Primary: local normal ──────────────────────────────
        var n = result.normal;
        var name = "";

        if (n) {
            var len = Math.sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (len > 0.001) {
                var nx = n.x / len;
                var ny = n.y / len;
                var nz = n.z / len;

                if (nz > 0.95)
                    name = "top";
                else if (nz < -0.95)
                    name = "bottom";
                else if (ny < -0.95)
                    name = "front";
                else if (ny > 0.95)
                    name = "rear";
                else if (nx > 0.95)
                    name = "right";
                else if (nx < -0.95)
                    name = "left";
                else {
                    // Corner face: diagonal normal (±0.577 each)
                    var sx = nx > 0.3 ? 1 : (nx < -0.3 ? -1 : 0);
                    var sy = ny > 0.3 ? 1 : (ny < -0.3 ? -1 : 0);
                    var sz = nz > 0.3 ? 1 : (nz < -0.3 ? -1 : 0);
                    if (sx !== 0 && sy !== 0 && sz !== 0) {
                        name = (sz > 0 ? "t" : "b") + (sy < 0 ? "f" : "r") + (sx > 0 ? "r" : "l");
                        }
                    }
                }
            }

        // ── Fallback: local hit position ───────────────────────
        if (!name) {
            var p = result.position;
            if (!p)
                return "";

            var h = 50;   // halfSize
            var t = 18;   // truncation
            var eps = 3;

            var ax = Math.abs(p.x);
            var ay = Math.abs(p.y);
            var az = Math.abs(p.z);

            if (az > h - eps && az > ax + t && az > ay + t) {
                name = p.z > 0 ? "top" : "bottom";
                } else if (ay > h - eps && ay > ax + t && ay > az + t) {
                name = p.y < 0 ? "front" : "rear";
                } else if (ax > h - eps && ax > ay + t && ax > az + t) {
                name = p.x > 0 ? "right" : "left";
                } else {
                // Corner face: determine by sign pattern
                var sx2 = p.x > 0 ? 1 : -1;
                var sy2 = p.y > 0 ? 1 : -1;
                var sz2 = p.z > 0 ? 1 : -1;
                name = (sz2 > 0 ? "t" : "b") + (sy2 < 0 ? "f" : "r") + (sx2 > 0 ? "r" : "l");
                }
            }

        return name;
        }

    MouseArea {
        id: navMouse
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true

        onPositionChanged: mouse => {
            root.hoveredFace = root.faceAt(mouse.x, mouse.y);
            }

        onExited: {
            root.hoveredFace = "";
            }

        onClicked: mouse => {
            var name = root.faceAt(mouse.x, mouse.y);
            if (name) {
                var rot = root.rotationForFace(name);
                if (rot)
                    root.viewRequested(rot);
                }
            }
        }

    // ── Arrow Buttons ─────────────────────────────────────────────
    component ArrowButton: Rectangle {
        id: arr
        property string iconText: ""
        property int dx: 0
        property int dy: 0
        width: 30
        height: 30
        color: arrArea.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : "transparent"
        radius: 15
        Text {
            anchors.centerIn: parent
            text: arr.iconText
            color: root.faceBorderColor
            font.pixelSize: 20
            }
        MouseArea {
            id: arrArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                var r = root.sceneRotation;
                root.viewRequested(Qt.vector3d(r.x + arr.dy, r.y + arr.dx, r.z));
                }
            }
        }

    ArrowButton {
        iconText: "▲"
        dx: 0
        dy: -45
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        z: 1
        }
    ArrowButton {
        iconText: "▼"
        dx: 0
        dy: 45
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        z: 1
        }
    ArrowButton {
        iconText: "◀"
        dx: -45
        dy: 0
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        z: 1
        }
    ArrowButton {
        iconText: "▶"
        dx: 45
        dy: 0
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        z: 1
        }
    }