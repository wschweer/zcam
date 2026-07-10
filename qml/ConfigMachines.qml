//=============================================================================
//  ZCam
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published in the file LICENSE.GPL
//=============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ZCam

Item {
    id: root

    property int currentMachineIdx: -1

    Rectangle {
        anchors.fill: parent
        color: Material.color(Material.BlueGrey, Material.Shade900)
    }

    MachineModel {
        id: machineModel
        machine: (currentMachineIdx >= 0 && currentMachineIdx < ZCam.machines.machinesModel.length)
                 ? ZCam.machines.machine(currentMachineIdx) : null
    }

    // When the machine list changes, keep the current index valid.
    Connections {
        target: ZCam.machines
        function onMachinesModelChanged() {
            if (currentMachineIdx >= ZCam.machines.machinesModel.length) {
                currentMachineIdx = ZCam.machines.machinesModel.length - 1;
            } else if (currentMachineIdx < 0 && ZCam.machines.machinesModel.length > 0) {
                currentMachineIdx = 0;
            }
            machineList.model = ZCam.machines.machinesModel;
        }
    }

    // When machine data changes (property edited), update the machines list
    // so the machine name in the list stays in sync.
    Connections {
        target: machineModel
        function onMachineDataChanged() {
            // Force a refresh of the machines list display
            machineList.model = ZCam.machines.machinesModel;
        }
    }

    SplitView {
        anchors.fill: parent
        anchors.margins: 8

        // Left Panel: Machine List
        ColumnLayout {
            SplitView.preferredWidth: 200
            SplitView.minimumWidth: 150

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Machines"; font.bold: true; Layout.fillWidth: true }
                ToolButton {
                    text: "+"
                    onClicked: ZCam.machines.addMachine("New Machine")
                }
                ToolButton {
                    text: "-"
                    onClicked: {
                        if (currentMachineIdx >= 0) {
                            ZCam.machines.removeMachine(currentMachineIdx);
                        }
                    }
                }
            }

            ListView {
                id: machineList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: ZCam.machines.machinesModel
                clip: true
                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: modelData
                    highlighted: index === currentMachineIdx
                    onClicked: {
                        currentMachineIdx = index;
                    }
                }
            }

            Button {
                Layout.fillWidth: true
                text: "Save"
                onClicked: ZCam.saveAssets()
            }
        }

        // Right Panel: Machine Details (dynamically built from properties())
        StackLayout {
            SplitView.fillWidth: true
            currentIndex: currentMachineIdx >= 0 ? 1 : 0

            // 0: Placeholder
            Item {
                Label {
                    anchors.centerIn: parent
                    text: "No machine selected"
                    color: Material.color(Material.BlueGrey, Material.Shade300)
                }
            }

            // 1: Machine Details
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Title
                Label {
                    text: machineModel.title.length > 0 ? machineModel.title : "Machine"
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: 2
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    color: Material.accentColor
                }

                // Thin accent divider
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: Material.accentColor
                    opacity: 0.4
                    Layout.bottomMargin: 2
                }

                // Property editor built from properties() JSON
                PropertyEditor {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: machineModel
                    propertiesJson: machineModel.propertiesJson
                    labelWidth: 140
                }
            }
        }
    }

    Component.onCompleted: {
        machineList.model = ZCam.machines.machinesModel;
        if (ZCam.machines.machinesModel.length > 0) {
            currentMachineIdx = 0;
        }
    }
}