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

    // Persistent splitter: which panel is active (0=Fonts, 1=Artwork, 2=Icons)
    property int activePanel: 0

    // Public API so callers can switch to the Fonts panel and optionally
    // pre-select a font family.
    function showFontsPanel(family) {
        root.activePanel = 0;
        if (family !== undefined && family.length > 0) {
            fontModel.currentFamily = family;
            var idx = fontModel.allFamilies().indexOf(family);
            if (idx >= 0) {
                fontList.currentIndex = idx;
                fontList.positionViewAtIndex(idx, ListView.Contain);
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
        activePanel = mediaSettings.activePanel;
        }
    onActivePanelChanged: mediaSettings.activePanel = activePanel

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Title bar with panel switch buttons
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: Material.color(Material.BlueGrey, Material.Shade900)

            TabBar {
                id: panelTabs
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                anchors.topMargin: 0
                anchors.bottomMargin: 3
                clip: true
                spacing: 2
                currentIndex: root.activePanel
                onCurrentIndexChanged: root.activePanel = currentIndex
                background: Rectangle { color: "transparent" }

                TabButton {
                    text: qsTr("Fonts")
                    width: implicitWidth
                    topPadding: 4
                    bottomPadding: 4
                    }
                TabButton {
                    text: qsTr("Artwork")
                    width: implicitWidth
                    topPadding: 4
                    bottomPadding: 4
                    }
                TabButton {
                    text: qsTr("Icons")
                    width: implicitWidth
                    topPadding: 4
                    bottomPadding: 4
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
            MediaIconsPanel {
                id: iconsPanel
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
                root.showFontsPanel(ZCam.currentElement.fontFamily);
            else
                root.showFontsPanel("");
            }
        }
    }