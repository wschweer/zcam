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

    ColumnLayout {
        spacing: 0
        anchors.fill: parent

        Label {
            Layout.alignment: Qt.AlignCenter
            text: ZCam.machine.name
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
            hoverEnabled: false
            checked: ZCam.laser.enabled
            onClicked: {
                console.log("toggled "+checked);
                checked ? ZCam.laser.exit() : ZCam.laser.init()
                }
            }
        Label {
            id: statusLabel
            Layout.alignment: Qt.AlignCenter
            text: ZCam.laser.stateText
            color: Material.foreground
            }

        Slider {
            id: elapsedTime
            from: 0
            to: ZCam.laser.estimatedEnd
            value: ZCam.laser.currentTime
            Layout.fillWidth: true
            enabled: ZCam.laser.enabled
            Layout.margins: 10
            }

        RowLayout {
            Layout.margins: 5
            implicitHeight: 20
            Button {
                height: parent.height
                id: framingButton
                text: "Framing"
                checkable: true
                enabled: ZCam.laser.enabled
                checked: ZCam.laser.framing
                Layout.fillWidth: true
                Layout.horizontalStretchFactor: 2
                onClicked: { ZCam.laser.startFraming()}
                Material.foreground: "black"
                }
            Button {
                height: parent.height
                id: startButton
                Layout.fillWidth: true
                Layout.horizontalStretchFactor: 2
                text: "Start"
                Material.foreground: "black"
                enabled: ZCam.laser.enabled
                checked: ZCam.laser.marking
                onClicked: { ZCam.laser.startMarking() }
                }
            }
        RowLayout {
            Layout.margins: 5
            implicitHeight: 20
            Button {
                id: stopButton
                text: "Stop"
                Material.foreground: "black"
                enabled: ZCam.laser.enabled
                Layout.fillWidth: true
                onClicked: { ZCam.laser.stop() }
                }
            }

        // two framing modes:
        //  - countour framing
        //  - bounding box framing

        RowLayout {
            Layout.margins: 10
            RadioButton {
                id: framing1
                text: "Contour"
                checkable: true
                enabled: laserLogo.checked
                checked: ZCam.laser.framing1
                Layout.fillWidth: true
                Layout.horizontalStretchFactor: 1
                onClicked: {
                    if (checked)
                        framing2.checked = false
                    }
                }
            RadioButton {
                id: framing2
                text: "Bounding Box"
                checkable: true
                enabled: laserLogo.checked
                checked: ZCam.laser.framing2
                padding: 10
                Layout.fillWidth: true
                Layout.horizontalStretchFactor: 1
                onClicked: {
                    if (checked)
                        framing1.checked = false
                    }
                }
            }

        // test mode:   write with reduced power
        // dry run:     do not output anything to the laser

        RowLayout {
            CheckBox {
                id: testMode
                text: "Test-Mode"
                Synchronizer on checked {
                    sourceObject: ZCam.laser
                    sourceProperty: "testMode"
                    }
                }
            CheckBox {
                id: dryRun
                text: "Dry Run"
                Synchronizer on checked {
                    sourceObject: ZCam.laser
                    sourceProperty: "dryRun"
                    }
                }
            }
        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
            }
        }
    }
