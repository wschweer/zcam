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
import QtCore
import ZCam

Item {
    id: root

    Material.theme: Material.Dark

    // ── Persistent UI state via QSettings ──────────────────────────────────
    Settings {
        id: treeSettings
        category: "TreeView"
        // JSON array of expanded element names
        property string expandedNodes: "[]"
        // Name of the last selected element
        property string currentElement: ""
        }

    // ── Helper: get model index for a row at root level or under parent ───
    function childIndex(tm, row, parentIdx) {
        if (parentIdx === null)
            return tm.index(row, 0);
        return tm.index(row, 0, parentIdx);
        }

    // ── Save expanded state and current selection to settings ─────────────
    function saveState() {
        var tm = ZCam.treeModel;
        if (!tm || !tm.root)
            return;
        var expandedNames = [];
        collectExpandedRecursive(null, expandedNames);
        treeSettings.expandedNodes = JSON.stringify(expandedNames);
        if (ZCam.currentElement)
            treeSettings.currentElement = ZCam.currentElement.name;
        }

    // Recursively collect names of all elements whose .expanded == true.
    // parentIdx === null means "start at root level".
    function collectExpandedRecursive(parentIdx, list) {
        var tm = ZCam.treeModel;
        var row = 0;
        while (true) {
            var childIdx = childIndex(tm, row, parentIdx);
            if (!childIdx || !childIdx.valid)
                break;
            var element = tm.elementForIndex(childIdx);
            if (element) {
                if (element.expanded && element.children.length > 0)
                    list.push(element.name);
                if (element.children.length > 0)
                    collectExpandedRecursive(childIdx, list);
                }
            row++;
            }
        }

    // ── Restore persistent expand state and selection ────────────────────
    //   Iterates over all *visible* view rows top-down (row 0, 1, 2, …).
    //   When a row's element should be expanded, treeView.expand(row) is
    //   called. This causes child rows to appear at higher row numbers
    //   which are then processed in subsequent loop iterations.
    function restoreState() {
        var tm = ZCam.treeModel;
        if (!tm || !tm.root)
            return;

        // Parse expanded names from settings into a lookup set
        var nameSet;
        try {
            nameSet = JSON.parse(treeSettings.expandedNodes);
            } catch (e) {
            nameSet = [];
            }
        var lookup = {};
        for (var i = 0; i < nameSet.length; i++)
            lookup[nameSet[i]] = true;

        // Walk all visible rows.  Expanding a row reveals its children
        // at higher row numbers. We call forceLayout() after each
        // expand so the new child rows become immediately visible to
        // the next iteration of this loop.
        var row = 0;
        while (true) {
            var idx = treeView.index(row, 0);
            if (!idx || !idx.valid)
                break;
            var element = tm.elementForIndex(idx);
            if (element && element.children.length > 0) {
                var shouldExpand = lookup[element.name] || element.expanded;
                if (shouldExpand && !treeView.isExpanded(row)) {
                    element.expanded = true;
                    treeView.expand(row);
                    treeView.forceLayout();
                    }
                }
            row++;
            }

        // Restore current selection
        restoreSelection();
        }

    // ── Find a model index by element name (depth-first) ─────────────────
    function findIndexByName(parentIdx, name) {
        var tm = ZCam.treeModel;
        var row = 0;
        while (true) {
            var childIdx = childIndex(tm, row, parentIdx);
            if (!childIdx || !childIdx.valid)
                break;
            var element = tm.elementForIndex(childIdx);
            if (element) {
                if (element.name === name)
                    return childIdx;
                if (element.children.length > 0) {
                    var found = findIndexByName(childIdx, name);
                    if (found)
                        return found;
                    }
                }
            row++;
            }
        return null;
        }

    function restoreSelection() {
        var name = treeSettings.currentElement;
        if (!name || name.length === 0)
            return;
        var idx = findIndexByName(null, name);
        if (idx && idx.valid) {
            treeView.selectionModel.setCurrentIndex(idx, ItemSelectionModel.ClearAndSelect);
            var element = ZCam.treeModel.elementForIndex(idx);
            if (element)
                ZCam.currentElement = element;
            }
        }

    TreeView {
        id: treeView
        anchors.fill: parent
        anchors.margins: 4
        model: ZCam.treeModel
        clip: true

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            }

        selectionModel: ItemSelectionModel {
            id: selModel
            model: ZCam.treeModel
            }

        // ── Restore state after model (re)population ───────────────────────
        Component.onCompleted: restoreTimer.start()

        // React to model resets (e.g. project loaded)
        Connections {
            target: ZCam.treeModel
            function onRootChanged() {
                restoreTimer.start();
                }
            }

        // Track current element changes for persistence
        Connections {
            target: ZCam
            function onCurrentElementChanged() {
                if (ZCam.currentElement)
                    treeSettings.currentElement = ZCam.currentElement.name;
                }
            }

        Timer {
            id: restoreTimer
            interval: 50
            onTriggered: root.restoreState()
            }

        // ── Save state when the item is destroyed ──────────────────────────
        Component.onDestruction: saveState()

        // ── Row delegate ──────────────────────────────────────────────────
        delegate: ItemDelegate {
            id: delegateItem

            // Required TreeView properties
            required property TreeView treeView
            required property bool isTreeNode
            required property bool expanded
            required property int hasChildren
            required property int depth
            required property int row
            required property int column
            required property bool current

            readonly property real levelIndent: 20

            // Full width of the view, Material ripple needs it
            implicitWidth: treeView.width
            implicitHeight: 28

            // Material highlight when this row is selected
            highlighted: current

            // Horizontal padding so text never touches the edge
            leftPadding: 8 + depth * levelIndent
            rightPadding: 8
            topPadding: 2
            bottomPadding: 2

            // Custom content: optional chevron + element name
            contentItem: RowLayout {
                spacing: 4

                // ── Expand / collapse chevron ─────────────────────────────────
                Label {
                    id: indicator
                    visible: delegateItem.isTreeNode && delegateItem.hasChildren
                    text: "▸"
                    // One point smaller than the body font
                    font.pointSize: Math.max(6, Qt.application.font.pointSize - 1)
                    color: delegateItem.current ? Material.primaryHighlightedTextColor : Material.foreground
                    rotation: delegateItem.expanded ? 90 : 0
                    Layout.alignment: Qt.AlignVCenter

                    Behavior on rotation {
                        NumberAnimation {
                            duration: 120
                            easing.type: Easing.OutCubic
                            }
                        }

                    TapHandler {
                        onTapped: root.toggleRow(delegateItem.row, model.element)
                        }
                    }

                // Placeholder so non-node leaves align with node labels
                Item {
                    visible: !(delegateItem.isTreeNode && delegateItem.hasChildren)
                    Layout.preferredWidth: indicator.implicitWidth + 4
                    height: 1
                    }

                // ── Element name ──────────────────────────────────────────────────
                //  Default: show a Label.
                //  On double-click of an editable element, switch to a TextField
                //  for inline editing. Editing ends on Enter (accept), Escape
                //  (revert), or focus loss (accept).
                Label {
                    id: nameLabel
                    visible: !delegateItem.editing
                    text: model.element ? model.element.name : (model.name ?? "")
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: delegateItem.current ? Material.primaryHighlightedTextColor : Material.foreground
                    Layout.alignment: Qt.AlignVCenter
                    }

                TextField {
                    id: nameEdit
                    visible: delegateItem.editing
                    // text is set imperatively when editing starts
                    // (see onDoubleClicked), not via a binding, so the
                    // user can freely type without breaking a binding.
                    Layout.fillWidth: true
                    font.pointSize: Qt.application.font.pointSize
                    color: Material.foreground
                    background: Rectangle {
                        color: "transparent"
                        border.color: Material.accentColor
                        border.width: 1
                        radius: 2
                        }
                    horizontalAlignment: TextInput.AlignLeft
                    selectByMouse: true
                    padding: 0
                    Layout.alignment: Qt.AlignVCenter

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            selectAll();
                            }
                        else {
                            // Focus lost -> accept edit
                            delegateItem.acceptEdit();
                            }
                        }
                    Keys.onReturnPressed: function(event) {
                        // Enter -> accept edit
                        delegateItem.acceptEdit();
                        event.accepted = true;
                        }
                    Keys.onEnterPressed: function(event) {
                        // Keypad Enter -> accept edit
                        delegateItem.acceptEdit();
                        event.accepted = true;
                        }
                    Keys.onEscapePressed: function(event) {
                        // Escape -> revert, no change
                        delegateItem.editing = false;
                        event.accepted = true;
                        }
                    }


                // ── Visibility toggle icon ──────────────────────────────────────
                //  Shown only for elements whose visible() == true.
                //  Displays visible.svg or invisible.svg depending on show flag.
                //  When any ancestor has show == false the icon is greyed out
                //  (low opacity). Clicking still toggles show, but the effect
                //  only becomes visible once all ancestors are shown again.
                Image {
                    id: visibilityIcon
                    visible: model.element ? model.element.visible() : false
                    source: (model.element && model.element.visible()) ? (model.element.show ? "qrc:///icons/visible.svg" : "qrc:///icons/invisible.svg") : ""
                    opacity: (model.element && model.element.ancestorsShow) ? 1.0 : 0.3
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                    fillMode: Image.PreserveAspectFit

                    TapHandler {
                        onTapped: {
                            if (model.element)
                                model.element.show = !model.element.show;
                            }
                        }
                    }
                }

            // ── Inline-editing state ──────────────────────────────────────
            property bool editing: false

            function acceptEdit() {
                if (!delegateItem.editing)
                    return;
                delegateItem.editing = false;
                if (model.element && nameEdit.text.length > 0) {
                    // Route through the undo system so the name change
                    // is recorded for undo/redo and marks the project dirty.
                    // Element::setName() may de-duplicate the name (e.g.
                    // "foo" -> "foo-1"); the actual name flows back through
                    // nameChanged -> Label binding automatically.
                    ZCam.projectManager.changeProperty(model.element, "name", nameEdit.text);
                    }
                }

            // Select element on tap (row tap, not the chevron tap)
            onClicked: {
                treeView.selectionModel.setCurrentIndex(treeView.index(row, column), ItemSelectionModel.ClearAndSelect);
                ZCam.currentElement = model.element;
                }
            onDoubleClicked: {
                if (model.element && model.element.nameEditable()) {
                    // Enter inline-editing mode
                    delegateItem.editing = true;
                    // Set the TextField text imperatively to the current
                    // element name (no binding, so the user can type freely).
                    nameEdit.text = model.element.name;
                    nameEdit.forceActiveFocus();
                    nameEdit.selectAll();
                    }
                else if (delegateItem.isTreeNode && delegateItem.hasChildren) {
                    root.toggleRow(delegateItem.row, model.element);
                    }
                }

            // ── Right-click context menu ────────────────────────────────────
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                propagateComposedEvents: true
                onClicked: function(mouse) {
                    treeView.selectionModel.setCurrentIndex(treeView.index(row, column), ItemSelectionModel.ClearAndSelect);
                    ZCam.currentElement = model.element;
                    var global = delegateItem.mapToItem(root, mouse.x, mouse.y);
                    root.showContextMenu(model.element, global.x, global.y);
                    }
                }
            }
        }

    // ── Toggle helper: expand/collapse and persist ────────────────────────
    function toggleRow(row, element) {
        if (treeView.isExpanded(row)) {
            treeView.collapse(row);
            if (element)
                element.expanded = false;
            } else {
            treeView.expand(row);
            if (element)
                element.expanded = true;
            }
        // Persist immediately so it survives even ungraceful exits
        saveState();
        }

    // ── Context menu for tree items ──────────────────────────────────────
    function showContextMenu(element, x, y) {
        if (!element)
            return;
        var tn = element.typeName();
        if (tn === "cad") {
            cadMenu.popup(x, y);
            }
        else if (tn === "layer") {
            layerMenu.popup(x, y);
            }
        }

    // Menu for Cad elements: "Add Layer"
    Menu {
        id: cadMenu
        Material.theme: Material.Dark
        MenuItem {
            text: "Add Layer"
            onTriggered: {
                if (ZCam.topLevel)
                    ZCam.topLevel.addLayer();
                }
            }
        }

    // Menu for Layer elements: "Remove"
    Menu {
        id: layerMenu
        Material.theme: Material.Dark
        MenuItem {
            text: "Remove"
            onTriggered: {
                if (ZCam.topLevel)
                    ZCam.topLevel.removeElement(ZCam.currentElement);
                }
            }
        }
    }