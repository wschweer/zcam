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
import QtQuick.Controls.Basic
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import ZCam

pragma ComponentBehavior: Bound

// Reusable property editor that builds its GUI from a propertiesJson
// definition and a QAbstractListModel with the standard roles
// (propName, propValue, isRow, subProps, subValues, rowLabel).
//
// Used by both InspectorPanel (with InspectorModel) and
// ConfigMachines (with MachineModel).

Item {
    id: root

    // The model providing property data (InspectorModel or MachineModel)
    property var model

    // The propertiesJson string from the model
    property string propertiesJson: ""

    // Optional label width
    property int labelWidth: 75

    // Signals forwarded from the model
    signal modelDataChanged()

    Material.theme: Material.Dark

    // Parse the JSON string once when it changes.
    property var propMeta: {
        const s = root.propertiesJson
        if (!s || s.length === 0)
            return null
        try { return JSON.parse(s) } catch(e) { return null }
        }

    // Build a flat lookup map from property name to its metadata object.
    property var propMetaMap: {
        if (!propMeta)
            return ({})
        const map = ({})
        if (propMeta.rows && Array.isArray(propMeta.rows)) {
            // New format: top-level "rows" array
            for (let i = 0; i < propMeta.rows.length; ++i) {
                const row = propMeta.rows[i]
                if (row.cells && Array.isArray(row.cells)) {
                    for (let j = 0; j < row.cells.length; ++j) {
                        const cell = row.cells[j]
                        if (cell.name)
                            map[cell.name] = cell
                        // Handle nested cells (row within columns)
                        if (cell.cells && Array.isArray(cell.cells)) {
                            for (let k = 0; k < cell.cells.length; ++k) {
                                const subCell = cell.cells[k]
                                if (subCell.name)
                                    map[subCell.name] = subCell
                                }
                            }
                        }
                    }
                }
            }
        for (const key in propMeta) {
            if (key === "class" || key === "rows" || key === "columns")
                continue
            const val = propMeta[key]
            if (val && typeof val === "object" && !Array.isArray(val)) {
                if (val.label)
                    map[key] = val
                }
            }
        return map
        }

    function metaFor(name) {
        if (!propMetaMap)
            return null
        return propMetaMap[name] || null
        }

    function metaForSub(rowKey, subName) {
        if (propMetaMap && propMetaMap[subName])
            return propMetaMap[subName]
        if (!propMeta || !propMeta[rowKey])
            return null
        const rowVal = propMeta[rowKey]
        if (Array.isArray(rowVal)) {
            for (let i = 0; i < rowVal.length; ++i) {
                const elem = rowVal[i]
                if (elem && elem[subName])
                    return elem[subName]
                }
            return null
            }
        if (!rowVal || !rowVal[subName])
            return null
        return rowVal[subName]
        }

    function defaultScalar(name, component) {
        const m = metaFor(name)
        if (!m || m.default === undefined)
            return 0
        if (Array.isArray(m.default))
            return m.default[component] ?? 0
        return Number(m.default)
        }

    // Like defaultScalar but takes the metadata object directly.
    // Used by sub-delegates that already hold subMeta.
    function defaultScalarFromMeta(m, component) {
        if (!m || m.default === undefined)
            return 0
        if (Array.isArray(m.default))
            return m.default[component] ?? 0
        return Number(m.default)
        }

    // Derive step sizes from precision.
    // precision N means N decimal places.
    //   stepSize  = 10^(1-N)  → precision 2 gives 0.1
    //   bigStep   = 10^(2-N)  → precision 2 gives 1.0
    //   minStep   = 10^(-N)   → precision 2 gives 0.01
    // JSON config can override with "step", "bigStep", "minStep".
    function defaultStepSize(meta) {
        if (meta && meta.step !== undefined)
            return meta.step
        const p = meta && meta.precision !== undefined ? meta.precision : 2
        return Math.pow(10, 1 - p)
        }

    function defaultBigStep(meta) {
        if (meta && meta.bigStep !== undefined)
            return meta.bigStep
        const p = meta && meta.precision !== undefined ? meta.precision : 2
        return Math.pow(10, 2 - p)
        }

    function defaultMinStep(meta) {
        if (meta && meta.minStep !== undefined)
            return meta.minStep
        const p = meta && meta.precision !== undefined ? meta.precision : 2
        return Math.pow(10, -p)
        }

    // ── "enabled" keyword support ──────────────────────────────────────
    // A cell JSON object may contain an "enabled" key whose value is the
    // name of a bool property on the same element/machine/pass.  The GUI
    // control for that cell is disabled when the named property is false.
    //
    // We need to re-evaluate the enabled state whenever any property value
    // changes.  The model emits dataChanged for the row whose property
    // changed, but the delegate for the *enabled* cell may be on a different
    // row.  To propagate, we use a shared counter property _dataChangeCounter
    // that is incremented on every dataChanged emission from the model.  The
    // enabled bindings depend on this counter, so they re-evaluate.
    property int _dataChangeCounter: 0

    Connections {
        target: root.model
        ignoreUnknownSignals: true
        function onDataChanged() { root._dataChangeCounter++ }
    }

    // Returns true if the control for the given metadata should be enabled.
    // meta is the cell's JSON metadata object (from propMetaMap).
    function isPropEnabled(meta) {
        // Touch the counter so the binding re-evaluates on dataChanged.
        const _ = root._dataChangeCounter
        if (!meta || meta.enabled === undefined)
            return true
        const depName = meta.enabled
        if (!root.model || !root.model.elementProperty)
            return true
        const v = root.model.elementProperty(depName)
        return v === true
        }

    // ── Reusable borderless SpinBox ──────────────────────────────────────────
    component BareSpinBox : SpinBox {
        id: _sb
        editable: true

        up.indicator: Item {}
        down.indicator: Item {}

        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0

        background: Item {}

        contentItem: TextInput {
            text: _sb.displayText
            color: _sb.enabled ? "#ffffff" : "#888888"
            font.bold: true
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            readOnly: !_sb.editable
            validator: _sb.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            z: 1000

            onWheel: event => {
                event.accepted = true;
                let step = _sb.stepSize > 0 ? _sb.stepSize : 1;
                if (event.angleDelta.y > 0)
                    _sb.value = Math.min(_sb.to, _sb.value + step);
                else
                    _sb.value = Math.max(_sb.from, _sb.value - step);
                }

            onClicked: mouse => {
                if (_sb.contentItem) {
                    _sb.contentItem.forceActiveFocus();
                    const pos = mapToItem(_sb.contentItem, mouse.x, mouse.y);
                    _sb.contentItem.cursorPosition = _sb.contentItem.positionAt(pos.x, pos.y);
                    }
                }

            onDoubleClicked: mouse => {
                if (_sb.resetValue !== undefined)
                    _sb.value = _sb.resetValue;
                }
            }

        property int resetValue: 0
        }

    // ── Reusable borderless DoubleSpinBox ────────────────────────────────────
    component BareDoubleSpinBox : DoubleSpinBox {
        id: _dsb
        editable: true

        up.indicator: Item {}
        down.indicator: Item {}

        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0

        background: Item {}

        contentItem: TextInput {
            text: _dsb.displayText
            color: _dsb.enabled ? "#ffffff" : "#888888"
            font.bold: true
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            readOnly: !_dsb.editable
            validator: _dsb.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            z: 1000

            onWheel: event => {
                event.accepted = true;
                let step;
                if (event.modifiers & Qt.ControlModifier)
                    step = _dsb.bigStep > 0 ? _dsb.bigStep : (_dsb.stepSize > 0 ? _dsb.stepSize * 10.0 : 10.0);
                else if (event.modifiers & Qt.ShiftModifier)
                    step = _dsb.minStep > 0 ? _dsb.minStep : (_dsb.stepSize > 0 ? _dsb.stepSize * 0.1 : 0.1);
                else
                    step = _dsb.stepSize > 0 ? _dsb.stepSize : 1.0;
                                if (event.angleDelta.y > 0)
                    _dsb.value = Math.min(_dsb.to, _dsb.value + step);
                else
                    _dsb.value = Math.max(_dsb.from, _dsb.value - step);
                                }

            onClicked: mouse => {
                if (_dsb.contentItem) {
                    _dsb.contentItem.forceActiveFocus();
                    const pos = mapToItem(_dsb.contentItem, mouse.x, mouse.y);
                    _dsb.contentItem.cursorPosition = _dsb.contentItem.positionAt(pos.x, pos.y);
                    }
                }

            onDoubleClicked: mouse => {
                if (_dsb.resetValue !== undefined)
                    _dsb.value = _dsb.resetValue;
                }
            }

        property real resetValue
        property real bigStep
        property real minStep
        }

    // ── ValueBox ─────────────────────────────────────────────────────────────
    component ValueBox : Rectangle {
        id: vbox
        color: vbox.enabled ? "#a9a9a9" : "#5a5a5a"
        radius: 4
        implicitHeight: 28
        opacity: vbox.enabled ? 1.0 : 0.5

        property string unitText: ""
        property string subLabelText: ""
        property bool subLabelAlignRight: false

        default property alias contentChildren: contentColumn.data

        Item {
            id: contentColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: 6
            anchors.rightMargin: vbox.unitText.length > 0 ? 14 : 6
            }

        Text {
            visible: vbox.subLabelText.length > 0 && !vbox.subLabelAlignRight
            text: vbox.subLabelText
            font.pixelSize: 13
            color: vbox.enabled ? "#333333" : "#666666"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
            anchors.left: parent.left
            anchors.leftMargin: 4
            }

        Text {
            visible: vbox.subLabelText.length > 0 && vbox.subLabelAlignRight
            text: vbox.subLabelText
            font.pixelSize: 13
            color: vbox.enabled ? "#333333" : "#666666"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 4
            anchors.bottomMargin: 1
            }

        Item {
            id: unitContainer
            visible: vbox.unitText.length > 0
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 12
            height: parent.height

            Text {
                id: unitLabel
                text: vbox.unitText
                font.pixelSize: 9
                color: vbox.enabled ? "#333333" : "#666666"
                rotation: 90
                transformOrigin: Item.Center
                anchors.centerIn: parent
                }
            }
        }

    // ── Property ListView ────────────────────────────────────────────────────
    ListView {
        id: listView
        anchors.fill: parent
        model: root.model
        clip: true
        spacing: 2

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        delegate: Item {
            id: delegateRoot
            width: ListView.view.width
            height: loader.item ? loader.item.implicitHeight : 0

            required property var model
            required property int index

            Loader {
                id: loader
                width: delegateRoot.width

                sourceComponent: {
                    if (delegateRoot.model.isRow)
                        return rowDelegate
                    if (delegateRoot.model.isColumns)
                        return columnsDelegate
                    if (delegateRoot.model.propName === "empty")
                        return emptyDelegate
                    if (delegateRoot.model.propName === "line")
                        return lineDelegate
                    const m = root.metaFor(delegateRoot.model.propName)
                    if (!m) return null
                    const t = m.type || "string"
                    switch (t) {
                        case "bool":      return boolDelegate
                        case "int":       return intDelegate
                        case "float":     return floatDelegate
                        case "vector3d":  return vector3dDelegate
                        case "scale":      return vector3dDelegate
                        case "vector2d":  return vector2dDelegate
                        case "font":      return fontDelegate
                        case "halign":    return halignDelegate
                        case "multiline": return multilineDelegate
                        case "singleline":return singlelineDelegate
                        case "path":      return pathDelegate
                        case "line":      return lineDelegate
                        case "color":     return colorDelegate
                        case "layer":      return layerDelegate
                        case "laserLayer": return laserLayerDelegate
                        case "recipe":    return recipeDelegate
                        case "machine":   return machineDelegate
                        case "machineName": return machineNameDelegate
                        case "machineType": return machineTypeDelegate
                        case "boardType":  return boardTypeDelegate
                        case "override":  return overrideDelegate
                        case "pulsewidth": return pulsewidthDelegate
                        case "lineJoin":  return lineJoinDelegate
                        case "lineEnd":    return lineEndDelegate
                        case "lockScale":  return lockScaleDelegate
                        case "lockSize":   return lockSizeDelegate
                        case "framingType": return framingTypeDelegate
                        case "ethDevice":  return ethDeviceDelegate
                        case "cameraName": return cameraNameDelegate
                        case "cameraResolution": return cameraResolutionDelegate
                        case "cameraFrameRate": return cameraFrameRateDelegate
                        case "cameraView": return cameraViewDelegate
                        case "empty":      return emptyDelegate
                        default:          return stringDelegate
                        }
                    }

                onLoaded: {
                    if (!item)
                        return
                    if (delegateRoot.model.isRow) {
                        item.propName   = delegateRoot.model.propName
                        item.subProps   = delegateRoot.model.subProps
                        item.subValues  = Qt.binding(() => delegateRoot.model.subValues)
                        item.rowLabel   = delegateRoot.model.rowLabel ?? ""
                        item.propIndex  = delegateRoot.index
                        item.setSubValue = function(subName, v) {
                            root.model.setSubProperty(delegateRoot.index, subName, v)
                            }
                        }
                    else if (delegateRoot.model.isColumns) {
                        item.propIndex   = delegateRoot.index
                        item.columnCount = delegateRoot.model.columnCount
                        item.columnItems = Qt.binding(() => delegateRoot.model.columnItems)
                        item.setModelValue = function(propName, v) {
                            root.model.setColumnProperty(delegateRoot.index, propName, v)
                            }
                        item.setSubValue = function(rowItem, subName, v) {
                            root.model.setColumnProperty(delegateRoot.index, subName, v)
                            }
                        }
                    else {
                        item.propName  = delegateRoot.model.propName
                        item.propValue = Qt.binding(() => delegateRoot.model.propValue)
                        item.meta      = root.metaFor(delegateRoot.model.propName)
                        item.propIndex = delegateRoot.index
                        item.enabled  = Qt.binding(() => root.isPropEnabled(item.meta))
                        item.opacity  = Qt.binding(() => item.enabled ? 1.0 : 0.4)
                        item.setModelValue = function(v) {
                            delegateRoot.model.propValue = v
                            }
                        }
                    }
                }
            }

        // ── line: horizontal separator ───────────────────────────────────
        Component {
            id: lineDelegate

            Item {
                width: parent ? parent.width : 0
                implicitHeight: 8

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 1
                    color: Material.accentColor
                    opacity: 0.3
                    }
                }
            }

        // ── empty: placeholder that takes space but renders nothing ────
        Component {
            id: emptyDelegate

            Item {
                width: parent ? parent.width : 0
                implicitHeight: 28

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}
                }
            }

        // ── subEmpty: empty placeholder for row entries ─────────────────
        Component {
            id: subEmptyDelegate

            Item {
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                implicitHeight: 28

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}
                }
            }

        // ── row: multiple sub-properties on one line ──────────────────────
        Component {
            id: rowDelegate

            RowLayout {
                id: rowContainer
                width: parent ? parent.width : 0
                spacing: 4

                property string propName
                property var subProps
                property var subValues
                property string rowLabel
                property int propIndex
                property var setSubValue

                Label {
                    text: rowContainer.rowLabel
                    Layout.preferredWidth: root.labelWidth
                    Layout.rightMargin: 2
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    visible: rowContainer.rowLabel.length > 0
                    }

                Repeater {
                    model: rowContainer.subProps

                    delegate: Loader {
                        id: subLoader
                        Layout.fillWidth: true

                        required property string modelData
                        required property int index

                        property string subName: modelData
                        property var subMeta: root.metaForSub(rowContainer.propName, subName)
                        property var subValue: rowContainer.subValues ? rowContainer.subValues[index] : undefined

                        sourceComponent: {
                            if (subLoader.subName === "empty")
                                return subEmptyDelegate
                            const m = subLoader.subMeta
                            if (!m) return null
                            const t = m.type || "string"
                            switch (t) {
                                case "bool":      return subBoolDelegate
                                case "fontStyle": return subFontStyleDelegate
                                case "int":       return subIntDelegate
                                case "float":     return subFloatDelegate
                                case "vector3d":  return subVector3dDelegate
                                case "scale":     return subVector3dDelegate
                                case "vector2d":  return subVector2dDelegate
                                case "halign":    return subHalignDelegate
                                case "laserLayer": return subLaserLayerDelegate
                                case "recipe":    return subRecipeDelegate
                                case "color":     return subColorDelegate
                                case "machine": return subMachineDelegate
                                case "machineName": return subMachineNameDelegate
                                case "machineType": return subMachineTypeDelegate
                                case "boardType":  return subBoardTypeDelegate
                                case "override":  return subOverrideDelegate
                                case "pulsewidth": return subPulsewidthDelegate
                                case "lineJoin":  return subLineJoinDelegate
                                case "lineEnd":    return subLineEndDelegate
                                case "lockScale":  return subLockScaleDelegate
                                case "lockSize":   return subLockSizeDelegate
                                case "framingType": return subFramingTypeDelegate
                                case "cameraName": return subCameraNameDelegate
                                case "cameraResolution": return subCameraResolutionDelegate
                                case "cameraFrameRate": return subCameraFrameRateDelegate
                                case "ethDevice":  return subEthDeviceDelegate
                                case "multiline":return subMultilineDelegate
                                case "singleline":return subSinglelineDelegate
                                case "empty":      return subEmptyDelegate
                                case "string":    return subStringDelegate
                                default:          return subStringDelegate
                                }
                            }

                        onLoaded: {
                            if (!item)
                                return
                            item.subName  = subLoader.subName
                            item.subValue = Qt.binding(() => subLoader.subValue)
                            item.subMeta   = subLoader.subMeta
                            item.enabled  = Qt.binding(() => root.isPropEnabled(subLoader.subMeta))
                            item.opacity  = Qt.binding(() => item.enabled ? 1.0 : 0.4)
                            item.setSub    = function(v) {
                                if (rowContainer.setSubValue)
                                    rowContainer.setSubValue(subLoader.subName, v)
                                }
                            }
                        }
                    }
                }
            }

        // ── columns: multi-column layout for multiple properties ────────
        //  Uses a Column of RowLayouts. Each item gets a fixed width
        //  calculated from (parentWidth / columnCount * colSpan).
        //  This avoids Layout.fillWidth issues where inner RowLayout
        //  delegates override their width with parent.width.
        Component {
            id: columnsDelegate

            Column {
                id: colsContainer
                width: parent ? parent.width : 0
                spacing: 2

                property int propIndex
                property int columnCount: 2
                property var columnItems: []
                property var setModelValue: function(propName, v) {}
                property var setSubValue: function(rowItem, subName, v) {}

                // Structural key: changes only when items are added/removed/
                // reordered or change type, NOT when property values change.
                // This prevents the Repeater from rebuilding on value-only updates.
                property string _rowsKey: {
                    const items = colsContainer.columnItems
                    if (!items || !items.length)
                        return ""
                    let key = ""
                    for (let i = 0; i < items.length; ++i) {
                        const item = items[i]
                        key += (item.name || "") + "|" + (item.colSpan || 1) + "|"
                              + (item.isRow ? "1" : "0") + "|" + (item.isLine ? "1" : "0") + "|"
                              + (item.isEmpty ? "1" : "0") + ";"
                    }
                    return key
                    }

                // Cache of structural item data, rebuilt only when _rowsKey changes.
                property var _structItems: []

                // Group flat columnItems into visual rows respecting
                // columnCount and per-item colSpan.
                // Only depends on _rowsKey + _structItems so that value-only
                // changes don't cause the Repeater to rebuild (which would
                // flicker other widgets like the FPK checkbox).
                property var rows: {
                    // Touch _rowsKey to set up the dependency
                    const _k = colsContainer._rowsKey
                    const items = colsContainer._structItems
                    if (!items || !items.length)
                        return []
                    const numCols = colsContainer.columnCount
                    const result = []
                    let currentRow = []
                    let currentCol = 0
                    for (let i = 0; i < items.length; ++i) {
                        const item = items[i]
                        const span = item.colSpan || 1
                        if (currentCol + span > numCols && currentRow.length > 0) {
                            result.push(currentRow)
                            currentRow = []
                            currentCol = 0
                            }
                        currentRow.push({
                            name: item.name,
                            isRow: item.isRow,
                            isLine: item.isLine,
                            isEmpty: item.isEmpty,
                            colSpan: span,
                            rowLabel: item.rowLabel || "",
                            subProps: item.subProps || [],
                            _idx: i
                            })
                        currentCol += span
                        if (currentCol >= numCols) {
                            result.push(currentRow)
                            currentRow = []
                            currentCol = 0
                            }
                        }
                    if (currentRow.length > 0)
                        result.push(currentRow)
                    return result
                    }

                // Rebuild _structItems whenever _rowsKey changes
                on_RowsKeyChanged: {
                    const items = colsContainer.columnItems
                    const struct = []
                    if (items && items.length) {
                        for (let i = 0; i < items.length; ++i) {
                            const item = items[i]
                            struct.push({
                                name: item.name,
                                isRow: item.isRow,
                                isLine: item.isLine,
                                isEmpty: item.isEmpty,
                                colSpan: item.colSpan || 1,
                                rowLabel: item.rowLabel || "",
                                subProps: item.subProps || []
                            })
                        }
                    }
                    colsContainer._structItems = struct
                    }

                Repeater {
                    model: colsContainer.rows

                    delegate: Row {
                        id: colsRow
                        width: colsContainer.width
                        spacing: 4

                        required property var modelData
                        required property int index

                        Repeater {
                            model: colsRow.modelData

                            delegate: Loader {
                                id: colLoader

                                required property var modelData
                                required property int index

                                property var itemData: modelData

                                // Fixed width = (totalWidth - spacing) / numCols * colSpan
                                width: {
                                    const numCols = colsContainer.columnCount
                                    const span = (colLoader.itemData ? (colLoader.itemData.colSpan || 1) : 1)
                                    const totalW = colsContainer.width
                                    const gap = (numCols - 1) * colsRow.spacing
                                    const colW = Math.max(0, (totalW - gap) / numCols)
                                    if (span >= numCols)
                                        return totalW
                                    return colW * span + (span - 1) * colsRow.spacing
                                    }

                                sourceComponent: {
                                    const d = colLoader.itemData
                                    if (!d)
                                        return null
                                    if (d.isLine)
                                        return colLineDelegate
                                    if (d.isEmpty)
                                        return emptyDelegate
                                    if (d.isRow)
                                        return colRowDelegate
                                    const m = root.metaFor(d.name)
                                    if (!m)
                                        return null
                                    const t = m.type || "string"
                                    switch (t) {
                                        case "bool":      return boolDelegate
                                        case "int":       return intDelegate
                                        case "float":     return floatDelegate
                                        case "vector3d":  return vector3dDelegate
                                        case "scale":      return vector3dDelegate
                                        case "vector2d":  return vector2dDelegate
                                        case "font":      return fontDelegate
                                        case "halign":    return halignDelegate
                                        case "multiline": return multilineDelegate
                                        case "singleline":return singlelineDelegate
                                        case "path":      return pathDelegate
                                        case "color":     return colorDelegate
                                        case "layer":      return layerDelegate
                                        case "laserLayer": return laserLayerDelegate
                                        case "recipe":    return recipeDelegate
                                        case "machine":   return machineDelegate
                                        case "machineName": return machineNameDelegate
                                        case "machineType": return machineTypeDelegate
                                        case "boardType":  return boardTypeDelegate
                                        case "override":  return overrideDelegate
                                        case "pulsewidth": return pulsewidthDelegate
                                        case "lineJoin":  return lineJoinDelegate
                                        case "lineEnd":    return lineEndDelegate
                                        case "lockScale":  return lockScaleDelegate
                                        case "lockSize":   return lockSizeDelegate
                                        case "framingType": return framingTypeDelegate
                                        case "ethDevice":  return ethDeviceDelegate
                                        case "cameraName": return cameraNameDelegate
                                        case "cameraResolution": return cameraResolutionDelegate
                                        case "cameraFrameRate": return cameraFrameRateDelegate
                                        case "cameraView": return cameraViewDelegate
                                        case "empty":      return emptyDelegate
                                        default:          return stringDelegate
                                        }
                                    }

                                onLoaded: {
                                    const d = colLoader.itemData
                                    if (!d || !item)
                                        return
                                    if (d.isLine)
                                        return
                                    if (d.isEmpty)
                                        return
                                    if (d.isRow) {
                                        item.propName   = "row"
                                        item.subProps   = d.subProps
                                        item.subValues  = Qt.binding(() => {
                                            // Read directly from the model's columnItems
                                            // so value-only changes don't rebuild the Repeater.
                                            const ci = colsContainer.columnItems
                                            if (!ci || d._idx >= ci.length) return undefined
                                            return ci[d._idx].subValues
                                        })
                                        item.rowLabel   = d.rowLabel || ""
                                        item.propIndex  = colsContainer.propIndex
                                        item.setSubValue = function(subName, v) {
                                            colsContainer.setSubValue(d, subName, v)
                                            }
                                        }
                                    else {
                                        item.propName  = d.name
                                        item.propValue = Qt.binding(() => {
                                            // Read directly from the model's columnItems
                                            // so value-only changes don't rebuild the Repeater.
                                            const ci = colsContainer.columnItems
                                            if (!ci || d._idx >= ci.length) return undefined
                                            return ci[d._idx].propValue
                                            })
                                        item.meta = Qt.binding(() => root.metaFor(item.propName))
                                        item.propIndex = colsContainer.propIndex
                                        item.enabled = Qt.binding(() => root.isPropEnabled(item.meta))
                                        item.opacity = Qt.binding(() => item.enabled ? 1.0 : 0.4)
                                        item.setModelValue = function(v) {
                                            colsContainer.setModelValue(d.name, v)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

        // ── colLineDelegate: horizontal separator inside columns ────────
        Component {
            id: colLineDelegate

            Item {
                width: parent ? parent.width : 0
                height: 8
                implicitHeight: 8

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 1
                    color: Material.accentColor
                    opacity: 0.3
                    }
                }
            }

        // ── colRowDelegate: row of sub-properties inside columns ────────
        Component {
            id: colRowDelegate

            RowLayout {
                id: rowContainerCol
                width: parent ? parent.width : 0
                spacing: 4

                property string propName
                property var subProps
                property var subValues
                property string rowLabel
                property int propIndex
                property var setSubValue

                Label {
                    text: rowContainerCol.rowLabel
                    Layout.preferredWidth: root.labelWidth
                    Layout.rightMargin: 2
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    visible: rowContainerCol.rowLabel.length > 0
                    }

                Repeater {
                    model: rowContainerCol.subProps

                    delegate: Loader {
                        id: subLoaderCol
                        Layout.fillWidth: true

                        required property string modelData
                        required property int index

                        property string subName: modelData
                        property var subMeta: root.metaForSub(rowContainerCol.propName, subName)
                        property var subValue: rowContainerCol.subValues ? rowContainerCol.subValues[index] : undefined

                        sourceComponent: {
                            if (subLoaderCol.subName === "empty")
                                return subEmptyDelegate
                            const m = subLoaderCol.subMeta
                            if (!m) return null
                            const t = m.type || "string"
                            switch (t) {
                                case "bool":      return subBoolDelegate
                                case "fontStyle": return subFontStyleDelegate
                                case "int":       return subIntDelegate
                                case "float":     return subFloatDelegate
                                case "vector3d":  return subVector3dDelegate
                                case "scale":     return subVector3dDelegate
                                case "vector2d":  return subVector2dDelegate
                                case "halign":    return subHalignDelegate
                                case "laserLayer": return subLaserLayerDelegate
                                case "recipe":    return subRecipeDelegate
                                case "color":     return subColorDelegate
                                case "machine": return subMachineDelegate
                                case "machineName": return subMachineNameDelegate
                                case "machineType": return subMachineTypeDelegate
                                case "boardType":  return subBoardTypeDelegate
                                case "override":  return subOverrideDelegate
                                case "pulsewidth": return subPulsewidthDelegate
                                case "lineJoin":  return subLineJoinDelegate
                                case "lineEnd":    return subLineEndDelegate
                                case "lockScale":  return subLockScaleDelegate
                                case "lockSize":   return subLockSizeDelegate
                                case "framingType": return subFramingTypeDelegate
                                case "cameraName": return subCameraNameDelegate
                                case "cameraResolution": return subCameraResolutionDelegate
                                case "cameraFrameRate": return subCameraFrameRateDelegate
                                case "ethDevice":  return subEthDeviceDelegate
                                case "multiline":return subMultilineDelegate
                                case "singleline":return subSinglelineDelegate
                                case "empty":      return subEmptyDelegate
                                case "string":    return subStringDelegate
                                default:          return subStringDelegate
                                }
                            }

                        onLoaded: {
                            if (!item)
                                return
                            item.subName  = subLoaderCol.subName
                            item.subValue = Qt.binding(() => subLoaderCol.subValue)
                            item.subMeta   = subLoaderCol.subMeta
                            item.enabled  = Qt.binding(() => root.isPropEnabled(subLoaderCol.subMeta))
                            item.opacity  = Qt.binding(() => item.enabled ? 1.0 : 0.4)
                            item.setSub    = function(v) {
                                if (rowContainerCol.setSubValue)
                                    rowContainerCol.setSubValue(subLoaderCol.subName, v)
                                }
                            }
                        }
                    }
                }
            }

        // ── Sub-delegates for row entries ─────────────────────────────────

        Component {
            id: subBoolDelegate

            ValueBox {
                id: subBool
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                width: parent ? parent.width : 0
                subLabelText: subBool.subMeta ? subBool.subMeta.sublabel ?? subBool.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                CheckBox {
                    anchors.centerIn: parent
                    checked: subBool.subValue === true
                    onToggled: if (subBool.setSub) subBool.setSub(checked)
                    }
                }
            }

        Component {
            id: subFontStyleDelegate

            ValueBox {
                id: subFontStyle
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                width: parent ? parent.width : 0

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                // The subLabelText is rendered as a styled Text instead of
                // the default sub-label, so the label reflects the font style:
                //   bold      → bold "B"
                //   italic    → italic "I"
                //   underline → underlined "U"
                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 4
                    anchors.bottomMargin: 1
                    text: subFontStyle.subMeta ? subFontStyle.subMeta.sublabel ?? subFontStyle.subMeta.label ?? "" : ""
                    font.pixelSize: 13
                    font.bold: subFontStyle.subName === "bold"
                    font.italic: subFontStyle.subName === "italic"
                    font.underline: subFontStyle.subName === "underline"
                    color: "#333333"
                }

                CheckBox {
                    anchors.centerIn: parent
                    checked: subFontStyle.subValue === true
                    onToggled: if (subFontStyle.setSub) subFontStyle.setSub(checked)
                    }
                }
            }

        Component {
            id: subIntDelegate

            ValueBox {
                id: subInt
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                unitText: subInt.subMeta ? subInt.subMeta.unit ?? "" : ""
                subLabelText: subInt.subMeta ? subInt.subMeta.sublabel ?? subInt.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                BareSpinBox {
                    id: subIntSpin
                    anchors.fill: parent
                    from: subInt.subMeta && subInt.subMeta.min !== undefined ? Math.round(subInt.subMeta.min) : -1000000
                    to: subInt.subMeta && subInt.subMeta.max !== undefined ? Math.round(subInt.subMeta.max) : 1000000
                    resetValue: root.defaultScalarFromMeta(subInt.subMeta, 0)

                    property int modelValue: subInt.subValue !== undefined ? Number(subInt.subValue) : 0
                    value: modelValue
                    onModelValueChanged: if (value !== modelValue) value = modelValue

                    onValueChanged: {
                        if (subInt.setSub && subInt.subValue !== value)
                            subInt.setSub(value);
                        }

                    }
                }
            }

        Component {
            id: subFloatDelegate

            ValueBox {
                id: subFloat
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                unitText: subFloat.subMeta ? subFloat.subMeta.unit ?? "" : ""
                subLabelText: subFloat.subMeta ? subFloat.subMeta.sublabel ?? subFloat.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                BareDoubleSpinBox {
                    id: subFloatSpin
                    anchors.fill: parent
                    from: subFloat.subMeta && subFloat.subMeta.min !== undefined ? subFloat.subMeta.min : -1000000.0
                    to: subFloat.subMeta && subFloat.subMeta.max !== undefined ? subFloat.subMeta.max : 1000000.0
                    stepSize: root.defaultStepSize(subFloat.subMeta)
                    bigStep: root.defaultBigStep(subFloat.subMeta)
                    minStep: root.defaultMinStep(subFloat.subMeta)
                    resetValue: root.defaultScalarFromMeta(subFloat.subMeta, 0)

                    decimals: subFloat.subMeta && subFloat.subMeta.precision !== undefined ? subFloat.subMeta.precision : 2

                    property real modelValue: subFloat.subValue !== undefined ? Number(subFloat.subValue) : 0.0
                    value: modelValue
                    onModelValueChanged: if (value !== modelValue) value = modelValue

                    onValueChanged: {
                        if (subFloat.setSub && subFloat.subValue !== value)
                            subFloat.setSub(value);
                        }

                    }
                }
            }

        Component {
            id: subSinglelineDelegate

            ValueBox {
                id: subSingle
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelText: subSingle.subMeta ? subSingle.subMeta.sublabel ?? subSingle.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                TextInput {
                    anchors.fill: parent
                    text: subSingle.subValue !== undefined ? subSingle.subValue : ""
                    onEditingFinished: if (subSingle.setSub) subSingle.setSub(text)
                    horizontalAlignment: TextInput.AlignRight
                    verticalAlignment: TextInput.AlignVCenter
                    color: "#ffffff"
                    clip: true
                    }
                }
            }

        // ── subVector3d: three ValueBox+BareDoubleSpinBox for vector3d in row entries ─
        Component {
            id: subVector3dDelegate

            RowLayout {
                id: subVec3
                Layout.fillWidth: true
                spacing: 2

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ValueBox {
                    id: subVec3xBox
                    Layout.fillWidth: true
                    unitText: subVec3.subMeta ? subVec3.subMeta.unit ?? "" : ""
                    subLabelText: "X"

                    BareDoubleSpinBox {
                        id: subVec3xSpin
                        anchors.fill: parent
                        from: subVec3.subMeta && subVec3.subMeta.min !== undefined ? subVec3.subMeta.min : -1000000.0
                        to: subVec3.subMeta && subVec3.subMeta.max !== undefined ? subVec3.subMeta.max : 1000000.0
                        stepSize: root.defaultStepSize(subVec3.subMeta)
                        bigStep: root.defaultBigStep(subVec3.subMeta)
                        minStep: root.defaultMinStep(subVec3.subMeta)
                        resetValue: root.defaultScalarFromMeta(subVec3.subMeta, 0)
                        decimals: subVec3.subMeta && subVec3.subMeta.precision !== undefined ? subVec3.subMeta.precision : 2

                        property real modelValue: subVec3.subValue !== undefined && subVec3.subValue.x !== undefined ? Number(subVec3.subValue.x) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue
                        onValueChanged: {
                            if (subVec3.subValue !== undefined && subVec3.subValue.x !== value) {
                                let v = subVec3.subValue
                                subVec3.setSub(Qt.vector3d(value, v.y, v.z))
                            }
                        }
                    }
                }
                ValueBox {
                    id: subVec3yBox
                    Layout.fillWidth: true
                    unitText: subVec3.subMeta ? subVec3.subMeta.unit ?? "" : ""
                    subLabelText: "Y"

                    BareDoubleSpinBox {
                        id: subVec3ySpin
                        anchors.fill: parent
                        from: subVec3.subMeta && subVec3.subMeta.min !== undefined ? subVec3.subMeta.min : -1000000.0
                        to: subVec3.subMeta && subVec3.subMeta.max !== undefined ? subVec3.subMeta.max : 1000000.0
                        stepSize: root.defaultStepSize(subVec3.subMeta)
                        bigStep: root.defaultBigStep(subVec3.subMeta)
                        minStep: root.defaultMinStep(subVec3.subMeta)
                        resetValue: root.defaultScalarFromMeta(subVec3.subMeta, 1)
                        decimals: subVec3.subMeta && subVec3.subMeta.precision !== undefined ? subVec3.subMeta.precision : 2

                        property real modelValue: subVec3.subValue !== undefined && subVec3.subValue.y !== undefined ? Number(subVec3.subValue.y) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue
                        onValueChanged: {
                            if (subVec3.subValue !== undefined && subVec3.subValue.y !== value) {
                                let v = subVec3.subValue
                                subVec3.setSub(Qt.vector3d(v.x, value, v.z))
                            }
                        }
                    }
                }
                ValueBox {
                    id: subVec3zBox
                    Layout.fillWidth: true
                    unitText: subVec3.subMeta ? subVec3.subMeta.unit ?? "" : ""
                    subLabelText: "Z"

                    BareDoubleSpinBox {
                        id: subVec3zSpin
                        anchors.fill: parent
                        from: subVec3.subMeta && subVec3.subMeta.min !== undefined ? subVec3.subMeta.min : -1000000.0
                        to: subVec3.subMeta && subVec3.subMeta.max !== undefined ? subVec3.subMeta.max : 1000000.0
                        stepSize: root.defaultStepSize(subVec3.subMeta)
                        bigStep: root.defaultBigStep(subVec3.subMeta)
                        minStep: root.defaultMinStep(subVec3.subMeta)
                        resetValue: root.defaultScalarFromMeta(subVec3.subMeta, 2)
                        decimals: subVec3.subMeta && subVec3.subMeta.precision !== undefined ? subVec3.subMeta.precision : 2

                        property real modelValue: subVec3.subValue !== undefined && subVec3.subValue.z !== undefined ? Number(subVec3.subValue.z) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue
                        onValueChanged: {
                            if (subVec3.subValue !== undefined && subVec3.subValue.z !== value) {
                                let v = subVec3.subValue
                                subVec3.setSub(Qt.vector3d(v.x, v.y, value))
                            }
                        }
                    }
                }
            }
        }

        // ── subVector2d: two ValueBox+BareDoubleSpinBox for vector2d in row entries ─
        Component {
            id: subVector2dDelegate

            RowLayout {
                id: subVec2
                Layout.fillWidth: true
                spacing: 2

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ValueBox {
                    id: subVec2xBox
                    Layout.fillWidth: true
                    unitText: subVec2.subMeta ? subVec2.subMeta.unit ?? "" : ""
                    subLabelText: "X"

                    BareDoubleSpinBox {
                        id: subVec2xSpin
                        anchors.fill: parent
                        from: subVec2.subMeta && subVec2.subMeta.min !== undefined ? subVec2.subMeta.min : -1000000.0
                        to: subVec2.subMeta && subVec2.subMeta.max !== undefined ? subVec2.subMeta.max : 1000000.0
                        stepSize: root.defaultStepSize(subVec2.subMeta)
                        bigStep: root.defaultBigStep(subVec2.subMeta)
                        minStep: root.defaultMinStep(subVec2.subMeta)
                        resetValue: root.defaultScalarFromMeta(subVec2.subMeta, 0)
                        decimals: subVec2.subMeta && subVec2.subMeta.precision !== undefined ? subVec2.subMeta.precision : 2

                        property real modelValue: subVec2.subValue !== undefined && subVec2.subValue.x !== undefined ? Number(subVec2.subValue.x) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue
                        onValueChanged: {
                            if (subVec2.subValue !== undefined && subVec2.subValue.x !== value) {
                                let v = subVec2.subValue
                                subVec2.setSub(Qt.vector2d(value, v.y))
                            }
                        }
                    }
                }
                ValueBox {
                    id: subVec2yBox
                    Layout.fillWidth: true
                    unitText: subVec2.subMeta ? subVec2.subMeta.unit ?? "" : ""
                    subLabelText: "Y"

                    BareDoubleSpinBox {
                        id: subVec2ySpin
                        anchors.fill: parent
                        from: subVec2.subMeta && subVec2.subMeta.min !== undefined ? subVec2.subMeta.min : -1000000.0
                        to: subVec2.subMeta && subVec2.subMeta.max !== undefined ? subVec2.subMeta.max : 1000000.0
                        stepSize: root.defaultStepSize(subVec2.subMeta)
                        bigStep: root.defaultBigStep(subVec2.subMeta)
                        minStep: root.defaultMinStep(subVec2.subMeta)
                        resetValue: root.defaultScalarFromMeta(subVec2.subMeta, 1)
                        decimals: subVec2.subMeta && subVec2.subMeta.precision !== undefined ? subVec2.subMeta.precision : 2

                        property real modelValue: subVec2.subValue !== undefined && subVec2.subValue.y !== undefined ? Number(subVec2.subValue.y) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue
                        onValueChanged: {
                            if (subVec2.subValue !== undefined && subVec2.subValue.y !== value) {
                                let v = subVec2.subValue
                                subVec2.setSub(Qt.vector2d(v.x, value))
                            }
                        }
                    }
                }
            }
        }

        // ── subMultiline: multi-line TextArea for row entries ────────────
        Component {
            id: subMultilineDelegate

            ValueBox {
                id: subMulti
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                implicitHeight: 80
                subLabelText: subMulti.subMeta ? subMulti.subMeta.sublabel ?? subMulti.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ScrollView {
                    anchors.fill: parent
                    clip: true

                    TextArea {
                        id: subMultiText
                        text: subMulti.subValue !== undefined ? subMulti.subValue : ""
                        wrapMode: TextArea.Wrap
                        horizontalAlignment: {
                            if (!root.model || !root.model.elementProperty)
                                return TextInput.AlignLeft
                            let a = root.model.elementProperty("align")
                            if (a === undefined || a === null)
                                return TextInput.AlignLeft
                            a = Number(a)
                            if (a === 2)  return TextInput.AlignRight
                            if (a === 4)  return TextInput.AlignHCenter
                            if (a === 8)  return TextInput.AlignJustify
                            return TextInput.AlignLeft
                        }
                        onActiveFocusChanged: {
                            if (!activeFocus && subMultiText._userEdited) {
                                subMulti.setSub(text)
                                subMultiText._userEdited = false
                            }
                        }
                        onTextChanged: {
                            if (activeFocus)
                                subMultiText._userEdited = true
                        }
                        property bool _userEdited: false
                        color: "#ffffff"
                        background: Item {}
                        padding: 2
                    }
                }
            }
        }

        // ── subLaserLayer: ComboBox for LaserLayer selection in row entries ─
        Component {
            id: subLaserLayerDelegate

            ValueBox {
                id: subLaserLayer
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelText: subLaserLayer.subMeta ? subLaserLayer.subMeta.sublabel ?? subLaserLayer.subMeta.label ?? "" : ""
                subLabelAlignRight: true

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subLaserLayerCombo
                    anchors.fill: parent

                    property var baseNames: root.model.laserLayerNames ? root.model.laserLayerNames() : []
                    model: {
                        var list = ["(inherited)"]
                        for (var i = 0; i < subLaserLayerCombo.baseNames.length; ++i)
                            list.push(subLaserLayerCombo.baseNames[i])
                        return list
                        }

                    property string resolvedName: {
                        if (subLaserLayer.subValue === undefined)
                            return ""
                        if (root.model.laserLayerToName)
                            return root.model.laserLayerToName(subLaserLayer.subValue)
                        return ""
                        }
                    property string currentName: resolvedName.length > 0 ? resolvedName : "(inherited)"

                    currentIndex: {
                        let idx = subLaserLayerCombo.find(subLaserLayerCombo.currentName)
                        return idx >= 0 ? idx : 0
                        }

                    onActivated: index => {
                        if (index === 0)
                            subLaserLayer.setSub(null)
                        else {
                            let name = subLaserLayerCombo.model[index]
                            let ptr = root.model.nameToLaserLayer ? root.model.nameToLaserLayer(name) : null
                            subLaserLayer.setSub(ptr)
                            }
                        }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subLaserLayerCombo.currentName
                        font.bold: true
                        color: subLaserLayerCombo.currentName === "(inherited)" ? "#888888" : "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }
                }
            }

        // ── subRecipe: ComboBox for Recipe selection in row entries ──
        Component {
            id: subRecipeDelegate

            ValueBox {
                id: subRecipe
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subRecipe.subMeta ? subRecipe.subMeta.sublabel ?? subRecipe.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subRecipeCombo
                    parent: subRecipe
                    anchors.left: parent.left
                    anchors.right: subRecipeEditBtn.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 2
                    model: root.model.recipeNames ? root.model.recipeNames() : []

                    property string currentName: {
                        if (subRecipe.subValue === undefined || subRecipe.subValue === null)
                            return ""
                        return root.model.recipeToName ? root.model.recipeToName(subRecipe.subValue) : ""
                        }

                    currentIndex: {
                        let idx = subRecipeCombo.find(subRecipeCombo.currentName)
                        return idx >= 0 ? idx : -1
                        }

                    onActivated: index => {
                        let name = subRecipeCombo.model[index]
                        let ptr = root.model.nameToRecipe ? root.model.nameToRecipe(name) : null
                        subRecipe.setSub(ptr)
                        }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subRecipeCombo.currentName
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }

                // Edit button — opens the Recipe editor for the selected recipe
                ToolButton {
                    id: subRecipeEditBtn
                    parent: subRecipe
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 28
                    enabled: subRecipeCombo.currentName !== ""
                    text: "✎"
                    font.pixelSize: 14
                    onClicked: ZCam.openRecipeEditor(subRecipeCombo.currentName)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Edit recipe")
                    background: Rectangle {
                        color: subRecipeEditBtn.hovered ? Material.color(Material.Teal, Material.Shade700)
                               : (subRecipeEditBtn.enabled ? "#3a3a3a" : "transparent")
                        radius: 3
                        }
                    }
                }
            }

        // ── subColor: color swatch + hex input for row entries ──────────
        Component {
            id: subColorDelegate

            ValueBox {
                id: subColor
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelText: subColor.subMeta ? subColor.subMeta.sublabel ?? subColor.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                function toColor(v) {
                    if (v === undefined || v === null)
                        return Qt.rgba(0, 0, 0, 1)
                    if (typeof v === "string")
                        return Qt.color(v)
                    return v
                    }

                function toHex(c) {
                    if (!c)
                        return "#000000"
                    let r = Math.round(c.r * 255).toString(16).padStart(2, '0')
                    let g = Math.round(c.g * 255).toString(16).padStart(2, '0')
                    let b = Math.round(c.b * 255).toString(16).padStart(2, '0')
                    return "#" + r + g + b
                    }

                RowLayout {
                    anchors.fill: parent
                    spacing: 2

                    Rectangle {
                        id: subColorSwatch
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                        radius: 3
                        color: subColor.toColor(subColor.subValue)
                        border.width: 1
                        border.color: Material.accentColor

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                subColorDialog.selectedColor = subColor.toColor(subColor.subValue)
                                subColorDialog.open()
                            }
                        }

                        ColorDialog {
                            id: subColorDialog
                            options: ColorDialog.DontUseNativeDialog
                            onAccepted: {
                                subColor.setSub(selectedColor)
                            }
                        }
                    }

                    TextInput {
                        id: subHexInput
                        Layout.fillWidth: true
                        text: subColor.toHex(subColor.toColor(subColor.subValue))
                        font.family: "monospace"
                        font.bold: true
                        onEditingFinished: {
                            let c = subColor.toColor(text)
                            if (c)
                                subColor.setSub(c)
                        }
                        horizontalAlignment: TextInput.AlignRight
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#ffffff"
                        clip: true

                        Connections {
                            target: subColor
                            function onSubValueChanged() {
                                let c = subColor.toColor(subColor.subValue)
                                let h = subColor.toHex(c)
                                if (subHexInput.text.toLowerCase() !== h.toLowerCase())
                                    subHexInput.text = h
                            }
                        }
                    }
                }
            }
        }

        Component {
            id: subStringDelegate

            ValueBox {
                id: subString
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelText: subString.subMeta ? subString.subMeta.sublabel ?? subString.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                TextInput {
                    anchors.fill: parent
                    text: subString.subValue !== undefined ? subString.subValue : ""
                    onEditingFinished: if (subString.setSub) subString.setSub(text)
                    horizontalAlignment: TextInput.AlignRight
                    verticalAlignment: TextInput.AlignVCenter
                    color: "#ffffff"
                    clip: true
                    }
                }
            }

        Component {
            id: subHalignDelegate

            ValueBox {
                id: subHalign
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subHalign.subMeta ? subHalign.subMeta.sublabel ?? subHalign.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                readonly property var alignMap: [
                    { value: Qt.AlignLeft,    text: "Left"    },
                    { value: Qt.AlignRight,   text: "Right"   },
                    { value: Qt.AlignHCenter, text: "HCenter" },
                    { value: Qt.AlignJustify, text: "Justify" }
                    ]

                function indexOfValue(val) {
                    for (let i = 0; i < alignMap.length; ++i) {
                        if (alignMap[i].value === val)
                            return i;
                        }
                    return 0;
                    }

                ComboBox {
                    id: subAlignCombo
                    anchors.fill: parent
                    model: subHalign.alignMap
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: subHalign.indexOfValue(subHalign.subValue)
                    onActivated: index => {
                        subHalign.setSub(subHalign.alignMap[index].value);
                        }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subAlignCombo.displayText
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }
                }
            }

        // ── Sub-delegate for machine (Machine* pointer) in row entries ──
        Component {
            id: subMachineDelegate

            ValueBox {
                id: subMachine
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subMachine.subMeta ? subMachine.subMeta.sublabel ?? subMachine.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subMachineCombo
                    anchors.fill: parent
                    model: root.model.machineNames ? root.model.machineNames() : []

                    property string currentName: {
                        if (subMachine.subValue === undefined || subMachine.subValue === null)
                            return ""
                        return root.model.machineToName ? root.model.machineToName(subMachine.subValue) : ""
                        }

                    currentIndex: {
                        let idx = subMachineCombo.find(subMachineCombo.currentName)
                        return idx >= 0 ? idx : -1
                    }

                    onActivated: index => {
                        let name = subMachineCombo.model[index]
                        let ptr = root.model.nameToMachine ? root.model.nameToMachine(name) : null
                        subMachine.setSub(ptr)
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subMachineCombo.currentName
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }

        // ── Sub-delegate for machineName (string) in row entries ─────────
        Component {
            id: subMachineNameDelegate

            ValueBox {
                id: subMachineName
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subMachineName.subMeta ? subMachineName.subMeta.sublabel ?? subMachineName.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subMachineNameCombo
                    anchors.fill: parent
                    model: root.model.machineNames ? root.model.machineNames() : []

                    property string currentName: subMachineName.subValue !== undefined ? subMachineName.subValue : ""
                    currentIndex: {
                        let idx = subMachineNameCombo.find(subMachineNameCombo.currentName)
                        return idx >= 0 ? idx : -1
                    }

                    onActivated: index => {
                        subMachineName.setSub(subMachineNameCombo.model[index])
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subMachineNameCombo.currentName
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }

        // ── Sub-delegate for machineType in row entries ─────────────────
        Component {
            id: subMachineTypeDelegate

            ValueBox {
                id: subMachineType
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subMachineType.subMeta ? subMachineType.subMeta.sublabel ?? subMachineType.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subTypeCombo
                    anchors.fill: parent
                    model: root.model.machineTypes ? root.model.machineTypes() : []

                    property string currentName: subMachineType.subValue !== undefined ? subMachineType.subValue : ""
                    currentIndex: {
                        let idx = subTypeCombo.find(subTypeCombo.currentName)
                        return idx >= 0 ? idx : -1
                    }

                    onActivated: index => {
                        subMachineType.setSub(subTypeCombo.model[index])
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subTypeCombo.currentName
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }

        // ── Sub-delegate for boardType in row entries ─────────────────
        Component {
            id: subBoardTypeDelegate

            ValueBox {
                id: subBoardType
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subBoardType.subMeta ? subBoardType.subMeta.sublabel ?? subBoardType.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subBoardTypeCombo
                    anchors.fill: parent
                    model: root.model.boardTypes ? root.model.boardTypes() : []

                    property string currentName: subBoardType.subValue !== undefined ? subBoardType.subValue : ""
                    currentIndex: {
                        let idx = subBoardTypeCombo.find(subBoardTypeCombo.currentName)
                        return idx >= 0 ? idx : -1
                    }

                    onActivated: index => {
                        subBoardType.setSub(subBoardTypeCombo.model[index])
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subBoardTypeCombo.currentName
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }

        // ── Sub-delegate for override type in row entries ───────────────────
        Component {
            id: subOverrideDelegate

            ValueBox {
                id: subOverride
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subOverride.subMeta ? subOverride.subMeta.sublabel ?? subOverride.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subOverrideCombo
                    anchors.fill: parent
                    model: root.model.overrideTypeNames ? root.model.overrideTypeNames() : []

                    property int modelValue: subOverride.subValue !== undefined ? Number(subOverride.subValue) : 0
                    currentIndex: {
                        if (subOverrideCombo.modelValue >= 0 && subOverrideCombo.modelValue < subOverrideCombo.model.length)
                            return subOverrideCombo.modelValue
                        return 0
                    }

                    onActivated: index => {
                        subOverride.setSub(index)
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subOverrideCombo.currentText
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }

        // ── Sub-delegate for pulsewidth type in row entries ───────────────
        Component {
            id: subPulsewidthDelegate

            ValueBox {
                id: subPulsewidth
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                unitText: subPulsewidth.subMeta ? subPulsewidth.subMeta.unit ?? "" : ""
                subLabelAlignRight: true
                subLabelText: subPulsewidth.subMeta ? subPulsewidth.subMeta.sublabel ?? subPulsewidth.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                function freqModel() {
                    if (root.model.pulsewidthNames)
                        return root.model.pulsewidthNames()
                    if (ZCam.project?.machine?.laserPulseList)
                        return ZCam.project.machine.laserPulseList()
                    return []
                    }

                ComboBox {
                    id: subFreqCombo
                    anchors.fill: parent
                    model: subPulsewidth.freqModel()

                    property string freqValue: subPulsewidth.subValue !== undefined ? String(Math.round(Number(subPulsewidth.subValue))) : ""
                    currentIndex: {
                        let idx = subFreqCombo.find(subFreqCombo.freqValue)
                        return idx >= 0 ? idx : -1
                        }

                    onActivated: index => {
                        subPulsewidth.setSub(Number(subFreqCombo.model[index]))
                        }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subFreqCombo.freqValue.length > 0 ? subFreqCombo.freqValue : ""
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }
                }
            }

        // ── Sub-delegate for lineJoin type in row entries ────────────────
        Component {
            id: subLineJoinDelegate

            ValueBox {
                id: subLineJoin
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subLineJoin.subMeta ? subLineJoin.subMeta.sublabel ?? subLineJoin.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subLineJoinCombo
                    anchors.fill: parent
                    model: root.model.joinTypeNames ? root.model.joinTypeNames() : []

                    property int modelValue: subLineJoin.subValue !== undefined ? Number(subLineJoin.subValue) : 0
                    currentIndex: {
                        if (subLineJoinCombo.modelValue >= 0 && subLineJoinCombo.modelValue < subLineJoinCombo.model.length)
                            return subLineJoinCombo.modelValue
                        return 0
                    }

                    onActivated: index => {
                        subLineJoin.setSub(index)
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subLineJoinCombo.currentText
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }

        // ── Sub-delegate for lineEnd type in row entries ─────────────────
        Component {
            id: subLineEndDelegate

            ValueBox {
                id: subLineEnd
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subLineEnd.subMeta ? subLineEnd.subMeta.sublabel ?? subLineEnd.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subLineEndCombo
                    anchors.fill: parent
                    model: root.model.endTypeNames ? root.model.endTypeNames() : []

                    property int modelValue: subLineEnd.subValue !== undefined ? Number(subLineEnd.subValue) : 0
                    currentIndex: {
                        if (subLineEndCombo.modelValue >= 0 && subLineEndCombo.modelValue < subLineEndCombo.model.length)
                            return subLineEndCombo.modelValue
                        return 0
                    }

                    onActivated: index => {
                        subLineEnd.setSub(index)
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subLineEndCombo.currentText
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }

        // ── bool: CheckBox ────────────────────────────────────────────────
        Component {
            id: boolDelegate

            RowLayout {
                id: rowBool
                width: parent ? parent.width : 0

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowBool.meta ? rowBool.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    unitText: rowBool.meta ? rowBool.meta.unit ?? "" : ""

                    CheckBox {
                        anchors.centerIn: parent
                        checked: rowBool.propValue === true
                        onToggled: rowBool.setModelValue(checked)
                        }
                    }
                }
            }

        // ── int: SpinBox ──────────────────────────────────────────────────
        Component {
            id: intDelegate

            RowLayout {
                id: rowInt
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowInt.meta ? rowInt.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    id: intVBox
                    Layout.fillWidth: true
                    unitText: rowInt.meta ? rowInt.meta.unit ?? "" : ""

                    BareSpinBox {
                        id: intSpinBox
                        anchors.fill: parent
                        from: rowInt.meta && rowInt.meta.min !== undefined ? Math.round(rowInt.meta.min) : -1000000
                        to: rowInt.meta && rowInt.meta.max !== undefined ? Math.round(rowInt.meta.max) : 1000000
                        resetValue: root.defaultScalar(rowInt.propName, 0)

                        property int modelValue: rowInt.propValue !== undefined ? Number(rowInt.propValue) : 0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowInt.propValue !== value)
                                rowInt.setModelValue(value);
                            }

                        }
                    }
                }
            }

        // ── float: DoubleSpinBox ──────────────────────────────────────────
        Component {
            id: floatDelegate

            RowLayout {
                id: rowFloat
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta: root.metaFor(propName)
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowFloat.meta ? rowFloat.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    id: floatVBox
                    Layout.fillWidth: true
                    unitText: rowFloat.meta ? rowFloat.meta.unit ?? "" : ""

                    BareDoubleSpinBox {
                        id: floatSpinBox
                        anchors.fill: parent
                        from: rowFloat.meta && rowFloat.meta.min !== undefined ? rowFloat.meta.min : -1000000.0
                        to: rowFloat.meta && rowFloat.meta.max !== undefined ? rowFloat.meta.max : 1000000.0
                        stepSize: root.defaultStepSize(rowFloat.meta)
                        bigStep: root.defaultBigStep(rowFloat.meta)
                        minStep: root.defaultMinStep(rowFloat.meta)
                        resetValue: root.defaultScalar(rowFloat.propName, 0)

                        decimals: rowFloat.meta && rowFloat.meta.precision !== undefined ? rowFloat.meta.precision : 2

                        property real modelValue: rowFloat.propValue !== undefined ? Number(rowFloat.propValue) : 0.0
                        value: modelValue
                        onModelValueChanged: {
                            if (value !== modelValue) value = modelValue
                        }

                        onValueChanged: {
                            if (rowFloat.propValue !== value)
                                rowFloat.setModelValue(value);
                            }

                        }
                    }
                }
            }

        // ── vector3d: three DoubleSpinBox in ValueBoxes ────────────────────
        Component {
            id: vector3dDelegate

            RowLayout {
                id: rowVec3
                width: parent ? parent.width : 0
                spacing: 4

                property string propName
                property var propValue
                property var meta: root.metaFor(propName)
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowVec3.meta ? rowVec3.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    id: vec3xVBox
                    Layout.fillWidth: true
                    unitText: rowVec3.meta ? rowVec3.meta.unit ?? "" : ""
                    subLabelText: "X"

                    BareDoubleSpinBox {
                        id: vec3xSpinBox
                        anchors.fill: parent
                        from: rowVec3.meta && rowVec3.meta.min !== undefined ? rowVec3.meta.min : -1000000.0
                        to: rowVec3.meta && rowVec3.meta.max !== undefined ? rowVec3.meta.max : 1000000.0
                        stepSize: root.defaultStepSize(rowVec3.meta)
                        bigStep: root.defaultBigStep(rowVec3.meta)
                        minStep: root.defaultMinStep(rowVec3.meta)
                        resetValue: root.defaultScalar(rowVec3.propName, 0)

                        decimals: rowVec3.meta && rowVec3.meta.precision !== undefined ? rowVec3.meta.precision : 2

                        property real modelValue: rowVec3.propValue !== undefined && rowVec3.propValue.x !== undefined ? Number(rowVec3.propValue.x) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowVec3.propValue !== undefined && rowVec3.propValue.x !== value) {
                                let v = rowVec3.propValue;
                                rowVec3.setModelValue(Qt.vector3d(value, v.y, v.z));
                                }
                            }

                        }
                    }

                ValueBox {
                    id: vec3yVBox
                    Layout.fillWidth: true
                    unitText: rowVec3.meta ? rowVec3.meta.unit ?? "" : ""
                    subLabelText: "Y"

                    BareDoubleSpinBox {
                        id: vec3ySpinBox
                        anchors.fill: parent
                        from: rowVec3.meta && rowVec3.meta.min !== undefined ? rowVec3.meta.min : -1000000.0
                        to: rowVec3.meta && rowVec3.meta.max !== undefined ? rowVec3.meta.max : 1000000.0
                        stepSize: root.defaultStepSize(rowVec3.meta)
                        bigStep: root.defaultBigStep(rowVec3.meta)
                        minStep: root.defaultMinStep(rowVec3.meta)
                        resetValue: root.defaultScalar(rowVec3.propName, 1)

                        decimals: rowVec3.meta && rowVec3.meta.precision !== undefined ? rowVec3.meta.precision : 2

                        property real modelValue: rowVec3.propValue !== undefined && rowVec3.propValue.y !== undefined ? Number(rowVec3.propValue.y) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowVec3.propValue !== undefined && rowVec3.propValue.y !== value) {
                                let v = rowVec3.propValue;
                                rowVec3.setModelValue(Qt.vector3d(v.x, value, v.z));
                                }
                            }

                        }
                    }

                ValueBox {
                    id: vec3zVBox
                    Layout.fillWidth: true
                    unitText: rowVec3.meta ? rowVec3.meta.unit ?? "" : ""
                    subLabelText: "Z"

                    BareDoubleSpinBox {
                        id: vec3zSpinBox
                        anchors.fill: parent
                        from: rowVec3.meta && rowVec3.meta.min !== undefined ? rowVec3.meta.min : -1000000.0
                        to: rowVec3.meta && rowVec3.meta.max !== undefined ? rowVec3.meta.max : 1000000.0
                        stepSize: root.defaultStepSize(rowVec3.meta)
                        bigStep: root.defaultBigStep(rowVec3.meta)
                        minStep: root.defaultMinStep(rowVec3.meta)
                        resetValue: root.defaultScalar(rowVec3.propName, 2)

                        decimals: rowVec3.meta && rowVec3.meta.precision !== undefined ? rowVec3.meta.precision : 2

                        property real modelValue: rowVec3.propValue !== undefined && rowVec3.propValue.z !== undefined ? Number(rowVec3.propValue.z) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowVec3.propValue !== undefined && rowVec3.propValue.z !== value) {
                                let v = rowVec3.propValue;
                                rowVec3.setModelValue(Qt.vector3d(v.x, v.y, value));
                                }
                            }

                        }
                    }
                }
            }

        // ── vector2d: two DoubleSpinBox in ValueBoxes ────────────────────
        Component {
            id: vector2dDelegate

            RowLayout {
                id: rowVec2
                width: parent ? parent.width : 0
                spacing: 4

                property string propName
                property var propValue
                property var meta: root.metaFor(propName)
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowVec2.meta ? rowVec2.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    id: vec2xVBox
                    Layout.fillWidth: true
                    unitText: rowVec2.meta ? rowVec2.meta.unit ?? "" : ""
                    subLabelText: "X"

                    BareDoubleSpinBox {
                        id: vec2xSpinBox
                        anchors.fill: parent
                        from: rowVec2.meta && rowVec2.meta.min !== undefined ? rowVec2.meta.min : -1000000.0
                        to: rowVec2.meta && rowVec2.meta.max !== undefined ? rowVec2.meta.max : 1000000.0
                        stepSize: root.defaultStepSize(rowVec2.meta)
                        bigStep: root.defaultBigStep(rowVec2.meta)
                        minStep: root.defaultMinStep(rowVec2.meta)
                        resetValue: root.defaultScalar(rowVec2.propName, 0)

                        decimals: rowVec2.meta && rowVec2.meta.precision !== undefined ? rowVec2.meta.precision : 2

                        property real modelValue: rowVec2.propValue !== undefined && rowVec2.propValue.x !== undefined ? Number(rowVec2.propValue.x) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowVec2.propValue !== undefined && rowVec2.propValue.x !== value) {
                                let v = rowVec2.propValue;
                                rowVec2.setModelValue(Qt.vector2d(value, v.y));
                                }
                            }

                        }
                    }

                ValueBox {
                    id: vec2yVBox
                    Layout.fillWidth: true
                    unitText: rowVec2.meta ? rowVec2.meta.unit ?? "" : ""
                    subLabelText: "Y"

                    BareDoubleSpinBox {
                        id: vec2ySpinBox
                        anchors.fill: parent
                        from: rowVec2.meta && rowVec2.meta.min !== undefined ? rowVec2.meta.min : -1000000.0
                        to: rowVec2.meta && rowVec2.meta.max !== undefined ? rowVec2.meta.max : 1000000.0
                        stepSize: root.defaultStepSize(rowVec2.meta)
                        bigStep: root.defaultBigStep(rowVec2.meta)
                        minStep: root.defaultMinStep(rowVec2.meta)
                        resetValue: root.defaultScalar(rowVec2.propName, 1)

                        decimals: rowVec2.meta && rowVec2.meta.precision !== undefined ? rowVec2.meta.precision : 2

                        property real modelValue: rowVec2.propValue !== undefined && rowVec2.propValue.y !== undefined ? Number(rowVec2.propValue.y) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowVec2.propValue !== undefined && rowVec2.propValue.y !== value) {
                                let v = rowVec2.propValue;
                                rowVec2.setModelValue(Qt.vector2d(v.x, value));
                                }
                            }

                        }
                    }
                }
            }

        // ── font: FontFamilyButton ────────────────────────────────────────
        Component {
            id: fontDelegate

            RowLayout {
                id: rowFont
                width: parent ? parent.width : 0

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowFont.meta ? rowFont.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true

                    FontFamilyButton {
                        anchors.fill: parent
                        family: rowFont.propValue !== undefined ? rowFont.propValue : ""
                        onFamilySelected: fam => {
                            rowFont.setModelValue(fam);
                            }
                        }
                    }
                }
            }

        // ── halign: ComboBox for horizontal alignment ──────────────────────
        Component {
            id: halignDelegate

            RowLayout {
                id: rowHalign
                width: parent ? parent.width : 0

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                readonly property var alignMap: [
                    { value: Qt.AlignLeft,     text: "Left"     },
                    { value: Qt.AlignRight,    text: "Right"    },
                    { value: Qt.AlignHCenter,  text: "HCenter"  },
                    { value: Qt.AlignJustify,  text: "Justify"  }
                    ]

                function indexOfValue(val) {
                    for (let i = 0; i < alignMap.length; ++i) {
                        if (alignMap[i].value === val)
                            return i;
                        }
                    return 0;
                    }

                Label {
                    text: rowHalign.meta ? rowHalign.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true

                    ComboBox {
                        id: alignCombo
                        anchors.fill: parent
                        model: rowHalign.alignMap
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: rowHalign.indexOfValue(rowHalign.propValue)
                        onActivated: index => {
                            rowHalign.setModelValue(rowHalign.alignMap[index].value);
                            }

                        background: Item {}
                        padding: 2
                        contentItem: Text {
                            text: alignCombo.displayText
                            font.bold: true
                            color: "#ffffff"
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            }
                        indicator: Item {}
                        }
                    }
                }
            }

        // ── color: ColorDialog swatch + hex input ─────────────────────────
        Component {
            id: colorDelegate

            RowLayout {
                id: rowColor
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                function toColor(v) {
                    if (v === undefined || v === null)
                        return Qt.rgba(0, 0, 0, 1)
                    if (typeof v === "string")
                        return Qt.color(v)
                    return v
                    }

                function toHex(c) {
                    if (!c)
                        return "#000000"
                    let r = Math.round(c.r * 255).toString(16).padStart(2, '0')
                    let g = Math.round(c.g * 255).toString(16).padStart(2, '0')
                    let b = Math.round(c.b * 255).toString(16).padStart(2, '0')
                    return "#" + r + g + b
                    }

                Label {
                    text: rowColor.meta ? rowColor.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                Rectangle {
                    id: swatch
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 28
                    radius: 4
                    color: rowColor.toColor(rowColor.propValue)
                    border.width: 1
                    border.color: Material.accentColor

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            colorDialog.selectedColor = rowColor.toColor(rowColor.propValue)
                            colorDialog.open()
                        }
                    }

                    ColorDialog {
                        id: colorDialog
                        options: ColorDialog.DontUseNativeDialog
                        onAccepted: {
                            rowColor.setModelValue(selectedColor)
                        }
                    }
                }

                ValueBox {
                    Layout.fillWidth: true

                    TextInput {
                        id: hexInput
                        anchors.fill: parent
                        text: rowColor.toHex(rowColor.toColor(rowColor.propValue))
                        font.family: "monospace"
                        font.bold: true
                        onEditingFinished: {
                            let c = rowColor.toColor(text)
                            if (c)
                                rowColor.setModelValue(c)
                            }
                        horizontalAlignment: TextInput.AlignRight
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#ffffff"
                        clip: true

                        Connections {
                            target: rowColor
                            function onPropValueChanged() {
                                let c = rowColor.toColor(rowColor.propValue)
                                let h = rowColor.toHex(c)
                                if (hexInput.text.toLowerCase() !== h.toLowerCase())
                                    hexInput.text = h
                            }
                        }
                    }
                }
            }
            }

        // ── multiline: ScrollView + TextArea ──────────────────────────────
        Component {
            id: multilineDelegate

            RowLayout {
                id: rowMulti
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowMulti.meta ? rowMulti.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    Layout.alignment: Qt.AlignTop
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignTop
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true
                    implicitHeight: 80

                    ScrollView {
                        anchors.fill: parent
                        clip: true

                        TextArea {
                            id: multiText
                            text: rowMulti.propValue !== undefined ? rowMulti.propValue : ""
                            wrapMode: TextArea.Wrap
                            onActiveFocusChanged: {
                                if (!activeFocus && multiText._userEdited) {
                                    rowMulti.setModelValue(text)
                                    multiText._userEdited = false
                                    }
                                }
                            onTextChanged: {
                                if (activeFocus)
                                    multiText._userEdited = true
                                }
                            property bool _userEdited: false
                            color: "#ffffff"
                            background: Item {}
                            padding: 2
                            }
                        }
                    }
                }
            }

        // ── singleline: TextInput ────────────────────────────────────────
        Component {
            id: singlelineDelegate

            RowLayout {
                id: rowSingle
                width: parent ? parent.width : 0

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowSingle.meta ? rowSingle.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignTop
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true
                    unitText: rowSingle.meta ? rowSingle.meta.unit ?? "" : ""

                    TextInput {
                        anchors.fill: parent
                        text: rowSingle.propValue !== undefined ? rowSingle.propValue : ""
                        onEditingFinished: rowSingle.setModelValue(text)
                        horizontalAlignment: TextInput.AlignLeft
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#ffffff"
                        clip: true
                        }
                    }
                }
            }

        // ── path: TextInput + folder button with FolderDialog ────────────
        Component {
            id: pathDelegate

            RowLayout {
                id: rowPath
                width: parent ? parent.width : 0
                spacing: 4

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowPath.meta ? rowPath.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true

                    TextInput {
                        id: pathInput
                        anchors.fill: parent
                        text: rowPath.propValue !== undefined ? rowPath.propValue : ""
                        onEditingFinished: rowPath.setModelValue(text)
                        horizontalAlignment: TextInput.AlignLeft
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#ffffff"
                        clip: true

                        Connections {
                            target: rowPath
                            function onPropValueChanged() {
                                if (pathInput.activeFocus)
                                    return
                                let v = rowPath.propValue !== undefined ? rowPath.propValue : ""
                                if (pathInput.text !== v)
                                    pathInput.text = v
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 28
                    color: "#a9a9a9"
                    radius: 4

                    Image {
                        anchors.fill: parent
                        anchors.margins: 4
                        source: "qrc:/icons/folder-browse.svg"
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: 28
                        sourceSize.height: 28
                    }

                    MouseArea {
                        id: pathBrowseMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        ToolTip.text: qsTr("Browse...")
                        ToolTip.visible: containsMouse
                        onClicked: {
                            let cur = rowPath.propValue !== undefined ? rowPath.propValue : ""
                            if (cur.length > 0)
                                folderDialog.currentFolder = "file://" + ZCam.expandPath(cur)
                            folderDialog.open()
                        }
                    }

                    FolderDialog {
                        id: folderDialog
                        title: rowPath.meta ? rowPath.meta.label ?? qsTr("Select Directory") : qsTr("Select Directory")
                        onAccepted: {
                            let f = folderDialog.selectedFolder.toString()
                            if (f.startsWith("file://"))
                                f = f.substring(7)
                            pathInput.text = f
                            rowPath.setModelValue(f)
                        }
                    }
                }
            }
        }

        // ── string: generic single-line TextInput ─────────────────────────
        Component {
            id: stringDelegate

            RowLayout {
                id: rowString
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowString.meta ? rowString.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignTop
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true
                    unitText: rowString.meta ? rowString.meta.unit ?? "" : ""

                    TextInput {
                        anchors.fill: parent
                        text: rowString.propValue !== undefined ? rowString.propValue : ""
                        onEditingFinished: rowString.setModelValue(text)
                        horizontalAlignment: TextInput.AlignLeft
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#ffffff"
                        clip: true
                        }
                    }
                }
            }

        // ── layer: ComboBox for Layer selection ────────────────────────────
        Component {
            id: layerDelegate

            RowLayout {
                id: rowLayer
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowLayer.meta ? rowLayer.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true

                    ComboBox {
                        id: layerCombo
                        anchors.fill: parent
                        model: root.model.layerNames ? root.model.layerNames() : []

                        property string currentName: {
                            if (rowLayer.propValue === undefined || rowLayer.propValue === null)
                                return ""
                            return root.model.layerToName ? root.model.layerToName(rowLayer.propValue) : ""
                            }

                        currentIndex: {
                            let idx = layerCombo.find(layerCombo.currentName)
                            return idx >= 0 ? idx : -1
                            }

                        onActivated: index => {
                            let name = layerCombo.model[index]
                            let ptr = root.model.nameToLayer ? root.model.nameToLayer(name) : null
                            rowLayer.setModelValue(ptr)
                            }

                        background: Item {}
                        padding: 2
                        contentItem: Text {
                            text: layerCombo.currentName
                            font.bold: true
                            color: "#ffffff"
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            }
                        indicator: Item {}
                        }
                    }
                }
            }


        // ── laserLayer: ComboBox for LaserLayer selection ──────────────────
        //    Shows "(inherited)" when the property is null (i.e. the
        //    element inherits the LaserLayer from its parent).
        Component {
            id: laserLayerDelegate

            RowLayout {
                id: rowLaserLayer
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowLaserLayer.meta ? rowLaserLayer.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true

                    ComboBox {
                        id: laserLayerCombo
                        anchors.fill: parent

                        // Build model: prepend "(inherited)" so the user can
                        // set the property back to null.
                        property var baseNames: root.model.laserLayerNames ? root.model.laserLayerNames() : []
                        model: {
                            var list = ["(inherited)"]
                            for (var i = 0; i < laserLayerCombo.baseNames.length; ++i)
                                list.push(laserLayerCombo.baseNames[i])
                            return list
                            }

                        // Resolve the pointer to a name.  A null pointer
                        // (QVariant(LaserLayer*, 0x0)) yields an empty string
                        // from laserLayerToName(), which we map to "(inherited)".
                        property string resolvedName: {
                            if (rowLaserLayer.propValue === undefined)
                                return ""
                            if (root.model.laserLayerToName)
                                return root.model.laserLayerToName(rowLaserLayer.propValue)
                            return ""
                            }
                        property string currentName: resolvedName.length > 0 ? resolvedName : "(inherited)"

                        currentIndex: {
                            let idx = laserLayerCombo.find(laserLayerCombo.currentName)
                            return idx >= 0 ? idx : 0
                            }

                        onActivated: index => {
                            if (index === 0) {
                                // Setting to null: pass a null LaserLayer* so
                                // the PROPV setter accepts it (QVariant(nullptr)
                                // maps to a null pointer for Q_DECLARE_OPAQUE_POINTER types).
                                rowLaserLayer.setModelValue(null)
                                }
                            else {
                                let name = laserLayerCombo.model[index]
                                let ptr = root.model.nameToLaserLayer ? root.model.nameToLaserLayer(name) : null
                                rowLaserLayer.setModelValue(ptr)
                                }
                            }

                        background: Item {}
                        padding: 2
                        contentItem: Text {
                            text: laserLayerCombo.currentName
                            font.bold: true
                            color: laserLayerCombo.currentName === "(inherited)" ? "#888888" : "#ffffff"
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            }
                        indicator: Item {}
                        }
                    }
                }
            }
        // ── recipe: ComboBox for Recipe selection ───────────────────────────
        Component {
            id: recipeDelegate

            RowLayout {
                id: rowRecipe
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowRecipe.meta ? rowRecipe.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    id: recipeVBox
                    Layout.fillWidth: true

                    ComboBox {
                        id: recipeCombo
                        parent: recipeVBox
                        anchors.left: parent.left
                        anchors.right: recipeEditBtn.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: 2
                        model: root.model.recipeNames ? root.model.recipeNames() : []

                        property string currentName: {
                            if (rowRecipe.propValue === undefined || rowRecipe.propValue === null)
                                return ""
                            return root.model.recipeToName ? root.model.recipeToName(rowRecipe.propValue) : ""
                            }

                        currentIndex: {
                            let idx = recipeCombo.find(recipeCombo.currentName)
                            return idx >= 0 ? idx : -1
                            }

                        onActivated: index => {
                            let name = recipeCombo.model[index]
                            let ptr = root.model.nameToRecipe ? root.model.nameToRecipe(name) : null
                            rowRecipe.setModelValue(ptr)
                            }

                        background: Item {}
                        padding: 2
                        contentItem: Text {
                            text: recipeCombo.currentName
                            font.bold: true
                            color: "#ffffff"
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            }
                        indicator: Item {}
                        }
                    }

                    // Edit button — opens the Recipe editor for the selected recipe
                    ToolButton {
                        id: recipeEditBtn
                        parent: recipeVBox
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 28
                        enabled: recipeCombo.currentName !== ""
                        text: "✎"
                        font.pixelSize: 14
                        onClicked: ZCam.openRecipeEditor(recipeCombo.currentName)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Edit recipe")
                        background: Rectangle {
                            color: recipeEditBtn.hovered ? Material.color(Material.Teal, Material.Shade700)
                                   : (recipeEditBtn.enabled ? "#3a3a3a" : "transparent")
                            radius: 3
                            }
                        }
                }
            }

        // ── machineType: ComboBox for machine type selection ───────────────
        Component {
            id: machineTypeDelegate

            RowLayout {
                id: rowMachineType
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowMachineType.meta ? rowMachineType.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true

                    ComboBox {
                        id: typeCombo
                        anchors.fill: parent
                        model: root.model.machineTypes ? root.model.machineTypes() : []

                        property string currentName: rowMachineType.propValue !== undefined ? rowMachineType.propValue : ""
                        currentIndex: {
                            let idx = typeCombo.find(typeCombo.currentName)
                            return idx >= 0 ? idx : -1
                        }

                        onActivated: index => {
                            rowMachineType.setModelValue(typeCombo.model[index])
                        }

                        background: Item {}
                        padding: 2
                        contentItem: Text {
                            text: typeCombo.currentName
                            font.bold: true
                            color: "#ffffff"
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        indicator: Item {}
                    }
                }
            }
        }

        // ── boardType: ComboBox for board type selection ────────────────
        Component {
            id: boardTypeDelegate

            RowLayout {
                id: rowBoardType
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowBoardType.meta ? rowBoardType.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true

                    ComboBox {
                        id: boardTypeCombo
                        anchors.fill: parent
                        model: root.model.boardTypes ? root.model.boardTypes() : []

                        property string currentName: rowBoardType.propValue !== undefined ? rowBoardType.propValue : ""
                        currentIndex: {
                            let idx = boardTypeCombo.find(boardTypeCombo.currentName)
                            return idx >= 0 ? idx : -1
                        }

                        onActivated: index => {
                            rowBoardType.setModelValue(boardTypeCombo.model[index])
                        }

                        background: Item {}
                        padding: 2
                        contentItem: Text {
                            text: boardTypeCombo.currentName
                            font.bold: true
                            color: "#ffffff"
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        indicator: Item {}
                    }
                }
            }
        }

        // ── ethDevice: ComboBox for Ethernet device selection ────────────
        Component {
            id: ethDeviceDelegate

            RowLayout {
                id: rowEthDevice
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowEthDevice.meta ? rowEthDevice.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true

                    ComboBox {
                        id: ethDeviceCombo
                        anchors.fill: parent
                        model: root.model.ethDevices ? root.model.ethDevices() : []

                        property string currentName: rowEthDevice.propValue !== undefined ? rowEthDevice.propValue : ""
                        currentIndex: {
                            let idx = ethDeviceCombo.find(ethDeviceCombo.currentName)
                            return idx >= 0 ? idx : -1
                        }

                        onActivated: index => {
                            rowEthDevice.setModelValue(ethDeviceCombo.model[index])
                        }

                        background: Item {}
                        padding: 2
                        contentItem: Text {
                            text: ethDeviceCombo.currentName
                            font.bold: true
                            color: "#ffffff"
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        indicator: Item {}
                    }
                }
            }
        }

        // ── Sub-delegate for cameraName in row entries ─────────────────
        Component {
            id: subCameraNameDelegate

            ValueBox {
                id: subCameraName
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subCameraName.subMeta ? subCameraName.subMeta.sublabel ?? subCameraName.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subCameraNameCombo
                    anchors.fill: parent

                    // Model = available cameras with an optional "(none)"
                    // entry so the camera can be disabled.
                    property var camNames: root.model.cameraNames ? root.model.cameraNames() : []
                    model: {
                        var list = ["(none)"]
                        for (var i = 0; i < subCameraNameCombo.camNames.length; ++i)
                            list.push(subCameraNameCombo.camNames[i])
                        return list
                        }

                    property string currentName: {
                        if (subCameraName.subValue === undefined || subCameraName.subValue === null)
                            return "(none)"
                        const s = String(subCameraName.subValue)
                        return s.length > 0 ? s : "(none)"
                        }

                    currentIndex: {
                        let idx = subCameraNameCombo.find(subCameraNameCombo.currentName)
                        return idx >= 0 ? idx : 0
                        }

                    onActivated: index => {
                        if (index === 0)
                            subCameraName.setSub("")
                        else
                            subCameraName.setSub(subCameraNameCombo.model[index])
                        }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subCameraNameCombo.currentName
                        font.bold: true
                        color: subCameraNameCombo.currentName === "(none)" ? "#888888" : "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }
                }
            }

        // ── Sub-delegate for cameraResolution in row entries ─────────
        Component {
            id: subCameraResolutionDelegate

            ValueBox {
                id: subCameraResolution
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subCameraResolution.subMeta ? subCameraResolution.subMeta.sublabel ?? subCameraResolution.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subCameraResolutionCombo
                    anchors.fill: parent

                    property var camElement: root.model.element
                    model: {
                        var list = ["(default)"]
                        if (camElement && camElement.resolutionNames)
                            list = list.concat(camElement.resolutionNames())
                        return list
                    }

                    property string currentVal: subCameraResolution.subValue !== undefined ? String(subCameraResolution.subValue) : ""
                    currentIndex: {
                        if (currentVal.length === 0)
                            return 0
                        let idx = subCameraResolutionCombo.find(currentVal)
                        return idx >= 0 ? idx : 0
                    }

                    onActivated: index => {
                        if (index === 0)
                            subCameraResolution.setSub("")
                        else
                            subCameraResolution.setSub(subCameraResolutionCombo.model[index])
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subCameraResolutionCombo.currentVal.length > 0 ? subCameraResolutionCombo.currentVal : "(default)"
                        font.bold: true
                        color: subCameraResolutionCombo.currentVal.length === 0 ? "#888888" : "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }

        // ── Sub-delegate for cameraFrameRate in row entries ───────────
        Component {
            id: subCameraFrameRateDelegate

            ValueBox {
                id: subCameraFrameRate
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subCameraFrameRate.subMeta ? subCameraFrameRate.subMeta.sublabel ?? subCameraFrameRate.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subCameraFrameRateCombo
                    anchors.fill: parent

                    property var camElement: root.model.element
                    model: {
                        var list = ["(default)"]
                        if (camElement && camElement.frameRateNames)
                            list = list.concat(camElement.frameRateNames())
                        return list
                    }

                    property string currentVal: subCameraFrameRate.subValue !== undefined ? String(subCameraFrameRate.subValue) : ""
                    currentIndex: {
                        if (currentVal.length === 0)
                            return 0
                        let idx = subCameraFrameRateCombo.find(currentVal)
                        return idx >= 0 ? idx : 0
                    }

                    onActivated: index => {
                        if (index === 0)
                            subCameraFrameRate.setSub("")
                        else
                            subCameraFrameRate.setSub(subCameraFrameRateCombo.model[index])
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subCameraFrameRateCombo.currentVal.length > 0 ? subCameraFrameRateCombo.currentVal : "(default)"
                        font.bold: true
                        color: subCameraFrameRateCombo.currentVal.length === 0 ? "#888888" : "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }

        // ── subEthDevice: ComboBox for Ethernet device in row entries ───
        Component {
            id: subEthDeviceDelegate

            ValueBox {
                id: subEthDevice
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelAlignRight: true
                subLabelText: subEthDevice.subMeta ? subEthDevice.subMeta.sublabel ?? subEthDevice.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                ComboBox {
                    id: subEthDeviceCombo
                    anchors.fill: parent
                    model: root.model.ethDevices ? root.model.ethDevices() : []

                    property string currentName: subEthDevice.subValue !== undefined ? subEthDevice.subValue : ""
                    currentIndex: {
                        let idx = subEthDeviceCombo.find(subEthDeviceCombo.currentName)
                        return idx >= 0 ? idx : -1
                    }

                    onActivated: index => {
                        subEthDevice.setSub(subEthDeviceCombo.model[index])
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: subEthDeviceCombo.currentName
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }

        // ── machine: ComboBox for Machine selection ───────────────────────
        Component {
            id: machineDelegate

            RowLayout {
                id: rowMachine
                width: parent ? parent.width : 0
                spacing: 6

                property string propName
                property var propValue
                property var meta
                property int propIndex
                property var setModelValue: function(v) {}

                Label {
                    text: rowMachine.meta ? rowMachine.meta.label ?? "" : ""
                    Layout.preferredWidth: root.labelWidth
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: Material.foreground
                    opacity: 0.75
                    }

                ValueBox {
                    Layout.fillWidth: true

                    ComboBox {
                        id: machineCombo
                        anchors.fill: parent
                        model: root.model.machineNames ? root.model.machineNames() : []

                        property string currentName: {
                            if (rowMachine.propValue === undefined || rowMachine.propValue === null)
                                return ""
                            // For "machine" type, the value is a Machine* pointer
                            return root.model.machineToName ? root.model.machineToName(rowMachine.propValue) : ""
                            }

                        currentIndex: {
                            let idx = machineCombo.find(machineCombo.currentName)
                            return idx >= 0 ? idx : -1
                            }

                        onActivated: index => {
                            let name = machineCombo.model[index]
                            let ptr = root.model.nameToMachine ? root.model.nameToMachine(name) : null
                            rowMachine.setModelValue(ptr)
                            }

                        background: Item {}
                        padding: 2
                        contentItem: Text {
                            text: machineCombo.currentName
                            font.bold: true
                            color: "#ffffff"
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            }
                        indicator: Item {}
                        }
                    }
                }
            }
        }

    // ── machineName: ComboBox for machine name (string) selection ────
    Component {
        id: machineNameDelegate

        RowLayout {
            id: rowMachineName
            width: parent ? parent.width : 0
            spacing: 6

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            Label {
                text: rowMachineName.meta ? rowMachineName.meta.label ?? "" : ""
                Layout.preferredWidth: root.labelWidth
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                color: Material.foreground
                opacity: 0.75
                }

            ValueBox {
                Layout.fillWidth: true

                ComboBox {
                    id: machineNameCombo
                    anchors.fill: parent
                    model: root.model.machineNames ? root.model.machineNames() : []

                    property string currentName: rowMachineName.propValue !== undefined ? rowMachineName.propValue : ""
                    currentIndex: {
                        let idx = machineNameCombo.find(machineNameCombo.currentName)
                        return idx >= 0 ? idx : -1
                        }

                    onActivated: index => {
                        rowMachineName.setModelValue(machineNameCombo.model[index])
                        }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: machineNameCombo.currentName
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }
                }
            }
        }

    // ── override: ComboBox for ParameterType selection ────────────────
    Component {
        id: overrideDelegate

        RowLayout {
            id: rowOverride
            width: parent ? parent.width : 0
            spacing: 6

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            Label {
                text: rowOverride.meta ? rowOverride.meta.label ?? "" : ""
                Layout.preferredWidth: root.labelWidth
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                color: Material.foreground
                opacity: 0.75
                }

            ValueBox {
                Layout.fillWidth: true

                ComboBox {
                    id: overrideCombo
                    anchors.fill: parent
                    model: root.model.overrideTypeNames ? root.model.overrideTypeNames() : []

                    property int modelValue: rowOverride.propValue !== undefined ? Number(rowOverride.propValue) : 0
                    currentIndex: {
                        if (overrideCombo.modelValue >= 0 && overrideCombo.modelValue < overrideCombo.model.length)
                            return overrideCombo.modelValue
                        return 0
                    }

                    onActivated: index => {
                        rowOverride.setModelValue(index)
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: overrideCombo.currentText
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }
                }
            }
        }

    // ── pulsewidth: ComboBox for pulseTable frequency selection ─────────
    Component {
        id: pulsewidthDelegate

        RowLayout {
            id: rowPulsewidth
            width: parent ? parent.width : 0
            spacing: 6

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            function freqModel() {
                if (root.model.pulsewidthNames)
                    return root.model.pulsewidthNames()
                if (ZCam.project?.machine?.laserPulseList)
                    return ZCam.project.machine.laserPulseList()
                return []
                }

            Label {
                text: rowPulsewidth.meta ? rowPulsewidth.meta.label ?? "" : ""
                Layout.preferredWidth: root.labelWidth
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                color: Material.foreground
                opacity: 0.75
                }

            ValueBox {
                Layout.fillWidth: true
                unitText: rowPulsewidth.meta ? rowPulsewidth.meta.unit ?? "" : ""

                ComboBox {
                    id: freqCombo
                    anchors.fill: parent
                    model: rowPulsewidth.freqModel()

                    property string freqValue: rowPulsewidth.propValue !== undefined ? String(Math.round(Number(rowPulsewidth.propValue))) : ""
                    currentIndex: {
                        let idx = freqCombo.find(freqCombo.freqValue)
                        return idx >= 0 ? idx : -1
                        }

                    onActivated: index => {
                        rowPulsewidth.setModelValue(Number(freqCombo.model[index]))
                        }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: freqCombo.freqValue.length > 0 ? freqCombo.freqValue : ""
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }
                }
            }
        }

    // ── lineJoin: ComboBox for Clipper2Lib::JoinType selection ────────
    Component {
        id: lineJoinDelegate

        RowLayout {
            id: rowLineJoin
            width: parent ? parent.width : 0
            spacing: 6

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            Label {
                text: rowLineJoin.meta ? rowLineJoin.meta.label ?? "" : ""
                Layout.preferredWidth: root.labelWidth
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                color: Material.foreground
                opacity: 0.75
                }

            ValueBox {
                Layout.fillWidth: true

                ComboBox {
                    id: lineJoinCombo
                    anchors.fill: parent
                    model: root.model.joinTypeNames ? root.model.joinTypeNames() : []

                    property int modelValue: rowLineJoin.propValue !== undefined ? Number(rowLineJoin.propValue) : 0
                    currentIndex: {
                        if (lineJoinCombo.modelValue >= 0 && lineJoinCombo.modelValue < lineJoinCombo.model.length)
                            return lineJoinCombo.modelValue
                        return 0
                    }

                    onActivated: index => {
                        rowLineJoin.setModelValue(index)
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: lineJoinCombo.currentText
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }
                }
            }
        }

    // ── lineEnd: ComboBox for Clipper2Lib::EndType selection ──────────
    Component {
        id: lineEndDelegate

        RowLayout {
            id: rowLineEnd
            width: parent ? parent.width : 0
            spacing: 6

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            Label {
                text: rowLineEnd.meta ? rowLineEnd.meta.label ?? "" : ""
                Layout.preferredWidth: root.labelWidth
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                color: Material.foreground
                opacity: 0.75
                }

            ValueBox {
                Layout.fillWidth: true

                ComboBox {
                    id: lineEndCombo
                    anchors.fill: parent
                    model: root.model.endTypeNames ? root.model.endTypeNames() : []

                    property int modelValue: rowLineEnd.propValue !== undefined ? Number(rowLineEnd.propValue) : 0
                    currentIndex: {
                        if (lineEndCombo.modelValue >= 0 && lineEndCombo.modelValue < lineEndCombo.model.length)
                            return lineEndCombo.modelValue
                        return 0
                    }

                    onActivated: index => {
                        rowLineEnd.setModelValue(index)
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: lineEndCombo.currentText
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }
                }
            }
        }

    // ── framingType: ComboBox for FramingType selection ────────────────
    Component {
        id: framingTypeDelegate

        RowLayout {
            id: rowFramingType
            width: parent ? parent.width : 0
            spacing: 6

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            Label {
                text: rowFramingType.meta ? rowFramingType.meta.label ?? "" : ""
                Layout.preferredWidth: root.labelWidth
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                color: Material.foreground
                opacity: 0.75
                }

            ValueBox {
                Layout.fillWidth: true

                ComboBox {
                    id: framingTypeCombo
                    anchors.fill: parent
                    model: root.model.framingTypeNames ? root.model.framingTypeNames() : ["BoundingBox", "ConvexHull"]

                    property int modelValue: rowFramingType.propValue !== undefined ? Number(rowFramingType.propValue) : 1
                    currentIndex: {
                        if (framingTypeCombo.modelValue >= 0 && framingTypeCombo.modelValue < framingTypeCombo.model.length)
                            return framingTypeCombo.modelValue
                        return 1
                    }

                    onActivated: index => {
                        rowFramingType.setModelValue(index)
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: framingTypeCombo.currentText
                        font.bold: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }
                }
            }
        }

    // ── subFramingType: ComboBox for FramingType in row entries ──────
    Component {
        id: subFramingTypeDelegate

        ValueBox {
            id: subFramingType
            Layout.fillWidth: true
            width: parent ? parent.width : 0
                subLabelAlignRight: true
            subLabelText: subFramingType.subMeta ? subFramingType.subMeta.sublabel ?? subFramingType.subMeta.label ?? "" : ""

            property string subName
            property var subValue
            property var subMeta
            property var setSub: function(v) {}

            ComboBox {
                id: subFramingTypeCombo
                anchors.fill: parent
                model: root.model.framingTypeNames ? root.model.framingTypeNames() : ["BoundingBox", "ConvexHull"]

                property int modelValue: subFramingType.subValue !== undefined ? Number(subFramingType.subValue) : 1
                currentIndex: {
                    if (subFramingTypeCombo.modelValue >= 0 && subFramingTypeCombo.modelValue < subFramingTypeCombo.model.length)
                        return subFramingTypeCombo.modelValue
                    return 1
                }

                onActivated: index => {
                    subFramingType.setSub(index)
                }

                background: Item {}
                padding: 2
                contentItem: Text {
                    text: subFramingTypeCombo.currentText
                    font.bold: true
                    color: "#ffffff"
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    }
                indicator: Item {}
                }
            }
        }

// ── lockScale: three CheckBoxes (Off / Lock / Square) ──────────
    Component {
        id: lockScaleDelegate

        RowLayout {
            id: rowLockScale
            width: parent ? parent.width : 0
            spacing: 4

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            readonly property var modeNames: root.model.lockScaleNames ? root.model.lockScaleNames() : ["Off", "Lock", "Square"]

            Label {
                text: rowLockScale.meta ? rowLockScale.meta.label ?? "" : ""
                Layout.preferredWidth: root.labelWidth
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                color: Material.foreground
                opacity: 0.75
                }

            Repeater {
                model: rowLockScale.modeNames

                delegate: ValueBox {
                    required property string modelData
                    required property int index

                    Layout.fillWidth: true
                    subLabelText: modelData

                    LockCheckBox {
                        modeIndex: index
                        modeValue: rowLockScale.propValue
                        onActivated: idx => rowLockScale.setModelValue(idx)
                    }
                    }
                }
            }
        }

    // ── subLockScale: three CheckBoxes for row entries ─────────────
    Component {
        id: subLockScaleDelegate

        RowLayout {
            id: subLockScaleRow
            Layout.fillWidth: true
            spacing: 4

            property string subName
            property var subValue
            property var subMeta
            property var setSub: function(v) {}

            readonly property var modeNames: root.model.lockScaleNames ? root.model.lockScaleNames() : ["Off", "Lock", "Square"]

            Repeater {
                model: subLockScaleRow.modeNames

                delegate: ValueBox {
                    required property string modelData
                    required property int index

                    Layout.fillWidth: true
                    subLabelText: modelData

                    LockCheckBox {
                        modeIndex: index
                        modeValue: subLockScaleRow.subValue
                        onActivated: idx => subLockScaleRow.setSub(idx)
                    }
                    }
                }
            }
        }

    // ── lockSize: three CheckBoxes (Off / Lock / Square) ──────────
    //    Analogous to lockScale but for the 2D size property.
    Component {
        id: lockSizeDelegate

        RowLayout {
            id: rowLockSize
            width: parent ? parent.width : 0
            spacing: 4

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            readonly property var modeNames: root.model.lockSizeNames ? root.model.lockSizeNames() : ["Off", "Lock", "Square"]

            Label {
                text: rowLockSize.meta ? rowLockSize.meta.label ?? "" : ""
                Layout.preferredWidth: root.labelWidth
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                color: Material.foreground
                opacity: 0.75
                }

            Repeater {
                model: rowLockSize.modeNames

                delegate: ValueBox {
                    required property string modelData
                    required property int index

                    Layout.fillWidth: true
                    subLabelText: modelData

                    LockCheckBox {
                        modeIndex: index
                        modeValue: rowLockSize.propValue
                        onActivated: idx => rowLockSize.setModelValue(idx)
                    }
                    }
                }
            }
        }

    // ── subLockSize: three CheckBoxes for row entries ─────────────
    //    Analogous to subLockScale but for the 2D size property.
    Component {
        id: subLockSizeDelegate

        RowLayout {
            id: subLockSizeRow
            Layout.fillWidth: true
            spacing: 4

            property string subName
            property var subValue
            property var subMeta
            property var setSub: function(v) {}

            readonly property var modeNames: root.model.lockSizeNames ? root.model.lockSizeNames() : ["Off", "Lock", "Square"]

            Repeater {
                model: subLockSizeRow.modeNames

                delegate: ValueBox {
                    required property string modelData
                    required property int index

                    Layout.fillWidth: true
                    subLabelText: modelData

                    LockCheckBox {
                        modeIndex: index
                        modeValue: subLockSizeRow.subValue
                        onActivated: idx => subLockSizeRow.setSub(idx)
                    }
                    }
                }
            }
        }

    // ── cameraName: ComboBox listing the available video input devices ─────
    Component {
        id: cameraNameDelegate

        RowLayout {
            id: rowCameraName
            width: parent ? parent.width : 0
            spacing: 6

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            Label {
                text: rowCameraName.meta ? rowCameraName.meta.label ?? "" : ""
                Layout.preferredWidth: root.labelWidth
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                color: Material.foreground
                opacity: 0.75
                }

            ValueBox {
                Layout.fillWidth: true

                ComboBox {
                    id: cameraNameCombo
                    anchors.fill: parent

                    // Model = available cameras with an optional "(none)"
                    // entry so the camera can be disabled.
                    property var camNames: root.model.cameraNames ? root.model.cameraNames() : []
                    model: {
                        var list = ["(none)"]
                        for (var i = 0; i < cameraNameCombo.camNames.length; ++i)
                            list.push(cameraNameCombo.camNames[i])
                        return list
                        }

                    property string currentName: {
                        if (rowCameraName.propValue === undefined || rowCameraName.propValue === null)
                            return "(none)"
                        const s = String(rowCameraName.propValue)
                        return s.length > 0 ? s : "(none)"
                        }

                    currentIndex: {
                        let idx = cameraNameCombo.find(cameraNameCombo.currentName)
                        return idx >= 0 ? idx : 0
                        }

                    onActivated: index => {
                        if (index === 0)
                            rowCameraName.setModelValue("")
                        else
                            rowCameraName.setModelValue(cameraNameCombo.model[index])
                        }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: cameraNameCombo.currentName
                        font.bold: true
                        color: cameraNameCombo.currentName === "(none)" ? "#888888" : "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        }
                    indicator: Item {}
                    }
                }
            }
        }

    // ── cameraResolution: ComboBox listing available camera resolutions ──
    Component {
        id: cameraResolutionDelegate

        RowLayout {
            id: rowCameraResolution
            width: parent ? parent.width : 0
            spacing: 6

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            Label {
                text: rowCameraResolution.meta ? rowCameraResolution.meta.label ?? "" : ""
                Layout.preferredWidth: root.labelWidth
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                color: Material.foreground
                opacity: 0.75
                }

            ValueBox {
                Layout.fillWidth: true

                ComboBox {
                    id: cameraResolutionCombo
                    anchors.fill: parent

                    property var camElement: root.model.element
                    model: {
                        var list = ["(default)"]
                        if (camElement && camElement.resolutionNames)
                            list = list.concat(camElement.resolutionNames())
                        return list
                    }

                    property string currentVal: rowCameraResolution.propValue !== undefined ? String(rowCameraResolution.propValue) : ""
                    currentIndex: {
                        if (currentVal.length === 0)
                            return 0
                        let idx = cameraResolutionCombo.find(currentVal)
                        return idx >= 0 ? idx : 0
                    }

                    onActivated: index => {
                        if (index === 0)
                            rowCameraResolution.setModelValue("")
                        else
                            rowCameraResolution.setModelValue(cameraResolutionCombo.model[index])
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: cameraResolutionCombo.currentVal.length > 0 ? cameraResolutionCombo.currentVal : "(default)"
                        font.bold: true
                        color: cameraResolutionCombo.currentVal.length === 0 ? "#888888" : "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }
    }

    // ── cameraFrameRate: ComboBox listing available frame rates ────────
    Component {
        id: cameraFrameRateDelegate

        RowLayout {
            id: rowCameraFrameRate
            width: parent ? parent.width : 0
            spacing: 6

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            Label {
                text: rowCameraFrameRate.meta ? rowCameraFrameRate.meta.label ?? "" : ""
                Layout.preferredWidth: root.labelWidth
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
                color: Material.foreground
                opacity: 0.75
                }

            ValueBox {
                Layout.fillWidth: true

                ComboBox {
                    id: cameraFrameRateCombo
                    anchors.fill: parent

                    property var camElement: root.model.element
                    model: {
                        var list = ["(default)"]
                        if (camElement && camElement.frameRateNames)
                            list = list.concat(camElement.frameRateNames())
                        return list
                    }

                    property string currentVal: rowCameraFrameRate.propValue !== undefined ? String(rowCameraFrameRate.propValue) : ""
                    currentIndex: {
                        if (currentVal.length === 0)
                            return 0
                        let idx = cameraFrameRateCombo.find(currentVal)
                        return idx >= 0 ? idx : 0
                    }

                    onActivated: index => {
                        if (index === 0)
                            rowCameraFrameRate.setModelValue("")
                        else
                            rowCameraFrameRate.setModelValue(cameraFrameRateCombo.model[index])
                    }

                    background: Item {}
                    padding: 2
                    contentItem: Text {
                        text: cameraFrameRateCombo.currentVal.length > 0 ? cameraFrameRateCombo.currentVal : "(default)"
                        font.bold: true
                        color: cameraFrameRateCombo.currentVal.length === 0 ? "#888888" : "#ffffff"
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: Item {}
                }
            }
        }
    }

    // ── cameraView: live camera image, zoomable (wheel) and pannable (drag) ──
    Component {
        id: cameraViewDelegate

        Column {
            id: colCameraView
            width: parent ? parent.width : 0
            spacing: 4

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setModelValue: function(v) {}

            property var camElement: (ZCam.project && ZCam.project.cameraElement) ? ZCam.project.cameraElement : null
            // zoom factor and pan offset for the preview image
            property real _zoom: 1.0
            property point _pan: Qt.point(0, 0)

            Label {
                text: {
                    let s = colCameraView.meta ? colCameraView.meta.label ?? qsTr("Camera") : qsTr("Camera")
                    if (colCameraView.camElement && colCameraView.camElement.frameSize && colCameraView.camElement.frameSize.width > 0)
                        s += "  (" + colCameraView.camElement.frameSize.width + "\u00D7" + colCameraView.camElement.frameSize.height + ")"
                    s += "  [" + Math.round(colCameraView._zoom * 100) + "%]"
                    return s
                    }
                color: Material.foreground
                opacity: 0.75
                }

            Rectangle {
                width: colCameraView.width
                height: 220
                color: "#202020"
                radius: 4
                border.width: 1
                border.color: Material.accentColor
                clip: true

                CaptureSession {
                    id: previewSession
                    videoOutput: previewVideo
                    Component.onCompleted: {
                        if (colCameraView.camElement)
                            previewSession.videoSink = colCameraView.camElement.videoSink;
                        }
                    }

                Connections {
                    target: colCameraView
                    function onCamElementChanged() {
                        if (colCameraView.camElement)
                            previewSession.videoSink = colCameraView.camElement.videoSink;
                        }
                    }

                VideoOutput {
                    id: previewVideo
                    anchors.fill: parent
                    fillMode: VideoOutput.Stretch
                    scale: colCameraView._zoom
                    // Pan: shift the item in device pixels.
                    transform: Translate {
                        x: colCameraView._pan.x
                        y: colCameraView._pan.y
                        }
                    }

                Text {
                    anchors.centerIn: parent
                    visible: !colCameraView.camElement
                    text: qsTr("no camera")
                    color: "#888888"
                    }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    property point _lastPos: Qt.point(0, 0)

                    onPressed: mouse => {
                        _lastPos = Qt.point(mouse.x, mouse.y)
                        }
                    onPositionChanged: mouse => {
                        if (!(mouse.buttons & Qt.LeftButton))
                            return
                        const dx = mouse.x - _lastPos.x
                        const dy = mouse.y - _lastPos.y
                        _lastPos = Qt.point(mouse.x, mouse.y)
                        colCameraView._pan = Qt.point(colCameraView._pan.x + dx, colCameraView._pan.y + dy)
                        }
                    onDoubleClicked: {
                        // reset view
                        colCameraView._zoom = 1.0
                        colCameraView._pan = Qt.point(0, 0)
                        }
                    onWheel: wheel => {
                        const oldZoom = colCameraView._zoom
                        const f = wheel.angleDelta.y > 0 ? 1.25 : 0.8
                        const newZoom = Math.min(20.0, Math.max(0.1, oldZoom * f))
                        if (newZoom === oldZoom)
                            return
                        // Keep the point under the cursor fixed.
                        const cx = width / 2
                        const cy = height / 2
                        const mx = wheel.x - cx
                        const my = wheel.y - cy
                        const k = newZoom / oldZoom
                        colCameraView._pan = Qt.point(colCameraView._pan.x + mx * (1.0 - k), colCameraView._pan.y + my * (1.0 - k))
                        colCameraView._zoom = newZoom
                        }
                    }
                }
            }
        }
    }
