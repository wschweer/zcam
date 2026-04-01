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
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Controls.Material
import QtCore

Window {
    id: mainWindow

    width:  settings.windowWidth
    height: settings.windowHeight
    x:      settings.windowX >= 0 ? settings.windowX : Screen.width  / 2 - width  / 2
    y:      settings.windowY >= 0 ? settings.windowY : Screen.height / 2 - height / 2
    visible: true
    title: "ZCam"

    Material.theme: Material.Dark
    Material.accent: Material.Teal
    Material.primary: Material.BlueGrey

    // ── Persistent settings ───────────────────────────────────────────────────
    // Stored in the platform-specific location (e.g. ~/.config/ZCam/ZCam.conf).
    Settings {
        id: settings
        category: "MainWindow"

        // Window geometry
        property int  windowWidth:  800
        property int  windowHeight: 600
        property int  windowX:      -1
        property int  windowY:      -1

        // SplitView states (QByteArray <-> var round-trip via QtCore.Settings)
        property var  outerSplitState   // horizontal: left panel | 3-D viewport
        property var  innerSplitState   // vertical:   tree       | inspector
    }

    // ── Quit shortcut ─────────────────────────────────────────────────────────
    Shortcut {
        sequence: StandardKey.Quit   // Ctrl+Q on Linux/Windows, Cmd+Q on macOS
        onActivated: Qt.quit()
    }

    // ── Save window geometry whenever it changes ──────────────────────────────
    onWidthChanged:  if (visible) settings.windowWidth  = width
    onHeightChanged: if (visible) settings.windowHeight = height
    onXChanged:      if (visible) settings.windowX      = x
    onYChanged:      if (visible) settings.windowY      = y

    // ── Restore splitter states after all children are instantiated ───────────
    Component.onCompleted: {
        if (settings.outerSplitState)
            outerSplit.restoreState(settings.outerSplitState)
        if (settings.innerSplitState)
            innerSplit.restoreState(settings.innerSplitState)
    }

    // ── Outer horizontal split ────────────────────────────────────────────────
    SplitView {
        id: outerSplit
        anchors.fill: parent
        orientation: Qt.Horizontal

        // Persist state as soon as a drag ends (resizing → false).
        onResizingChanged: if (!resizing) settings.outerSplitState = saveState()

        // ── Left panel ────────────────────────────────────────────────────────
        SplitView {
            id: innerSplit
            SplitView.preferredWidth: 360
            SplitView.minimumWidth:   180
            orientation: Qt.Vertical

            // Persist state as soon as a drag ends.
            onResizingChanged: if (!resizing) settings.innerSplitState = saveState()

            // Upper half: scene tree
            Rectangle {
                SplitView.preferredHeight: 300   // static default; restored from Settings on startup
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
