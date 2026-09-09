//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ZCam

// ── JavaScript panel ─────────────────────────────────────────────────────────
// Named-script workbench, organised like the AI sessions (AiPanel.qml):
//   • toolbar — "JS" badge, a ComboBox listing the saved scripts (bound
//               to the scriptList / currentScript Q_PROPERTYs), a "+" button
//               to create a new script and a "−" button to delete the current
//               one.
//   • top — the engine's console output (read-only, auto-scrolls to the
//               end), fed by runScript() results and by print() calls.
//   • bottom — the editor for the *currently selected* script (auto-saved).
// Running a script is done with Ctrl+Return (consistent with the AI panel).
// Scripts are persisted by C++ (ScriptEngine) as one .js file each in
// ~/ZCam/scripts, mirroring the AI sessions in ~/ZCam/ai_sessions.
// Script names are auto-generated like sessions: Script-yy-MM-dd-n.js
// (vs Session-yy-MM-dd-n.json for AI sessions).

Item {
    id: scriptPanel

    Material.theme: Material.Dark

    property var se: ZCam.scriptEngine
    property string scriptOutput: ""
    property string scriptError: ""

    // Raised only when the new text is a strict extension of the old text
    // (a real append); the output area reacts to that flag and jumps to the
    // end.  A replace does not raise it.
    property bool outputScrollRequest: false

    //======================================================================================================
    //     appendOutput
    //======================================================================================================
    //   Append a line to the console output (accumulates across runs,
    //   console-style).  Used both for the runScript() result and for
    //   JavaScript print() calls (via the scriptPrinted signal below).
    function appendOutput(text) {
        if (text.length === 0)
            return;
        var oldLen = scriptOutput.length;
        var oldText = scriptOutput;
        scriptOutput = oldText.length > 0 ? oldText + "\n" + text : text;
        // Only auto-scroll for a genuine append (so a replace does not drag
        // the view down).
        outputScrollRequest = oldLen > 0 && scriptOutput.startsWith(oldText);
    }

    //======================================================================================================
    //     saveCurrent
    //======================================================================================================
    //   Persist whatever is in the editor to the currently active script
    //   (a no-op when no script is selected yet).
    function saveCurrent() {
        if (!se || se.currentScriptName === "")
            return;
        // currentScriptName is the file name (e.g. "Script-25-06-10-1.js");
        // saveScript expects a display name (without "Script-" prefix).
        var name = se.currentScriptName;
        if (name.startsWith("Script-"))
            name = name.substring(7);
        if (name.endsWith(".js"))
            name = name.substring(0, name.length - 3);
        se.saveScript(name, editor.text);
    }

    //======================================================================================================
    //     runScript
    //======================================================================================================
    //   Evaluate the script currently in the editor (Ctrl+Return).
    function runScript() {
        var code = editor.text.trim();
        if (code.length === 0)
            return;
        var result = se.evalImperativeQml(code);
        if (result.ok) {
            scriptError = "";
            var val = result.value;
            if (val === undefined || val === null)
                appendOutput("→ undefined");
            else if (typeof val === "object")
                appendOutput("→ " + JSON.stringify(val, null, 2));
            else
                appendOutput("→ " + String(val));
        } else {
            scriptError = result.error;
            appendOutput("✗ " + result.error);
        }
    }

    // Load the script list on startup and select the first script if any.
    Component.onCompleted: {
        se.scriptNames();   // populates scriptList
        if (se.scriptList.length > 0)
            se.selectScript(0);
    }

    // JavaScript print() → append to the console output area.
    Connections {
        target: se
        function onScriptPrinted(msg) {
            scriptPanel.appendOutput(msg);
        }
    }

    // When the current script changes (selection, new, delete), reload
    // the editor content from disk.
    Connections {
        target: se
        function onCurrentScriptNameChanged() {
            var name = se.currentScriptName;
            if (name === "") {
                editor.text = "";
                return;
            }
            // scriptText expects a display name (without "Script-" prefix).
            var disp = name;
            if (disp.startsWith("Script-"))
                disp = disp.substring(7);
            if (disp.endsWith(".js"))
                disp = disp.substring(0, disp.length - 3);
            editor.text = se.scriptText(disp);
            scriptOutput = "";
            scriptError  = "";
        }
    }

    // Debounced auto-save: persist the editor content to the active script
    // shortly after the user stops typing.
    Timer {
        id: saveTimer
        interval: 800
        repeat: false
        onTriggered: scriptPanel.saveCurrent()
    }

    Rectangle {
        anchors.fill: parent
        color: Material.color(Material.BlueGrey, Material.Shade900)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        // ── Toolbar ─────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            // "JS" badge — white text on amber, styled like the toolbar Script
            // button.  Sized to Material.touchTarget so it matches the "JS"
            // icon in the app header exactly.
            Label {
                text: qsTr("JS")
                font.bold: true
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                Layout.preferredWidth: Material.touchTarget
                Layout.preferredHeight: Material.touchTarget
                background: Rectangle {
                    color: Material.color(Material.Amber, Material.Shade700)
                    radius: 4
                }
                HoverHandler { id: jsBadgeHover }
                ToolTip.visible: jsBadgeHover.hovered
                ToolTip.text: qsTr("JavaScript Console")
            }

            // ── Script selector ──────────────────────────────────────────────
            //   The available scripts are shown as a ComboBox bound to the
            //   ScriptEngine's scriptList / currentScript Q_PROPERTYs
            //   (same pattern as AiPanel.qml's session selector).
            ComboBox {
                id: scriptComboBox
                Layout.fillWidth: true
                Layout.preferredHeight: Material.touchTarget
                model: se ? se.scriptList : []
                currentIndex: se ? se.currentScript : -1
                displayText: currentText.length > 0 ? currentText : qsTr("Script…")

                onActivated: function (index) {
                    if (index < 0)
                        return;
                    scriptPanel.saveCurrent();
                    se.selectScript(index);
                }
            }

            // ── "+" — new script ────────────────────────────────────────────
            Button {
                text: "+"
                display: AbstractButton.TextOnly
                Layout.preferredWidth: 36
                Layout.preferredHeight: Material.touchTarget
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.pointSize: 16
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Create a new script")
                onClicked: {
                    scriptPanel.saveCurrent();
                    se.newScript();
                }
            }

            // ── "−" — delete the current script ─────────────────────────────
            Button {
                text: "−"
                display: AbstractButton.TextOnly
                enabled: se && se.currentScript >= 0
                Layout.preferredWidth: 36
                Layout.preferredHeight: Material.touchTarget
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? "white" : Material.color(Material.Grey, Material.Shade600)
                    font.pointSize: 16
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Delete the current script")
                onClicked: se.deleteScriptByIndex(se.currentScript)
            }
        }

        // ── Split: top = engine output, bottom = script editor ────────────────
        // A vertical SplitView gives two flickable halves divided by a
        // draggable handle.  Each half shows a scrollbar only when its
        // content overflows (ScrollBar.AsNeeded).  The halves are direct
        // children of the SplitView (sized via SplitView.* attached props),
        // the same idiom used in MainPanel.qml.
        SplitView {
            id: split
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            background: null
            handle: Rectangle {
                implicitHeight: 8
                color: Material.color(Material.Grey, Material.Shade500)
            }

            // ── Top: engine output (read-only console) ─────────────────────
            ScrollView {
                id: outputScroll
                SplitView.fillHeight: true
                SplitView.minimumHeight: 60
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                background: Rectangle {
                    color: Material.color(Material.BlueGrey, Material.Shade900)
                    radius: 3
                    border.color: Material.color(Material.Grey, Material.Shade600)
                    border.width: 1
                }

                TextArea {
                    id: outputArea
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    textFormat: TextEdit.PlainText
                    color: scriptError.length > 0 ? "#FF6B6B" : "#7FE57F"
                    text: scriptOutput
                    background: null
                    padding: 8

                    // Whenever the engine appends output, jump to the very
                    // end of the console.  Guarded by the scroll-request flag
                    // so a replace doesn't force it.
                    Connections {
                        target: scriptPanel
                        function onOutputScrollRequestChanged() {
                            if (!scriptPanel.outputScrollRequest)
                                return;
                            Qt.callLater(function() {
                                if (outputScroll.flickableItem)
                                    outputScroll.flickableItem.contentY
                                        = outputScroll.flickableItem.contentHeight;
                            });
                            scriptPanel.outputScrollRequest = false;
                        }
                    }
                }
            }

            // ── Bottom: script editor (editable) ────────────────────────────
            ScrollView {
                id: editorScroll
                SplitView.preferredHeight: 180
                SplitView.minimumHeight: 120
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                background: Rectangle {
                    color: Material.color(Material.BlueGrey, Material.Shade900)
                    radius: 3
                    border.color: Material.color(Material.Grey, Material.Shade600)
                    border.width: 1
                }

                TextArea {
                    id: editor
                    placeholderText: qsTr("// JavaScript code — use zcam.*, geom.*, project.*, config.*\n// Ctrl+Return to run\n\nzcam.createElement('rectangle', 10, 20);")
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    textFormat: TextEdit.PlainText
                    color: "white"
                    background: null
                    padding: 8

                    // Follow the cursor while typing at the end (the standard
                    // "stay on the current line" behaviour).
                    onCursorPositionChanged: {
                        if (cursorPosition >= length && length > 0)
                            Qt.callLater(function() {
                                if (editorScroll.flickableItem)
                                    editorScroll.flickableItem.contentY
                                        = editorScroll.flickableItem.contentHeight;
                            });
                    }

                    // Any edit → debounced auto-save to the active script.
                    onTextChanged: saveTimer.restart()

                    Keys.onPressed: function (event) {
                        if ((event.modifiers & Qt.ControlModifier) &&
                            (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                            event.accepted = true;
                            runScript();
                        }
                    }
                }
            }
        }

    }
}