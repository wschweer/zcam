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
    width: 760
    height: 640
    padding: 20

    property Machine machine: null
    property double nominalSpacing: machine ? machine.maxTravel.x * 0.5 : 87.5
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

    property bool allInputsValid: {
        var fields = [xTopLeft, xTopRight, xMiddleLeft, xMiddleRight, xBottomLeft, xBottomRight,
                      yLeftTop, yLeftBottom, yCenterTop, yCenterBottom, yRightTop, yRightBottom]
        for (var i = 0; i < fields.length; ++i)
            if (!isFinite(fields[i]) || fields[i] <= 0.0)
                return false
        return machine !== null
    }

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
        spacing: 12

        // --- info ---
        Label {
            text: "Machine: " + (galvoCalDialog.machine ? galvoCalDialog.machine.name : qsTr("(no machine)"))
            font.bold: true
            font.pixelSize: 14
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: qsTr("Burn the \"Galvo Test 9\" pattern on laser paper and enter the 12 measured line lengths (mm).")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
        }

        // --- diagram + input fields ---
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20

            // --- canvas diagram ---
            Item {
                Layout.preferredWidth: 340
                Layout.preferredHeight: 340
                Layout.alignment: Qt.AlignTop

                Canvas {
                    id: canvas
                    anchors.fill: parent

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        var cx = width / 2
                        var cy = height / 2
                        var half = width * 0.40   // half grid dimension in px

                        ctx.strokeStyle = "white"
                        ctx.lineWidth = 1.2

                        // outer rectangle (the work area)
                        ctx.strokeRect(cx - half, cy - half, half * 2, half * 2)

                        // 3×3 grid lines
                        ctx.beginPath()
                        // vertical grid lines (-half, 0, +half)
                        ctx.moveTo(cx - half, cy - half); ctx.lineTo(cx - half, cy + half)
                        ctx.moveTo(cx,        cy - half); ctx.lineTo(cx,        cy + half)
                        ctx.moveTo(cx + half, cy - half); ctx.lineTo(cx + half, cy + half)
                        ctx.moveTo(cx + half + 4, cy);   ctx.lineTo(cx + half + 22, cy)   // ext marker right
                        ctx.moveTo(cx - half - 4, cy);   ctx.lineTo(cx - half - 22, cy)   // ext marker left
                        // horizontal grid lines (-half, 0, +half)
                        ctx.moveTo(cx - half, cy - half); ctx.lineTo(cx + half, cy - half)
                        ctx.moveTo(cx - half, cy);        ctx.lineTo(cx + half, cy)
                        ctx.moveTo(cx - half, cy + half); ctx.lineTo(cx + half, cy + half)
                        ctx.moveTo(cx, cy - half - 4);   ctx.lineTo(cx, cy - half - 22)  // ext marker top
                        ctx.moveTo(cx, cy + half + 4);   ctx.lineTo(cx, cy + half + 22)  // ext marker bottom
                        ctx.stroke()

                        // measurement arrows: 12 double-headed arrows
                        // X axis (6 horizontal arrows at 3 Y positions: -half*.5, 0, +half*.5):
                        //   x1/x2 at y=-half*.5, x3/x4 at y=0, x5/x6 at y=+half*.5
                        // Y axis (6 vertical arrows at 3 X positions: -half*.5, 0, +half*.5):
                        //   y1/y2 at x=-half*.5, y3/y4 at x=0, y5/y6 at x=+half*.5
                        var arrows = [
                            // X cross — horizontal double-headed arrows
                            { x1: cx - half, y1: cy - half*0.5, x2: cx,            y2: cy - half*0.5, id: "x1" },
                            { x1: cx,        y1: cy - half*0.5, x2: cx + half,     y2: cy - half*0.5, id: "x2" },
                            { x1: cx - half, y1: cy,            x2: cx,            y2: cy,            id: "x3" },
                            { x1: cx,        y1: cy,            x2: cx + half,     y2: cy,            id: "x4" },
                            { x1: cx - half, y1: cy + half*0.5, x2: cx,            y2: cy + half*0.5, id: "x5" },
                            { x1: cx,        y1: cy + half*0.5, x2: cx + half,     y2: cy + half*0.5, id: "x6" },
                            // Y cross — vertical double-headed arrows
                            { x1: cx - half*0.5, y1: cy - half, x2: cx - half*0.5, y2: cy,            id: "y1" },
                            { x1: cx - half*0.5, y1: cy,        x2: cx - half*0.5, y2: cy + half,     id: "y2" },
                            { x1: cx,            y1: cy - half, x2: cx,            y2: cy,            id: "y3" },
                            { x1: cx,            y1: cy,        x2: cx,            y2: cy + half,     id: "y4" },
                            { x1: cx + half*0.5, y1: cy - half, x2: cx + half*0.5, y2: cy,            id: "y5" },
                            { x1: cx + half*0.5, y1: cy,        x2: cx + half*0.5, y2: cy + half,     id: "y6" }
                        ]

                        ctx.strokeStyle = "#4fc3f7"
                        ctx.fillStyle = "#4fc3f7"
                        ctx.lineWidth = 1.5
                        ctx.font = "bold 10px sans-serif"

                        for (var i = 0; i < arrows.length; i++) {
                            var a = arrows[i]
                            // double-headed arrow
                            var dx = a.x2 - a.x1
                            var dy = a.y2 - a.y1
                            var len = Math.sqrt(dx*dx + dy*dy)
                            if (len < 20) continue
                            var ux = dx/len, uy = dy/len
                            var px = -uy, py = ux
                            var head = 7

                            ctx.beginPath()
                            ctx.moveTo(a.x1 + ux*head, a.y1 + uy*head)
                            ctx.lineTo(a.x2 - ux*head, a.y2 - uy*head)
                            // head at end
                            ctx.moveTo(a.x2, a.y2)
                            ctx.lineTo(a.x2 - ux*head + px*3, a.y2 - uy*head + py*3)
                            ctx.lineTo(a.x2 - ux*head - px*3, a.y2 - uy*head - py*3)
                            ctx.closePath()
                            ctx.fill()
                            // head at start
                            ctx.beginPath()
                            ctx.moveTo(a.x1, a.y1)
                            ctx.lineTo(a.x1 + ux*head + px*3, a.y1 + uy*head + py*3)
                            ctx.lineTo(a.x1 + ux*head - px*3, a.y1 + uy*head - py*3)
                            ctx.closePath()
                            ctx.fill()

                            // label outside the arrow
                            var offX = px * 14, offY = py * 14
                            if (a.id.charAt(0) === "y")
                                offX += (a.x1 < cx ? -8 : 8)
                            else
                                offY += (a.y1 < cy ? -8 : 10)
                            ctx.fillText(a.id, (a.x1 + a.x2) / 2 + offX, (a.y1 + a.y2) / 2 + offY + 3)
                        }

                        // grid spacing label at bottom left
                        ctx.fillStyle = "white"
                        ctx.font = "10px sans-serif"
                        ctx.fillText("nominal = " + galvoCalDialog.nominalSpacing.toFixed(1) + " mm", 4, height - 6)
                    }
                }
            }

            // --- 12 input fields in two groups ---
            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                spacing: 10

                GroupBox {
                    title: qsTr("X-Axis (mm)")
                    Layout.fillWidth: true
                    GridLayout {
                        columns: 4
                        columnSpacing: 4
                        rowSpacing: 4
                        Label { text: "x1"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xTopLeft.toFixed(2)
                            onEditingFinished: galvoCalDialog.xTopLeft = text.replace(",", ".")
                        }
                        Label { text: "x2"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xTopRight.toFixed(2)
                            onEditingFinished: galvoCalDialog.xTopRight = text.replace(",", ".")
                        }
                        Label { text: "x3"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xMiddleLeft.toFixed(2)
                            onEditingFinished: galvoCalDialog.xMiddleLeft = text.replace(",", ".")
                        }
                        Label { text: "x4"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xMiddleRight.toFixed(2)
                            onEditingFinished: galvoCalDialog.xMiddleRight = text.replace(",", ".")
                        }
                        Label { text: "x5"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xBottomLeft.toFixed(2)
                            onEditingFinished: galvoCalDialog.xBottomLeft = text.replace(",", ".")
                        }
                        Label { text: "x6"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xBottomRight.toFixed(2)
                            onEditingFinished: galvoCalDialog.xBottomRight = text.replace(",", ".")
                        }
                    }
                }

                GroupBox {
                    title: qsTr("Y-Axis (mm)")
                    Layout.fillWidth: true
                    GridLayout {
                        columns: 4
                        columnSpacing: 4
                        rowSpacing: 4
                        Label { text: "y1"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yLeftTop.toFixed(2)
                            onEditingFinished: galvoCalDialog.yLeftTop = text.replace(",", ".")
                        }
                        Label { text: "y2"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yLeftBottom.toFixed(2)
                            onEditingFinished: galvoCalDialog.yLeftBottom = text.replace(",", ".")
                        }
                        Label { text: "y3"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yCenterTop.toFixed(2)
                            onEditingFinished: galvoCalDialog.yCenterTop = text.replace(",", ".")
                        }
                        Label { text: "y4"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yCenterBottom.toFixed(2)
                            onEditingFinished: galvoCalDialog.yCenterBottom = text.replace(",", ".")
                        }
                        Label { text: "y5"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yRightTop.toFixed(2)
                            onEditingFinished: galvoCalDialog.yRightTop = text.replace(",", ".")
                        }
                        Label { text: "y6"; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.preferredWidth: 80
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yRightBottom.toFixed(2)
                            onEditingFinished: galvoCalDialog.yRightBottom = text.replace(",", ".")
                        }
                    }
                }

                // --- results display ---
                GroupBox {
                    title: qsTr("Correction")
                    Layout.fillWidth: true
                    visible: calib.valid
                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 4
                        Label { text: qsTr("Scale:"); font.bold: true }
                        Label {
                            text: calib.valid ? "%1 %,  %2 %".arg(calib.scale.x.toFixed(3)).arg(calib.scale.y.toFixed(3)) : "—"
                        }
                        Label { text: qsTr("Bulge:"); font.bold: true }
                        Label {
                            text: calib.valid ? "%1,  %2".arg(calib.bulge.x.toExponential(3)).arg(calib.bulge.y.toExponential(3)) : "—"
                        }
                        Label { text: qsTr("RMS error:"); font.bold: true }
                        Label {
                            text: calib.valid ? calib.rmsError.toFixed(4) + " mm" : "—"
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
            spacing: 12

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Compute")
                enabled: galvoCalDialog.allInputsValid
                onClicked: {
                    galvoCalDialog.calib.compute(galvoCalDialog.machine,
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
                enabled: calib.valid && calib.rmsError < 1.0 && galvoCalDialog.allInputsValid
                onClicked: {
                    galvoCalDialog.calib.applyToMachine(galvoCalDialog.machine)
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
