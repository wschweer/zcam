//=============================================================================
//  wcam
//
//  Copyright (C) 2024-2025 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import Qt.labs.synchronizer
import ZCam

Rectangle {
    focus: true
    id: laserPanel
    color: Material.color(Material.BlueGrey, Material.Shade800)
    // The machine itself is the laser (Laser : Machine), so we cast
    // via qml.  If the machine is not a Laser (e.g. GCode CNC), laser is null.
    property var machine: ZCam.project?.machine ?? null
    property var laser: machine && machine.toString().indexOf("Laser") >= 0 ? machine : null

    ColumnLayout {
        spacing: 0
        anchors.fill: parent

        Label {
            Layout.alignment: Qt.AlignCenter
            text: ZCam.project && ZCam.project.machineName ? ZCam.project.machineName : "No Laser configured"
            color: Material.foreground
            }
        ToolButton {
            id: laserLogo
            icon.source: (checked) ? "qrc:///icons/laser.svg" : "qrc:///icons/laser-off.svg"
            icon.width: width
            icon.height: height
            icon.color: "transparent"
            implicitWidth: parent.width / 2
            implicitHeight: parent.width / 2
            flat: true
            Layout.alignment: Qt.AlignCenter
            Layout.fillWidth: true
            hoverEnabled: true
            checked: laserPanel.laser && laserPanel.laser.enabled
            onClicked: {
                console.log("toggled "+checked);
                checked ? laserPanel.laser.exit() : laserPanel.laser.init()
                }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Switch Laser On")
            }
        Label {
            id: statusLabel
            Layout.alignment: Qt.AlignCenter
            text: laserPanel.laser?.stateText ?? ""
            color: Material.foreground
            }

        Slider {
            id: elapsedTime
            from: 0
            to: laserPanel.laser?.estimatedEnd ?? 0
            value: laserPanel.laser?.currentTime ?? 0
            Layout.fillWidth: true
            enabled: laserPanel.laser?.enabled ?? false
            Layout.margins: 10
            }

        RowLayout {
            Layout.margins: 5
            implicitHeight: 20
                enabled: laserPanel.laser?.enabled ?? false
            Button {
                id: framingButton
                text: "Framing"
                checkable: true
                checked: laserPanel.laser?.framing ?? false
                Layout.fillWidth: true
                Layout.horizontalStretchFactor: 2
                onClicked: { laserPanel.laser.startFraming()}
                Material.foreground: "black"
                }
            Button {
                id: startButton
                Layout.fillWidth: true
                text: "Marking"
                Material.foreground: "black"
                enabled: laserPanel.laser?.enabled ?? false
                checked: laserPanel.laser?.marking ?? false
                onClicked: { laserPanel.laser.startMarking() }
                }
            }
        RowLayout {
            Layout.margins: 5
            implicitHeight: 20
            Button {
                id: stopButton
                text: "Stop"
                Material.foreground: "black"
                enabled: laserPanel.laser?.enabled ?? false
                Layout.fillWidth: true
                onClicked: { laserPanel.laser.stop() }
                }
            }

        // test mode:   write with reduced power
        // dry run:     do not output anything to the laser

        RowLayout {
            CheckBox {
                id: testMode
                text: "Test-Mode"
                Synchronizer on checked {
                    sourceObject: laserPanel.laser
                    sourceProperty: "testMode"
                    }
                }
            CheckBox {
                id: dryRun
                text: "Dry Run"
                Synchronizer on checked {
                    sourceObject: laserPanel.laser
                    sourceProperty: "dryRun"
                    }
                }
            }

        // ── IO Port Section ─────────────────────────────────────
        // Two rows of 16 toggle buttons (labelled 0–15) that show and
        // control the 16-bit input and output ports of the laser.
        // The output row buttons are toggleable and write the
        // corresponding bit to the laser output port.
        // The input row labels reflect the hardware input port state,
        // polled via the inputPort property.

        Label {
            Layout.alignment: Qt.AlignLeft
            Layout.margins: 5
            text: "Output Port"
            color: Material.foreground
            font.bold: true
            enabled: laserPanel.laser?.enabled ?? false
            }
        GridLayout {
            id: outputPortGrid
            columns: 16
            Layout.fillWidth: true
            Layout.margins: 5
            enabled: laserPanel.laser?.enabled ?? false

            Repeater {
                model: 16
                delegate: ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        radius: 4
                        color: laserPanel.laser && ((laserPanel.laser.outputPort >> modelData) & 1)
                               ? Material.color(Material.Blue, Material.Shade500)
                               : Material.color(Material.Grey, Material.Shade700)
                        border.width: 1
                        border.color: Material.color(Material.Grey, Material.Shade500)

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (laserPanel.laser)
                                    laserPanel.laser.toggleOutputBit(modelData)
                                }
                            }

                        Label {
                            anchors.centerIn: parent
                            text: modelData
                            color: "white"
                            font.bold: true
                            font.pixelSize: 10
                            }
                        }
                    Label {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignCenter
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData
                        color: Material.foreground
                        font.pixelSize: 9
                        }
                    }
                }
            }

        Label {
            Layout.alignment: Qt.AlignLeft
            Layout.margins: 5
            text: "Input Port"
            color: Material.foreground
            font.bold: true
            enabled: laserPanel.laser?.enabled ?? false
            }
        GridLayout {
            id: inputPortGrid
            columns: 16
            Layout.fillWidth: true
            Layout.margins: 5
            enabled: laserPanel.laser?.enabled ?? false

            Repeater {
                model: 16
                delegate: ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        radius: 4
                        color: laserPanel.laser && ((laserPanel.laser.inputPort >> modelData) & 1)
                               ? Material.color(Material.Green, Material.Shade500)
                               : Material.color(Material.Grey, Material.Shade700)
                        border.width: 1
                        border.color: Material.color(Material.Grey, Material.Shade500)

                        Label {
                            anchors.centerIn: parent
                            text: modelData
                            color: "white"
                            font.bold: true
                            font.pixelSize: 10
                            }
                        }
                    Label {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignCenter
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData
                        color: Material.foreground
                        font.pixelSize: 9
                        }
                    }
                }
            }

        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
            }
        }
    }