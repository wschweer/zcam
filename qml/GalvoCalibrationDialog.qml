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
    width: 988
    height: 680
    padding: 16

    // Use the configured font from ZCam → Config
    readonly property string cfgFontFamily: ZCam.config ? ZCam.config.font : "NotoSans"
    readonly property int cfgFontSize: ZCam.config ? ZCam.config.fontSize : 12
    // Canvas font uses px, QML font uses pt — convert: 1pt ≈ 1.333px at 96dpi
    readonly property int cfgFontPx: Math.round(cfgFontSize * 4 / 3)
    readonly property font unifiedFont: Qt.font({ family: cfgFontFamily, pointSize: cfgFontSize })
    readonly property font unifiedFontBold: Qt.font({ family: cfgFontFamily, pointSize: cfgFontSize, weight: Font.Bold })

    property Machine machine: null
    property double nominalSpacing: machine ? machine.maxTravel.x * 0.5 : 87.5
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
        xTopLeft = xTopRight = xMiddleLeft = xMiddleRight = xBottomLeft = xBottomRight = nominalSpacing
        yLeftTop = yLeftBottom = yCenterTop = yCenterBottom = yRightTop = yRightBottom = nominalSpacing
        calib.clear()
        canvas.requestPaint()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // --- info ---
        Label {
            text: "Machine: " + (galvoCalDialog.machine ? galvoCalDialog.machine.name : qsTr("(no machine)"))
            font: unifiedFontBold
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: qsTr("Burn the \"Galvo Test 9\" pattern on laser paper and enter the 12 measured line lengths (mm).")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            font: unifiedFont
            horizontalAlignment: Text.AlignHCenter
        }

        // --- diagram + input fields ---
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // --- canvas diagram + correction below ---
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 6

                Item {
                    Layout.preferredWidth: 380
                    Layout.preferredHeight: 380
                    Layout.alignment: Qt.AlignTop

                    Canvas {
                        id: canvas
                        anchors.fill: parent

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var cx = width / 2
                            var cy = height / 2
                            var half = width * 0.36   // half the square size
                            var ext = 20              // how far lines extend past the square

                            // The 3×3 grid: 3 horizontal + 3 vertical lines
                            // Lines extend slightly beyond the square so the
                            // corner crossings are clearly visible.
                            var ys = [cy - half, cy, cy + half]
                            var xs = [cx - half, cx, cx + half]

                            ctx.strokeStyle = "white"
                            ctx.lineWidth = 1.2
                            ctx.beginPath()
                            // 3 horizontal lines (extend beyond square left/right)
                            for (var yi = 0; yi < 3; yi++) {
                                ctx.moveTo(cx - half - ext, ys[yi])
                                ctx.lineTo(cx + half + ext, ys[yi])
                            }
                            // 3 vertical lines (extend beyond square top/bottom)
                            for (var xi = 0; xi < 3; xi++) {
                                ctx.moveTo(xs[xi], cy - half - ext)
                                ctx.lineTo(xs[xi], cy + half + ext)
                            }
                            ctx.stroke()

                            // --- X labels: above horizontal lines at horizontal midpoint ---
                            // 6 X measurements: left and right half of each of the 3 horizontal lines
                            // Label floats slightly above the line
                            ctx.fillStyle = "#4fc3f7"
                            ctx.font = "bold " + galvoCalDialog.cfgFontPx + "px " + galvoCalDialog.cfgFontFamily
                            ctx.textAlign = "center"
                            ctx.textBaseline = "bottom"
                            var xLift = 3   // px above the line

                            var xLabels = [
                                { mx: cx - half * 0.5, my: ys[0] - xLift, id: "x1" },
                                { mx: cx + half * 0.5, my: ys[0] - xLift, id: "x2" },
                                { mx: cx - half * 0.5, my: ys[1] - xLift, id: "x3" },
                                { mx: cx + half * 0.5, my: ys[1] - xLift, id: "x4" },
                                { mx: cx - half * 0.5, my: ys[2] - xLift, id: "x5" },
                                { mx: cx + half * 0.5, my: ys[2] - xLift, id: "x6" }
                            ]
                            for (var i = 0; i < xLabels.length; i++)
                                ctx.fillText(xLabels[i].id, xLabels[i].mx, xLabels[i].my)

                            // --- Y labels: beside vertical lines at vertical midpoint ---
                            // 6 Y measurements: top and bottom half of each of the 3 vertical lines
                            // Label is placed to the right of the line at the segment midpoint
                            ctx.textAlign = "left"
                            ctx.textBaseline = "middle"
                            var yOff = 4
                            var yLabels = [
                                { mx: xs[0] + yOff, my: cy - half * 0.5, id: "y1" },
                                { mx: xs[0] + yOff, my: cy + half * 0.5, id: "y2" },
                                { mx: xs[1] + yOff, my: cy - half * 0.5, id: "y3" },
                                { mx: xs[1] + yOff, my: cy + half * 0.5, id: "y4" },
                                { mx: xs[2] + yOff, my: cy - half * 0.5, id: "y5" },
                                { mx: xs[2] + yOff, my: cy + half * 0.5, id: "y6" }
                            ]
                            for (var j = 0; j < yLabels.length; j++)
                                ctx.fillText(yLabels[j].id, yLabels[j].mx, yLabels[j].my)

                            // nominal label
                            ctx.fillStyle = "white"
                            ctx.font = galvoCalDialog.cfgFontPx + "px " + galvoCalDialog.cfgFontFamily
                            ctx.textAlign = "left"
                            ctx.textBaseline = "alphabetic"
                            ctx.fillText("nominal = " + galvoCalDialog.nominalSpacing.toFixed(1) + " mm", 4, height - 6)
                        }
                    }
                }

                // --- correction (no GroupBox title, always visible) ---
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 12
                    rowSpacing: 2
                    Label {
                        text: qsTr("Scale:")
                        font: unifiedFontBold
                    }
                    Label {
                        font: unifiedFont
                        text: calib.valid
                            ? "%1 %,  %2 %".arg(calib.scale.x.toFixed(3)).arg(calib.scale.y.toFixed(3))
                            : (galvoCalDialog.machine
                                ? "%1 %,  %2 %".arg(galvoCalDialog.machine.galvoScale.x.toFixed(3)).arg(galvoCalDialog.machine.galvoScale.y.toFixed(3))
                                : "—")
                    }
                    Label {
                        text: qsTr("Bulge:")
                        font: unifiedFontBold
                    }
                    Label {
                        font: unifiedFont
                        text: calib.valid
                            ? "%1,  %2".arg(calib.bulge.x.toExponential(3)).arg(calib.bulge.y.toExponential(3))
                            : (galvoCalDialog.machine
                                ? "%1,  %2".arg(galvoCalDialog.machine.galvoBulge.x.toExponential(3)).arg(galvoCalDialog.machine.galvoBulge.y.toExponential(3))
                                : "—")
                    }
                    Label {
                        text: qsTr("RMS error:")
                        font: unifiedFontBold
                    }
                    Label {
                        font: unifiedFont
                        text: calib.valid ? calib.rmsError.toFixed(4) + " mm" : "—"
                        color: calib.valid && calib.rmsError < 0.1 ? "#4caf50" : (calib.valid && calib.rmsError < 0.5 ? "#ff9800" : "#f44336")
                    }
                }

                Item { Layout.fillHeight: true }
            }

            // --- 12 input fields in two groups ---
            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                spacing: 8

                GroupBox {
                    title: qsTr("X-Axis (mm)")
                    Layout.fillWidth: true
                    font: unifiedFont
                    GridLayout {
                        anchors.fill: parent
                        columns: 4
                        columnSpacing: 4
                        rowSpacing: 4
                        Label { text: "x1"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xTopLeft.toFixed(2)
                            onEditingFinished: galvoCalDialog.xTopLeft = parseFloat(text.replace(",", "."))
                        }
                        Label { text: "x2"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xTopRight.toFixed(2)
                            onEditingFinished: galvoCalDialog.xTopRight = parseFloat(text.replace(",", "."))
                        }
                        Label { text: "x3"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xMiddleLeft.toFixed(2)
                            onEditingFinished: galvoCalDialog.xMiddleLeft = parseFloat(text.replace(",", "."))
                        }
                        Label { text: "x4"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xMiddleRight.toFixed(2)
                            onEditingFinished: galvoCalDialog.xMiddleRight = parseFloat(text.replace(",", "."))
                        }
                        Label { text: "x5"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xBottomLeft.toFixed(2)
                            onEditingFinished: galvoCalDialog.xBottomLeft = parseFloat(text.replace(",", "."))
                        }
                        Label { text: "x6"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.xBottomRight.toFixed(2)
                            onEditingFinished: galvoCalDialog.xBottomRight = parseFloat(text.replace(",", "."))
                        }
                    }
                }

                GroupBox {
                    title: qsTr("Y-Axis (mm)")
                    Layout.fillWidth: true
                    font: unifiedFont
                    GridLayout {
                        anchors.fill: parent
                        columns: 4
                        columnSpacing: 4
                        rowSpacing: 4
                        Label { text: "y1"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yLeftTop.toFixed(2)
                            onEditingFinished: galvoCalDialog.yLeftTop = parseFloat(text.replace(",", "."))
                        }
                        Label { text: "y2"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yLeftBottom.toFixed(2)
                            onEditingFinished: galvoCalDialog.yLeftBottom = parseFloat(text.replace(",", "."))
                        }
                        Label { text: "y3"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yCenterTop.toFixed(2)
                            onEditingFinished: galvoCalDialog.yCenterTop = parseFloat(text.replace(",", "."))
                        }
                        Label { text: "y4"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yCenterBottom.toFixed(2)
                            onEditingFinished: galvoCalDialog.yCenterBottom = parseFloat(text.replace(",", "."))
                        }
                        Label { text: "y5"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yRightTop.toFixed(2)
                            onEditingFinished: galvoCalDialog.yRightTop = parseFloat(text.replace(",", "."))
                        }
                        Label { text: "y6"; font: unifiedFont; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 24 }
                        TextField {
                            Layout.fillWidth: true
                            font: unifiedFont
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            horizontalAlignment: Text.AlignHCenter
                            text: galvoCalDialog.yRightBottom.toFixed(2)
                            onEditingFinished: galvoCalDialog.yRightBottom = parseFloat(text.replace(",", "."))
                        }
                    }
                }

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
                font: unifiedFont
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
                font: unifiedFont
                highlighted: true
                enabled: calib.valid && calib.rmsError < 1.0 && galvoCalDialog.allInputsValid
                onClicked: {
                    galvoCalDialog.calib.applyToMachine(galvoCalDialog.machine)
                    galvoCalDialog.accept()
                }
            }

            Button {
                text: qsTr("Abort")
                font: unifiedFont
                onClicked: galvoCalDialog.reject()
            }
        }
    }
}