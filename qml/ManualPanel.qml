//=============================================================================
//  ZCam
//
//  Copyright (C) 2026 Werner Schweer
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
import QtWebEngine

// ── Manual panel ─────────────────────────────────────────────────────────────
// Displays the MkDocs-generated HTML manual inside a WebEngineView.
// The HTML is embedded via the Qt resource system under "qrc:/manual/".

Item {
    id: root

    // Current language — "de" (default) or "en".
    property string currentLang: "de"

    Rectangle {
        anchors.fill: parent
        color: Material.color(Material.BlueGrey, Material.Shade900)
    }

    // ── Language switcher ─────────────────────────────────────────────────────
    RowLayout {
        id: langBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 4
        spacing: 4
        z: 2

        Label {
            text: qsTr("Manual")
            font.bold: true
            color: Material.accentColor
            Layout.alignment: Qt.AlignVCenter
        }

        Item { Layout.fillWidth: true }

        Button {
            text: "Deutsch"
            checkable: true
            checked: root.currentLang === "de"
            flat: true
            onClicked: root.currentLang = "de"
        }
        Button {
            text: "English"
            checkable: true
            checked: root.currentLang === "en"
            flat: true
            onClicked: root.currentLang = "en"
        }
    }

    // ── WebEngine view ───────────────────────────────────────────────────────
    //   The url is recalculated whenever the language changes.
    //   For "de" the root index.html is loaded; for "en" the /en/ subdirectory.
    //
    //   The MkDocs readthedocs theme generates links like href="manual/"
    //   (directory URLs).  The Qt resource system cannot resolve directory
    //   URLs, so we intercept navigation requests and rewrite them to
    //   append "index.html".
    WebEngineView {
        id: webView
        anchors.top: langBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: 0

        url: root.currentLang === "de"
             ? "qrc:/manual/index.html"
             : "qrc:/manual/en/index.html"

        onNavigationRequested: function(request) {
            var u = request.url.toString()

            // Reject external http/https links
            if (u.startsWith("http://") || u.startsWith("https://")) {
                request.reject()
                return
            }

            // Rewrite directory URLs: qrc:/manual/foo/ → qrc:/manual/foo/index.html
            // The Qt resource system cannot resolve directory paths.
            if (u.startsWith("qrc:") && u.endsWith("/")) {
                request.redirect(Qt.resolvedUrl("index.html", request.url))
                return
            }

            request.accept()
        }
    }
}