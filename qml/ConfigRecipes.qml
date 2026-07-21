//=============================================================================
//  ZCam
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the file LICENSE.GPL
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
    property var currentFolderRelDir: ""   // relative path of currently selected folder, "" = root

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
                layerSettingModel.pass = ZCam.recipes.layerPtr(currentRecipeIdx, currentLayerIdx);
            } else {
                layerSettingModel.pass = null;
            }
        } else {
            recipeNameField.text = "";
            recipeDescField.text = "";
            layerList.model = [];
            currentLayerIdx = -1;
            layerSettingModel.pass = null;
        }
    }

    LayerSettingModel {
        id: layerSettingModel
    }

    Connections {
        target: layerSettingModel
        function onLayerDataChanged() {
            ZCam.recipes.recipeChanged(currentRecipeIdx);
            layerList.model = ZCam.recipes.layerModel(currentRecipeIdx);
        }
    }

    SplitView {
        anchors.fill: parent
        anchors.margins: 8

        // Left Panel: Recipe Tree
        ColumnLayout {
            SplitView.preferredWidth: 240
            SplitView.minimumWidth: 160

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Recipes"
                    font.bold: true
                    Layout.fillWidth: true
                }
                // Add Recipe button
                ToolButton {
                    text: "+"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Add Recipe")
                    onClicked: {
                        ZCam.recipes.addRecipeInDir("New Recipe", currentFolderRelDir)
                    }
                }
                // Add Folder button
                ToolButton {
                    text: "📁"  // folder icon
                    font.pointSize: 10
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Add Folder")
                    onClicked: {
                        folderDialog.open()
                    }
                }
                // Remove button
                ToolButton {
                    text: "−"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Remove selected")
                    onClicked: {
                        if (currentRecipeIdx >= 0) {
                            ZCam.recipes.removeRecipe(currentRecipeIdx);
                            currentRecipeIdx = -1;
                            updateDetails();
                        } else if (currentFolderRelDir !== "") {
                            ZCam.recipes.removeFolder(currentFolderRelDir);
                            currentFolderRelDir = "";
                        }
                    }
                }
            }

            TreeView {
                id: recipeTree
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: ZCam.recipes.recipeTreeModel
                clip: true

                selectionModel: ItemSelectionModel {
                    model: ZCam.recipes.recipeTreeModel

                    onCurrentIndexChanged: {
                        var idx = currentIndex
                        if (!idx || !idx.valid) {
                            currentFolderRelDir = ""
                            return
                        }
                        var tm = ZCam.recipes.recipeTreeModel
                        if (tm.isDir(idx)) {
                            currentFolderRelDir = tm.path(idx)
                            currentRecipeIdx = -1
                        } else {
                            currentFolderRelDir = tm.path(idx) // parent folder for context
                            currentRecipeIdx = tm.recipeIndex(idx)
                        }
                        updateDetails()
                    }
                }

                delegate: ItemDelegate {
                    id: recipeDelegate

                    required property TreeView treeView
                    required property bool isTreeNode
                    required property bool expanded
                    required property int hasChildren
                    required property int depth
                    required property int row
                    required property int column
                    required property bool current

                    implicitWidth: recipeTree.width
                    implicitHeight: 28
                    highlighted: current

                    leftPadding: 8 + depth * 20
                    rightPadding: 8

                    contentItem: RowLayout {
                        spacing: 4

                        // Expand/collapse chevron for folders
                        Label {
                            visible: recipeDelegate.isTreeNode && recipeDelegate.hasChildren
                            text: "▸"
                            font.pointSize: Math.max(6, Qt.application.font.pointSize - 1)
                            color: recipeDelegate.current ? Material.primaryHighlightedTextColor : Material.foreground
                            rotation: recipeDelegate.expanded ? 90 : 0
                            Layout.alignment: Qt.AlignVCenter

                            Behavior on rotation {
                                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
                            }

                            TapHandler {
                                onTapped: {
                                    if (recipeTree.isExpanded(recipeDelegate.row))
                                        recipeTree.collapse(recipeDelegate.row)
                                    else
                                        recipeTree.expand(recipeDelegate.row)
                                }
                            }
                        }

                        // Placeholder for non-folder items
                        Item {
                            visible: !(recipeDelegate.isTreeNode && recipeDelegate.hasChildren)
                            Layout.preferredWidth: 14
                            height: 1
                        }

                        // Folder/Recipe icon
                        Label {
                            text: model.isDir ? "📁" : "📄"
                            font.pointSize: 10
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // Name
                        Label {
                            text: model.nodeName ?? ""
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: recipeDelegate.current ? Material.primaryHighlightedTextColor : Material.foreground
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    onClicked: {
                        recipeTree.selectionModel.setCurrentIndex(
                            recipeTree.index(row, column), ItemSelectionModel.ClearAndSelect)
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
                                text: "−"
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
        }
    }

    // ── Add Folder dialog ──────────────────────────────────────────
    Dialog {
        id: folderDialog
        title: qsTr("New Folder")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            width: parent.width
            Label {
                text: qsTr("Folder name:")
            }
            TextField {
                id: folderNameField
                Layout.fillWidth: true
                placeholderText: qsTr("Folder name")
                focus: true
                onAccepted: folderDialog.accept()
            }
        }

        onOpened: {
            folderNameField.text = ""
            folderNameField.forceActiveFocus()
        }
        onAccepted: {
            if (folderNameField.text.length > 0) {
                ZCam.recipes.addFolder(folderNameField.text, currentFolderRelDir)
            }
        }
    }

    Component.onCompleted: {
        updateDetails();
    }
}