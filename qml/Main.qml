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
    title: ZCam.projectManager.projectName + (ZCam.projectManager.dirty ? " *" : "") + " – ZCam"

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
    onClosing: function (close) {
        if (ZCam.projectManager.dirty) {
            close.accepted = false;
            exitDialog.open();
            }
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

    // =========================================================================
    //  Actions
    // =========================================================================

    Action {
        id: actionNew
        text: qsTr("&New")
        icon.source: "qrc:/icons/dark/file-new.svg"
        shortcut: StandardKey.New
        onTriggered: {
            if (ZCam.projectManager.dirty) {
                newGuard.open();
                } else {
                ZCam.projectManager.newProject();
                }
            }
        }

    Action {
        id: actionOpen
        text: qsTr("&Open…")
        icon.source: "qrc:/icons/dark/file-open.svg"
        shortcut: StandardKey.Open
        onTriggered: {
            if (ZCam.projectManager.dirty) {
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
            if (ZCam.projectManager.projectPath === "")
                saveAsFileDialog.open();
            else
                ZCam.projectManager.save();
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
        enabled: ZCam.projectManager.canUndo
        onTriggered: ZCam.projectManager.undo()
        }

    Action {
        id: actionRedo
        text: qsTr("&Redo")
        icon.source: "qrc:/icons/dark/edit-redo.svg"
        shortcut: StandardKey.Redo
        enabled: ZCam.projectManager.canRedo
        onTriggered: ZCam.projectManager.redo()
        }

    Action {
        id: actionConfig
        text: qsTr("Config");
        icon.source: "qrc:/icons/dark/config.svg"
        }

    // =========================================================================
    //  File dialogs  (platform-native via Qt Labs)
    // =========================================================================

    FileDialog {
        id: openFileDialog
        title: qsTr("Open Project")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: ZCam.projectManager.openProject(selectedFile.toString().replace("file://", ""))
        }

    FileDialog {
        id: saveAsFileDialog
        title: qsTr("Save Project As")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zcam"
        onAccepted: ZCam.projectManager.saveAs(selectedFile.toString().replace("file://", ""))
        }

    FileDialog {
        id: importFileDialog
        title: qsTr("Import File")
        nameFilters: [qsTr("Supported formats (*.svg *.dxf *.stl *.obj)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: ZCam.projectManager.importFile(selectedFile.toString().replace("file://", ""))
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
            if (ZCam.projectManager.projectPath === "")
                saveAsFileDialog.open();
            else {
                ZCam.projectManager.save();
                ZCam.projectManager.newProject();
                }
            }
        onDiscarded: ZCam.projectManager.newProject()   // Discard
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
            if (ZCam.projectManager.projectPath === "")
                saveAsFileDialog.open();
            else {
                ZCam.projectManager.save();
                openFileDialog.open();
                }
            }
        onDiscarded: openFileDialog.open()
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
            if (ZCam.projectManager.projectPath === "")
                saveAsFileDialog.open();
            else {
                ZCam.projectManager.save();
                Qt.quit();
                }
            }
        onDiscarded: Qt.quit()
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
            }

        // ── Tool bar ──────────────────────────────────────────────────────────
        ToolBar {
            id: toolBar
            Layout.fillWidth: true

            RowLayout {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

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
                    width: 1
                    height: 24
                    color: Material.color(Material.BlueGrey, Material.Shade500)
                    }

                // Undo / Redo
                ToolButton {
                    action: actionUndo
                    display: AbstractButton.IconOnly
                    icon.color: "transparent"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Undo (Ctrl+Z)")
                    }
                ToolButton {
                    action: actionRedo
                    display: AbstractButton.IconOnly
                    icon.color: "transparent"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Redo (Ctrl+Y)")
                    }

                Item {
                    Layout.fillWidth: true
                    }   // spacer
                }
            }

        // ── Tab bar ───────────────────────────────────────────────────────────
        TabBar {
            id: tabBar
            Layout.fillWidth: true

            TabButton {
                text: qsTr("Main")
                width: 120
                }
            TabButton {
                text: qsTr("Config")
                width: 120
                }
            }

        // ── Stack / content area ──────────────────────────────────────────────
        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            // Tab 0 – Main work panel
            MainPanel {
                id: mainPanel
                }

            // Tab 1 – Configuration panel
            ConfigPanel {
                id: configPanel
                }
            }
        }
    }
