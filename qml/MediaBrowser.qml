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

Rectangle {
    id: root
    color: Material.color(Material.BlueGrey, Material.Shade800)

    // Persistent splitter: which panel is active (0=Fonts, 1=Artwork)
    property int activePanel: 0

    Settings {
        id: mediaSettings
        category: "MediaBrowser"
        property int activePanel: 0
        property real tileScale: 1.0
        }
    Component.onCompleted: {
        activePanel = mediaSettings.activePanel
        }
    onActivePanelChanged: mediaSettings.activePanel = activePanel

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Title bar with panel switch buttons ───────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Material.color(Material.BlueGrey, Material.Shade900)

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                Button {
                    text: qsTr("Fonts")
                    flat: true
                    checkable: true
                    checked: root.activePanel === 0
                    onClicked: root.activePanel = 0
                    Material.foreground: checked ? Material.accentColor : Material.foreground
                    }
                Button {
                    text: qsTr("Artwork")
                    flat: true
                    checkable: true
                    checked: root.activePanel === 1
                    onClicked: root.activePanel = 1
                    Material.foreground: checked ? Material.accentColor : Material.foreground
                    }

                Item { Layout.fillWidth: true }
                }
            }

        // ── Panel stack ───────────────────────────────────────────────────
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.activePanel

            MediaFontsPanel {
                id: fontsPanel
                }
            MediaArtworkPanel {
                id: artworkPanel
                tileScale: mediaSettings.tileScale
                onTileScaleChanged: mediaSettings.tileScale = tileScale
                }
            }
        }
    }