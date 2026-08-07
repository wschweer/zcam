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

    // ── Selection anchor for shift-click range selection ───────────────
    //   The row index of the last plain-clicked element.  Shift-click
    //   selects all visible rows from this anchor to the clicked row.
    property int selectionAnchor: -1

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

    // ── Sync TreeView selection with ZCam.currentElement ─────────────────
    //   Called when currentElement changes from outside the TreeView
    //   (e.g. clicking on an element in the 3D canvas).  Finds the
    //   model index for the current element, expands all parent nodes
    //   so the row is visible, and sets the selection.
    function syncSelection() {
        var tm = ZCam.treeModel;
        if (!tm || !tm.root || !ZCam.currentElement)
            return;
        var el = ZCam.currentElement;
        var idx = tm.indexForElement(el);
        if (!idx || !idx.valid)
            return;
        // Build the chain of parent indices from root to the element.
        var chain = [];
        var p = idx.parent;
        while (p && p.valid) {
            chain.unshift(p);
            p = p.parent;
            }
        // Expand each ancestor in the TreeView by walking visible rows.
        // After each expand, child rows appear at higher row numbers,
        // so we call forceLayout() to make them immediately available.
        for (var i = 0; i < chain.length; ++i) {
            var ancestorIdx = chain[i];
            var ancestorEl = tm.elementForIndex(ancestorIdx);
            if (ancestorEl && !ancestorEl.expanded) {
                ancestorEl.expanded = true;
                // Find the visible row for this ancestor and expand it.
                var row = treeView.rowAtIndex(ancestorIdx);
                if (row >= 0) {
                    treeView.expand(row);
                    treeView.forceLayout();
                    }
                }
            }
        // Re-query the index after potential model layout changes.
        idx = tm.indexForElement(el);
        if (!idx || !idx.valid)
            return;
        treeView.selectionModel.setCurrentIndex(idx, ItemSelectionModel.ClearAndSelect);
        }

    // ── Sync TreeView selection with the lasso multi-selection ─────────
    //   Called when selectedElements changes.  When a multi-selection
    //   is active, all selected elements are highlighted in the tree
    //   via the selectionModel.  The currentElement is also selected.
    //   When the multi-selection is empty, the selection falls back
    //   to just the currentElement (handled by onCurrentElementChanged).
    function syncMultiSelection() {
        var tm = ZCam.treeModel;
        if (!tm || !tm.root)
            return;
        var selected = ZCam.selectedElements;
        if (!selected || selected.length === 0) {
            // No multi-selection — just sync the current element.
            if (ZCam.currentElement)
                root.syncSelection();
            else
                treeView.selectionModel.clear();
            return;
            }
        // Clear the selection model and re-select all selected elements.
        treeView.selectionModel.clear();
        for (var i = 0; i < selected.length; ++i) {
            var el = selected[i];
            if (!el)
                continue;
            var idx = tm.indexForElement(el);
            if (idx && idx.valid) {
                // Expand all parent nodes so the row is visible.
                var chain = [];
                var p = idx.parent;
                while (p && p.valid) {
                    chain.unshift(p);
                    p = p.parent;
                    }
                for (var j = 0; j < chain.length; ++j) {
                    var ancestorIdx = chain[j];
                    var ancestorEl = tm.elementForIndex(ancestorIdx);
                    if (ancestorEl && !ancestorEl.expanded) {
                        ancestorEl.expanded = true;
                        var row = treeView.rowAtIndex(ancestorIdx);
                        if (row >= 0) {
                            treeView.expand(row);
                            treeView.forceLayout();
                            }
                        }
                    }
                // Re-query the index after potential layout changes.
                idx = tm.indexForElement(el);
                if (idx && idx.valid)
                    treeView.selectionModel.select(idx, ItemSelectionModel.Select);
                }
            }
        // Ensure the currentElement is also selected and is the current index.
        if (ZCam.currentElement) {
            var curIdx = tm.indexForElement(ZCam.currentElement);
            if (curIdx && curIdx.valid)
                treeView.selectionModel.setCurrentIndex(curIdx, ItemSelectionModel.NoUpdate);
            }
        }

    TreeView {
        id: treeView
        anchors.fill: parent
        anchors.margins: 4
        model: ZCam.treeModel
        clip: true

        // ── Drag & drop state ───────────────────────────────────────────────
        //   The currently dragged element (set when drag starts,
        //   cleared when drag ends).  Used to prevent self-drop.
        property var draggedElement: null
        //   The currently hovered drop target element (for visual feedback).
        property var dropTargetElement: null
        //   Drop position relative to the target row: "before", "on", "after"
        property string dropPosition: ""

        // ── Helper: find the element at a given Y position in the tree ────
        //   Returns { element, row, yRel } or null.
        //   Iterates visible delegates and checks which one contains y.
        function elementAtY(y) {
            var tm = ZCam.treeModel;
            if (!tm)
                return null;
            var row = 0;
            while (true) {
                var idx = treeView.index(row, 0);
                if (!idx || !idx.valid)
                    break;
                var item = treeView.itemAtIndex(idx);
                if (item) {
                    var itemY = item.y;
                    var itemH = item.height;
                    if (y >= itemY && y < itemY + itemH) {
                        var el = tm.elementForIndex(idx);
                        return { element: el, row: row, yRel: y - itemY, itemH: itemH };
                        }
                    }
                row++;
                }
            return null;
            }

        // ── Helper: perform the drop ─────────────────────────────────────
        function performDrop(yInContent) {
            var dragged = treeView.draggedElement;
            if (!dragged)
                return;
            var hit = treeView.elementAtY(yInContent);
            if (!hit || !hit.element)
                return;
            var target = hit.element;
            if (dragged === target)
                return;
            // Prevent dropping into a descendant
            var p = target;
            while (p) {
                if (p === dragged)
                    return;
                p = p.parent();
                }
            // Determine drop position
            // Shape elements (text, polygon, ellipse, rectangle) can
            // also act as drop targets ("on") — dropping onto them
            // re-parents the dragged element as their child.
            var tn = target.typeName();
            var isContainer = (tn === "layer" || tn === "cad" || tn === "fixture" || tn === "cam" || tn === "project");
            var isShape = (tn === "text" || tn === "polygon" || tn === "ellipse" || tn === "rectangle");
            var pos;
            if ((isContainer || isShape) && hit.yRel > hit.itemH * 0.33 && hit.yRel < hit.itemH * 0.67)
                pos = "on";
            else if (hit.yRel < hit.itemH * 0.5)
                pos = "before";
            else
                pos = "after";
            // Determine new parent and row
            if (pos === "on") {
                if (isShape) {
                    // Re-parent the dragged element to the shape target.
                    // This requires coordinate transformation so the
                    // element doesn't visually jump.
                    ZCam.reparentElement(dragged, target);
                    }
                else {
                    // Drop into a container element (layer, cad, etc.)
                    var newRow = target.children.length;
                    ZCam.project.moveElement(dragged, target, newRow);
                    }
                }
            else {
                // Drop before/after the target element (reorder within parent)
                var newParent = target.parent();
                if (!newParent)
                    return;
                var targetRow = 0;
                for (var i = 0; i < newParent.children.length; ++i) {
                    if (newParent.children[i] === target)
                        break;
                    ++targetRow;
                    }
                var newRow2 = (pos === "before") ? targetRow : targetRow + 1;
                ZCam.project.moveElement(dragged, newParent, newRow2);
                }
            // Expand the target container after a drop "on" so the
            // newly inserted child becomes visible immediately.
            if (pos === "on" && target.children.length > 0) {
                target.expanded = true;
                var idx = ZCam.treeModel.indexForElement(target);
                if (idx && idx.valid) {
                    var visRow = treeView.rowAtIndex(idx);
                    if (visRow >= 0 && !treeView.isExpanded(visRow))
                        treeView.expand(visRow);
                    }
                }
            }

        // Handle Delete and Escape keys directly on the TreeView so the
        // shortcuts work even when the tree has keyboard focus.
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Delete) {
                ZCam.deleteCurrentElement()
                event.accepted = true
                }
            if (event.key === Qt.Key_Escape) {
                ZCam.clearSelection()
                event.accepted = true
                }
            }

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

        // Track current element changes for persistence and selection sync
        Connections {
            target: ZCam
            function onCurrentElementChanged() {
                if (ZCam.currentElement) {
                    treeSettings.currentElement = ZCam.currentElement.name;
                    root.syncSelection();
                    }
                else {
                    treeSettings.currentElement = "";
                    treeView.selectionModel.clear();
                    }
                }
            // Sync the TreeView selection to the lasso multi-selection.
            // When a multi-selection is active (selectedElements.length > 0),
            // all selected elements are highlighted in the tree.
            function onSelectedElementsChanged() {
                root.syncMultiSelection();
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

            // Check if this delegate's element is in the lasso
            // multi-selection.  Accesses ZCam.selectedElements
            // (which has a NOTIFY signal) so the binding
            // re-evaluates when the selection changes.
            function _isSelected(el) {
                var sel = ZCam.selectedElements;
                if (!sel || sel.length === 0)
                    return false;
                for (var i = 0; i < sel.length; ++i) {
                    if (sel[i] === el)
                        return true;
                    }
                return false;
                }

            // Full width of the view, Material ripple needs it
            implicitWidth: treeView.width
            implicitHeight: 28

            // Material highlight when this row is selected.
            // Highlighted when it's the current row OR when it's part
            // of the lasso multi-selection (ZCam.selectedElements).
            // The selectedElements property has NOTIFY selectedElementsChanged
            // so QML re-evaluates this binding whenever the selection changes.
            highlighted: current || (model.element && _isSelected(model.element))

            // Horizontal padding so text never touches the edge
            leftPadding: 8 + depth * levelIndent
            rightPadding: 8
            topPadding: 2
            bottomPadding: 2

            // ── Drop indicator line ─────────────────────────────────────────
            //   A thin accent-colored line shown at the top or bottom of
            //   the row when the row is a valid drop target for "before" or
            //   "after" insertion.
            Rectangle {
                id: dropLineTop
                visible: treeView.dropTargetElement === model.element && treeView.dropPosition === "before"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 2
                color: Material.accentColor
                z: 100
                }
            Rectangle {
                id: dropLineBottom
                visible: treeView.dropTargetElement === model.element && treeView.dropPosition === "after"
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 2
                color: Material.accentColor
                z: 100
                }
            // Highlight the full row when dropping "on" (into a container)
            Rectangle {
                id: dropHighlightOn
                visible: treeView.dropTargetElement === model.element && treeView.dropPosition === "on"
                anchors.fill: parent
                color: Material.accentColor
                opacity: 0.2
                z: -1
                radius: 3
                }

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
                    color: delegateItem.highlighted ? Material.primaryHighlightedTextColor : Material.foreground
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
                    color: delegateItem.highlighted ? Material.primaryHighlightedTextColor : Material.foreground
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
                //
                //  The icon is wrapped in an Item that is wider than the 16 px
                //  image so the TapHandler has a generous hit area covering the
                //  full row height.  This prevents the "right half of the icon
                //  does nothing" problem.
                Item {
                    id: visibilityToggle
                    visible: model.element ? model.element.visible() : false
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight

                    Image {
                        id: visibilityIcon
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        source: (model.element && model.element.visible()) ? (model.element.show ? "qrc:///icons/visible.svg" : "qrc:///icons/invisible.svg") : ""
                        opacity: (model.element && model.element.ancestorsShow) ? 1.0 : 0.3
                        fillMode: Image.PreserveAspectFit
                        }

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
                    ZCam.project.changeProperty(model.element, "name", nameEdit.text);
                    }
                }

            // Select element on tap (row tap, not the chevron tap)
            // ── Selection handling ───────────────────────────────────
            //   Ctrl-click: toggle the element in/out of the multi-selection.
            //   Shift-click: range select from the last clicked row to this row.
            //   Plain click: clear selection and select only this element.
            //   The current (primary) element is always set to the clicked
            //   element so the inspector shows it.
            //
            //   Modifier-clicks (Ctrl/Shift) are handled by the MouseArea
            //   below, which intercepts the press so this handler is only
            //   reached for plain clicks.
            onClicked: {
                // Clear any existing multi-selection without clearing
                // currentElement (which would cause an intermediate null
                // state in the InspectorModel and trigger runtime errors).
                if (ZCam.selectedElements.length > 0)
                    ZCam.clearSelectionList();
                treeView.selectionModel.setCurrentIndex(treeView.index(row, column), ItemSelectionModel.ClearAndSelect);
                ZCam.currentElement = model.element;
                root.selectionAnchor = delegateItem.row;
                }
            onDoubleClicked: {
                if (delegateItem.isTreeNode && delegateItem.hasChildren) {
                    root.toggleRow(delegateItem.row, model.element);
                    }
                else if (model.element && model.element.nameEditable()) {
                    // Enter inline-editing mode
                    delegateItem.editing = true;
                    // Set the TextField text imperatively to the current
                    // element name (no binding, so the user can type freely).
                    nameEdit.text = model.element.name;
                    nameEdit.forceActiveFocus();
                    nameEdit.selectAll();
                    }
                }

            // ── Mouse handler for left-click (with modifiers) and right-click ─
            //   Left-click with Ctrl: toggle element in multi-selection.
            //   Left-click with Shift: range select from anchor to here.
            //   Plain left-click: handled by ItemDelegate.onClicked above.
            //   Right-click: context menu (see below).
            //
            //   This MouseArea handles only modifier-clicks to avoid
            //   conflicting with the ItemDelegate's built-in click/
            //   doubleClick handling.  When modifiers are present, the
            //   press is accepted so ItemDelegate doesn't also fire.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                propagateComposedEvents: true

                onPressed: function(mouse) {
                    if (mouse.button === Qt.LeftButton
                        && (mouse.modifiers & Qt.ControlModifier
                            || mouse.modifiers & Qt.ShiftModifier)) {
                        // Accept the press so ItemDelegate.onClicked doesn't fire.
                        mouse.accepted = true;
                        }
                    else {
                        // Let ItemDelegate handle plain clicks.
                        mouse.accepted = false;
                        }
                    }

                onClicked: function(mouse) {
                    if (mouse.button === Qt.LeftButton) {
                        if (mouse.modifiers & Qt.ControlModifier) {
                            // Ctrl-click: toggle this element in the multi-selection.
                            ZCam.toggleSelection(model.element);
                            root.syncMultiSelection();
                            }
                        else if (mouse.modifiers & Qt.ShiftModifier) {
                            // Shift-click: range select from anchor to here.
                            root.rangeSelect(delegateItem.row);
                            }
                        // Plain left-click falls through to ItemDelegate.
                        }
                    else if (mouse.button === Qt.RightButton) {
                        // If the element is not already in the multi-selection,
                        // clear and select only this element (like a plain click).
                        // Otherwise keep the current multi-selection and just
                        // make this element the current one for the context menu.
                        if (!ZCam.isSelected(model.element)) {
                            root.selectionAnchor = delegateItem.row;
                            treeView.selectionModel.setCurrentIndex(treeView.index(row, column), ItemSelectionModel.ClearAndSelect);
                            ZCam.currentElement = model.element;
                            }
                        else {
                            // Make this the current element without clearing selection.
                            ZCam.currentElement = model.element;
                            }
                        var global = delegateItem.mapToItem(root, mouse.x, mouse.y);
                        root.showContextMenu(model.element, global.x, global.y);
                        }
                    }
                }

            // ── Drag handler ─────────────────────────────────────────────────
            //   Uses DragHandler with target: null so it only tracks the
            //   gesture without visually moving the delegate.  On drag end
            //   we manually find the target row at the release position and
            //   call Project.moveElement().
            DragHandler {
                id: dragHandler
                target: null
                margin: 5

                onActiveChanged: {
                    if (active) {
                        if (model.element) {
                            treeView.draggedElement = model.element;
                            delegateItem.opacity = 0.5;
                            }
                        }
                    else {
                        // Drag ended: find the drop target from the release position
                        delegateItem.opacity = 1.0;
                        // Convert the pointer's scene position to treeView content Y
                        var pos = dragHandler.centroid.scenePosition;
                        var local = treeView.mapFromItem(null, pos.x, pos.y);
                        // Adjust for scroll (contentY)
                        var yInContent = local.y + treeView.contentY;
                        treeView.performDrop(yInContent);
                        // Clear drag state
                        treeView.draggedElement = null;
                        treeView.dropTargetElement = null;
                        treeView.dropPosition = "";
                        }
                    }

                // Update visual drop feedback while dragging
                onCentroidChanged: {
                    if (!active || !model.element)
                        return;
                    var pos = dragHandler.centroid.scenePosition;
                    var local = treeView.mapFromItem(null, pos.x, pos.y);
                    var yInContent = local.y + treeView.contentY;
                    var hit = treeView.elementAtY(yInContent);
                    if (!hit || !hit.element) {
                        treeView.dropTargetElement = null;
                        treeView.dropPosition = "";
                        return;
                        }
                    var dragged = treeView.draggedElement;
                    var target = hit.element;
                    if (dragged === target) {
                        treeView.dropTargetElement = null;
                        treeView.dropPosition = "";
                        return;
                        }
                    // Prevent dropping into a descendant
                    var p = target;
                    while (p) {
                        if (p === dragged) {
                            treeView.dropTargetElement = null;
                            treeView.dropPosition = "";
                            return;
                            }
                        p = p.parent();
                        }
                    var tn = target.typeName();
                    var isContainer = (tn === "layer" || tn === "cad" || tn === "fixture" || tn === "cam" || tn === "project");
                    var isShape = (tn === "text" || tn === "polygon" || tn === "ellipse" || tn === "rectangle");
                    if ((isContainer || isShape) && hit.yRel > hit.itemH * 0.33 && hit.yRel < hit.itemH * 0.67) {
                        treeView.dropTargetElement = target;
                        treeView.dropPosition = "on";
                        }
                    else if (hit.yRel < hit.itemH * 0.5) {
                        treeView.dropTargetElement = target;
                        treeView.dropPosition = "before";
                        }
                    else {
                        treeView.dropTargetElement = target;
                        treeView.dropPosition = "after";
                        }
                    }
                }
            }
        }

    // ── Range select: select all visible rows from anchor to clicked row ─
    //   Used by Shift-click in the tree delegate.  Clears the current
    //   multi-selection and selects all visible elements between
    //   the anchor row and the clicked row (inclusive).  The clicked
    //   element becomes the current element.
    function rangeSelect(clickedRow) {
        var tm = ZCam.treeModel;
        if (!tm || !tm.root)
            return;
        // Determine the range of rows to select.
        var startRow, endRow;
        if (root.selectionAnchor < 0) {
            startRow = 0;
            endRow = clickedRow;
            }
        else {
            startRow = Math.min(root.selectionAnchor, clickedRow);
            endRow = Math.max(root.selectionAnchor, clickedRow);
            }
        // Collect all elements in the visible row range.
        var elements = [];
        for (var r = startRow; r <= endRow; ++r) {
            var idx = treeView.index(r, 0);
            if (idx && idx.valid) {
                var el = tm.elementForIndex(idx);
                if (el)
                    elements.push(el);
                }
            }
        // Update the C++ multi-selection directly: clear and add all.
        // We temporarily bypass the normal toggle/add API and set
        // the selection in bulk by calling clearSelection then
        // addToSelection for each element.
        ZCam.clearSelectionList();
        for (var i = 0; i < elements.length; ++i)
            ZCam.addToSelection(elements[i]);
        // The last element added becomes current via addToSelection.
        // Sync the tree view selection model.
        root.syncMultiSelection();
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
        else if (tn === "cam") {
            camMenu.popup(x, y);
            }
        else if (tn === "fixture") {
            fixtureMenu.popup(x, y);
            }
        else if (tn === "text" || tn === "polygon" || tn === "ellipse" || tn === "rectangle") {
            shapeMenu.popup(x, y);
            }
        else if (tn === "cameraElement") {
            cameraMenu.popup(x, y);
            }
        else if (tn === "grid") {
            gridMenu.popup(x, y);
            }
        else if (element.deletable()) {
            deleteMenu.popup(x, y);
            }
        }

    // Menu for Cad elements: "Add Group"
    Menu {
        id: cadMenu
        Material.theme: Material.Dark
        MenuItem {
            text: "Add Group"
            onTriggered: {
                if (ZCam.project)
                    ZCam.project.addLayer();
                }
            }
        }

    // Menu for Cam elements: "Add Fixture", "Add Camera", "Add Grid"
    Menu {
        id: camMenu
        Material.theme: Material.Dark
        MenuItem {
            text: "Add Fixture"
            onTriggered: {
                if (ZCam.project)
                    ZCam.project.addFixtureCmd();
                }
            }
        MenuItem {
            text: "Add Camera"
            onTriggered: {
                if (ZCam.project)
                    ZCam.project.addCameraCmd();
                }
            }
        MenuItem {
            text: "Add Grid"
            onTriggered: {
                if (ZCam.project)
                    ZCam.project.addGridCmd();
                }
            }
        }

    // Menu for Fixture elements: "Add Laserlayer" + "Delete"
    Menu {
        id: fixtureMenu
        Material.theme: Material.Dark
        MenuItem {
            text: "Add Laserlayer"
            onTriggered: {
                if (ZCam.project)
                    ZCam.project.addLaserLayerCmd(ZCam.currentElement);
                }
            }
        MenuItem {
            text: "Delete"
            onTriggered: {
                if (ZCam.project)
                    ZCam.project.removeElement(ZCam.currentElement);
                }
            }
        }

    // Menu for deletable elements (Text, LaserLayer, Fixture, Layer): "Delete"
    Menu {
        id: deleteMenu
        Material.theme: Material.Dark
        MenuItem {
            text: "Delete"
            onTriggered: {
                if (ZCam.project)
                    ZCam.project.removeElement(ZCam.currentElement);
                }
            }
        }

    // Menu for shape elements (Text, Polygon, Ellipse, Rectangle):
    // "Center on Workspace" + "Delete"
    Menu {
        id: shapeMenu
        Material.theme: Material.Dark
        MenuItem {
            text: qsTr("Center &P")
            onTriggered: {
                ZCam.centerOnWorkspace(ZCam.currentElement);
                }
            }
        MenuItem {
            text: qsTr("Delete")
            onTriggered: {
                if (ZCam.project)
                    ZCam.project.removeElement(ZCam.currentElement);
                }
            }
        }

    // Menu for CameraElement: "Delete"
    Menu {
        id: cameraMenu
        Material.theme: Material.Dark
        MenuItem {
            text: "Delete"
            onTriggered: {
                if (ZCam.project)
                    ZCam.project.removeElement(ZCam.currentElement);
                }
            }
        }

    // Menu for Grid elements: "Delete"
    Menu {
        id: gridMenu
        Material.theme: Material.Dark
        MenuItem {
            text: "Delete"
            onTriggered: {
                if (ZCam.project)
                    ZCam.project.removeElement(ZCam.currentElement);
                }
            }
        }
    }