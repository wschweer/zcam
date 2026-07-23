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
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import ZCam

Window {
    id: mainWindow

    width: settings.windowWidth
    height: settings.windowHeight
    x: settings.windowX >= 0 ? settings.windowX : Screen.width / 2 - width / 2
    y: settings.windowY >= 0 ? settings.windowY : Screen.height / 2 - height / 2
    visible: true
    title: ZCam.project ? ZCam.project.projectName + (ZCam.project.undo.dirty ? " *" : "") + " – ZCam" : "--"

    Material.theme: Material.Dark
    Material.accent: Material.Teal
    Material.primary: Material.BlueGrey

    // ── Persistent window geometry ────────────────────────────────────────────
    Settings {
        id: settings
        category: "MainWindow"
        property int windowWidth: 1024
        property int windowHeight: 700
        property int windowX: -1
        property int windowY: -1
        property bool mediaBrowserVisible: false
        }

    onWidthChanged: if (visible)
        settings.windowWidth = width
    onHeightChanged: if (visible)
        settings.windowHeight = height
    onXChanged: if (visible)
        settings.windowX = x
    onYChanged: if (visible)
        settings.windowY = y

    // ── Close guard ───────────────────────────────────────────────────────────
    //   closeConfirmed prevents the exit dialog from re-popping when
    //   Qt.quit() triggers another onClosing event while the project is
    //   still dirty (e.g. after the user chose "Discard").
    property bool closeConfirmed: false
    onClosing: function (close) {
        if (ZCam.project.dirty && !closeConfirmed) {
            close.accepted = false;
            exitDialog.open();
            }
        }

    // ── Restore last project on startup ────────────────────────────────────────
    //   Use a zero-timer so the call happens after the current event loop
    //   iteration finishes, i.e. after all child QML components (ProjectTree,
    //   TreeViewPanel, InspectorPanel, View3DPanel) have completed their
    //   construction and connected their signal handlers.
    Component.onCompleted: restoreTimer.start()

    Timer {
        id: restoreTimer
        interval: 0
        onTriggered: ZCam.restoreLastProject()
        }

    // ── Keyboard shortcuts ────────────────────────────────────────────────────
    Shortcut {
        sequence: [StandardKey.Quit]
        onActivated: mainWindow.close()
        }
    Shortcut {
        sequence: [StandardKey.New]
        onActivated: actionNew.trigger()
        }
    Shortcut {
        sequence: [StandardKey.Open]
        onActivated: actionOpen.trigger()
        }
    Shortcut {
        sequence: [StandardKey.Save]
        onActivated: actionSave.trigger()
        }
    Shortcut {
        sequence: [StandardKey.SaveAs]
        onActivated: actionSaveAs.trigger()
        }
    Shortcut {
        sequence: [StandardKey.Undo]
        onActivated: actionUndo.trigger()
        }
    Shortcut {
        sequence: [StandardKey.Redo]
        onActivated: actionRedo.trigger()
        }
    Shortcut {
        sequence: "Delete"
        onActivated: ZCam.deleteCurrentElement()
        }

    // =========================================================================
    //  Actions
    // =========================================================================

    Action {
        id: actionNew
        text: qsTr("&New")
        icon.source: "qrc:/icons/dark/file-new.svg"
        shortcut: StandardKey.New
        onTriggered: {
            if (ZCam.project.dirty) {
                newGuard.open();
                } else {
                ZCam.newProject();
                }
            }
        }

    Action {
        id: actionOpen
        text: qsTr("&Open…")
        icon.source: "qrc:/icons/dark/file-open.svg"
        shortcut: StandardKey.Open
        onTriggered: {
            if (ZCam.project.dirty) {
                openGuard.open();
                } else {
                openFileDialog.open();
                }
            }
        }

    Action {
        id: actionSave
        text: qsTr("&Save")
        icon.source: "qrc:/icons/dark/file-save.svg"
        shortcut: StandardKey.Save
        onTriggered: {
            if (ZCam.project.projectPath === "")
                saveAsFileDialog.open();
            else
                ZCam.save();
            }
        }

    Action {
        id: actionSaveAs
        text: qsTr("Save &As…")
        icon.source: "qrc:/icons/dark/file-save-as.svg"
        shortcut: StandardKey.SaveAs
        onTriggered: saveAsFileDialog.open()
        }

    Action {
        id: actionImport
        text: qsTr("&Import…")
        icon.source: "qrc:/icons/dark/file-import.svg"
        onTriggered: importFileDialog.open()
        }

    Action {
        id: actionQuit
        text: qsTr("&Quit")
        shortcut: StandardKey.Quit
        onTriggered: mainWindow.close()
        }

    Action {
        id: actionUndo
        text: qsTr("&Undo")
        icon.source: "qrc:/icons/dark/edit-undo.svg"
        shortcut: StandardKey.Undo
        enabled: ZCam.project.undo.canUndo
        onTriggered: ZCam.project.undo.undo()
        }

    Action {
        id: actionRedo
        text: qsTr("&Redo")
        icon.source: "qrc:/icons/dark/edit-redo.svg"
        shortcut: StandardKey.Redo
        enabled: ZCam.project.undo.canRedo
        onTriggered: ZCam.project.undo.redo()
        }

    Action {
        id: actionConfig
        text: qsTr("Config")
        icon.source: "qrc:/icons/dark/config.svg"
        }

    Action {
        id: actionMaterialTest
        text: qsTr("Material Test")
        onTriggered: {
            if (ZCam.project.dirty) {
                materialTestGuard.open();
                } else {
                ZCam.createMaterialTest();
                }
            }
        }

    Action {
        id: actionGalvoTest
        text: qsTr("Galvo Test")
        onTriggered: {
            if (ZCam.project.dirty) {
                galvoTestGuard.open();
                } else {
                ZCam.createGalvoTest();
                }
            }
        }

    Action {
        id: actionShowLaserPanel
        text: qsTr("Show laser panel")
        checkable: true
        icon.source: "qrc:/icons/laser.svg"
        }

    Action {
        id: actionShowMediaBrowser
        text: qsTr("Show media browser")
        checkable: true
        checked: settings.mediaBrowserVisible
        onCheckedChanged: settings.mediaBrowserVisible = checked
        }

    // =========================================================================
    //  File dialogs  (platform-native via Qt Labs)
    // =========================================================================

    FileDialog {
        id: openFileDialog
        title: qsTr("Open Project")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: ZCam.openProject(selectedFile.toString().replace("file://", ""))
        }

    FileDialog {
        id: saveAsFileDialog
        title: qsTr("Save Project As")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zcam"
        onAccepted: ZCam.saveAs(selectedFile.toString().replace("file://", ""))
        }

    FileDialog {
        id: importFileDialog
        title: qsTr("Import File")
        nameFilters: [qsTr("Supported formats (*.svg *.dxf *.stl *.obj)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: ZCam.importFile(selectedFile.toString().replace("file://", ""))
        }

    // =========================================================================
    //  Dialogs – unsaved-changes guard
    // =========================================================================

    // "New" guard
    Dialog {
        id: newGuard
        title: qsTr("Unsaved Changes")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        Label {
            text: qsTr("The current project has unsaved changes.\nDo you want to save before creating a new project?")
            }
        onAccepted: {   // Save
            if (ZCam.project.projectPath === "")
                newSaveAsFileDialog.open();
            else {
                ZCam.save();
                ZCam.newProject();
                }
            }
        onDiscarded: Qt.callLater(function () {
            ZCam.newProject();
            })   // Discard
        }

    // Dedicated save-as dialog for the new-project flow
    FileDialog {
        id: newSaveAsFileDialog
        title: qsTr("Save Project As")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zcam"
        onAccepted: {
            ZCam.saveAs(selectedFile.toString().replace("file://", ""));
            ZCam.newProject();
            }
        }

    // "Open" guard
    Dialog {
        id: openGuard
        title: qsTr("Unsaved Changes")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        Label {
            text: qsTr("The current project has unsaved changes.\nDo you want to save before opening another project?")
            }
        onAccepted: {
            if (ZCam.project.projectPath === "")
                openSaveAsFileDialog.open();
            else {
                ZCam.save();
                openFileDialog.open();
                }
            }
        onDiscarded: Qt.callLater(function () {
            openFileDialog.open();
            })
        }

    // Dedicated save-as dialog for the open-project flow
    FileDialog {
        id: openSaveAsFileDialog
        title: qsTr("Save Project As")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zcam"
        onAccepted: {
            ZCam.saveAs(selectedFile.toString().replace("file://", ""));
            openFileDialog.open();
            }
        }

    // "Exit" guard
    Dialog {
        id: exitDialog
        title: qsTr("Unsaved Changes")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        Label {
            text: qsTr("The current project has unsaved changes.\nDo you want to save before quitting?")
            }
        onAccepted: {
            closeConfirmed = true;
            if (ZCam.project.projectPath === "")
                exitSaveAsFileDialog.open();
            else {
                ZCam.save();
                Qt.quit();
                }
            }
        onDiscarded: {
            closeConfirmed = true;
            Qt.callLater(Qt.quit);
            }
        onRejected: {
            closeConfirmed = false;
            }
        }

    // Dedicated save-as dialog for the exit flow so we can quit after saving
    FileDialog {
        id: exitSaveAsFileDialog
        title: qsTr("Save Project As")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zcam"
        onAccepted: {
            ZCam.saveAs(selectedFile.toString().replace("file://", ""));
            Qt.quit();
            }
        onRejected: {
            closeConfirmed = false;
            }
        }

    // "Material Test" guard
    Dialog {
        id: materialTestGuard
        title: qsTr("Unsaved Changes")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        Label {
            text: qsTr("The current project has unsaved changes.\nDo you want to save before creating a Material Test?")
            }
        onAccepted: {   // Save
            if (ZCam.project.projectPath === "")
                materialTestSaveAsFileDialog.open();
            else {
                ZCam.save();
                ZCam.createMaterialTest();
                }
            }
        onDiscarded: Qt.callLater(function () {
            ZCam.createMaterialTest();
            })
        }

    FileDialog {
        id: materialTestSaveAsFileDialog
        title: qsTr("Save Project As")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zcam"
        onAccepted: {
            ZCam.saveAs(selectedFile.toString().replace("file://", ""));
            ZCam.createMaterialTest();
            }
        }

    // "Galvo Test" guard
    Dialog {
        id: galvoTestGuard
        title: qsTr("Unsaved Changes")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        Label {
            text: qsTr("The current project has unsaved changes.\nDo you want to save before creating a Galvo Test?")
            }
        onAccepted: {   // Save
            if (ZCam.project.projectPath === "")
                galvoTestSaveAsFileDialog.open();
            else {
                ZCam.save();
                ZCam.createGalvoTest();
                }
            }
        onDiscarded: Qt.callLater(function () {
            ZCam.createGalvoTest();
            })
        }

    FileDialog {
        id: galvoTestSaveAsFileDialog
        title: qsTr("Save Project As")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zcam"
        onAccepted: {
            ZCam.saveAs(selectedFile.toString().replace("file://", ""));
            ZCam.createGalvoTest();
            }
        }

    // =========================================================================
    //  Layout: MenuBar / ToolBar / TabBar / StackLayout
    // =========================================================================

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Menu bar ──────────────────────────────────────────────────────────
        MenuBar {
            id: menuBar
            Layout.fillWidth: true

            // File menu
            Menu {
                title: qsTr("&File")
                MenuItem {
                    action: actionNew
                    }
                MenuItem {
                    action: actionOpen
                    }
                MenuSeparator {}
                MenuItem {
                    action: actionSave
                    }
                MenuItem {
                    action: actionSaveAs
                    }
                MenuSeparator {}
                MenuItem {
                    action: actionImport
                    }
                MenuSeparator {}
                MenuItem {
                    action: actionQuit
                    }
                }

            // Edit menu
            Menu {
                title: qsTr("&Edit")
                MenuItem {
                    action: actionUndo
                    }
                MenuItem {
                    action: actionRedo
                    }
                MenuItem {
                    action: actionConfig
                    }
                }

            // Tools menu
            Menu {
                title: qsTr("&Tools")
                MenuItem {
                    action: actionMaterialTest
                    }
                MenuItem {
                    action: actionGalvoTest
                    }
                }
            }

        // ── Tool bar ──────────────────────────────────────────────────────────
        ToolBar {
            id: toolBar
            Layout.fillWidth: true

            RowLayout {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                width: parent.width

                // File operations
                ToolButton {
                    action: actionNew
                    display: AbstractButton.IconOnly
                    icon.color: "transparent"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("New project (Ctrl+N)")
                    }
                ToolButton {
                    action: actionOpen
                    display: AbstractButton.IconOnly
                    icon.color: "transparent"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open project (Ctrl+O)")
                    }
                ToolButton {
                    action: actionSave
                    display: AbstractButton.IconOnly
                    icon.color: "transparent"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Save project (Ctrl+S)")
                    }
                ToolButton {
                    action: actionSaveAs
                    display: AbstractButton.IconOnly
                    icon.color: "transparent"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Save project as…")
                    }
                ToolButton {
                    action: actionImport
                    display: AbstractButton.IconOnly
                    icon.color: "transparent"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Import file…")
                    }

                // Separator
                Rectangle {
                    implicitWidth: 1
                    implicitHeight: 24
                    color: Material.color(Material.BlueGrey, Material.Shade500)
                    }

                // Undo / Redo
                ToolButton {
                    action: actionUndo
                    display: AbstractButton.IconOnly
                    icon.color: enabled ? Material.foreground : Material.color(Material.Grey, Material.Shade600)
                    opacity: enabled ? 1.0 : 0.4
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Undo (Ctrl+Z)")
                    }
                ToolButton {
                    action: actionRedo
                    display: AbstractButton.IconOnly
                    icon.color: enabled ? Material.foreground : Material.color(Material.Grey, Material.Shade600)
                    opacity: enabled ? 1.0 : 0.4
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Redo (Ctrl+Y)")
                    }

                // Spacer
                Item {
                    Layout.fillWidth: true
                    }

                // placed on the right side of the toolbar

                // Media Browser toggle button — placed left of the Laser button
                ToolButton {
                    id: mediaBrowserBtn
                    action: actionShowMediaBrowser
                    display: AbstractButton.TextOnly
                    text: "M"
                    font.bold: true
                    contentItem: Text {
                        text: mediaBrowserBtn.text
                        color: mediaBrowserBtn.checked ? "white" : "black"
                        font: mediaBrowserBtn.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        }
                    background: Rectangle {
                        color: mediaBrowserBtn.checked ? Material.color(Material.Teal, Material.Shade700)
                               : (mediaBrowserBtn.hovered ? Material.color(Material.BlueGrey, Material.Shade600)
                                  : "transparent")
                        radius: 4
                        }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Show Media Browser")
                    Layout.rightMargin: 4
                    }
                ToolButton {
                    id: laserPanelBtn
                    action: actionShowLaserPanel
                    display: AbstractButton.IconOnly
                    icon.color: "transparent"
                    background: Rectangle {
                        color: laserPanelBtn.checked ? Material.color(Material.Teal, Material.Shade700)
                               : "transparent"
                        radius: 4
                        }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Show Laser Panel")
                    }
                }
            }

        // ── Tab bar with Cam refresh button ──────────────────────────────────
        // Light-grey bar behind tab buttons and Cam button.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: tabBar.implicitHeight
            color: Material.color(Material.Grey, Material.Shade500)

            RowLayout {
                anchors.fill: parent
                spacing: 0

                TabBar {
                    id: tabBar
                    background: Rectangle { color: "transparent" }

                    component TabBtn: TabButton {
                        width: 120
                        background: Rectangle {
                            color: "transparent"
                            }
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font: parent.font
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            }
                        }

                    TabBtn { text: qsTr("Main") }
                    TabBtn { text: qsTr("Recipes") }
                    TabBtn { text: qsTr("Machines") }
                    TabBtn { text: qsTr("Config") }
                    }

                // Spacer pushes the Cam button to the right edge
                Item { Layout.fillWidth: true }

                // Fixture selector — choose the active fixture for Cam
                ComboBox {
                    id: fixtureComboBox
                    Layout.preferredWidth: 160
                    Layout.alignment: Qt.AlignVCenter
                    Layout.rightMargin: 6
                    flat: true
                    // Model: prepend a "---" (no fixture) entry to the fixture list
                    model: {
                        var fixtures = (ZCam.project ? ZCam.project.fixtures : [])
                        var items = [{ "name": "---" }]
                        for (var i = 0; i < fixtures.length; ++i)
                            items.push(fixtures[i])
                        return items
                    }
                    textRole: "name"
                    currentIndex: {
                        if (!ZCam.project || !ZCam.project.fixture)
                            return 0  // the "---" entry
                        var idx = ZCam.project.fixtures.indexOf(ZCam.project.fixture)
                        return idx >= 0 ? idx + 1 : 0  // +1 to account for "---"
                    }
                    onActivated: function(index) {
                        if (!ZCam.project)
                            return
                        if (index === 0) {
                            // "---" selected — no fixture
                            // Only allow this if there are fixtures to deselect
                            // (otherwise there's nothing to change)
                            if (ZCam.project.fixture)
                                ZCam.project.fixture = null
                        } else {
                            var fixtures = ZCam.project.fixtures
                            var fi = index - 1  // offset for "---" entry
                            if (fi >= 0 && fi < fixtures.length)
                                ZCam.project.fixture = fixtures[fi]
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Select active fixture")
                    }

                // Cam refresh button — not a TabButton so it does not
                // change the active panel / StackLayout index.
                Button {
                    id: camRefreshButton
                    text: qsTr("Cam")
                    icon.source: "qrc:///icons/cam-refresh.svg"
                    icon.color: camRefreshButton.enabled ? "white" : camRefreshButton.Material.color(Material.Grey, Material.Shade600)
                    display: AbstractButton.TextBesideIcon
                    enabled: ZCam.camDirty
                    flat: true
                    Layout.preferredWidth: 120
                    Layout.alignment: Qt.AlignVCenter
                    Layout.rightMargin: 10
                    background: null
                    onClicked: ZCam.refreshCam()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Recalculate CAM data")
                    }
                }
            }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 4
            Layout.topMargin: 0
            // ── Stack / content area ──────────────────────────────────────────────
            StackLayout {
                id: stack
                SplitView.fillWidth: true
                currentIndex: tabBar.currentIndex

                // Tab 0 – Main work panel
                MainPanel {
                    id: mainPanel
                    }

                // Tab 1 – Configure Recipes
                ConfigRecipes {
                    id: configRecipes
                    }

                // Tab 2 – Configure Machines
                ConfigMachines {
                    id: configMachines
                    }
                // Tab 2 – Configure App
                ConfigSystem {
                    id: configSystem
                    }
                }
            LaserPanel {
                id: laserPanel
                visible: actionShowLaserPanel.checked
                SplitView.minimumWidth: 300
                SplitView.maximumWidth: 300
                }
            }
        }

    // ── Media Browser visibility binding ──────────────────────────────────────
    //   The MediaBrowser lives inside MainPanel. Toggle its visibility
    //   via the mediaBrowserVisible property exposed by MainPanel.
    //   The action's checked state is persisted via settings.mediaBrowserVisible.
    Binding {
        target: mainPanel
        property: "mediaBrowserVisible"
        value: actionShowMediaBrowser.checked
        }

    // Show the font media browser when requested by FontFamilyButton.
    Connections {
        target: ZCam
        function onShowFontMediaBrowserRequested() {
            actionShowMediaBrowser.checked = true
            }
        }
    }
