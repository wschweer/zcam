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
import QtQuick.Dialogs
import QtQuick.Layouts
import ZCam

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
    property int labelWidth: 70

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

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            z: 1000

            onWheel: event => {
                event.accepted = true;
                let step = _dsb.stepSize > 0 ? _dsb.stepSize : 1.0;
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

        property real resetValue: 0.0
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
            anchors.rightMargin: vbox.unitText.length > 0 ? unitLabel.width + unitLabel.anchors.rightMargin + 4 : 6
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

        Text {
            id: unitLabel
            visible: vbox.unitText.length > 0
            text: vbox.unitText
            font.pixelSize: 9
            color: "#333333"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 4
            anchors.bottomMargin: 1
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
                    if (model.isRow)
                        return rowDelegate
                    if (model.isColumns)
                        return columnsDelegate
                    const m = root.metaFor(model.propName)
                    if (!m) return null
                    const t = m.type || "string"
                    switch (t) {
                        case "bool":      return boolDelegate
                        case "int":       return intDelegate
                        case "float":     return floatDelegate
                        case "vector3d":  return vector3dDelegate
                        case "vector2d":  return vector2dDelegate
                        case "font":      return fontDelegate
                        case "halign":    return halignDelegate
                        case "multiline": return multilineDelegate
                        case "singleline":return singlelineDelegate
                        case "line":      return lineDelegate
                        case "color":     return colorDelegate
                        case "layer":     return layerDelegate
                        case "recipe":    return recipeDelegate
                        case "machineType": return machineTypeDelegate
                        default:          return stringDelegate
                        }
                    }

                onLoaded: {
                    if (model.isRow) {
                        item.propName   = model.propName
                        item.subProps   = model.subProps
                        item.subValues  = Qt.binding(() => model.subValues)
                        item.rowLabel   = model.rowLabel ?? ""
                        item.propIndex  = index
                        item.setSubValue = function(subName, v) {
                            root.model.setSubProperty(index, subName, v)
                            }
                        }
                    else if (model.isColumns) {
                        item.propIndex   = index
                        item.columnCount = model.columnCount
                        item.columnItems = Qt.binding(() => model.columnItems)
                        item.setModelValue = function(propName, v) {
                            root.model.setColumnProperty(index, propName, v)
                            }
                        item.setSubValue = function(rowItem, subName, v) {
                            root.model.setColumnProperty(index, subName, v)
                            }
                        }
                    else {
                        item.propName  = model.propName
                        item.propValue = Qt.binding(() => model.propValue)
                        item.meta      = root.metaFor(model.propName)
                        item.propIndex = index
                        item.setModelValue = function(v) {
                            model.propValue = v
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
                                case "int":       return subIntDelegate
                                case "float":     return subFloatDelegate
                                case "halign":    return subHalignDelegate
                                case "machineType": return subMachineTypeDelegate
                                case "singleline":return subSinglelineDelegate
                                case "string":    return subStringDelegate
                                default:          return subStringDelegate
                                }
                            }

                        onLoaded: {
                            item.subName  = subLoader.subName
                            item.subValue = Qt.binding(() => subLoader.subValue)
                            item.subMeta   = subLoader.subMeta
                            item.setSub    = function(v) {
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
                                        case "vector2d":  return vector2dDelegate
                                        case "font":      return fontDelegate
                                        case "halign":    return halignDelegate
                                        case "multiline": return multilineDelegate
                                        case "singleline":return singlelineDelegate
                                        case "color":     return colorDelegate
                                        case "layer":     return layerDelegate
                                        case "recipe":    return recipeDelegate
                                        case "machineType": return machineTypeDelegate
                                        default:          return stringDelegate
                                        }
                                    }

                                onLoaded: {
                                    const d = colLoader.itemData
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
                                        item.propValue = Qt.binding(() => d.propValue)
                                        item.meta      = root.metaFor(d.name)
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
                                case "int":       return subIntDelegate
                                case "float":     return subFloatDelegate
                                case "halign":    return subHalignDelegate
                                case "machineType": return subMachineTypeDelegate
                                case "singleline":return subSinglelineDelegate
                                case "string":    return subStringDelegate
                                default:          return subStringDelegate
                                }
                            }

                        onLoaded: {
                            item.subName  = subLoaderCol.subName
                            item.subValue = Qt.binding(() => subLoaderCol.subValue)
                            item.subMeta   = subLoaderCol.subMeta
                            item.setSub    = function(v) {
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

                    contentItem: TextInput {
                        text: subIntSpin.textFromValue(subIntSpin.value)
                        color: "#ffffff"
                        font.bold: true
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        readOnly: !subIntSpin.editable
                        validator: subIntSpin.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
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

                    property real modelValue: subFloat.subValue !== undefined ? Number(subFloat.subValue) : 0.0
                    value: modelValue
                    onModelValueChanged: if (value !== modelValue) value = modelValue

                    onValueChanged: {
                        if (subFloat.setSub && subFloat.subValue !== value)
                            subFloat.setSub(value);
                        }

                    contentItem: TextInput {
                        text: subFloatSpin.textFromValue(subFloatSpin.value)
                        color: "#ffffff"
                        font.bold: true
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        readOnly: !subFloatSpin.editable
                        validator: subFloatSpin.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
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

                        contentItem: TextInput {
                            text: intSpinBox.textFromValue(intSpinBox.value)
                            color: "#ffffff"
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !intSpinBox.editable
                            validator: intSpinBox.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
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
                property var meta
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
                        resetValue: root.defaultScalar(rowFloat.propName, 0)

                        property real modelValue: rowFloat.propValue !== undefined ? Number(rowFloat.propValue) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowFloat.propValue !== value)
                                rowFloat.setModelValue(value);
                            }

                        contentItem: TextInput {
                            text: floatSpinBox.textFromValue(floatSpinBox.value)
                            color: "#ffffff"
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !floatSpinBox.editable
                            validator: floatSpinBox.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
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
                property var meta
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
                        resetValue: root.defaultScalar(rowVec3.propName, 0)

                        property real modelValue: rowVec3.propValue !== undefined && rowVec3.propValue.x !== undefined ? Number(rowVec3.propValue.x) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowVec3.propValue !== undefined && rowVec3.propValue.x !== value) {
                                let v = rowVec3.propValue;
                                rowVec3.setModelValue(Qt.vector3d(value, v.y, v.z));
                                }
                            }

                        contentItem: TextInput {
                            text: vec3xSpinBox.textFromValue(vec3xSpinBox.value)
                            color: "#ffffff"
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !vec3xSpinBox.editable
                            validator: vec3xSpinBox.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
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
                        resetValue: root.defaultScalar(rowVec3.propName, 1)

                        property real modelValue: rowVec3.propValue !== undefined && rowVec3.propValue.y !== undefined ? Number(rowVec3.propValue.y) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowVec3.propValue !== undefined && rowVec3.propValue.y !== value) {
                                let v = rowVec3.propValue;
                                rowVec3.setModelValue(Qt.vector3d(v.x, value, v.z));
                                }
                            }

                        contentItem: TextInput {
                            text: vec3ySpinBox.textFromValue(vec3ySpinBox.value)
                            color: "#ffffff"
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !vec3ySpinBox.editable
                            validator: vec3ySpinBox.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
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
                        resetValue: root.defaultScalar(rowVec3.propName, 2)

                        property real modelValue: rowVec3.propValue !== undefined && rowVec3.propValue.z !== undefined ? Number(rowVec3.propValue.z) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowVec3.propValue !== undefined && rowVec3.propValue.z !== value) {
                                let v = rowVec3.propValue;
                                rowVec3.setModelValue(Qt.vector3d(v.x, v.y, value));
                                }
                            }

                        contentItem: TextInput {
                            text: vec3zSpinBox.textFromValue(vec3zSpinBox.value)
                            color: "#ffffff"
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !vec3zSpinBox.editable
                            validator: vec3zSpinBox.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
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
                property var meta
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
                        resetValue: root.defaultScalar(rowVec2.propName, 0)

                        property real modelValue: rowVec2.propValue !== undefined && rowVec2.propValue.x !== undefined ? Number(rowVec2.propValue.x) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowVec2.propValue !== undefined && rowVec2.propValue.x !== value) {
                                let v = rowVec2.propValue;
                                rowVec2.setModelValue(Qt.vector2d(value, v.y));
                                }
                            }

                        contentItem: TextInput {
                            text: vec2xSpinBox.textFromValue(vec2xSpinBox.value)
                            color: "#ffffff"
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !vec2xSpinBox.editable
                            validator: vec2xSpinBox.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
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
                        resetValue: root.defaultScalar(rowVec2.propName, 1)

                        property real modelValue: rowVec2.propValue !== undefined && rowVec2.propValue.y !== undefined ? Number(rowVec2.propValue.y) : 0.0
                        value: modelValue
                        onModelValueChanged: if (value !== modelValue) value = modelValue

                        onValueChanged: {
                            if (rowVec2.propValue !== undefined && rowVec2.propValue.y !== value) {
                                let v = rowVec2.propValue;
                                rowVec2.setModelValue(Qt.vector2d(v.x, value));
                                }
                            }

                        contentItem: TextInput {
                            text: vec2ySpinBox.textFromValue(vec2ySpinBox.value)
                            color: "#ffffff"
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !vec2ySpinBox.editable
                            validator: vec2ySpinBox.validator
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
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
                        horizontalAlignment: TextInput.AlignRight
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#ffffff"
                        clip: true
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
                        horizontalAlignment: TextInput.AlignRight
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
        }
    }