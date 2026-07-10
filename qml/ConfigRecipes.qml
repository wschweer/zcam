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

            // Update the layer setting model pointer
            if (currentLayerIdx >= 0 && currentLayerIdx < layerList.model.length) {
                layerSettingModel.layerSetting = ZCam.recipes.layerPtr(currentRecipeIdx, currentLayerIdx);
                } else {
                layerSettingModel.layerSetting = null;
                }
            } else {
            recipeNameField.text = "";
            recipeDescField.text = "";
            layerList.model = [];
            currentLayerIdx = -1;
            layerSettingModel.layerSetting = null;
            }
        }

    LayerSettingModel {
        id: layerSettingModel
    }

    Connections {
        target: layerSettingModel
        function onLayerDataChanged() {
            ZCam.recipes.recipeChanged(currentRecipeIdx);
            // Refresh layer list names in case the layer name changed
            layerList.model = ZCam.recipes.layerModel(currentRecipeIdx);
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
                Label {
                    text: "Recipes"
                    font.bold: true
                    Layout.fillWidth: true
                    }
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

                        Label {
                            text: "Name:"
                            }
                        TextField {
                            id: recipeNameField
                            Layout.fillWidth: true
                            onEditingFinished: {
                                let r = ZCam.recipes.recipe(currentRecipeIdx);
                                r.name = text;
                                ZCam.recipes.updateRecipe(currentRecipeIdx, r);
                                }
                            }

                        Label {
                            text: "Description:"
                            }
                        TextField {
                            id: recipeDescField
                            Layout.fillWidth: true
                            onEditingFinished: {
                                let r = ZCam.recipes.recipe(currentRecipeIdx);
                                r.description = text;
                                ZCam.recipes.updateRecipe(currentRecipeIdx, r);
                                }
                            }

                        Label {
                            text: "Passes:"
                            }
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
                            Label {
                                text: "Layers"
                                font.bold: true
                                Layout.fillWidth: true
                                }
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

                    // Right: Layer Details (built from PropertyEditor)
                    StackLayout {
                        SplitView.fillWidth: true
                        SplitView.fillHeight: true
                        currentIndex: currentLayerIdx >= 0 ? 1 : 0

                        // 0: Placeholder
                        Item {
                            Label {
                                anchors.centerIn: parent
                                text: "No layer selected"
                                color: Material.color(Material.BlueGrey, Material.Shade300)
                                }
                            }

                        // 1: Layer Settings via PropertyEditor
                        ColumnLayout {
                            // Title
                            Label {
                                text: layerSettingModel.title.length > 0 ? layerSettingModel.title : "Layer"
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

                            PropertyEditor {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                model: layerSettingModel
                                propertiesJson: layerSettingModel.propertiesJson
                                labelWidth: 100
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