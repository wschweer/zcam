//=============================================================================
//  ZCam - manufactoring tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ZCam

Dialog {
    id: galvoCalDialog
    title: qsTr("9-Point Galvo Calibration")
    modal: true
    anchors.centerIn: parent
    width: 700
    height: 560

    property Machine machine: null
    property double nominalSpacing: machine ? machine.maxTravel.x * 0.5 : 75.0

    // The GalvoCalibration instance is owned by ZCam and exposed
    // via the galvoCalibration property.
    property GalvoCalibration calib: ZCam.galvoCalibration

    // 12 measurement values (defaults = nominal)
    property double xTopLeft: nominalSpacing
    property double xTopRight: nominalSpacing
    property double xMiddleLeft: nominalSpacing
    property double xMiddleRight: nominalSpacing
    property double xBottomLeft: nominalSpacing
    property double xBottomRight: nominalSpacing
    property double yLeftTop: nominalSpacing
    property double yLeftBottom: nominalSpacing
    property double yCenterTop: nominalSpacing
    property double yCenterBottom: nominalSpacing
    property double yRightTop: nominalSpacing
    property double yRightBottom: nominalSpacing

    onOpened: {
        machine = ZCam.project ? ZCam.project.machine : null
        if (machine)
            nominalSpacing = machine.maxTravel.x * 0.5
        // reset fields to nominal
        xTopLeft = xTopRight = xMiddleLeft = xMiddleRight = xBottomLeft = xBottomRight = nominalSpacing
        yLeftTop = yLeftBottom = yCenterTop = yCenterBottom = yRightTop = yRightBottom = nominalSpacing
        canvas.requestPaint()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // --- title row: machine name ---
        Label {
            text: galvoCalDialog.machine ? galvoCalDialog.machine.name : qsTr("(no machine)")
            font.bold: true
            font.pixelSize: 16
            Layout.alignment: Qt.AlignHCenter
        }

        // --- info ---
        Label {
            text: qsTr("Burn the \"Galvo Test 9\" pattern on laser paper, measure the 12 lines and enter the values below.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            font.pixelSize: 11
        }

        // --- diagram + input fields ---
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // --- canvas diagram ---
            Canvas {
                id: canvas
                Layout.preferredWidth: 320
                Layout.preferredHeight: 320
                Layout.alignment: Qt.AlignTop

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    var cx = width / 2
                    var cy = height / 2
                    var half = width * 0.38   // half grid dimension in px

                    ctx.strokeStyle = "white"
                    ctx.lineWidth = 1

                    // outer rectangle
                    ctx.strokeRect(cx - half, cy - half, half * 2, half * 2)

                    // grid lines at 0
                    ctx.beginPath()
                    ctx.moveTo(cx - half, cy); ctx.lineTo(cx + half, cy)
                    ctx.moveTo(cx, cy - half); ctx.lineTo(cx, cy + half)
                    ctx.stroke()

                    // measurement arrows: X axis (horizontal), left/right
                    var arrows = [
                        // x cross: 3 row positions, each with left+right arrow
                        { x1: cx - half, y1: cy - half/2, x2: cx, y2: cy - half/2, id: "x1" },
                        { x1: cx, y1: cy - half/2, x2: cx + half, y2: cy - half/2, id: "x2" },
                        { x1: cx - half, y1: cy, x2: cx, y2: cy, id: "x3" },
                        { x1: cx, y1: cy, x2: cx + half, y2: cy, id: "x4" },
                        { x1: cx - half, y1: cy + half/2, x2: cx, y2: cy + half/2, id: "x5" },
                        { x1: cx, y1: cy + half/2, x2: cx + half, y2: cy + half/2, id: "x6" },
                        // y cross: 3 column positions, each with top+bottom arrow
                        { x1: cx - half/2, y1: cy - half, x2: cx - half/2, y2: cy, id: "y1" },
                        { x1: cx - half/2, y1: cy, x2: cx - half/2, y2: cy + half, id: "y2" },
                        { x1: cx, y1: cy - half, x2: cx, y2: cy, id: "y3" },
                        { x1: cx, y1: cy, x2: cx, y2: cy + half, id: "y4" },
                        { x1: cx + half/2, y1: cy - half, x2: cx + half/2, y2: cy, id: "y5" },
                        { x1: cx + half/2, y1: cy, x2: cx + half/2, y2: cy + half, id: "y6" }
                    ]

                    ctx.strokeStyle = "#4fc3f7"
                    ctx.fillStyle = "#4fc3f7"
                    ctx.lineWidth = 1.8
                    ctx.font = "bold 9px sans-serif"

                    for (var i = 0; i < arrows.length; i++) {
                        var a = arrows[i]
                        ctx.beginPath()
                        ctx.moveTo(a.x1, a.y1)
                        ctx.lineTo(a.x2, a.y2)
                        ctx.stroke()
                        // arrow head
                        var dx = a.x2 - a.x1
                        var dy = a.y2 - a.y1
                        var len = Math.sqrt(dx*dx + dy*dy)
                        if (len > 10) {
                            var ux = dx/len, uy = dy/len
                            var px = -uy, py = ux
                            var tipX = a.x2 - ux*8, tipY = a.y2 - uy*8
                            ctx.beginPath()
                            ctx.moveTo(a.x2, a.y2)
                            ctx.lineTo(tipX + px*4, tipY + py*4)
                            ctx.lineTo(tipX - px*4, tipY - py*4)
                            ctx.closePath()
                            ctx.fill()
                        }
                        // label
                        var lx = (a.x1 + a.x2) / 2
                        var ly = (a.y1 + a.y2) / 2
                        ctx.fillText(a.id, lx - 8, ly - 4)
                    }

                    // grid spacing text
                    ctx.fillStyle = "white"
                    ctx.font = "9px sans-serif"
                    ctx.fillText(galvoCalDialog.nominalSpacing.toFixed(1) + " mm", 6, height - 6)
                }
            }

            // --- 12 input fields in two groups ---
            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                spacing: 4

                GroupBox {
                    title: qsTr("X-Axis Lines (mm)")
                    Layout.fillWidth: true
                    GridLayout {
                        columns: 4
                        columnSpacing: 4
                        rowSpacing: 2
                        Label { text: "x1"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: xF1; Layout.preferredWidth: 65
                            text: galvoCalDialog.xTopLeft.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.xTopLeft = parseFloat(text)
                        }
                        Label { text: "x2"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: xF2; Layout.preferredWidth: 65
                            text: galvoCalDialog.xTopRight.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.xTopRight = parseFloat(text)
                        }
                        Label { text: "x3"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: xF3; Layout.preferredWidth: 65
                            text: galvoCalDialog.xMiddleLeft.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.xMiddleLeft = parseFloat(text)
                        }
                        Label { text: "x4"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: xF4; Layout.preferredWidth: 65
                            text: galvoCalDialog.xMiddleRight.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.xMiddleRight = parseFloat(text)
                        }
                        Label { text: "x5"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: xF5; Layout.preferredWidth: 65
                            text: galvoCalDialog.xBottomLeft.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.xBottomLeft = parseFloat(text)
                        }
                        Label { text: "x6"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: xF6; Layout.preferredWidth: 65
                            text: galvoCalDialog.xBottomRight.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.xBottomRight = parseFloat(text)
                        }
                    }
                }

                GroupBox {
                    title: qsTr("Y-Axis Lines (mm)")
                    Layout.fillWidth: true
                    GridLayout {
                        columns: 4
                        columnSpacing: 4
                        rowSpacing: 2
                        Label { text: "y1"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: yF1; Layout.preferredWidth: 65
                            text: galvoCalDialog.yLeftTop.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.yLeftTop = parseFloat(text)
                        }
                        Label { text: "y2"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: yF2; Layout.preferredWidth: 65
                            text: galvoCalDialog.yLeftBottom.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.yLeftBottom = parseFloat(text)
                        }
                        Label { text: "y3"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: yF3; Layout.preferredWidth: 65
                            text: galvoCalDialog.yCenterTop.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.yCenterTop = parseFloat(text)
                        }
                        Label { text: "y4"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: yF4; Layout.preferredWidth: 65
                            text: galvoCalDialog.yCenterBottom.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.yCenterBottom = parseFloat(text)
                        }
                        Label { text: "y5"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: yF5; Layout.preferredWidth: 65
                            text: galvoCalDialog.yRightTop.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.yRightTop = parseFloat(text)
                        }
                        Label { text: "y6"; Layout.alignment: Qt.AlignVCenter }
                        TextField {
                            id: yF6; Layout.preferredWidth: 65
                            text: galvoCalDialog.yRightBottom.toFixed(2)
                            validator: DoubleValidator { bottom: 0.01 }
                            onEditingFinished: galvoCalDialog.yRightBottom = parseFloat(text)
                        }
                    }
                }

                // --- results display ---
                GroupBox {
                    title: qsTr("Computed Correction")
                    Layout.fillWidth: true
                    visible: calib.valid
                    GridLayout {
                        columns: 2
                        columnSpacing: 8
                        Label { text: qsTr("Scale:"); font.bold: true }
                        Label { text: calib.valid ? "(%1, %2)".arg(calib.scale.x.toFixed(6)).arg(calib.scale.y.toFixed(6)) : "-" }
                        Label { text: qsTr("Bulge:"); font.bold: true }
                        Label { text: calib.valid ? "(%1, %2)".arg(calib.bulge.x.toExponential(4)).arg(calib.bulge.y.toExponential(4)) : "-" }
                        Label { text: qsTr("RMS error:"); font.bold: true }
                        Label {
                            text: calib.valid ? calib.rmsError.toFixed(4) + " mm" : "-"
                            color: calib.valid && calib.rmsError < 0.1 ? "#4caf50" : (calib.valid && calib.rmsError < 0.5 ? "#ff9800" : "#f44336")
                        }
                    }
                }

                // Spacer
                Item { Layout.fillHeight: true }
            }
        }

        // --- buttons ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Compute")
                onClicked: {
                    calib.compute(galvoCalDialog.machine,
                                  galvoCalDialog.xTopLeft, galvoCalDialog.xTopRight,
                                  galvoCalDialog.xMiddleLeft, galvoCalDialog.xMiddleRight,
                                  galvoCalDialog.xBottomLeft, galvoCalDialog.xBottomRight,
                                  galvoCalDialog.yLeftTop, galvoCalDialog.yLeftBottom,
                                  galvoCalDialog.yCenterTop, galvoCalDialog.yCenterBottom,
                                  galvoCalDialog.yRightTop, galvoCalDialog.yRightBottom)
                }
            }

            Button {
                text: qsTr("Change Calibration")
                highlighted: true
                enabled: calib.valid
                onClicked: {
                    calib.applyToMachine(galvoCalDialog.machine)
                    galvoCalDialog.accept()
                }
            }

            Button {
                text: qsTr("Abort")
                onClicked: galvoCalDialog.reject()
            }
        }
    }
}
