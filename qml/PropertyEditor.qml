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
        if (propMeta.items && Array.isArray(propMeta.items)) {
            for (let i = 0; i < propMeta.items.length; ++i) {
                const item = propMeta.items[i]
                if (item.columns) {
                    // Parse items inside the columns block
                    if (item.columns.items && Array.isArray(item.columns.items)) {
                        for (let j = 0; j < item.columns.items.length; ++j) {
                            const colItem = item.columns.items[j]
                            if (colItem.row) {
                                const rowObj = colItem.row
                                for (const subName in rowObj)
                                    map[subName] = rowObj[subName]
                                }
                            else if (colItem.name)
                                map[colItem.name] = colItem
                            }
                        }
                    }
                else if (item.row) {
                    const rowObj = item.row
                    for (const subName in rowObj)
                        map[subName] = rowObj[subName]
                    }
                else if (item.name)
                    map[item.name] = item
                }
            }
        for (const key in propMeta) {
            if (key === "class" || key === "items")
                continue
            const val = propMeta[key]
            if (val && typeof val === "object" && !Array.isArray(val)) {
                let isRow = true
                for (const subKey in val) {
                    if (!val[subKey] || !val[subKey].label) {
                        isRow = false
                        break
                        }
                    }
                if (isRow) {
                    for (const subKey in val)
                        map[subKey] = val[subKey]
                    }
                else if (val.label)
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
            color: "#ffffff"
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
            color: "#ffffff"
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
        color: "#a9a9a9"
        radius: 4
        implicitHeight: 28

        property string unitText: ""
        property string subLabelText: ""

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
            visible: vbox.subLabelText.length > 0
            text: vbox.subLabelText
            font.pixelSize: 13
            color: "#333333"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 4
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
                color: "#333333"
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
                            const m = subLoader.subMeta
                            if (!m) return null
                            const t = m.type || "string"
                            switch (t) {
                                case "bool":      return subBoolDelegate
                                case "fontStyle": return subFontStyleDelegate
                                case "int":       return subIntDelegate
                                case "float":     return subFloatDelegate
                                case "halign":    return subHalignDelegate
                                case "machineType": return subMachineTypeDelegate
                                case "boardType":  return subBoardTypeDelegate
                                case "override":  return subOverrideDelegate
                                case "pulsewidth": return subPulsewidthDelegate
                                case "lineJoin":  return subLineJoinDelegate
                                case "lineEnd":    return subLineEndDelegate
                                case "lockScale":  return subLockScaleDelegate
                                case "lockSize":   return subLockSizeDelegate
                                case "framingType": return subFramingTypeDelegate
                                case "ethDevice":  return subEthDeviceDelegate
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

                // Group flat columnItems into visual rows respecting
                // columnCount and per-item colSpan.
                property var rows: {
                    const items = colsContainer.columnItems
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
                        currentRow.push(item)
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
                                    if (d.isRow) {
                                        item.propName   = "row"
                                        item.subProps   = d.subProps
                                        item.subValues  = Qt.binding(() => d.subValues)
                                        item.rowLabel   = d.rowLabel || ""
                                        item.propIndex  = colsContainer.propIndex
                                        item.setSubValue = function(subName, v) {
                                            colsContainer.setSubValue(d, subName, v)
                                            }
                                        }
                                    else {
                                        item.propName  = d.name
                                        item.propValue = Qt.binding(() => {
                                            // Read directly from the model's columnItems for live updates
                                            const ci = colsContainer.columnItems
                                            if (!ci) return undefined
                                            for (let i = 0; i < ci.length; ++i) {
                                                if (ci[i].name === d.name)
                                                    return ci[i].propValue
                                                }
                                            return undefined
                                            })
                                        item.meta = Qt.binding(() => root.metaFor(item.propName))
                                        item.propIndex = colsContainer.propIndex
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
                            const m = subLoaderCol.subMeta
                            if (!m) return null
                            const t = m.type || "string"
                            switch (t) {
                                case "bool":      return subBoolDelegate
                                case "fontStyle": return subFontStyleDelegate
                                case "int":       return subIntDelegate
                                case "float":     return subFloatDelegate
                                case "halign":    return subHalignDelegate
                                case "machineType": return subMachineTypeDelegate
                                case "boardType":  return subBoardTypeDelegate
                                case "override":  return subOverrideDelegate
                                case "pulsewidth": return subPulsewidthDelegate
                                case "lineJoin":  return subLineJoinDelegate
                                case "lineEnd":    return subLineEndDelegate
                                case "lockScale":  return subLockScaleDelegate
                                case "lockSize":   return subLockSizeDelegate
                                case "framingType": return subFramingTypeDelegate
                                case "ethDevice":  return subEthDeviceDelegate
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
                subLabelText: subBool.subMeta ? subBool.subMeta.label ?? "" : ""

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
                    text: subFontStyle.subMeta ? subFontStyle.subMeta.label ?? "" : ""
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
                subLabelText: subInt.subMeta ? subInt.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                BareSpinBox {
                    id: subIntSpin
                    anchors.fill: parent
                    from: subInt.subMeta && subInt.subMeta.min !== undefined ? Math.round(subInt.subMeta.min) : -1000000
                    to: subInt.subMeta && subInt.subMeta.max !== undefined ? Math.round(subInt.subMeta.max) : 1000000

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
                subLabelText: subFloat.subMeta ? subFloat.subMeta.label ?? "" : ""

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
                subLabelText: subSingle.subMeta ? subSingle.subMeta.label ?? "" : ""

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

        Component {
            id: subStringDelegate

            ValueBox {
                id: subString
                Layout.fillWidth: true
                width: parent ? parent.width : 0
                subLabelText: subString.subMeta ? subString.subMeta.label ?? "" : ""

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
                subLabelText: subHalign.subMeta ? subHalign.subMeta.label ?? "" : ""

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
                        horizontalAlignment: Text.AlignRight
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
                subLabelText: subMachineType.subMeta ? subMachineType.subMeta.label ?? "" : ""

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
                        horizontalAlignment: Text.AlignRight
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
                subLabelText: subBoardType.subMeta ? subBoardType.subMeta.label ?? "" : ""

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
                        horizontalAlignment: Text.AlignRight
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
                subLabelText: subOverride.subMeta ? subOverride.subMeta.label ?? "" : ""

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
                        horizontalAlignment: Text.AlignRight
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
                subLabelText: subPulsewidth.subMeta ? subPulsewidth.subMeta.label ?? "" : ""

                property string subName
                property var subValue
                property var subMeta
                property var setSub: function(v) {}

                function freqModel() {
                    if (root.model.pulsewidthNames)
                        return root.model.pulsewidthNames()
                    if (ZCam.project.laser && ZCam.project.laser.engine)
                        return ZCam.project.laser.engine.laserPulseList
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
                        horizontalAlignment: Text.AlignRight
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
                subLabelText: subLineJoin.subMeta ? subLineJoin.subMeta.label ?? "" : ""

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
                        horizontalAlignment: Text.AlignRight
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
                subLabelText: subLineEnd.subMeta ? subLineEnd.subMeta.label ?? "" : ""

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
                        horizontalAlignment: Text.AlignRight
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
                            horizontalAlignment: Text.AlignRight
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
                            horizontalAlignment: Text.AlignRight
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
                            horizontalAlignment: Text.AlignRight
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
                    Layout.fillWidth: true

                    ComboBox {
                        id: recipeCombo
                        anchors.fill: parent
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
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            }
                        indicator: Item {}
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
                            horizontalAlignment: Text.AlignRight
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
                            horizontalAlignment: Text.AlignRight
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
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        indicator: Item {}
                    }
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
                subLabelText: subEthDevice.subMeta ? subEthDevice.subMeta.label ?? "" : ""

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
                        horizontalAlignment: Text.AlignRight
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
                            horizontalAlignment: Text.AlignRight
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
                        horizontalAlignment: Text.AlignRight
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
                        horizontalAlignment: Text.AlignRight
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
                if (ZCam.project.laser && ZCam.project.laser.engine)
                    return ZCam.project.laser.engine.laserPulseList
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
                        horizontalAlignment: Text.AlignRight
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
                        horizontalAlignment: Text.AlignRight
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
                        horizontalAlignment: Text.AlignRight
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
                        horizontalAlignment: Text.AlignRight
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
            subLabelText: subFramingType.subMeta ? subFramingType.subMeta.label ?? "" : ""

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
                    horizontalAlignment: Text.AlignRight
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

        Row {
            id: subLockScaleRow
            width: parent ? parent.width : 0
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

        Row {
            id: subLockSizeRow
            width: parent ? parent.width : 0
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
    }
