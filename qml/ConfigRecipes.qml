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

    property int currentRecipeIdx: -1
    property int currentLayerIdx: -1

    Rectangle {
        anchors.fill: parent
        color: Material.color(Material.BlueGrey, Material.Shade900)
    }

    Connections {
        target: ZCam.recipes
        function onRecipeModelChanged() {
            if (currentRecipeIdx >= ZCam.recipes.recipeModel.length) {
                currentRecipeIdx = ZCam.recipes.recipeModel.length - 1;
            } else if (currentRecipeIdx < 0 && ZCam.recipes.recipeModel.length > 0) {
                currentRecipeIdx = 0;
            }
            recipeList.model = ZCam.recipes.recipeModel;
            updateDetails();
        }
        function onRecipeChanged(idx) {
            if (idx === currentRecipeIdx) {
                updateDetails();
            }
        }
    }

    function updateDetails() {
        if (currentRecipeIdx >= 0 && currentRecipeIdx < ZCam.recipes.recipeModel.length) {
            let r = ZCam.recipes.recipe(currentRecipeIdx);
            recipeNameField.text = r.name;
            recipeDescField.text = r.description;
            recipePassesBox.value = r.numPasses;
            layerList.model = ZCam.recipes.layerModel(currentRecipeIdx);
            
            if (currentLayerIdx >= layerList.model.length) {
                currentLayerIdx = layerList.model.length - 1;
            } else if (currentLayerIdx < 0 && layerList.model.length > 0) {
                currentLayerIdx = 0;
            }
            layerList.currentIndex = currentLayerIdx;
            
            if (currentLayerIdx >= 0 && currentLayerIdx < layerList.model.length) {
                let l = ZCam.recipes.layer(currentRecipeIdx, currentLayerIdx);
                layerNameField.text = l.name;
                layerEnabledBox.checked = l.enabled;
                layerPowerBox.value = l.power * 100;
                layerSpeedBox.value = l.speed;
                layerTravelSpeedBox.value = l.travelSpeed;
                layerFrequencyBox.value = l.frequency * 100;
                layerPulseWidthBox.value = l.pulseWidth;
                layerNumPassesBox.value = l.numPasses;
                layerIntervalBox.value = l.interval * 1000;
                layerStartAngleBox.value = l.startAngle * 100;
                layerAngleIncrementBox.value = l.angleIncrement * 100;
                layerZigzagBox.checked = l.zigzag;
                layerInterleaveBox.value = l.interleave;
                layerWobbleBox.checked = l.wobble;
                layerWobbleStepBox.value = l.wobbleStep * 1000;
                layerWobbleSizeBox.value = l.wobbleSize * 1000;
            } else {
                layerNameField.text = "";
            }
        } else {
            recipeNameField.text = "";
            recipeDescField.text = "";
            layerList.model = [];
            currentLayerIdx = -1;
        }
    }

    SplitView {
        anchors.fill: parent
        anchors.margins: 8

        // Left Panel: Recipe List
        ColumnLayout {
            SplitView.preferredWidth: 200
            SplitView.minimumWidth: 150
            
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Recipes"; font.bold: true; Layout.fillWidth: true }
                ToolButton {
                    text: "+"
                    onClicked: ZCam.recipes.addRecipe("New Recipe")
                }
                ToolButton {
                    text: "-"
                    onClicked: {
                        if (currentRecipeIdx >= 0) {
                            ZCam.recipes.removeRecipe(currentRecipeIdx);
                        }
                    }
                }
            }

            ListView {
                id: recipeList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: ZCam.recipes.recipeModel
                clip: true
                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: modelData
                    highlighted: index === currentRecipeIdx
                    onClicked: {
                        currentRecipeIdx = index;
                        currentLayerIdx = -1;
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

        // Right Panel: Recipe Details
        StackLayout {
            SplitView.fillWidth: true
            currentIndex: currentRecipeIdx >= 0 ? 1 : 0

            // 0: Placeholder
            Item {
                Label {
                    anchors.centerIn: parent
                    text: "No recipe selected"
                    color: Material.color(Material.BlueGrey, Material.Shade300)
                }
            }

            // 1: Recipe Details
            ColumnLayout {

            // Header: Recipe Info
            GroupBox {
                title: "Recipe Details"
                Layout.fillWidth: true
                GridLayout {
                    columns: 2
                    anchors.fill: parent

                    Label { text: "Name:" }
                    TextField {
                        id: recipeNameField
                        Layout.fillWidth: true
                        onEditingFinished: {
                            let r = ZCam.recipes.recipe(currentRecipeIdx);
                            r.name = text;
                            ZCam.recipes.updateRecipe(currentRecipeIdx, r);
                        }
                    }

                    Label { text: "Description:" }
                    TextField {
                        id: recipeDescField
                        Layout.fillWidth: true
                        onEditingFinished: {
                            let r = ZCam.recipes.recipe(currentRecipeIdx);
                            r.description = text;
                            ZCam.recipes.updateRecipe(currentRecipeIdx, r);
                        }
                    }

                    Label { text: "Passes:" }
                    SpinBox {
                        id: recipePassesBox
                        from: 1
                        to: 1000
                        onValueModified: {
                            let r = ZCam.recipes.recipe(currentRecipeIdx);
                            r.numPasses = value;
                            ZCam.recipes.updateRecipe(currentRecipeIdx, r);
                        }
                    }
                }
            }

            // Body: Layers
            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Left: Layers List
                ColumnLayout {
                    SplitView.preferredWidth: 200
                    SplitView.minimumWidth: 150

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Layers"; font.bold: true; Layout.fillWidth: true }
                        ToolButton {
                            text: "+"
                            onClicked: ZCam.recipes.addLayer(currentRecipeIdx, "New Layer")
                        }
                        ToolButton {
                            text: "-"
                            onClicked: {
                                if (currentLayerIdx >= 0) {
                                    ZCam.recipes.removeLayer(currentRecipeIdx, currentLayerIdx);
                                    currentLayerIdx = -1;
                                    updateDetails();
                                }
                            }
                        }
                    }

                    ListView {
                        id: layerList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        delegate: ItemDelegate {
                            width: ListView.view.width
                            text: modelData
                            highlighted: index === currentLayerIdx
                            onClicked: {
                                currentLayerIdx = index;
                                updateDetails();
                            }
                        }
                    }
                }

                // Right: Layer Details
                GroupBox {
                    title: "Layer Settings"
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true
                    visible: currentLayerIdx >= 0

                    ScrollView {
                        id: layerScrollView
                        anchors.fill: parent
                        clip: true
                        contentWidth: availableWidth
                        contentHeight: layerSettingsGrid.implicitHeight
                        
                        GridLayout {
                            id: layerSettingsGrid
                            columns: 2
                            width: layerScrollView.availableWidth

                            Label { text: "Name:" }
                                TextField {
                                    id: layerNameField
                                    Layout.fillWidth: true
                                    onEditingFinished: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.name = text;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                }

                                Label { text: "Enabled:" }
                                CheckBox {
                                    id: layerEnabledBox
                                    onCheckedChanged: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.enabled = checked;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                }

                                Label { text: "Power (%):" }
                                SpinBox {
                                    id: layerPowerBox
                                    from: 0
                                    to: 10000
                                    value: 0
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.power = value / 100.0;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                    property real realValue: value / 100.0
                                    validator: DoubleValidator {
                                        bottom: 0
                                        top: 10000
                                    }
                                    textFromValue: function(value, locale) {
                                        return parseFloat(value / 100.0).toFixed(2);
                                    }
                                    valueFromText: function(text, locale) {
                                        return parseInt(parseFloat(text) * 100.0);
                                    }
                                }

                                Label { text: "Speed (mm/s):" }
                                SpinBox {
                                    id: layerSpeedBox
                                    from: 1
                                    to: 10000
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.speed = value;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                }

                                Label { text: "Travel Speed (mm/s):" }
                                SpinBox {
                                    id: layerTravelSpeedBox
                                    from: 1
                                    to: 10000
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.travelSpeed = value;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                }

                                Label { text: "Frequency (kHz):" }
                                SpinBox {
                                    id: layerFrequencyBox
                                    from: 0
                                    to: 100000
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.frequency = value / 100.0;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                    textFromValue: function(value, locale) { return parseFloat(value / 100.0).toFixed(2); }
                                    valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 100.0); }
                                }

                                Label { text: "Pulse Width (ns):" }
                                SpinBox {
                                    id: layerPulseWidthBox
                                    from: 0
                                    to: 1000
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.pulseWidth = value;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                }

                                Label { text: "Num Passes:" }
                                SpinBox {
                                    id: layerNumPassesBox
                                    from: 1
                                    to: 10000
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.numPasses = value;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                }

                                Label { text: "Interval (mm):" }
                                SpinBox {
                                    id: layerIntervalBox
                                    from: 1
                                    to: 10000
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.interval = value / 1000.0;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                    textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                                    valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                                }

                                Label { text: "Start Angle (°):" }
                                SpinBox {
                                    id: layerStartAngleBox
                                    from: -36000
                                    to: 36000
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.startAngle = value / 100.0;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                    textFromValue: function(value, locale) { return parseFloat(value / 100.0).toFixed(2); }
                                    valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 100.0); }
                                }

                                Label { text: "Angle Increment (°):" }
                                SpinBox {
                                    id: layerAngleIncrementBox
                                    from: -36000
                                    to: 36000
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.angleIncrement = value / 100.0;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                    textFromValue: function(value, locale) { return parseFloat(value / 100.0).toFixed(2); }
                                    valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 100.0); }
                                }

                                Label { text: "Zigzag:" }
                                CheckBox {
                                    id: layerZigzagBox
                                    onCheckedChanged: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.zigzag = checked;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                }

                                Label { text: "Interleave:" }
                                SpinBox {
                                    id: layerInterleaveBox
                                    from: 1
                                    to: 100
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.interleave = value;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                }

                                Label { text: "Wobble:" }
                                CheckBox {
                                    id: layerWobbleBox
                                    onCheckedChanged: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.wobble = checked;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                }

                                Label { text: "Wobble Step (mm):" }
                                SpinBox {
                                    id: layerWobbleStepBox
                                    from: 1
                                    to: 10000
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.wobbleStep = value / 1000.0;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                    textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                                    valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                                }

                                Label { text: "Wobble Size (mm):" }
                                SpinBox {
                                    id: layerWobbleSizeBox
                                    from: 1
                                    to: 10000
                                    editable: true
                                    onValueModified: {
                                        let l = ZCam.recipes.layer(root.currentRecipeIdx, root.currentLayerIdx);
                                        l.wobbleSize = value / 1000.0;
                                        ZCam.recipes.updateLayer(root.currentRecipeIdx, root.currentLayerIdx, l);
                                    }
                                    textFromValue: function(value, locale) { return parseFloat(value / 1000.0).toFixed(3); }
                                    valueFromText: function(text, locale) { return parseInt(parseFloat(text) * 1000.0); }
                                }
                            }
                        }
                    }
                }
            }
        } // End StackLayout
    } // End SplitView
    
    Component.onCompleted: {
        recipeList.model = ZCam.recipes.recipeModel;
        if (ZCam.recipes.recipeModel.length > 0) {
            currentRecipeIdx = 0;
            updateDetails();
        }
    }
}
