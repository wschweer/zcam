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

    Connections {
        target: ZCam.machines
        function onMachinesModelChanged() {
            if (currentMachineIdx >= ZCam.machines.machinesModel.length) {
                currentMachineIdx = ZCam.machines.machinesModel.length - 1;
            } else if (currentMachineIdx < 0 && ZCam.machines.machinesModel.length > 0) {
                currentMachineIdx = 0;
            }
            machineList.model = ZCam.machines.machinesModel;
            updateDetails();
        }
    }

    function updateDetails() {
        if (currentMachineIdx >= 0 && currentMachineIdx < ZCam.machines.machinesModel.length) {
            let m = ZCam.machines.machine(currentMachineIdx);
            machineNameField.text = m.name;
            machineTypeField.text = m.machineType;
            machineDescField.text = m.description;
            travelXBox.value = m.travelX * 1000;
            travelYBox.value = m.travelY * 1000;
            travelZBox.value = m.travelZ * 1000;
            travelSpeedBox.value = m.travelSpeed * 1000;
            framingSpeedBox.value = m.framingSpeed * 1000;
            safeDist1Box.value = m.safeDist1 * 1000;
            safeDist2Box.value = m.safeDist2 * 1000;
            maxXYFeedBox.value = m.maxXYFeed * 1000;
            maxZFeedBox.value = m.maxZFeed * 1000;
            maxXYAccelerationBox.value = m.maxXYAcceleration * 1000;
            maxZAccelerationBox.value = m.maxZAcceleration * 1000;
            minSpindleBox.value = m.minSpindle * 1000;
            maxSpindleBox.value = m.maxSpindle * 1000;
            precisionBox.value = m.precision * 1000;
            ncPrecisionBox.value = m.ncPrecision * 1000;
            circlePrecisionBox.value = m.circlePrecision * 1000;
        } else {
            machineNameField.text = "";
            machineTypeField.text = "";
            machineDescField.text = "";
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
                        updateDetails();
                    }
                }
            }

            Button {
                Layout.fillWidth: true
                text: "Save"
                onClicked: ZCam.saveAssets()
            }
        }

        // Right Panel: Machine Details
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
            GroupBox {
                title: "Machine Settings"
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScrollView {
                    id: machineScrollView
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: machineSettingsGrid.implicitHeight

                    GridLayout {
                        id: machineSettingsGrid
                        columns: 2
                        width: machineScrollView.availableWidth

                        Label { text: "Name:" }
                        TextField {
                            id: machineNameField
                            Layout.fillWidth: true
                            onEditingFinished: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.name = text;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                        }

                        Label { text: "Type:" }
                        TextField {
                            id: machineTypeField
                            Layout.fillWidth: true
                            onEditingFinished: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.machineType = text;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                        }

                        Label { text: "Description:" }
                        TextField {
                            id: machineDescField
                            Layout.fillWidth: true
                            onEditingFinished: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.description = text;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                        }

                        Label { text: "Travel X (mm):" }
                        SpinBox {
                            id: travelXBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.travelX = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Travel Y (mm):" }
                        SpinBox {
                            id: travelYBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.travelY = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Travel Z (mm):" }
                        SpinBox {
                            id: travelZBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.travelZ = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Travel Speed (mm/s):" }
                        SpinBox {
                            id: travelSpeedBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.travelSpeed = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Framing Speed (mm/s):" }
                        SpinBox {
                            id: framingSpeedBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.framingSpeed = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Safe Dist 1 (mm):" }
                        SpinBox {
                            id: safeDist1Box
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.safeDist1 = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Safe Dist 2 (mm):" }
                        SpinBox {
                            id: safeDist2Box
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.safeDist2 = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Max XY Feed:" }
                        SpinBox {
                            id: maxXYFeedBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.maxXYFeed = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Max Z Feed:" }
                        SpinBox {
                            id: maxZFeedBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.maxZFeed = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Max XY Acceleration:" }
                        SpinBox {
                            id: maxXYAccelerationBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.maxXYAcceleration = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Max Z Acceleration:" }
                        SpinBox {
                            id: maxZAccelerationBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.maxZAcceleration = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Min Spindle:" }
                        SpinBox {
                            id: minSpindleBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.minSpindle = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Max Spindle:" }
                        SpinBox {
                            id: maxSpindleBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.maxSpindle = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Precision:" }
                        SpinBox {
                            id: precisionBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.precision = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "NC Precision:" }
                        SpinBox {
                            id: ncPrecisionBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.ncPrecision = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                        Label { text: "Circle Precision:" }
                        SpinBox {
                            id: circlePrecisionBox
                            from: 0
                            to: 1000000
                            editable: true
                            onValueModified: {
                                let m = ZCam.machines.machine(currentMachineIdx);
                                m.circlePrecision = value / 1000.0;
                                ZCam.machines.updateMachine(currentMachineIdx, m);
                            }
                            textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                            valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                        }

                    }
                }
            }
        } // End StackLayout
    } // End SplitView

    Component.onCompleted: {
        machineList.model = ZCam.machines.machinesModel;
        if (ZCam.machines.machinesModel.length > 0) {
            currentMachineIdx = 0;
            updateDetails();
        }
    }
}
