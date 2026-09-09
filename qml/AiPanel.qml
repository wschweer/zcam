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

// ── AI Panel ─────────────────────────────────────────────────────────────────
// Chat interface for the LLM-powered AI agent.
// Upper half: AI output / conversation log (scrollable).
// Lower half: user input field with send button.
// A toolbar at the top provides session management (new, select, delete)
// and shows the current Ollama model.
//
// The AIAgent is owned by ZCam and exposed as ZCam.aiAgent.  Methods are
// called via Q_INVOKABLE wrappers on ZCam (ZCam.aiSendMessage etc.) to
// avoid QML type-resolution issues with the AIAgent* pointer property.

Item {
    id: root

    Material.theme: Material.Dark

    property var ai: ZCam.aiAgent

    //======================================================================================================
    //     send
    //======================================================================================================
    //   Submits the current prompt.  Called by the Ctrl+Return shortcut
    //   (there is no dedicated "Send" button anymore).  Does nothing while
    //   the agent is busy or when the prompt is empty.
    function send() {
        if (ai && ai.busy)
            return
        var msg = inputField.text.trim()
        if (msg.length === 0)
            return
        conversationArea.appendUserText("You: " + msg + "\n\n")
        inputField.text = ""
        ZCam.aiSendMessage(msg)
    }

    Rectangle {
        anchors.fill: parent
        color: Material.color(Material.BlueGrey, Material.Shade900)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

    // ── Toolbar ──────────────────────────────────────────────────────────────
    RowLayout {
        id: toolbar
        Layout.fillWidth: true
        spacing: 4

/*        Label {
            text: qsTr("AI Assistant")
            font.bold: true
            color: Material.accentColor
            Layout.alignment: Qt.AlignVCenter
        }
  */
        // "AI" badge — white text on teal, styled like the toolbar AI button
        // (square Material.touchTarget so it reads as an icon).
        Label {
            text: qsTr("AI")
            font.bold: true
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            Layout.preferredWidth: Material.touchTarget
            Layout.preferredHeight: Material.touchTarget
            background: Rectangle {
                color: Material.color(Material.Teal, Material.Shade700)
                radius: 4
                }
            HoverHandler { id: aiBadgeHover }
            ToolTip.visible: aiBadgeHover.hovered
            ToolTip.text: qsTr("AI Assistant — ") + (ai && ai.ollamaModel ? ai.ollamaModel : "?")
        }

        // Session selector
        ComboBox {
            id: sessionCombo
            Layout.fillWidth: true
            Layout.preferredHeight: Material.touchTarget
            model: ai ? ai.sessionList : []
            currentIndex: ai ? ai.currentSession : -1
            displayText: currentText.length > 0 ? currentText : qsTr("(new session)")
            onActivated: ZCam.aiSelectSession(index)
        }

        Button {
            text: qsTr("+")
            display: AbstractButton.TextOnly
            enabled: ai && !ai.busy
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
            ToolTip.text: qsTr("Create a new session")
            onClicked: ZCam.aiNewSession()
        }

        Button {
            text: qsTr("−")
            display: AbstractButton.TextOnly
            enabled: ai && !ai.busy && sessionCombo.currentIndex >= 0
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
            ToolTip.text: qsTr("Delete the current session")
            onClicked: ZCam.aiDeleteSession(sessionCombo.currentIndex)
        }
    }

    // ── Conversation view ─────────────────────────────────────────────────────
    ScrollView {
        id: conversationScroll
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        background: Rectangle {
            color: Material.color(Material.BlueGrey, Material.Shade900)
            radius: 3
            border.color: Material.color(Material.Grey, Material.Shade600)
            border.width: 1
        }

        TextArea {
            id: conversationArea
            readOnly: true
            wrapMode: TextArea.Wrap
            color: Material.foreground
            textFormat: TextEdit.RichText
            background: null
            // Ensure the text area fills the ScrollView viewport width so
            // word-wrap uses the full available width rather than the
            // content's implicit (unwrapped) width.
            width: conversationScroll.width
            padding: 8

            // ── Markdown-to-HTML converter ──────────────────────────────
            // Converts a subset of Markdown to HTML suitable for Qt's
            // RichText engine.  Input must already be HTML-escaped.
            function markdownToHtml(md) {
                var html = md
                // Headings: # / ## / ###
                html = html.replace(/^### (.+)$/gm, '<b>$1</b><br>')
                html = html.replace(/^## (.+)$/gm, '<b><font size="+1">$1</font></b><br>')
                html = html.replace(/^# (.+)$/gm, '<b><font size="+2">$1</font></b><br>')
                // Bold and italic (bold first so it doesn't conflict)
                html = html.replace(/\*\*(.+?)\*\*/g, '<b>$1</b>')
                html = html.replace(/__(.+?)__/g, '<b>$1</b>')
                html = html.replace(/\*(.+?)\*/g, '<i>$1</i>')
                html = html.replace(/_(.+?)_/g, '<i>$1</i>')
                // Inline code
                html = html.replace(/`(.+?)`/g, '<tt>$1</tt>')
                // Links: [text](url)
                html = html.replace(/\[(.+?)\]\((.+?)\)/g, '<a href="$2">$1</a>')
                // Bullet lists: lines starting with - or *
                html = html.replace(/(?:^|<br>)([-*] .+?)(?=<br>|$)/g, function(match) {
                    var items = match.replace(/(^|<br>)[-*] /g, '<br>')
                    return '<ul>' + items.replace(/^<br>/, '')
                              .split('<br>').filter(function(s) { return s.length > 0 })
                              .map(function(s) { return '<li>' + s + '</li>' }).join('')
                           + '</ul>'
                })
                // Paragraph breaks: double newline → <br><br> (already <br> from escape)
                // Single newlines → <br> (already done by escape step)
                return html
            }

            // Accumulated HTML content for finished segments (user text,
            // tool results, completed AI messages).
            property string htmlContent: ""
            // Raw markdown buffer for the AI message currently streaming.
            property string aiRawBuffer: ""
            // Length of htmlContent when the current AI message started.
            property int aiHtmlStart: 0

            function flushAiBuffer() {
                if (aiRawBuffer.length === 0)
                    return
                var escaped = aiRawBuffer.replace(/&/g, "&amp;")
                                 .replace(/</g, "&lt;")
                                 .replace(/>/g, "&gt;")
                                 .replace(/\n/g, "<br>")
                var mdHtml = markdownToHtml(escaped)
                htmlContent += mdHtml
                aiRawBuffer = ""
                aiHtmlStart = htmlContent.length
            }

            function appendText(txt) {
                // AI streamed text: accumulate raw markdown, re-render the
                // current AI block on each chunk so partial formatting is
                // replaced as complete markdown arrives.
                aiRawBuffer += txt
                var escaped = aiRawBuffer.replace(/&/g, "&amp;")
                                 .replace(/</g, "&lt;")
                                 .replace(/>/g, "&gt;")
                                 .replace(/\n/g, "<br>")
                var mdHtml = markdownToHtml(escaped)
                text = htmlContent.substring(0, aiHtmlStart) + mdHtml
                cursorPosition = length
            }

            function appendUserText(txt) {
                flushAiBuffer()
                // The "You:" prefix is styled yellow; the rest is escaped HTML.
                var colonIdx = txt.indexOf(": ")
                var prefix = ""
                var rest = txt
                if (colonIdx >= 0) {
                    prefix = txt.substring(0, colonIdx + 1)
                    rest = txt.substring(colonIdx + 1)
                }
                var escaped = rest.replace(/&/g, "&amp;")
                                 .replace(/</g, "&lt;")
                                 .replace(/>/g, "&gt;")
                                 .replace(/\n/g, "<br>")
                htmlContent += "<span style=\"color:#FFD54F\">" + prefix + "</span>" + escaped
                aiHtmlStart = htmlContent.length
                text = htmlContent
                cursorPosition = length
            }

            function appendPlainText(txt) {
                flushAiBuffer()
                var escaped = txt.replace(/&/g, "&amp;")
                                 .replace(/</g, "&lt;")
                                 .replace(/>/g, "&gt;")
                                 .replace(/\n/g, "<br>")
                htmlContent += '<span style="color:#80CBC4">' + escaped + '</span>'
                aiHtmlStart = htmlContent.length
                text = htmlContent
                cursorPosition = length
            }

            function clearText() {
                htmlContent = ""
                aiRawBuffer = ""
                aiHtmlStart = 0
                text = ""
            }
        }

        onContentHeightChanged: {
            conversationScroll.contentItem.contentY = Math.max(0, conversationScroll.contentHeight - conversationScroll.height)
        }
    }

    // ── Input area ─────────────────────────────────────────────────────────
    //   Multi-line prompt editor (grows from 4 lines upward).  The prompt
    //   is submitted with Ctrl+Return; there is no persistent Send button.
    //   A Stop button appears only while the agent is busy so a running
    //   request can still be cancelled.
    TextArea {
        id: inputField
        Layout.fillWidth: true
        // At least 4 lines tall (grows with content).  ~1.4x the pixel
        // size per line plus padding, so the minimum height is robust
        // across font sizes.
        Layout.minimumHeight: 4 * Math.ceil(font.pixelSize * 1.4) + 16
        Layout.maximumHeight: 280
        wrapMode: TextArea.Wrap
        selectByMouse: true
        background: Rectangle {
            color: Material.color(Material.BlueGrey, Material.Shade900)
            radius: 3
            border.color: Material.color(Material.Grey, Material.Shade600)
            border.width: 1
        }
//            placeholderText: qsTr("Ask the AI to create, modify, or inspect elements…\n(Ctrl+Return to send)")
        placeholderText: qsTr("(Ctrl+Return to send)")
        enabled: !ai || !ai.busy

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Return && (event.modifiers & Qt.ControlModifier)) {
                send()
                event.accepted = true
            }
        }
    }

    // ── Status / Stop row ─────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }

/*            Label {
            text: qsTr("Ctrl+Return to send")
            font.italic: true
            color: Material.color(Material.Grey, Material.Shade400)
            visible: !ai || !ai.busy
            Layout.alignment: Qt.AlignVCenter
        }
  */
        Button {
            id: stopButton
            text: qsTr("Stop")
            highlighted: true
            Material.accent: Material.Orange
            visible: ai && ai.busy
            Layout.alignment: Qt.AlignVCenter
            onClicked: ZCam.aiStop()
        }
    }
    }

    // ── Error banner ──────────────────────────────────────────────────────────
    //   A sibling of the ColumnLayout above (NOT a layout child): anchors on
    //   layout-managed items are undefined behaviour and trigger a QML
    //   warning.  As a plain child of root it overlays the bottom edge of
    //   the panel (over the input area, z: 10) without consuming layout
    //   space.
    Rectangle {
        id: errorBanner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 0
        color: Material.color(Material.Red, Material.Shade700)
        visible: height > 0
        z: 10

        Label {
            anchors.fill: parent
            anchors.margins: 4
            id: errorLabel
            color: "white"
            wrapMode: Text.Wrap
            font.bold: true
        }

        Behavior on height { NumberAnimation { duration: 200 } }
    }

    // ── Signal connections ────────────────────────────────────────────────────
    Connections {
        target: ai

        function onChunkReceived(thought, text) {
            if (text && text.length > 0) {
                // Tool result text starts with \n[ - use plain text mode.
                // AI streamed content does not start with \n[.
                if (text.charAt(0) === "\n" && text.trim().charAt(0) === "[")
                    conversationArea.appendPlainText(text)
                else
                    conversationArea.appendText(text)
            }
        }

        function onFinished(fullText) {
            conversationArea.appendText("\n")
            conversationArea.flushAiBuffer()
        }

        function onAgentError(message) {
            errorLabel.text = message
            errorBanner.height = 28
            errorHideTimer.start()
        }

        // ── Session rebuild ─────────────────────────────────────────
        // When a session is loaded (selectSession) or cleared (newSession)
        // the agent emits sessionLoaded().  We rebuild the conversation
        // view from the agent's history via sessionConversation().
        function onSessionLoaded() {
            conversationArea.clearText()
            if (!ai)
                return
            var conv = ai.sessionConversation()
            for (var i = 0; i < conv.length; ++i) {
                var entry = conv[i]
                var role = entry.role
                var text = entry.text
                if (role === "user") {
                    conversationArea.appendUserText("You: " + text + "\n\n")
                }
                else if (role === "assistant") {
                    conversationArea.appendText(text)
                    conversationArea.flushAiBuffer()
                    conversationArea.appendText("\n\n")
                }
                else if (role === "tool") {
                    conversationArea.appendPlainText("\n" + text + "\n\n")
                }
            }
        }
    }

    Timer {
        id: errorHideTimer
        interval: 5000
        repeat: false
        onTriggered: errorBanner.height = 0
    }
}