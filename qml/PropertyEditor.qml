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
import QtQuick.Window
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

    // Color for script-bound property values shown in the inspector.
    readonly property string _boundColor: "#1565c0"  // dark blue

    // Signals forwarded from the model
    signal modelDataChanged()

    // Exposed so child ScriptButtons can locate the shared popup
    // without walking QML scoping trees.
    property alias _scriptPopup: scriptPopup

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
        // Fallback: some sub-cells are not flattened into propMetaMap,
        // so look them up directly by name.
        return metaFor(subName)
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

    // ── Scripting helpers ────────────────────────────────────────────────
    // True when the given property is driven by a JavaScript binding
    // (read-only in the GUI, greyed out, f(x) button shows "active").
    function isScriptBound(propName) {
        const _ = root._dataChangeCounter   // re-evaluate on dataChanged
        if (!root.model || !root.model.isScriptBound)
            return false
        return root.model.isScriptBound(propName)
        }

    function isScriptActive(propName) {
        const _ = root._dataChangeCounter
        if (!root.model || !root.model.isScriptActive)
            return true
        return root.model.isScriptActive(propName)
        }

    /// True when a script exists for this property (active or inactive).
    /// Used by the f(x) button to show the "has script" icon even
    /// when the binding is paused.
    function hasScript(propName) {
        const _ = root._dataChangeCounter
        if (root.isScriptBound(propName))
            return true
        // Also check for inactive bindings: scriptFor returns the
        // stored script text even when the binding is inactive.
        if (scriptFor(propName, -1).length > 0)
            return true
        for (let c = 0; c < 3; ++c) {
            if (scriptFor(propName, c).length > 0)
                return true
            }
        return false
        }

    function boundComponents(propName) {
        const _ = root._dataChangeCounter
        if (!root.model || !root.model.boundComponents)
            return ""
        return root.model.boundComponents(propName)
        }

    function scriptFor(propName, comp) {
        if (!root.model || !root.model.scriptFor)
            return ""
        return root.model.scriptFor(propName, comp)
        }

    function scriptError(propName, comp) {
        if (!root.model || !root.model.scriptError)
            return ""
        return root.model.scriptError(propName, comp)
        }

    // ── Shared utility functions ────────────────────────────────────────
    // Build a string list with a prefix entry (e.g. "(inherited)", "(none)").
    function buildListWithPrefix(prefix, baseList) {
        var list = [prefix]
        if (baseList) {
            for (var i = 0; i < baseList.length; ++i)
                list.push(baseList[i])
            }
        return list
        }

    // Resolve a pointer to a display name, returning "" for null/undefined.
    function resolvePointerName(toNameFn, ptr) {
        if (ptr === undefined || ptr === null)
            return ""
        return toNameFn ? toNameFn(ptr) : ""
        }

    // Safely assign a property only if the target item defines it.
    function safeSetProp(item, propName, value) {
        if (item && item[propName] !== undefined)
            item[propName] = value
        }

    // ══════════════════════════════════════════════════════════════════════
    //  Unified delegate dispatch & setup helpers
    // ══════════════════════════════════════════════════════════════════════

    // Central type → Component dispatch, replacing the three duplicated
    // switch statements that existed in the top-level, sub, and column
    // loaders.  Every type maps to a single unified delegate that handles
    // both top-level (showLabel=true) and sub (showLabel=false) contexts.
    function delegateForType(type) {
        switch (type) {
            case "bool":
            case "fontStyle":   return boolDelegate
            case "int":          return intDelegate
            case "float":        return floatDelegate
            case "vector3d":
            case "scale":
            case "vector2d":
            case "size":         return vectorDelegate
            case "font":          return fontDelegate
            case "halign":        return halignDelegate
            case "multiline":
            case "singleline":
            case "string":       return textDelegate
            case "path":          return pathDelegate
            case "line":          return lineDelegate
            case "color":         return colorDelegate
            case "layer":
            case "laserLayer":
            case "recipe":
            case "machine":      return pointerComboDelegate
            case "machineName":
            case "machineType":
            case "boardType":
            case "ethDevice":    return stringComboDelegate
            case "override":
            case "lineJoin":
            case "lineEnd":
            case "framingType":  return enumComboDelegate
            case "pulsewidth":   return pulsewidthDelegate
            case "lockScale":
            case "lockSize":      return lockDelegate
            case "cameraName":
            case "cameraResolution":
            case "cameraFrameRate": return cameraComboDelegate
            case "cameraView":    return cameraViewDelegate
            case "cameraCapture": return cameraCaptureDelegate
            case "empty":         return emptyDelegate
            default:              return textDelegate
            }
        }

    // Common setup for scalar property delegates (bool, int, float, combo, …).
    // Called from every Loader's onLoaded to set the unified property
    // interface with a single function call, replacing ~6 repeated
    // assignment blocks.
    function setupDelegate(item, name, valueFn, metaObj, index, setter, isTop) {
        if (!item)
            return
        item.propName  = name
        item.propValue = Qt.binding(valueFn)
        item.meta       = metaObj
        item.propIndex  = index
        item.showLabel  = isTop
        item.enabled    = Qt.binding(() => root.isPropEnabled(item.meta))
        item.opacity    = Qt.binding(() => item ? (item.enabled ? 1.0 : 0.4) : 0.4)
        item.bound      = Qt.binding(() => root.isScriptBound(name))
        item.boundComponents = Qt.binding(() => root.boundComponents(name))
        item.setValue   = setter
        }

    // Setup for row-type delegates (subProps / subValues / rowLabel).
    function setupRowDelegate(item, propName, subProps, subValuesBinding, rowLabel, propIndex, setSubValueFn) {
        if (!item)
            return
        item.propName   = propName
        item.subProps   = subProps
        item.subValues  = subValuesBinding
        item.rowLabel   = rowLabel ?? ""
        item.propIndex  = propIndex
        item.setSubValue = setSubValueFn
        }

    // Setup for sub-delegates loaded inside row/colRow repeaters.
    function setupSubDelegate(item, subName, subValueFn, subMeta, setSubFn) {
        if (!item)
            return
        item.propName  = subName
        item.propValue = Qt.binding(subValueFn)
        item.meta       = subMeta
        item.showLabel  = false
        item.enabled    = Qt.binding(() => root.isPropEnabled(item.meta))
        item.opacity    = Qt.binding(() => item ? (item.enabled ? 1.0 : 0.4) : 0.4)
        item.bound      = Qt.binding(() => root.isScriptBound(subName))
        item.boundComponents = Qt.binding(() => root.boundComponents(subName))
        item.setValue   = setSubFn
        }

    // ══════════════════════════════════════════════════════════════════════
    //  ScriptButton: f(x) button shown next to every scriptable prop
    // ══════════════════════════════════════════════════════════════════════
    component ScriptButton: ToolButton {
        id: scriptBtn

        required property string propName
        /// -1 → scalar binding; 0/1/2 → vector component x/y/z
        property int component: -1
        /// True when this button represents a vector property.
        /// The popup will show a component selector (All/X/Y/Z).
        property bool vectorMode: false
        property bool vectorIs2d: false
        property string boundComps: ""

        Layout.preferredWidth: 22
        Layout.preferredHeight: 22
        Layout.minimumWidth: 22
        padding: 0
        checkable: false

        property bool bound: {
            if (vectorMode) {
                // Bound if any component has a script (active or inactive).
                return root.hasScript(propName)
            }
            return root.hasScript(propName)
        }

        icon.source: bound ? "qrc:/icons/bound-expression.svg"
                           : "qrc:/icons/bound-expression-unset.svg"
        icon.width: 32
        icon.height: 32
        icon.color: "transparent"

        background: Rectangle {
            color: scriptBtn.hovered ? "#4a4a4a" : "transparent"
            radius: 3
            }

        ToolTip.visible: hovered
        ToolTip.text: scriptBtn.bound ? qsTr("Edit script binding")
                                      : qsTr("Create script binding")

        onClicked: {
            root._openScriptPopup(scriptBtn.propName, scriptBtn.component,
                                  scriptBtn.vectorMode, scriptBtn.vectorIs2d,
                                  scriptBtn.boundComps)
            }
        }

    // ══════════════════════════════════════════════════════════════════════
    //  ScriptPopup: edit/evaluate/enable a script binding
    // ══════════════════════════════════════════════════════════════════════
    function _openScriptPopup(propName, component, vectorMode, vectorIs2d, boundComps) {
        scriptPopup.targetProp = propName
        scriptPopup.vectorMode = vectorMode ?? false
        scriptPopup.vectorIs2d = vectorIs2d ?? false

        // In vector mode, pick the component to show:
        //   1. If _lastComponent has a non-empty script, reuse it.
        //   2. Otherwise, if there are existing bindings, use the
        //      first bound component so the script text is loaded.
        //   3. Otherwise default to the passed component (-1 = scalar).
        let comp = component
        if (vectorMode) {
            if (scriptPopup._lastComponent >= 0
                && scriptFor(propName, scriptPopup._lastComponent).length > 0)
                comp = scriptPopup._lastComponent
            else if (boundComps && boundComps !== "") {
                const parts = boundComps.split(",")
                const first = parts[0]
                if (first === "all")
                    comp = -1
                else
                    comp = parseInt(first)
                }
            else {
                // No active binding info (boundComps is empty for
                // inactive bindings) — scan scriptFor to find the
                // component that has a stored script.
                if (scriptFor(propName, -1).length > 0)
                    comp = -1
                else {
                    for (let c = 0; c < 3; ++c) {
                        if (scriptFor(propName, c).length > 0) {
                            comp = c
                            break
                            }
                        }
                    }
                }
            }
        scriptPopup.component  = comp
        scriptPopup._lastComponent = comp
        scriptPopup.script     = scriptFor(propName, comp)
        scriptPopup.error      = scriptError(propName, comp)
        scriptPopup.result     = ""
        scriptPopup.active     = root.isScriptActive(propName)
        scriptPopup.open()
        }

    Popup {
        id: scriptPopup
        modal: true
        width: 500
        padding: 10
        parent: root.Window.window ? root.Window.window.contentItem : root
        anchors.centerIn: parent
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string targetProp: ""
        property int component: -1
        property string script: ""
        property string result: ""
        property string error: ""
        property bool active: false
        property bool _userDeactivated: false

        // Vector mode: show a component selector (All/X/Y/Z) so the
        // user can pick which component to bind.
        property bool vectorMode: false
        property bool vectorIs2d: false
        property int _lastComponent: -1   // remembers last-used component

        function _compLabel(c) {
            if (vectorIs2d)
                return ["All", "Width", "Height"][c + 1] ?? "All"
            return ["All", "X", "Y", "Z"][c + 1] ?? "All"
            }

        onOpened: {
            scriptTextArea.text = scriptPopup.script
            activeCheck.checked = scriptPopup.active
            scriptPopup._userDeactivated = false
            scriptTextArea.forceActiveFocus()
            scriptPopup.evaluate()
            }

        onClosed: {
            if (scriptPopup._userDeactivated)
                return
            if (scriptTextArea.text.length > 0)
                scriptPopup.apply()
            else if (scriptPopup.active)
                scriptPopup.deactivate()
            }

        function evaluate() {
            if (!root.model || !root.model.testScript)
                return
            let r
            if (root.model.testScriptWithContext)
                r = root.model.testScriptWithContext(scriptTextArea.text)
            else
                r = root.model.testScript(scriptTextArea.text)
            if (r === undefined || r === null) {
                scriptPopup.result = "(undefined)"
                scriptPopup.error  = ""
                }
            else if (typeof r === "string" && r.length > 0 && r.indexOf(":") >= 0 && r.indexOf("error") >= 0) {
                // ScriptEngine returns the error string on failure
                scriptPopup.result = ""
                scriptPopup.error  = r
                }
            else {
                scriptPopup.result = String(r)
                scriptPopup.error  = ""
                }
            }

        function apply() {
            if (!root.model || !root.model.setScript)
                return
            root.model.setScript(scriptPopup.targetProp,
                                 scriptPopup.component,
                                 scriptTextArea.text)
            scriptPopup.active = true
            scriptPopup.error  = root.scriptError(scriptPopup.targetProp, scriptPopup.component)
            scriptPopup.result = ""
            }

        function deactivate() {
            if (!root.model || !root.model.setScriptActive)
                return
            root.model.setScriptActive(scriptPopup.targetProp, false)
            scriptPopup.active = false
            scriptPopup.error  = ""
            scriptPopup.result = ""
            }

        function activate() {
            if (!root.model || !root.model.setScriptActive)
                return
            root.model.setScriptActive(scriptPopup.targetProp, true)
            scriptPopup.active = true
            scriptPopup.error  = root.scriptError(scriptPopup.targetProp, scriptPopup.component)
            scriptPopup.result = ""
            }

        contentItem: ColumnLayout {
            spacing: 6

            Label {
                text: qsTr("Script for %1%2")
                      .arg(scriptPopup.targetProp)
                      .arg(scriptPopup.vectorMode ? "." + scriptPopup._compLabel(scriptPopup.component)
                                                  : (scriptPopup.component >= 0 ? "." + ["x","y","z"][scriptPopup.component] : ""))
                font.bold: true
                color: Material.accentColor
                Layout.fillWidth: true
                }

            // Component selector for vector properties
            RowLayout {
                visible: scriptPopup.vectorMode
                Layout.fillWidth: true
                spacing: 4

                Label { text: qsTr("Component:"); opacity: 0.7 }

                Repeater {
                    model: scriptPopup.vectorIs2d
                           ? ["All", "Width", "Height"]
                           : ["All", "X", "Y", "Z"]

                    delegate: Button {
                        required property string modelData
                        required property int index

                        readonly property int compValue: index - 1  // All=-1, X=0, Y=1, Z=2

                        text: modelData
                        checkable: true
                        checked: scriptPopup.component === compValue
                        Layout.preferredHeight: 24
                        flat: true
                        onClicked: {
                            scriptPopup._lastComponent = compValue
                            scriptPopup.component = compValue
                            scriptPopup.script = root.scriptFor(scriptPopup.targetProp, compValue)
                            scriptPopup.error  = root.scriptError(scriptPopup.targetProp, compValue)
                            scriptTextArea.text = scriptPopup.script
                            scriptPopup.evaluate()
                            }
                        }
                    }
                }

            // script input
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 100
                color: "#2a2a2a"
                radius: 4
                border.color: scriptTextArea.activeFocus ? Material.accentColor : "#555555"

                ScrollView {
                    id: scriptScroll
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded
                    TextArea {
                        id: scriptTextArea
                        width: Math.max(scriptScroll.width - 8, implicitWidth)
                        wrapMode: TextArea.Wrap
                        font.family: "monospace"
                        color: "#ffffff"
                        background: Item {}
                        padding: 0
                        onTextChanged: scriptPopup.evaluate()
                        }
                    }
                }

            // live evaluation
            RowLayout {
                spacing: 4
                Layout.fillWidth: true

                Label { text: qsTr("Result:"); opacity: 0.7 }

                ToolButton {
                    text: "="
                    onClicked: scriptPopup.evaluate()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Evaluate expression")
                    }
                Label {
                    id: resultLabel
                    text: scriptPopup.result
                    Layout.fillWidth: true
                    font.family: "monospace"
                    color: "#aaffaa"
                    wrapMode: Text.Wrap
                    }
                }

            // error display
            Label {
                id: errorLabel
                text: scriptPopup.error
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#ff8080"
                font.pixelSize: 11
                }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Material.accentColor
                opacity: 0.3
                }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                CheckBox {
                    id: activeCheck
                    text: qsTr("Active")
                    checked: scriptPopup.active
                    onToggled: {
                        if (checked) {
                            scriptPopup.activate()
                            scriptPopup._userDeactivated = false
                            }
                        else {
                            scriptPopup.deactivate()
                            scriptPopup._userDeactivated = true
                            }
                        }
                    }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Close")
                    onClicked: scriptPopup.close()
                    }
                }
            }
        }

    // ══════════════════════════════════════════════════════════════════════
    //  Reusable borderless SpinBox
    // ══════════════════════════════════════════════════════════════════════
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
            color: _sb.boundColor.length > 0 ? _sb.boundColor : (_sb.enabled ? "#ffffff" : "#888888")
            font.bold: true
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            readOnly: !_sb.editable
            validator: _sb.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            }

        MouseArea {
            id: sbHover
            anchors.fill: parent
            hoverEnabled: true
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

        // When non-empty, overrides the text color to indicate a
        // script-bound (read-only) property value.
        property string boundColor: ""

        // The surrounding ValueBox provides hover feedback, but its MouseArea
        // sits below this SpinBox's own MouseArea.  Bind the ValueBox's hover
        // state to this SpinBox's MouseArea so the highlight works uniformly.
        property Item valueBox: {
            let p = parent
            while (p) {
                if (p.toString().indexOf("ValueBox") === 0)
                    return p
                p = p.parent
                }
            return null
            }

        Component.onCompleted: {
            if (valueBox)
                valueBox.hovered = Qt.binding(() => sbHover.containsMouse)
            }
        }

    // ══════════════════════════════════════════════════════════════════════
    //  Reusable borderless DoubleSpinBox
    // ══════════════════════════════════════════════════════════════════════
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
            color: _dsb.boundColor.length > 0 ? _dsb.boundColor : (_dsb.enabled ? "#ffffff" : "#888888")
            font.bold: true
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            readOnly: !_dsb.editable
            validator: _dsb.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            }

        MouseArea {
            id: dsbHover
            anchors.fill: parent
            hoverEnabled: true
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

        // When non-empty, overrides the text color to indicate a
        // script-bound (read-only) property value.
        property string boundColor: ""

        property Item valueBox: {
            let p = parent
            while (p) {
                if (p.toString().indexOf("ValueBox") === 0)
                    return p
                p = p.parent
                }
            return null
            }

        Component.onCompleted: {
            if (valueBox)
                valueBox.hovered = Qt.binding(() => dsbHover.containsMouse)
            }
        }

    // ══════════════════════════════════════════════════════════════════════
    //  BareComboBox — borderless styled ComboBox (eliminates ~30× duplication)
    // ══════════════════════════════════════════════════════════════════════
    component BareComboBox : ComboBox {
        id: _combo

        background: Item {}
        padding: 2
        indicator: Item {}

        // Optional override for the display text.  When non-empty, this is
        // used instead of the ComboBox's own displayText.
        property string displayTextOverride: ""

        // Text color for the content item.
        property color textColor: "#ffffff"

        contentItem: Text {
            text: _combo.displayTextOverride.length > 0
                  ? _combo.displayTextOverride
                  : _combo.displayText
            font.bold: true
            color: _combo.textColor
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            }
        }

    // ══════════════════════════════════════════════════════════════════════
    //  PropLabel — standard property label (eliminates ~20× duplication)
    // ══════════════════════════════════════════════════════════════════════
    component PropLabel : Label {
        id: _lbl
        Layout.preferredWidth: root.labelWidth
        Layout.rightMargin: 2
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignRight
        verticalAlignment: _lbl.alignTop ? Text.AlignTop : Text.AlignVCenter
        color: Material.foreground
        opacity: 0.75

        property bool alignTop: false
        }

    // ══════════════════════════════════════════════════════════════════════
    //  VectorComponentBox — one ValueBox+BareDoubleSpinBox for a single
    //  vector component (X/Y/Z).  Eliminates the 3× repetition inside each
    //  vector delegate and the 4× top/sub vector2d/vector3d duplication.
    // ══════════════════════════════════════════════════════════════════════
    component VectorComponentBox : ValueBox {
        id: vecBox

        property string compLabel: "X"
        property int compIndex: 0
        property var vectorValue
        property var meta
        property var boundComponents: ""
        property var onComponentChange: function(newValue) {}

        subLabelText: compLabel
        unitText: meta ? meta.unit ?? "" : ""
        Layout.fillWidth: true

        BareDoubleSpinBox {
            id: vecSpin
            anchors.fill: parent
            editable: !(vecBox.boundComponents === "all"
                        || vecBox.boundComponents.split(",").indexOf(String(vecBox.compIndex)) >= 0)
            boundColor: (vecBox.boundComponents === "all"
                         || vecBox.boundComponents.split(",").indexOf(String(vecBox.compIndex)) >= 0)
                        ? root._boundColor : ""
            from: vecBox.meta && vecBox.meta.min !== undefined ? vecBox.meta.min : -1000000.0
            to:   vecBox.meta && vecBox.meta.max !== undefined ? vecBox.meta.max : 1000000.0
            stepSize: root.defaultStepSize(vecBox.meta)
            bigStep:  root.defaultBigStep(vecBox.meta)
            minStep:  root.defaultMinStep(vecBox.meta)
            resetValue: root.defaultScalarFromMeta(vecBox.meta, vecBox.compIndex)
            decimals: vecBox.meta && vecBox.meta.precision !== undefined ? vecBox.meta.precision : 2

            property real modelValue: {
                if (!vecBox.vectorValue)
                    return 0.0
                if (vecBox.compIndex === 0) return Number(vecBox.vectorValue.x) || 0.0
                if (vecBox.compIndex === 1) return Number(vecBox.vectorValue.y) || 0.0
                if (vecBox.compIndex === 2) return Number(vecBox.vectorValue.z) || 0.0
                return 0.0
                }
            value: modelValue
            onModelValueChanged: if (value !== modelValue) value = modelValue

            onValueChanged: vecBox.onComponentChange(value)
            }
        }

    // ══════════════════════════════════════════════════════════════════════
    //  ValueBox
    // ══════════════════════════════════════════════════════════════════════
    component ValueBox : Rectangle {
        id: vbox
        color: vbox.enabled ? (vbox.hovered ? "#c2c2c2" : "#a9a9a9") : "#5a5a5a"
        radius: 4
        implicitHeight: 28
        opacity: vbox.enabled ? 1.0 : 0.5

        property string unitText: ""
        property string subLabelText: ""
        property bool subLabelAlignRight: false
        property bool hovered: hoverArea.containsMouse

        default property alias contentChildren: contentColumn.data

        MouseArea {
            id: hoverArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            }

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

    // ══════════════════════════════════════════════════════════════════════
    //  Property ListView
    // ══════════════════════════════════════════════════════════════════════
    ListView {
        id: listView
        anchors.fill: parent
        model: root.model
        clip: true
        spacing: 2

        ScrollBar.vertical: ScrollBar {
            id: propScrollBar
            policy: ScrollBar.AsNeeded
        }

        delegate: Item {
            id: delegateRoot
            width: ListView.view.width - (propScrollBar.visible && propScrollBar.width > 0 ? propScrollBar.width + 2 : 0)
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
                    return root.delegateForType(m.type || "string")
                    }

                onLoaded: {
                    if (!item)
                        return
                    if (delegateRoot.model.isRow) {
                        root.setupRowDelegate(item,
                            delegateRoot.model.propName,
                            delegateRoot.model.subProps,
                            Qt.binding(() => delegateRoot.model.subValues),
                            delegateRoot.model.rowLabel,
                            delegateRoot.index,
                            function(subName, v) {
                                root.model.setSubProperty(delegateRoot.index, subName, v)
                                })
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
                        root.setupDelegate(item,
                            delegateRoot.model.propName,
                            () => delegateRoot.model.propValue,
                            root.metaFor(delegateRoot.model.propName),
                            delegateRoot.index,
                            function(v) { delegateRoot.model.propValue = v },
                            true)
                        root.safeSetProp(item, "rowLabel", delegateRoot.model.rowLabel ?? "")
                        }
                    }
                }
            }
        }

    // ══════════════════════════════════════════════════════════════════════
    //  Structural delegates
    // ══════════════════════════════════════════════════════════════════════

    // ── line: horizontal separator (optionally with label text) ──
    // Unified: replaces both lineDelegate and colLineDelegate.
    Component {
        id: lineDelegate

        Item {
            width: parent ? parent.width : 0
            implicitHeight: lineLabel.text.length > 0 ? Math.max(lineLabel.implicitHeight, 8) + 4 : 8

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property string rowLabel
            property bool showLabel: true
            property bool bound: false
            property var boundComponents: ""
            property var setValue: function(v) {}

            Label {
                id: lineLabel
                text: parent.rowLabel
                font.bold: true
                color: Material.foreground
                opacity: 0.75
                anchors.left: parent.left
                anchors.leftMargin: lineLabel.text.length > 0 ? 8 : 0
                anchors.top: parent.top
                anchors.topMargin: lineLabel.text.length > 0 ? 2 : 0
                }

            Rectangle {
                anchors.left: lineLabel.text.length > 0 ? lineLabel.right : parent.left
                anchors.leftMargin: lineLabel.text.length > 0 ? 6 : 0
                anchors.right: parent.right
                anchors.verticalCenter: lineLabel.text.length > 0 ? lineLabel.verticalCenter : parent.verticalCenter
                height: 1
                color: Material.accentColor
                opacity: 0.3
                }
            }
        }

    // ── empty: placeholder that takes space but renders nothing ────
    // Unified: replaces both emptyDelegate and subEmptyDelegate.
    Component {
        id: emptyDelegate

        Item {
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0
            implicitHeight: 28

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property bool showLabel: true
            property bool bound: false
            property var boundComponents: ""
            property string rowLabel: ""
            property var setValue: function(v) {}
            }
        }

    // ── row: multiple sub-properties on one line ──────────────────────
    // Unified: replaces both rowDelegate and colRowDelegate.
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
                    Layout.preferredWidth: {
                        const count = rowContainer.subProps ? rowContainer.subProps.length : 1
                        if (count <= 1)
                            return -1
                        const gap = (count - 1) * rowContainer.spacing
                        return Math.max(0, (rowContainer.width - gap) / count)
                        }

                    required property string modelData
                    required property int index

                    property string subName: modelData
                    property var subMeta: root.metaForSub(rowContainer.propName, subName)
                    property var subValue: rowContainer.subValues ? rowContainer.subValues[index] : undefined

                    sourceComponent: {
                        if (subLoader.subName === "empty")
                            return emptyDelegate
                        const m = subLoader.subMeta
                        if (!m) return null
                        return root.delegateForType(m.type || "string")
                        }

                    onLoaded: {
                        if (!item)
                            return
                        root.setupSubDelegate(item,
                            subLoader.subName,
                            () => subLoader.subValue,
                            subLoader.subMeta,
                            function(v) {
                                if (rowContainer.setSubValue)
                                    rowContainer.setSubValue(subLoader.subName, v)
                                })
                        }
                    }
                }
            }
        }

    // ── columns: multi-column layout for multiple properties ────────
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
            property string _rowsKey: {
                const items = colsContainer.columnItems
                if (!items || !items.length)
                    return ""
                let key = ""
                for (let i = 0; i < items.length; ++i) {
                    const item = items[i]
                    key += (item.name || "") + "|" + (item.colSpan || 1) + "|"
                          + (item.isRow ? "1" : "0") + "|" + (item.isLine ? "1" : "0") + "|"
                          + (item.isEmpty ? "1" : "0") + "|" + (item.rowLabel || "") + ";"
                }
                return key
                }

            property var _structItems: []

            property var rows: {
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
                                    return lineDelegate
                                if (d.isEmpty)
                                    return emptyDelegate
                                if (d.isRow)
                                    return rowDelegate
                                const m = root.metaFor(d.name)
                                if (!m)
                                    return null
                                return root.delegateForType(m.type || "string")
                                }

                            onLoaded: {
                                const d = colLoader.itemData
                                if (!d || !item)
                                    return
                                if (d.isLine) {
                                    item.rowLabel = d.rowLabel || ""
                                    return
                                    }
                                if (d.isEmpty)
                                    return
                                if (d.isRow) {
                                    root.setupRowDelegate(item,
                                        "row",
                                        d.subProps,
                                        Qt.binding(() => {
                                            const ci = colsContainer.columnItems
                                            if (!ci || d._idx >= ci.length) return undefined
                                            return ci[d._idx].subValues
                                        }),
                                        d.rowLabel,
                                        colsContainer.propIndex,
                                        function(subName, v) {
                                            colsContainer.setSubValue(d, subName, v)
                                            })
                                    }
                                else {
                                    root.setupDelegate(item,
                                        d.name,
                                        () => {
                                            const ci = colsContainer.columnItems
                                            if (!ci || d._idx >= ci.length) return undefined
                                            return ci[d._idx].propValue
                                        },
                                        root.metaFor(d.name),
                                        colsContainer.propIndex,
                                        function(v) { colsContainer.setModelValue(d.name, v) },
                                        true)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

    // ══════════════════════════════════════════════════════════════════════
    //  Type delegates — unified top/sub
    //
    //  Every delegate uses the same unified interface:
    //    property string propName
    //    property var propValue
    //    property var meta
    //    property int propIndex
    //    property var setValue: function(v) {}
    //    property bool bound
    //    property var boundComponents
    //    property bool showLabel   (true=top, false=sub)
    //    property string rowLabel
    //
    //  When showLabel is true, a PropLabel is shown on the left.
    //  When false, subLabelText is used on the ValueBox instead.
    // ══════════════════════════════════════════════════════════════════════

    // ── bool / fontStyle: CheckBox ───────────────────────────────────
    Component {
        id: boolDelegate

        RowLayout {
            id: boolDel
            Layout.fillWidth: !showLabel
            width: parent ? parent.width : 0
            spacing: 2

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            PropLabel {
                visible: boolDel.showLabel
                text: boolDel.meta ? boolDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                unitText: boolDel.meta ? boolDel.meta.unit ?? "" : ""
                subLabelText: !boolDel.showLabel ? (boolDel.meta ? boolDel.meta.sublabel ?? boolDel.meta.label ?? "" : "") : ""

                // Custom styled label for fontStyle sub-properties
                Text {
                    visible: !boolDel.showLabel && boolDel.meta?.type === "fontStyle"
                    text: boolDel.meta ? boolDel.meta.sublabel ?? boolDel.meta.label ?? "" : ""
                    font.pixelSize: 13
                    font.bold: boolDel.propName === "bold"
                    font.italic: boolDel.propName === "italic"
                    font.underline: boolDel.propName === "underline"
                    color: "#333333"
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 4
                    anchors.bottomMargin: 1
                }

                CheckBox {
                    anchors.centerIn: parent
                    checked: boolDel.propValue === true
                    enabled: !boolDel.bound
                    onToggled: boolDel.setValue(checked)
                    }
                }
            }
        }

    // ── int: SpinBox ──────────────────────────────────────────────────
    Component {
        id: intDelegate

        RowLayout {
            id: intDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0
            spacing: 2

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            PropLabel {
                visible: intDel.showLabel
                text: intDel.meta ? intDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                unitText: intDel.meta ? intDel.meta.unit ?? "" : ""
                subLabelText: !intDel.showLabel ? (intDel.meta ? intDel.meta.sublabel ?? intDel.meta.label ?? "" : "") : ""

                BareSpinBox {
                    anchors.fill: parent
                    editable: !intDel.bound
                    boundColor: intDel.bound ? root._boundColor : ""
                    from: intDel.meta && intDel.meta.min !== undefined ? Math.round(intDel.meta.min) : -1000000
                    to:   intDel.meta && intDel.meta.max !== undefined ? Math.round(intDel.meta.max) : 1000000
                    resetValue: root.defaultScalarFromMeta(intDel.meta, 0)

                    property int modelValue: intDel.propValue !== undefined ? Number(intDel.propValue) : 0
                    value: modelValue
                    onModelValueChanged: if (value !== modelValue) value = modelValue

                    onValueChanged: {
                        if (intDel.propValue !== value)
                            intDel.setValue(value);
                        }
                    }
                }

            ScriptButton {
                visible: intDel.meta ? ((intDel.meta.scriptable === true) || (intDel.meta.script !== undefined && intDel.meta.script.length > 0)) : false
                propName: intDel.propName
                component: -1
                }
            }
        }

    // ── float: DoubleSpinBox ──────────────────────────────────────────
    Component {
        id: floatDelegate

        RowLayout {
            id: floatDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0
            spacing: 2

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            PropLabel {
                visible: floatDel.showLabel
                text: floatDel.meta ? floatDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                unitText: floatDel.meta ? floatDel.meta.unit ?? "" : ""
                subLabelText: !floatDel.showLabel ? (floatDel.meta ? floatDel.meta.sublabel ?? floatDel.meta.label ?? "" : "") : ""

                BareDoubleSpinBox {
                    anchors.fill: parent
                    editable: !floatDel.bound
                    boundColor: floatDel.bound ? root._boundColor : ""
                    from: floatDel.meta && floatDel.meta.min !== undefined ? floatDel.meta.min : -1000000.0
                    to:   floatDel.meta && floatDel.meta.max !== undefined ? floatDel.meta.max : 1000000.0
                    stepSize: root.defaultStepSize(floatDel.meta)
                    bigStep:  root.defaultBigStep(floatDel.meta)
                    minStep:  root.defaultMinStep(floatDel.meta)
                    resetValue: root.defaultScalarFromMeta(floatDel.meta, 0)
                    decimals: floatDel.meta && floatDel.meta.precision !== undefined ? floatDel.meta.precision : 2

                    property real modelValue: floatDel.propValue !== undefined ? Number(floatDel.propValue) : 0.0
                    value: modelValue
                    onModelValueChanged: if (value !== modelValue) value = modelValue

                    onValueChanged: {
                        if (floatDel.propValue !== value)
                            floatDel.setValue(value);
                        }
                    }
                }

            ScriptButton {
                visible: floatDel.meta ? ((floatDel.meta.scriptable === true) || (floatDel.meta.script !== undefined && floatDel.meta.script.length > 0)) : false
                propName: floatDel.propName
                component: -1
                }
            }
        }

    // ── vector: 2D / 3D DoubleSpinBox in ValueBoxes ────────────────────
    // Unified: replaces vector3dDelegate, vector2dDelegate,
    //          subVector3dDelegate, subVector2dDelegate.
    Component {
        id: vectorDelegate

        RowLayout {
            id: vecDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0
            spacing: 2

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            readonly property bool is2d: meta?.type === "vector2d" || meta?.type === "size"
            readonly property bool isSize: meta?.type === "size"

            function compLabel(index) {
                if (isSize)
                    return index === 0 ? qsTr("width") : qsTr("height")
                return ["X", "Y", "Z"][index]
                }

            PropLabel {
                visible: vecDel.showLabel
                text: vecDel.meta ? vecDel.meta.label ?? "" : ""
            }

            // ── Component 0 (X / width) ──
            VectorComponentBox {
                compLabel: vecDel.compLabel(0)
                compIndex: 0
                vectorValue: vecDel.propValue
                meta: vecDel.meta
                boundComponents: vecDel.boundComponents
                onComponentChange: v => {
                    var cur = root.model ? root.model.elementProperty(vecDel.propName) : vecDel.propValue
                    if (!cur) return
                    if (vecDel.is2d)
                        vecDel.setValue(Qt.vector2d(v, cur.y))
                    else
                        vecDel.setValue(Qt.vector3d(v, cur.y, cur.z))
                    }
                }

            // ── Component 1 (Y / height) ──
            VectorComponentBox {
                compLabel: vecDel.compLabel(1)
                compIndex: 1
                vectorValue: vecDel.propValue
                meta: vecDel.meta
                boundComponents: vecDel.boundComponents
                onComponentChange: v => {
                    var cur = root.model ? root.model.elementProperty(vecDel.propName) : vecDel.propValue
                    if (!cur) return
                    if (vecDel.is2d)
                        vecDel.setValue(Qt.vector2d(cur.x, v))
                    else
                        vecDel.setValue(Qt.vector3d(cur.x, v, cur.z))
                    }
                }

            // ── Component 2 (Z) ──
            VectorComponentBox {
                visible: !vecDel.is2d
                compLabel: vecDel.compLabel(2)
                compIndex: 2
                vectorValue: vecDel.propValue
                meta: vecDel.meta
                boundComponents: vecDel.boundComponents
                onComponentChange: v => {
                    var cur = root.model ? root.model.elementProperty(vecDel.propName) : vecDel.propValue
                    if (!cur) return
                    vecDel.setValue(Qt.vector3d(cur.x, cur.y, v))
                    }
                }

            // ── Single script button for the whole vector ──
            // The popup offers a component selector (All/X/Y/Z) so
            // individual components can be bound independently.
            ScriptButton {
                visible: vecDel.meta ? ((vecDel.meta.scriptable === true) || (vecDel.meta.script !== undefined && vecDel.meta.script.length > 0)) : false
                propName: vecDel.propName
                component: -1
                vectorMode: true
                vectorIs2d: vecDel.is2d
                boundComps: vecDel.boundComponents
                }
            }
        }

    // ── text: string / singleline / multiline TextInput ────────────────
    // Unified: replaces stringDelegate, singlelineDelegate, multilineDelegate,
    //          subStringDelegate, subSinglelineDelegate, subMultilineDelegate.
    Component {
        id: textDelegate

        RowLayout {
            id: textDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            readonly property bool isMultiline: meta?.type === "multiline"

            PropLabel {
                visible: textDel.showLabel
                alignTop: textDel.isMultiline
                text: textDel.meta ? textDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                implicitHeight: textDel.isMultiline ? 80 : 28
                unitText: textDel.meta ? textDel.meta.unit ?? "" : ""
                subLabelText: !textDel.showLabel ? (textDel.meta ? textDel.meta.sublabel ?? textDel.meta.label ?? "" : "") : ""

                // Multi-line: ScrollView + TextArea
                ScrollView {
                    visible: textDel.isMultiline
                    anchors.fill: parent
                    clip: true

                    TextArea {
                        id: multiText
                        text: textDel.propValue !== undefined ? textDel.propValue : ""
                        readOnly: textDel.bound
                        wrapMode: TextArea.Wrap
                        horizontalAlignment: {
                            if (textDel.showLabel) return TextInput.AlignLeft
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
                            if (!activeFocus && multiText._userEdited) {
                                textDel.setValue(text)
                                multiText._userEdited = false
                            }
                        }
                        onTextChanged: {
                            if (activeFocus)
                                multiText._userEdited = true
                        }
                        property bool _userEdited: false
                        color: textDel.bound ? root._boundColor : "#ffffff"
                        background: Item {}
                        padding: 2
                    }
                }

                // Single-line: TextInput
                TextInput {
                    visible: !textDel.isMultiline
                    anchors.fill: parent
                    text: textDel.propValue !== undefined ? textDel.propValue : ""
                    readOnly: textDel.bound
                    onEditingFinished: textDel.setValue(text)
                    horizontalAlignment: textDel.showLabel ? TextInput.AlignLeft : TextInput.AlignRight
                    verticalAlignment: TextInput.AlignVCenter
                    color: textDel.bound ? root._boundColor : "#ffffff"
                    clip: true
                    }
                }
            }
        }

    // ── color: ColorDialog swatch + hex input ─────────────────────────
    Component {
        id: colorDelegate

        RowLayout {
            id: colorDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0
            spacing: 2

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

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

            PropLabel {
                visible: colorDel.showLabel
                text: colorDel.meta ? colorDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                subLabelText: !colorDel.showLabel ? (colorDel.meta ? colorDel.meta.sublabel ?? colorDel.meta.label ?? "" : "") : ""

                RowLayout {
                    anchors.fill: parent
                    spacing: 2

                    Rectangle {
                        id: colorSwatch
                        Layout.preferredWidth: colorDel.showLabel ? 32 : 22
                        Layout.preferredHeight: colorDel.showLabel ? 28 : 22
                        radius: 3
                        color: colorDel.toColor(colorDel.propValue)
                        border.width: 1
                        border.color: Material.accentColor

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                colorDialog.selectedColor = colorDel.toColor(colorDel.propValue)
                                colorDialog.open()
                            }
                        }

                        ColorDialog {
                            id: colorDialog
                            options: ColorDialog.DontUseNativeDialog
                            onAccepted: colorDel.setValue(selectedColor)
                        }
                    }

                    TextInput {
                        id: hexInput
                        Layout.fillWidth: true
                        text: colorDel.toHex(colorDel.toColor(colorDel.propValue))
                        readOnly: colorDel.bound
                        font.family: "monospace"
                        font.bold: true
                        onEditingFinished: {
                            let c = colorDel.toColor(text)
                            if (c)
                                colorDel.setValue(c)
                        }
                        horizontalAlignment: TextInput.AlignRight
                        verticalAlignment: TextInput.AlignVCenter
                        color: colorDel.bound ? root._boundColor : "#ffffff"
                        clip: true

                        Connections {
                            target: colorDel
                            function onPropValueChanged() {
                                let c = colorDel.toColor(colorDel.propValue)
                                let h = colorDel.toHex(c)
                                if (hexInput.text.toLowerCase() !== h.toLowerCase())
                                    hexInput.text = h
                            }
                        }
                    }
                }
                }
            }
        }

    // ── halign: ComboBox for horizontal alignment ──────────────────────
    Component {
        id: halignDelegate

        RowLayout {
            id: halignDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

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

            PropLabel {
                visible: halignDel.showLabel
                text: halignDel.meta ? halignDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                subLabelAlignRight: !halignDel.showLabel
                subLabelText: !halignDel.showLabel ? (halignDel.meta ? halignDel.meta.sublabel ?? halignDel.meta.label ?? "" : "") : ""

                BareComboBox {
                    id: alignCombo
                    enabled: !halignDel.bound
                    anchors.fill: parent
                    model: halignDel.alignMap
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: halignDel.indexOfValue(halignDel.propValue)
                    textColor: halignDel.bound ? root._boundColor : "#ffffff"
                    onActivated: index => halignDel.setValue(halignDel.alignMap[index].value)
                    }
                }
            }
        }

    // ── font: FontFamilyButton (top-level only) ────────────────────────
    Component {
        id: fontDelegate

        RowLayout {
            id: fontDel
            width: parent ? parent.width : 0

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            PropLabel {
                visible: fontDel.showLabel
                text: fontDel.meta ? fontDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true

                FontFamilyButton {
                    anchors.fill: parent
                    enabled: !fontDel.bound
                    family: fontDel.propValue !== undefined ? fontDel.propValue : ""
                    onFamilySelected: fam => fontDel.setValue(fam)
                    }
                }
            }
        }

    // ── path: TextInput + folder button (top-level only) ──────────────
    Component {
        id: pathDelegate

        RowLayout {
            id: pathDel
            width: parent ? parent.width : 0
            spacing: 4

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            PropLabel {
                visible: pathDel.showLabel
                text: pathDel.meta ? pathDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true

                TextInput {
                    id: pathInput
                    anchors.fill: parent
                    text: pathDel.propValue !== undefined ? pathDel.propValue : ""
                    readOnly: pathDel.bound
                    onEditingFinished: pathDel.setValue(text)
                    horizontalAlignment: TextInput.AlignLeft
                    verticalAlignment: TextInput.AlignVCenter
                    color: pathDel.bound ? root._boundColor : "#ffffff"
                    clip: true

                    Connections {
                        target: pathDel
                        function onPropValueChanged() {
                            if (pathInput.activeFocus)
                                return
                            let v = pathDel.propValue !== undefined ? pathDel.propValue : ""
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
                        let cur = pathDel.propValue !== undefined ? pathDel.propValue : ""
                        if (cur.length > 0)
                            folderDialog.currentFolder = "file://" + ZCam.expandPath(cur)
                        folderDialog.open()
                    }
                }

                FolderDialog {
                    id: folderDialog
                    title: pathDel.meta ? pathDel.meta.label ?? qsTr("Select Directory") : qsTr("Select Directory")
                    onAccepted: {
                        let f = folderDialog.selectedFolder.toString()
                        if (f.startsWith("file://"))
                            f = f.substring(7)
                        pathInput.text = f
                        pathDel.setValue(f)
                    }
                }
            }
        }
    }

    // ── pointerCombo: ComboBox for pointer-type selection ─────────────
    // Unified: replaces layerDelegate, laserLayerDelegate, recipeDelegate,
    //          machineDelegate, and all their sub-variants.
    // Branches on meta.type to select the correct model/name functions.
    Component {
        id: pointerComboDelegate

        RowLayout {
            id: ptrDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0
            spacing: 2

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            readonly property string type: meta?.type ?? ""

            PropLabel {
                visible: ptrDel.showLabel
                text: ptrDel.meta ? ptrDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                subLabelAlignRight: !ptrDel.showLabel
                subLabelText: !ptrDel.showLabel ? (ptrDel.meta ? ptrDel.meta.sublabel ?? ptrDel.meta.label ?? "" : "") : ""

                RowLayout {
                    anchors.fill: parent
                    spacing: 2

                    BareComboBox {
                        id: ptrCombo
                        Layout.fillWidth: true

                        // Build model based on type
                        model: {
                            switch (ptrDel.type) {
                                case "layer":
                                    return root.model.layerNames ? root.model.layerNames() : []
                                case "laserLayer":
                                    return root.buildListWithPrefix(
                                        "(inherited)",
                                        root.model.laserLayerNames ? root.model.laserLayerNames() : [])
                                case "recipe":
                                    return root.model.recipeNames ? root.model.recipeNames() : []
                                case "machine":
                                    return root.model.machineNames ? root.model.machineNames() : []
                                default:
                                    return []
                                }
                            }

                        // Resolve current value to display name
                        displayTextOverride: {
                            if (ptrDel.type === "laserLayer") {
                                const name = root.resolvePointerName(
                                    root.model.laserLayerToName, ptrDel.propValue)
                                return name.length > 0 ? name : "(inherited)"
                                }
                            return root.resolvePointerName(
                                ptrDel.type === "layer"    ? root.model.layerToName :
                                ptrDel.type === "recipe"   ? root.model.recipeToName :
                                ptrDel.type === "machine"  ? root.model.machineToName :
                                null, ptrDel.propValue)
                            }

                        textColor: {
                            if (ptrDel.type === "laserLayer" && ptrCombo.displayTextOverride === "(inherited)")
                                return "#888888"
                            return "#ffffff"
                            }

                        currentIndex: {
                            let idx = ptrCombo.find(ptrCombo.displayTextOverride)
                            if (ptrDel.type === "laserLayer")
                                return idx >= 0 ? idx : 0
                            return idx >= 0 ? idx : -1
                            }

                        onActivated: index => {
                            if (ptrDel.type === "laserLayer") {
                                if (index === 0)
                                    ptrDel.setValue(null)
                                else {
                                    let name = ptrCombo.model[index]
                                    ptrDel.setValue(root.model.nameToLaserLayer ? root.model.nameToLaserLayer(name) : null)
                                    }
                                }
                            else {
                                let name = ptrCombo.model[index]
                                let ptr = null
                                switch (ptrDel.type) {
                                    case "layer":
                                        ptr = root.model.nameToLayer ? root.model.nameToLayer(name) : null
                                        break
                                    case "recipe":
                                        ptr = root.model.nameToRecipe ? root.model.nameToRecipe(name) : null
                                        break
                                    case "machine":
                                        ptr = root.model.nameToMachine ? root.model.nameToMachine(name) : null
                                        break
                                    }
                                ptrDel.setValue(ptr)
                                }
                            }
                        }

                    // Edit button for recipe type
                    ToolButton {
                        visible: ptrDel.type === "recipe"
                        Layout.preferredWidth: 28
                        enabled: ptrCombo.displayTextOverride !== ""
                        text: "✎"
                        font.pixelSize: 14
                        onClicked: ZCam.openRecipeEditor(ptrCombo.displayTextOverride)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Edit recipe")
                        background: Rectangle {
                            color: parent.hovered ? Material.color(Material.Teal, Material.Shade700)
                                   : (parent.enabled ? "#3a3a3a" : "transparent")
                            radius: 3
                            }
                        }
                    }
                }
            }
        }

    // ── stringCombo: ComboBox for string-type selection ───────────────
    // Unified: replaces machineNameDelegate, machineTypeDelegate,
    //          boardTypeDelegate, ethDeviceDelegate, and all sub-variants.
    // The value is a string that directly maps to a model entry.
    Component {
        id: stringComboDelegate

        RowLayout {
            id: strDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            readonly property string type: meta?.type ?? ""

            PropLabel {
                visible: strDel.showLabel
                text: strDel.meta ? strDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                subLabelAlignRight: !strDel.showLabel
                subLabelText: !strDel.showLabel ? (strDel.meta ? strDel.meta.sublabel ?? strDel.meta.label ?? "" : "") : ""

                BareComboBox {
                    id: strCombo
                    anchors.fill: parent

                    model: {
                        switch (strDel.type) {
                            case "machineName": return root.model.machineNames ? root.model.machineNames() : []
                            case "machineType": return root.model.machineTypes ? root.model.machineTypes() : []
                            case "boardType":   return root.model.boardTypes   ? root.model.boardTypes()   : []
                            case "ethDevice":   return root.model.ethDevices   ? root.model.ethDevices()   : []
                            default: return []
                            }
                        }

                    displayTextOverride: strDel.propValue !== undefined ? strDel.propValue : ""

                    currentIndex: {
                        let idx = strCombo.find(strCombo.displayTextOverride)
                        return idx >= 0 ? idx : -1
                        }

                    onActivated: index => strDel.setValue(strCombo.model[index])
                    }
                }
            }
        }

    // ── enumCombo: ComboBox for enum/int selection ────────────────────
    // Unified: replaces overrideDelegate, lineJoinDelegate, lineEndDelegate,
    //          framingTypeDelegate, and all sub-variants.
    // The value is an int index into the model list.
    Component {
        id: enumComboDelegate

        RowLayout {
            id: enumDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            readonly property string type: meta?.type ?? ""
            readonly property int defaultIdx: type === "framingType" ? 1 : 0

            PropLabel {
                visible: enumDel.showLabel
                text: enumDel.meta ? enumDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                subLabelAlignRight: !enumDel.showLabel
                subLabelText: !enumDel.showLabel ? (enumDel.meta ? enumDel.meta.sublabel ?? enumDel.meta.label ?? "" : "") : ""

                BareComboBox {
                    id: enumCombo
                    anchors.fill: parent

                    model: {
                        switch (enumDel.type) {
                            case "override":     return root.model.overrideTypeNames  ? root.model.overrideTypeNames()  : []
                            case "lineJoin":     return root.model.joinTypeNames      ? root.model.joinTypeNames()      : []
                            case "lineEnd":      return root.model.endTypeNames        ? root.model.endTypeNames()       : []
                            case "framingType":  return root.model.framingTypeNames    ? root.model.framingTypeNames()   : ["BoundingBox", "ConvexHull"]
                            default: return []
                            }
                        }

                    property int modelValue: enumDel.propValue !== undefined ? Number(enumDel.propValue) : enumDel.defaultIdx
                    currentIndex: {
                        if (enumCombo.modelValue >= 0 && enumCombo.modelValue < enumCombo.model.length)
                            return enumCombo.modelValue
                        return enumDel.defaultIdx
                        }

                    onActivated: index => enumDel.setValue(index)
                    }
                }
            }
        }

    // ── pulsewidth: ComboBox for pulseTable frequency selection ─────────
    Component {
        id: pulsewidthDelegate

        RowLayout {
            id: pwDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            function freqModel() {
                if (root.model.pulsewidthNames)
                    return root.model.pulsewidthNames()
                if (ZCam.project?.machine?.laserPulseList)
                    return ZCam.project.machine.laserPulseList()
                return []
                }

            PropLabel {
                visible: pwDel.showLabel
                text: pwDel.meta ? pwDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                unitText: pwDel.meta ? pwDel.meta.unit ?? "" : ""
                subLabelAlignRight: !pwDel.showLabel
                subLabelText: !pwDel.showLabel ? (pwDel.meta ? pwDel.meta.sublabel ?? pwDel.meta.label ?? "" : "") : ""

                BareComboBox {
                    id: freqCombo
                    anchors.fill: parent
                    model: pwDel.freqModel()

                    property string freqValue: pwDel.propValue !== undefined ? String(Math.round(Number(pwDel.propValue))) : ""
                    displayTextOverride: freqValue
                    currentIndex: {
                        let idx = freqCombo.find(freqCombo.freqValue)
                        return idx >= 0 ? idx : -1
                        }

                    onActivated: index => pwDel.setValue(Number(freqCombo.model[index]))
                    }
                }
            }
        }

    // ── lock: three CheckBoxes (Off / Lock / Square) ──────────────────
    // Unified: replaces lockScaleDelegate, lockSizeDelegate,
    //          subLockScaleDelegate, subLockSizeDelegate.
    Component {
        id: lockDelegate

        RowLayout {
            id: lockDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0
            spacing: 4

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            readonly property string type: meta?.type ?? ""
            readonly property var modeNames: {
                if (type === "lockSize")
                    return root.model.lockSizeNames ? root.model.lockSizeNames() : ["Off", "Lock", "Square"]
                return root.model.lockScaleNames ? root.model.lockScaleNames() : ["Off", "Lock", "Square"]
                }

            PropLabel {
                visible: lockDel.showLabel
                text: lockDel.meta ? lockDel.meta.label ?? "" : ""
            }

            Repeater {
                model: lockDel.modeNames

                delegate: ValueBox {
                    required property string modelData
                    required property int index

                    Layout.fillWidth: true
                    subLabelText: modelData

                    LockCheckBox {
                        modeIndex: index
                        modeValue: lockDel.propValue
                        onActivated: idx => lockDel.setValue(idx)
                    }
                    }
                }
            }
        }

    // ── cameraCombo: ComboBox for camera-related string selection ─────
    // Unified: replaces cameraNameDelegate, cameraResolutionDelegate,
    //          cameraFrameRateDelegate, and all sub-variants.
    Component {
        id: cameraComboDelegate

        RowLayout {
            id: camDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            readonly property string type: meta?.type ?? ""
            property var camElement: root.model.element

            PropLabel {
                visible: camDel.showLabel
                text: camDel.meta ? camDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                subLabelAlignRight: !camDel.showLabel
                subLabelText: !camDel.showLabel ? (camDel.meta ? camDel.meta.sublabel ?? camDel.meta.label ?? "" : "") : ""

                BareComboBox {
                    id: camCombo
                    anchors.fill: parent

                    model: {
                        switch (camDel.type) {
                            case "cameraName":
                                return root.buildListWithPrefix(
                                    "(none)",
                                    root.model.cameraNames ? root.model.cameraNames() : [])
                            case "cameraResolution":
                                var resList = ["(default)"]
                                if (camDel.camElement && camDel.camElement.resolutionNames)
                                    resList = resList.concat(camDel.camElement.resolutionNames())
                                return resList
                            case "cameraFrameRate":
                                var frList = ["(default)"]
                                if (camDel.camElement && camDel.camElement.frameRateNames)
                                    frList = frList.concat(camDel.camElement.frameRateNames())
                                return frList
                            default:
                                return []
                            }
                        }

                    property string currentName: {
                        if (camDel.type === "cameraName") {
                            if (!camDel.propValue || camDel.propValue === null)
                                return "(none)"
                            const s = String(camDel.propValue)
                            return s.length > 0 ? s : "(none)"
                            }
                        // cameraResolution / cameraFrameRate
                        return camDel.propValue !== undefined ? String(camDel.propValue) : ""
                        }

                    displayTextOverride: {
                        if (camCombo.currentName.length === 0)
                            return "(default)"
                        return camCombo.currentName
                        }

                    textColor: {
                        if (camCombo.currentName === "(none)" || camCombo.currentName === "(default)" || camCombo.currentName.length === 0)
                            return "#888888"
                        return "#ffffff"
                        }

                    currentIndex: {
                        let idx = camCombo.find(camCombo.currentName)
                        return idx >= 0 ? idx : 0
                        }

                    onActivated: index => {
                        if (index === 0)
                            camDel.setValue("")
                        else
                            camDel.setValue(camCombo.model[index])
                        }
                    }
                }
            }
        }

    // ── cameraView: live camera image, zoomable/pannable (top only) ──
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
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

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

    // ── cameraCapture: button that adopts the live 3D canvas camera ──
    // Unified: replaces cameraCaptureDelegate and subCameraCaptureDelegate.
    Component {
        id: cameraCaptureDelegate

        RowLayout {
            id: capDel
            Layout.fillWidth: !showLabel
            width: showLabel && parent ? parent.width : 0

            property string propName
            property var propValue
            property var meta
            property int propIndex
            property var setValue: function(v) {}
            property bool bound: false
            property var boundComponents: ""
            property bool showLabel: true
            property string rowLabel: ""

            property var camElement: (ZCam.project && ZCam.project.cam) ? ZCam.project.cam : null

            PropLabel {
                visible: capDel.showLabel
                text: capDel.meta ? capDel.meta.label ?? "" : ""
            }

            ValueBox {
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                subLabelText: !capDel.showLabel ? (capDel.meta ? capDel.meta.sublabel ?? capDel.meta.label ?? "" : "") : ""

                Button {
                    anchors.centerIn: parent
                    enabled: capDel.camElement !== null
                    flat: true
                    text: {
                        if (!capDel.camElement)
                            return capDel.showLabel ? qsTr("No Cam") : qsTr("No Cam")
                        return capDel.showLabel ? qsTr("Grab Camera View") : qsTr("Grab")
                        }
                    onClicked: {
                        if (capDel.camElement)
                            capDel.camElement.grabCameraView()
                        }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Adopt the current 3D canvas camera as the projection viewpoint")
                    ToolTip.delay: 800
                    ToolTip.timeout: 4000
                    }
                }
            }
        }
    }