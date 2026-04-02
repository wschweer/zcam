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

// ── Main work panel ───────────────────────────────────────────────────────────
// Contains the outer horizontal split (scene tree + inspector | 3-D viewport).
// Settings are stored in the "MainPanel" category so they are separate from the
// top-level window geometry stored in AppWindow.

Item {
    id: root

    // ── Persistent splitter states ────────────────────────────────────────────
    Settings {
        id: panelSettings
        category: "MainPanel"
        property var outerSplitState
        property var innerSplitState
    }

    Component.onCompleted: {
        if (panelSettings.outerSplitState)
            outerSplit.restoreState(panelSettings.outerSplitState)
        if (panelSettings.innerSplitState)
            innerSplit.restoreState(panelSettings.innerSplitState)
    }

    // ── Outer horizontal split: left panel | 3-D viewport ────────────────────
    SplitView {
        id: outerSplit
        anchors.fill: parent
        orientation: Qt.Horizontal

        onResizingChanged: if (!resizing) panelSettings.outerSplitState = saveState()

        // ── Left panel ────────────────────────────────────────────────────────
        SplitView {
            id: innerSplit
            SplitView.preferredWidth: 360
            SplitView.minimumWidth:   180
            orientation: Qt.Vertical

            onResizingChanged: if (!resizing) panelSettings.innerSplitState = saveState()

            // Upper half: scene tree
            Rectangle {
                SplitView.preferredHeight: 300
                SplitView.minimumHeight:   80
                color: Material.color(Material.BlueGrey, Material.Shade900)

                TreeViewPanel {
                    anchors.fill: parent
                }
            }

            // Lower half: inspector
            Rectangle {
                SplitView.fillHeight:    true
                SplitView.minimumHeight: 80
                color: Material.color(Material.BlueGrey, Material.Shade800)

                InspectorPanel {
                    anchors.fill: parent
                }
            }
        }

        // ── Right panel: 3-D viewport ─────────────────────────────────────────
        View3DPanel {
            SplitView.fillWidth:    true
            SplitView.minimumWidth: 200
        }
    }
}
