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
import QtCore
import ZCam

// ── Main work panel ───────────────────────────────────────────────────────────
// Contains the outer horizontal split (scene tree + inspector | 3-D viewport).
// Settings are stored in the "MainPanel" category so they are separate from the
// top-level window geometry stored in AppWindow.

Item {
    id: root

    // ── External control: toggle MediaBrowser from Main.qml ──────────────────
    // Visibility is persisted by Main.qml via settings.mediaBrowserVisible.
    property bool mediaBrowserVisible: false

    // Flag: suppress split-state saving while restoring a saved width
    property bool _restoringSplit: false

    // ── Persistent splitter states ────────────────────────────────────────────
    // outerSplit and innerSplit use saveState()/restoreState() because all
    // their children are always visible.
    // mediaSplit tracks the MediaBrowser width explicitly via a bindable
    // preferredWidth property so that the last expanded width is preserved
    // even when the panel is collapsed and the app is restarted.
    Settings {
        id: panelSettings
        category: "MainPanel"
        property var outerSplitState
        property var innerSplitState
        property int mediaBrowserWidth: 300
        }

    // Bindable preferred width — initialised from settings, updated on resize
    // and on restore. Bound to MediaBrowser's SplitView.preferredWidth so
    // the SplitView honours the saved width when the item (re)appears.
    property int mediaBrowserPrefWidth: panelSettings.mediaBrowserWidth >= 250
        ? panelSettings.mediaBrowserWidth : 300

    Component.onCompleted: {
        if (panelSettings.outerSplitState)
            outerSplit.restoreState(panelSettings.outerSplitState);
        if (panelSettings.innerSplitState)
            innerSplit.restoreState(panelSettings.innerSplitState);
        }

    // ── Restore MediaBrowser width when it becomes visible ───────────────────
    //   When the panel is toggled on, the SplitView uses the current
    //   preferredWidth to lay out the item. We update it from the saved
    //   value and suppress state saving until the restore has settled.
    onMediaBrowserVisibleChanged: {
        if (mediaBrowserVisible) {
            _restoringSplit = true;
            var w = panelSettings.mediaBrowserWidth >= 250
                ? panelSettings.mediaBrowserWidth : 300;
            mediaBrowserPrefWidth = w;
            Qt.callLater(function () {
                _restoringSplit = false;
                });
            }
        }

    // ── Outer horizontal split: left panel | 3-D viewport (+ media browser) ─
    SplitView {
        id: outerSplit
        anchors.fill: parent
        orientation: Qt.Horizontal

        onResizingChanged: if (!resizing)
            panelSettings.outerSplitState = saveState()

        // ── Left panel ────────────────────────────────────────────────────────
        SplitView {
            id: innerSplit
            SplitView.preferredWidth: 360
            SplitView.minimumWidth: 300
            orientation: Qt.Vertical

            onResizingChanged: if (!resizing)
                panelSettings.innerSplitState = saveState()

            // Upper half: scene tree
            Flickable {
                SplitView.preferredHeight: 300
                SplitView.minimumHeight: 80
                Rectangle {
                    anchors.fill: parent
                    color: Material.color(Material.BlueGrey, Material.Shade900)

                    TreeViewPanel {
                        anchors.fill: parent
                        }
                    }
                }

            // Lower half: inspector
            Flickable {
                SplitView.fillHeight: true
                SplitView.minimumHeight: 80
                Rectangle {
                    anchors.fill: parent
                    color: Material.color(Material.BlueGrey, Material.Shade800)

                    InspectorPanel {
                        anchors.fill: parent
                        }
                    }
                }
            }

        // ── Right panel: 3-D viewport + media browser splitter ───────────────
        SplitView {
            id: mediaSplit
            SplitView.fillWidth: true
            SplitView.minimumWidth: 200
            orientation: Qt.Horizontal

            View3DPanel {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 200
                }

            MediaBrowser {
                id: mediaBrowser
                objectName: "mediaBrowser"
                visible: root.mediaBrowserVisible
                SplitView.preferredWidth: root.mediaBrowserPrefWidth
                SplitView.minimumWidth: 250
                }

            // Only save the width when the MediaBrowser is visible and not
            // being restored, so the collapsed width is never persisted.
            onResizingChanged: if (!resizing && mediaBrowser.visible && !_restoringSplit) {
                panelSettings.mediaBrowserWidth = Math.round(mediaBrowser.width)
                mediaBrowserPrefWidth = Math.round(mediaBrowser.width)
                }
            }
        }
    }