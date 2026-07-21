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
    property var laser: ZCam.project?.machine?.laser ?? null

    ColumnLayout {
        spacing: 0
        anchors.fill: parent

        Label {
            Layout.alignment: Qt.AlignCenter
            text: ZCam.project && ZCam.project.machineName ? ZCam.project.machineName : ""
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
            checked: laserPanel.laser && laserPanel.laser.enabled
            onClicked: {
                console.log("toggled "+checked);
                checked ? laserPanel.laser.exit() : laserPanel.laser.init()
                }
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
            to: ZCam.project?.machine?.laser.estimatedEnd ?? 0
            value: laserPanel.laser?.currentTime ?? 0
            Layout.fillWidth: true
            enabled: laserPanel.laser?.enabled ?? false
            Layout.margins: 10
            }

        RowLayout {
            Layout.margins: 5
            implicitHeight: 20
            Button {
                implicitHeight: parent.height
                id: framingButton
                text: "Framing"
                checkable: true
                enabled: laserPanel.laser?.enabled ?? false
                checked: laserPanel.laser?.framing ?? false
                Layout.fillWidth: true
                Layout.horizontalStretchFactor: 2
                onClicked: { laserPanel.laser.startFraming()}
                Material.foreground: "black"
                }
            Button {
                implicitHeight: parent.height
                id: startButton
                Layout.fillWidth: true
                Layout.horizontalStretchFactor: 2
                text: "Start"
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
        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
            }
        }
    }
