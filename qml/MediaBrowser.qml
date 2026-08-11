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

    // "Loaded once" flags — once a panel has been activated it stays in
    // memory so switching back is instant.  The flags are set the first
    // time the corresponding tab becomes active, ensuring the panels are
    // instantiated lazily (not on startup).
    property bool _artworkLoaded: false
    property bool _iconsLoaded: false

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
        // If the saved active panel is Artwork or Icons, mark them as
        // loaded immediately so the Loader activates on first display.
        if (activePanel === 1)
            _artworkLoaded = true;
        if (activePanel === 2)
            _iconsLoaded = true;
        }
    onActivePanelChanged: {
        mediaSettings.activePanel = activePanel;
        if (activePanel === 1)
            _artworkLoaded = true;
        if (activePanel === 2)
            _iconsLoaded = true;
        }

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
        //
        // A StackLayout instantiates ALL children eagerly, which means the
        // Artwork panel would load directory images and generate DXF previews
        // on startup even when the Fonts tab is active.  Using a Loader per
        // panel with a "loaded once" flag ensures each panel is only
        // instantiated when first selected, and its initialization (image
        // scanning, DXF-to-SVG conversion) is deferred until the user
        // actually switches to it.  Once loaded, the panel stays in memory
        // so subsequent tab switches are instant.
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.activePanel

            // Fonts panel — always loaded (it is the default tab)
            MediaFontsPanel {
                id: fontsPanel
                onApplyFontRequested: family => ZCam.applyFontToCurrentText(family)
                }

            // Artwork panel — loaded lazily on first activation
            Loader {
                id: artworkLoader
                active: root._artworkLoaded
                sourceComponent: MediaArtworkPanel {
                    tileScale: mediaSettings.tileScale
                    onTileScaleChanged: mediaSettings.tileScale = tileScale
                    }
                }

            // Icons panel — loaded lazily on first activation
            Loader {
                id: iconsLoader
                active: root._iconsLoaded
                sourceComponent: MediaIconsPanel {
                    tileScale: mediaSettings.tileScale
                    onTileScaleChanged: mediaSettings.tileScale = tileScale
                    }
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