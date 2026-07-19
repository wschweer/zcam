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

    // Public API so callers can switch to the Fonts panel and optionally
    // pre-select a font family.
    function showFontsPanel(family) {
        root.activePanel = 0
        if (family !== undefined && family.length > 0) {
            fontModel.currentFamily = family
            var idx = fontModel.allFamilies().indexOf(family)
            if (idx >= 0) {
                fontList.currentIndex = idx
                fontList.positionViewAtIndex(idx, ListView.Contain)
                }
            }
        }

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

        // Title bar with panel switch buttons
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Material.color(Material.BlueGrey, Material.Shade900)

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 4
                spacing: 4

                Button {
                    text: qsTr("Fonts")
                    flat: true
                    checkable: true
                    checked: root.activePanel === 0
                    onClicked: root.activePanel = 0
                    Material.foreground: checked ? Material.accentColor : Material.foreground
                    topPadding: 0
                    bottomPadding: 0
                    }
                Button {
                    text: qsTr("Artwork")
                    flat: true
                    checkable: true
                    checked: root.activePanel === 1
                    onClicked: root.activePanel = 1
                    Material.foreground: checked ? Material.accentColor : Material.foreground
                    topPadding: 0
                    bottomPadding: 0
                    }
                }
            }

        // Panel stack
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.activePanel

            MediaFontsPanel {
                id: fontsPanel
                onApplyFontRequested: family => ZCam.applyFontToCurrentText(family)
                }
            MediaArtworkPanel {
                id: artworkPanel
                tileScale: mediaSettings.tileScale
                onTileScaleChanged: mediaSettings.tileScale = tileScale
                }
            }
        }

    // Apply the selected font to the current Text element when requested.
    Connections {
        target: ZCam
        function onShowFontMediaBrowserRequested() {
            if (ZCam.currentElement && ZCam.currentElement.typeName() === "text")
                root.showFontsPanel(ZCam.currentElement.fontFamily)
            else
                root.showFontsPanel("")
            }
        }
    }