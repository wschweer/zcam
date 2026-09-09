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
    property bool _updating: false
    property bool _removing: false   // guard: true while removeCurrent() is in progress
    property var currentFolderRelDir: ""   // relative path of currently selected folder, "" = root
    property var currentParentDir: ""      // directory of the currently selected entry (folder or recipe)

    // Delete removes the currently selected recipe/folder.
    // Scoped to this component so it only fires when the recipes panel is
    // loaded and something is selected.  No keyboard focus required.
    Shortcut {
        sequence: "Delete"
        enabled: root.visible && (root.currentRecipeIdx >= 0 || root.currentFolderRelDir !== "")
        onActivated: root.removeCurrent()
    }

    Rectangle {
        anchors.fill: parent
        color: Material.color(Material.BlueGrey, Material.Shade900)
    }

    Connections {
        target: ZCam.recipes
        function onRecipeModelChanged() {
            // Skip while removeCurrent() is in progress — it handles
            // index adjustment and updateDetails() itself after the
            // model has been fully rebuilt.
            if (_removing)
                return;
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
        _updating = true;
        if (currentRecipeIdx >= 0 && currentRecipeIdx < ZCam.recipes.recipeModel.length) {
            recipeModel.recipe = ZCam.recipes.recipePtr(currentRecipeIdx);
            layerList.model = ZCam.recipes.layerModel(currentRecipeIdx);

            if (currentLayerIdx >= layerList.model.length) {
                currentLayerIdx = layerList.model.length - 1;
            } else if (currentLayerIdx < 0 && layerList.model.length > 0) {
                currentLayerIdx = 0;
            }
            layerList.currentIndex = currentLayerIdx;

            if (currentLayerIdx >= 0 && currentLayerIdx < layerList.model.length) {
                layerSettingModel.recipe = ZCam.recipes.recipe(currentRecipeIdx)
                layerSettingModel.pass = ZCam.recipes.layerPtr(currentRecipeIdx, currentLayerIdx);
            } else {
                layerSettingModel.clearPass();
            }

            // Keep the left tree expanded so the recipe shown in the right
            // panel is always visible in the tree, and make sure the recipe
            // row is the selected (current) one.
            expandRecipeInTree(currentRecipeIdx);
            let sm = recipeTree.selectionModel;
            let tm = ZCam.recipes.recipeTreeModel;
            if (sm && tm) {
                let targetIdx = tm.indexForRecipe(currentRecipeIdx);
                if (targetIdx && targetIdx.valid &&
                    !(sm.currentIndex && sm.currentIndex === targetIdx)) {
                    sm.setCurrentIndex(targetIdx, ItemSelectionModel.ClearAndSelect);
                }
            }
        } else {
            recipeModel.recipe = null;
            layerList.model = [];
            currentLayerIdx = -1;
            layerSettingModel.clearPass();
        }
        _updating = false;
    }

    /// Expands every ancestor folder of the recipe with the given index so
    /// that its row is visible in the left TreeView.  A no-op when the recipe
    /// cannot be found in the tree model or is already fully expanded.
    function expandRecipeInTree(idx) {
        if (idx < 0)
            return;
        let tm = ZCam.recipes.recipeTreeModel;
        if (!tm)
            return;
        let targetIdx = tm.indexForRecipe(idx);
        if (!targetIdx || !targetIdx.valid)
            return;

        // Build the chain of ancestor indices from root to the target.
        let chain = [];
        let p = targetIdx.parent;
        while (p && p.valid) {
            chain.unshift(p);
            p = p.parent;
        }

        // Expand each ancestor from top to bottom so the target row
        // becomes visible.  forceLayout() makes newly revealed child
        // rows immediately available for the next iteration.
        for (let i = 0; i < chain.length; ++i) {
            let visRow = recipeTree.rowAtIndex(chain[i]);
            if (visRow >= 0 && !recipeTree.isExpanded(visRow)) {
                recipeTree.expand(visRow);
                recipeTree.forceLayout();
            }
        }
    }

    /// Removes the currently selected recipe (or the currently selected
    /// folder when no recipe is shown in the details panel).  Shared by the
    /// "−" button and the Delete key.  The folder that contained the removed
    /// entry is re-expanded afterwards so the tree does not collapse.
    function removeCurrent() {
        // Guard against re-entrant updates from the Connections handler
        // (recipeModelChanged fires inside removeRecipe / removeFolder,
        // before the tree model is rebuilt).
        _removing = true;

        // Capture the folders that are expanded right now.  removeRecipe() /
        // removeFolder() rebuild the tree model (resetModel), which clears the
        // TreeView's expanded state, so we re-expand them afterwards.
        let savedExpansion = expandedPaths();

        if (currentRecipeIdx >= 0) {
            ZCam.recipes.removeRecipe(currentRecipeIdx);
            currentRecipeIdx = -1;
        } else if (currentFolderRelDir !== "") {
            ZCam.recipes.removeFolder(currentFolderRelDir);
            currentFolderRelDir = "";
        }

        _removing = false;

        // Now that the model is fully rebuilt, update the details panel
        // and restore the tree's expanded state.
        updateDetails();
        restoreExpansion(savedExpansion);
    }

    /// Records the relative paths of all folder nodes that are currently
    /// expanded in the tree.  Used to restore the expanded state after the
    /// model is rebuilt (a model reset wipes the TreeView expansion).
    ///
    /// The visible rows of the QML TreeView are iterated (rows / isExpanded /
    /// index are QML built-ins).  Only QML-origin indices are fed to
    /// tm.path(), the same (working) pattern as the selection handler, so no
    /// invalid QModelIndex ever crosses the QML/C++ boundary.
    function expandedPaths() {
        let out = [];
        let tm = ZCam.recipes.recipeTreeModel;
        if (!tm)
            return out;
        let n = recipeTree.rows;
        for (let r = 0; r < n; ++r) {
            if (!recipeTree.isExpanded(r))
                continue;
            let idx = recipeTree.index(r, 0);
            let p = tm.path(idx);
            if (p.length > 0)
                out.push(p);
        }
        return out;
    }

    /// Re-expands the top-level machine node and then the folders with the
    /// given relative paths (parent-first).  Folders that no longer exist
    /// (e.g. because they were removed) are skipped silently.
    function restoreExpansion(paths) {
        let tm = ZCam.recipes.recipeTreeModel;
        if (!tm)
            return;
        // Re-expand the top-level machine node (always visible row 0) so its
        // children are visible again before the sub-folders are expanded.
        if (recipeTree.rows > 0 && !recipeTree.isExpanded(0)) {
            recipeTree.expand(0);
            recipeTree.forceLayout();
        }
        if (!paths)
            return;
        for (let i = 0; i < paths.length; ++i) {
            let idx = tm.indexForPath(paths[i]);
            if (!idx || !idx.valid)
                continue;
            let visRow = recipeTree.rowAtIndex(idx);
            if (visRow >= 0 && !recipeTree.isExpanded(visRow)) {
                recipeTree.expand(visRow);
                recipeTree.forceLayout();
            }
        }
    }

    /// Selects the recipe with the given name in the tree and detail view.
    /// updateDetails() takes care of expanding all parent folders and of
    /// selecting the recipe row in the TreeView.
    function selectRecipeByName(name) {
        let idx = ZCam.recipes.recipeIndexByName(name);
        if (idx < 0)
            return;
        currentRecipeIdx = idx;
        updateDetails();
    }

    RecipeModel {
        id: recipeModel
    }

    LayerSettingModel {
        id: layerSettingModel
    }

    Connections {
        target: recipeModel
        function onRecipeDataChanged() {
            // Recipe property was edited via PropertyEditor; persist
            // by notifying the Recipe container.
            ZCam.recipes.recipeChanged(currentRecipeIdx)
        }
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
                        // Create the new recipe in the directory of the
                        // currently selected entry (folder or recipe);
                        // clone the selected recipe's settings when one is
                        // selected.  Select and display the new recipe so it
                        // shows up on the right, selected and expanded.
                        let newIdx = ZCam.recipes.addRecipeInDir(
                            "New Recipe", currentParentDir, currentRecipeIdx);
                        if (newIdx >= 0) {
                            currentRecipeIdx = newIdx;
                            updateDetails();
                        }
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
                    ToolTip.text: qsTr("Remove selected (Del)")
                    onClicked: removeCurrent()
                }
            }

            TreeView {
                id: recipeTree
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: ZCam.recipes.recipeTreeModel
                clip: true

                // Auto-expand the top-level machine-type node (row 0)
                // so the user immediately sees the recipes underneath.
                Component.onCompleted: {
                    if (recipeTree.rows > 0)
                        recipeTree.expand(0)
                }

                selectionModel: ItemSelectionModel {
                    model: ZCam.recipes.recipeTreeModel

                    onCurrentIndexChanged: {
                        // Skip if we are currently updating the details panel
                        // (updateDetails sets the selection index programmatically)
                        // or removing an item (removeCurrent handles cleanup itself).
                        if (_updating || _removing)
                            return
                        var idx = currentIndex
                        if (!idx || !idx.valid) {
                            currentFolderRelDir = ""
                            currentParentDir = ""
                            return
                        }
                        var tm = ZCam.recipes.recipeTreeModel
                        if (tm.isDir(idx)) {
                            // Selecting a folder clears the recipe in the details panel.
                            currentFolderRelDir = tm.path(idx)
                            // A new entry created now goes directly into this folder.
                            currentParentDir = tm.path(idx)
                            if (currentRecipeIdx !== -1) {
                                currentRecipeIdx = -1
                                updateDetails()
                            }
                        } else {
                            currentFolderRelDir = tm.path(idx) // parent folder for context
                            // A new entry created now goes into the selected
                            // recipe's parent folder (next to it).
                            currentParentDir = tm.path(idx)
                            var rIdx = tm.recipeIndex(idx)
                            // Ignore the notification we just triggered ourselves
                            // from updateDetails() — nothing has actually changed.
                            if (rIdx === currentRecipeIdx)
                                return;
                            currentRecipeIdx = rIdx
                            updateDetails()
                        }
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
                    topPadding: 2
                    bottomPadding: 2

                    background: Rectangle {
                        anchors.fill: parent
                        color: recipeDelegate.highlighted
                               ? Material.accentColor
                               : "transparent"
                    }

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
                    onDoubleClicked: {
                        if (recipeDelegate.isTreeNode && recipeDelegate.hasChildren) {
                            if (recipeTree.isExpanded(recipeDelegate.row))
                                recipeTree.collapse(recipeDelegate.row)
                            else
                                recipeTree.expand(recipeDelegate.row)
                        }
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

                // Header: Recipe Info (dynamically built from properties())
                GroupBox {
                    title: recipeModel.title.length > 0 ? recipeModel.title : "Recipe"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200

                    ColumnLayout {
                        anchors.fill: parent

                        Label {
                            text: recipeModel.title.length > 0 ? recipeModel.title : "Recipe"
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
                            model: recipeModel
                            propertiesJson: recipeModel.propertiesJson
                            labelWidth: 100
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
        // Explicit width: the ColumnLayout below sizes itself to
        // parent.width, so without a fixed dialog width the content
        // width depends on implicitWidth and Qt can flag a binding
        // loop for "implicitWidth" (same as Main.qml unsavedChangesGuard).
        width: 360
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
                ZCam.recipes.addFolder(folderNameField.text, currentParentDir)
            }
        }
    }

    Component.onCompleted: {
        updateDetails();
    }
}