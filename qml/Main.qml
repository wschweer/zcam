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

ApplicationWindow {
    id: mainWindow

    width: settings.windowWidth
    height: settings.windowHeight
    x: settings.windowX >= 0 ? settings.windowX : Screen.width / 2 - width / 2
    y: settings.windowY >= 0 ? settings.windowY : Screen.height / 2 - height / 2
    visible: true
    title: ZCam.project ? ZCam.project.projectName + (ZCam.project.undo ? (ZCam.project.undo.dirty ? " *" : "") : "") + " – ZCam" : "--"

    // ── Application-wide font from Config ────────────────────────────────────
    //   All child Controls (Labels, Buttons, TextFields, ...) inherit this
    //   font via Qt Quick Controls' font propagation.  Individual components
    //   must NOT hardcode font.pixelSize so the configured size is uniform.
    font.family: ZCam.config ? ZCam.config.font : "NotoSans"
    font.pointSize: ZCam.config ? ZCam.config.fontSize : 12

    Material.theme: Material.Dark
    Material.accent: Material.Teal
    Material.primary: Material.BlueGrey

    // ── Persistent window geometry ────────────────────────────────────────────
    Settings {
        id: settings
        category: "MainWindow"
        property int windowWidth: 1280
        property int windowHeight: 850
        property int windowX: -1
        property int windowY: -1
        property bool mediaBrowserVisible: false
        property bool aiPanelVisible: false
        property bool scriptPanelVisible: false
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
        if (ZCam.project && ZCam.project.dirty && !closeConfirmed) {
            close.accepted = false;
            checkUnsavedAndProceed(qsTr("The current project has unsaved changes.\nDo you want to save before quitting?"), function () {
                Qt.quit();
                }, function () {
                closeConfirmed = false;
                });
            // closeConfirmed must be set before proceeding so that the
            // subsequent onClosing from Qt.quit() does not re-popup
            closeConfirmed = true;
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
        onTriggered: ZCam.handleStartupFile()
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
        // Disabled on the Recipes tab (index 1) and Machines tab (index 2)
        // — those tabs have their own Shortcut that deletes the selected
        // recipe/folder or machine.
        enabled: tabBar.currentIndex !== 1 && tabBar.currentIndex !== 2
        onActivated: ZCam.deleteCurrentElement()
        }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            // Escape is a two-step action:
            //   1st ESC — clear selection / currentElement
            //   2nd ESC — reset the current tool to Pointer
            if (ZCam.selectedElements && ZCam.selectedElements.length > 0)
                ZCam.clearSelection();
            else if (ZCam.currentElement)
                ZCam.currentElement = null;
            else
                ZCam.currentTool = "pointer";
            }
        }

    // =========================================================================
    //  Actions
    // =========================================================================

    Action {
        id: actionNew
        text: qsTr("&New")
        icon.source: "qrc:/icons/dark/file-new.svg"
        shortcut: StandardKey.New
        onTriggered: checkUnsavedAndProceed(qsTr("The current project has unsaved changes.\nDo you want to save before creating a new project?"), function () {
            ZCam.newProject();
            })
        }

    Action {
        id: actionOpen
        text: qsTr("&Open…")
        icon.source: "qrc:/icons/dark/file-open.svg"
        shortcut: StandardKey.Open
        onTriggered: checkUnsavedAndProceed(qsTr("The current project has unsaved changes.\nDo you want to save before opening another project?"), function () {
            openFileDialog.open();
            })
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
        id: actionExportSvg
        text: qsTr("&Export SVG…")
        icon.source: "qrc:/icons/dark/file-export.svg"
        onTriggered: {
            if (ZCam.project) {
                var dir = ZCam.project.projectPath !== ""
                    ? ZCam.project.projectPath.replace(/[^\/]*$/, "")
                    : ZCam.expandPath(ZCam.config.projectsDirectory) + "/"
                // Set folder + file BEFORE open(): Qt only reads the initial
                // selection while opening.  The "File name" field itself is
                // filled by the C++ prefill (SideBarColorFixer) since the
                // export file does not exist on disk yet.
                exportSvgFileDialog.currentFolder = "file://" + dir
                exportSvgFileDialog.selectedFile = "file://" + dir + ZCam.project.projectName + ".svg"
                }
            exportSvgFileDialog.open()
            }
        }

    Action {
        id: actionExportDxf
        text: qsTr("Export &DXF…")
        icon.source: "qrc:/icons/dark/file-export.svg"
        onTriggered: {
            if (ZCam.project) {
                var dir = ZCam.project.projectPath !== ""
                    ? ZCam.project.projectPath.replace(/[^\/]*$/, "")
                    : ZCam.expandPath(ZCam.config.projectsDirectory) + "/"
                // Set folder + file BEFORE open(): Qt only reads the initial
                // selection while opening.  The "File name" field itself is
                // filled by the C++ prefill (SideBarColorFixer) since the
                // export file does not exist on disk yet.
                exportDxfFileDialog.currentFolder = "file://" + dir
                exportDxfFileDialog.selectedFile = "file://" + dir + ZCam.project.projectName + ".dxf"
                }
            exportDxfFileDialog.open()
            }
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
        enabled: ZCam.project ? ZCam.project.undo.canUndo : false
        onTriggered: if (ZCam.project)
            ZCam.project.undo.undo()
        }

    Action {
        id: actionRedo
        text: qsTr("&Redo")
        icon.source: "qrc:/icons/dark/edit-redo.svg"
        shortcut: StandardKey.Redo
        enabled: ZCam.project ? ZCam.project.undo.canRedo : false
        onTriggered: if (ZCam.project)
            ZCam.project.undo.redo()
        }

    Action {
        id: actionConfig
        text: qsTr("Config")
        icon.source: "qrc:/icons/dark/config.svg"
        }

    Action {
        id: actionMaterialTest
        text: qsTr("Material Test")
        onTriggered: checkUnsavedAndProceed(qsTr("The current project has unsaved changes.\nDo you want to save before creating a Material Test?"), function () {
            ZCam.createMaterialTest();
            })
        }

    Action {
        id: actionGalvoTest
        text: qsTr("Galvo Test 9")
        onTriggered: checkUnsavedAndProceed(qsTr("The current project has unsaved changes.\nDo you want to save before creating a Galvo Test 9?"), function () {
            ZCam.createGalvoTest();
            })
        }

    Action {
        id: actionGalvoTest64
        text: qsTr("Galvo Test 64")
        onTriggered: checkUnsavedAndProceed(qsTr("The current project has unsaved changes.\nDo you want to save before creating a Galvo Test 64?"), function () {
            ZCam.createGalvoTest64();
            })
        }
    Action {
        id: actionCalibrationScan
        text: qsTr("Interpret Calibration Scan")
        onTriggered: checkUnsavedAndProceed(qsTr("The current project has unsaved changes.\nDo you want to save before creating a Galvo Test?"), function () {
            ZCam.calibrationScan();
            })
        }

    Action {
        id: actionGalvoCalibration
        text: qsTr("9-Point Galvo Calibration…")
        onTriggered: galvoCalDialog.open()
        }

    Action {
        id: actionTestProject
        text: qsTr("Test Project")
        onTriggered: checkUnsavedAndProceed(qsTr("The current project has unsaved changes.\nDo you want to save before creating a Test Project?"), function () {
            ZCam.createTestProject();
            })
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

    Action {
        id: actionShowAiPanel
        text: qsTr("Show AI panel")
        checkable: true
        checked: settings.aiPanelVisible
        onCheckedChanged: settings.aiPanelVisible = checked
        }

    Action {
        id: actionShowScriptPanel
        text: qsTr("Show Script Console")
        checkable: true
        checked: settings.scriptPanelVisible
        onCheckedChanged: settings.scriptPanelVisible = checked
        }

    Action {
        id: actionAbout
        text: qsTr("&About")
        onTriggered: aboutDialog.open()
        }

    // =========================================================================
    //  File dialogs  (ZFileDialog — wraps FileDialog with sidebar color fix)
    //
    //  Qt 6.12 removed the explicit `color: Material.foreground` binding
    //  from the Material SideBar.qml buttonDelegate's IconLabel, causing
    //  the sidebar text to default to black (unreadable on dark background).
    //  ZFileDialog walks the dialog tree after opening and sets the
    //  IconLabel color to white.
    // =========================================================================

    ZFileDialog {
        id: openFileDialog
        Material.theme: Material.Dark
        title: qsTr("Open Project")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFile
        currentFolder: ZCam.config ? "file://" + ZCam.expandPath(ZCam.config.projectsDirectory) : currentFolder
        onAccepted: ZCam.openProject(selectedFile.toString().replace("file://", ""))
        }

    ZFileDialog {
        id: saveAsFileDialog
        Material.theme: Material.Dark
        title: qsTr("Save Project As")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zcam"
        onAccepted: ZCam.saveAs(selectedFile.toString().replace("file://", ""))
        }

    ZFileDialog {
        id: importFileDialog
        Material.theme: Material.Dark
        title: qsTr("Import File")
        nameFilters: [qsTr("All supported formats (*.svg *.dxf *.dwg *.brep *.png *.jpg *.jpeg *.bmp *.gif *.tiff *.tif *.webp *.xml *.cvg)"), qsTr("Vector graphics (*.svg *.dxf *.dwg)"), qsTr("BREP CAD (*.brep)"), qsTr("Pixel images (*.png *.jpg *.jpeg *.bmp *.gif *.tiff *.tif *.webp)"), qsTr("IPC-2581 (*.xml *.cvg)"), qsTr("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: ZCam.importFile(selectedFile.toString().replace("file://", ""))
        }

    ZFileDialog {
        id: exportSvgFileDialog
        Material.theme: Material.Dark
        title: qsTr("Export SVG")
        nameFilters: [qsTr("SVG (*.svg)"), qsTr("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "svg"
        onAccepted: ZCam.exportSvg(selectedFile.toString().replace("file://", ""))
        }

    ZFileDialog {
        id: exportDxfFileDialog
        Material.theme: Material.Dark
        title: qsTr("Export DXF")
        nameFilters: [qsTr("DXF (*.dxf)"), qsTr("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "dxf"
        onAccepted: ZCam.exportDxf(selectedFile.toString().replace("file://", ""))
        }

    // =========================================================================
    //  Dialog – unsaved-changes guard (unified)
    // =========================================================================
    //   A single reusable guard dialog for all "unsaved changes" prompts.
    //   Call checkUnsavedAndProceed(message, action [, onCancel]) from any
    //   action handler.  If the project is dirty, the dialog asks the user
    //   whether to Save, Discard or Cancel.  On Save/Discard the provided
    //   *action* function is executed (after saving if Save was chosen).
    //   If the project is not dirty, *action* is called immediately.

    function checkUnsavedAndProceed(message, action, onCancel) {
        if (ZCam.project && ZCam.project.dirty) {
            unsavedChangesGuard.messageText = message;
            unsavedChangesGuard.proceedAction = action;
            unsavedChangesGuard.onCancelHandler = onCancel !== undefined ? onCancel : null;
            unsavedChangesGuard.open();
            } else {
            action();
            }
        }

    Dialog {
        id: unsavedChangesGuard
        title: qsTr("Unsaved Changes")
        modal: true
        anchors.centerIn: parent
        // Explicit width: without it the Material style computes implicitWidth
        // from the Label, whose width depends on the Dialog's contentWidth —
        // when the text changes while closed Qt evaluates the chain and flags
        // a binding loop ("Binding loop detected for property implicitWidth").
        width: 420
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel

        property string messageText: ""
        property var proceedAction: null
        property var onCancelHandler: null

        Label {
            text: unsavedChangesGuard.messageText
            width: 360
            wrapMode: Text.WordWrap
            }
        onAccepted: {   // Save
            if (ZCam.project.projectPath === "")
                guardSaveAsFileDialog.open();
            else {
                ZCam.save();
                if (unsavedChangesGuard.proceedAction)
                    unsavedChangesGuard.proceedAction();
                }
            }
        onDiscarded: Qt.callLater(function () {
            if (unsavedChangesGuard.proceedAction)
                unsavedChangesGuard.proceedAction();
            })
        onRejected: {
            if (unsavedChangesGuard.onCancelHandler)
                unsavedChangesGuard.onCancelHandler();
            }
        }

    ZFileDialog {
        id: guardSaveAsFileDialog
        Material.theme: Material.Dark
        title: qsTr("Save Project As")
        nameFilters: [qsTr("ZCam project (*.zcam)"), qsTr("All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zcam"
        onAccepted: {
            ZCam.saveAs(selectedFile.toString().replace("file://", ""));
            if (unsavedChangesGuard.proceedAction)
                unsavedChangesGuard.proceedAction();
            }
        onRejected: {
            if (unsavedChangesGuard.onCancelHandler)
                unsavedChangesGuard.onCancelHandler();
            }
        }

    // =========================================================================
    //  About dialog
    // =========================================================================

    Dialog {
        id: aboutDialog
        title: qsTr("About ZCam")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                text: "ZCam"
                font.bold: true
                font.pointSize: (ZCam.config ? ZCam.config.fontSize : 12) + 8
                Layout.alignment: Qt.AlignHCenter
                }
            Label {
                text: qsTr("Manufacturing tool for G-code machines and fiber laser engraving")
                Layout.alignment: Qt.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.maximumWidth: 320
                }
            Label {
                text: qsTr("Version: %1").arg(Qt.application.version)
                Layout.alignment: Qt.AlignHCenter
                }
            Label {
                text: qsTr("Copyright (C) 2026 Werner Schweer")
                Layout.alignment: Qt.AlignHCenter
                font.pointSize: (ZCam.config ? ZCam.config.fontSize : 12) - 2
                }
            }
        }

    // =========================================================================
    //  Galvo Calibration dialog
    // =========================================================================

    GalvoCalibrationDialog {
        id: galvoCalDialog
        }

    // =========================================================================
    //  Layout: MenuBar+ToolBar (single row) / TabBar / StackLayout
    //
    //  MenuBar and ToolBar are merged into one horizontal header row to
    //  save vertical screen space.  The MenuBar sits on the left, followed
    //  by a thin separator, then the file/edit tool buttons, another
    //  separator, undo/redo, a flexible spacer, and finally the panel
    //  toggle buttons (M, Laser, AI, JS) on the right.
    // =========================================================================

    header: ToolBar {
        RowLayout {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            width: parent.width

            // ── Menu bar (inline) ──────────────────────────────────────────
            MenuBar {
                id: inlineMenuBar
                Layout.alignment: Qt.AlignVCenter

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
                    MenuItem {
                        action: actionExportSvg
                        }
                    MenuItem {
                        action: actionExportDxf
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
                    MenuItem {
                        action: actionGalvoTest64
                        }
                    MenuItem {
                        action: actionCalibrationScan
                        }
                    MenuItem {
                        action: actionGalvoCalibration
                        }
                    MenuSeparator {}
                    MenuItem {
                        action: actionTestProject
                        }
                    }

                // Help menu
                Menu {
                    title: qsTr("&Help")
                    MenuItem {
                        action: actionAbout
                        }
                    }
                }

            // Separator between menu bar and tool buttons
            Rectangle {
                implicitWidth: 1
                implicitHeight: 24
                color: Material.color(Material.BlueGrey, Material.Shade500)
                }

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
                    color: mediaBrowserBtn.checked ? Material.color(Material.Teal, Material.Shade700) : (mediaBrowserBtn.hovered ? Material.color(Material.BlueGrey, Material.Shade600) : "transparent")
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
                    color: laserPanelBtn.checked ? Material.color(Material.Teal, Material.Shade700) : "transparent"
                    radius: 4
                    }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Show Laser Panel")
                Layout.rightMargin: 4
                }

            // AI panel toggle button — shows/hides the AI side panel
            ToolButton {
                id: aiPanelBtn
                action: actionShowAiPanel
                display: AbstractButton.TextOnly
                text: qsTr("AI")
                font.bold: true
                contentItem: Text {
                    text: aiPanelBtn.text
                    color: aiPanelBtn.checked ? "white" : "black"
                    font: aiPanelBtn.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    }
                background: Rectangle {
                    color: aiPanelBtn.checked ? Material.color(Material.Teal, Material.Shade700) : (aiPanelBtn.hovered ? Material.color(Material.BlueGrey, Material.Shade600) : "transparent")
                    radius: 4
                    }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Show AI Panel")
                Layout.rightMargin: 4
                }

            // Script Console toggle button
            ToolButton {
                id: scriptPanelBtn
                action: actionShowScriptPanel
                display: AbstractButton.TextOnly
                text: qsTr("JS")
                font.bold: true
                contentItem: Text {
                    text: scriptPanelBtn.text
                    color: scriptPanelBtn.checked ? "white" : "black"
                    font: scriptPanelBtn.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    }
                background: Rectangle {
                    color: scriptPanelBtn.checked ? Material.color(Material.Amber, Material.Shade700) : (scriptPanelBtn.hovered ? Material.color(Material.BlueGrey, Material.Shade600) : "transparent")
                    radius: 4
                    }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Show Script Console")
                Layout.rightMargin: 4
                }
            }
        }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

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
                    background: Rectangle {
                        color: "transparent"
                        }

                    TabBtn {
                        text: qsTr("Main")
                        }
                    TabBtn {
                        text: qsTr("Recipes")
                        }
                    TabBtn {
                        text: qsTr("Machines")
                        }
                    TabBtn {
                        text: qsTr("Config")
                        }
                    TabBtn {
                        text: qsTr("Manual")
                        }
                    }

                // Spacer pushes the Cam button to the right edge
                Item {
                    Layout.fillWidth: true
                    }

                // Fixture selector — choose the active fixture for Cam
                ComboBox {
                    id: fixtureComboBox
                    Layout.preferredWidth: 160
                    Layout.alignment: Qt.AlignVCenter
                    Layout.rightMargin: 6
                    flat: true
                    // Model: prepend a "---" (no fixture) entry to the fixture list
                    model: {
                        var fixtures = (ZCam.project ? ZCam.project.fixtures : []);
                        var items = [
                                {
                                "name": "---"
                                }
                        ];
                        for (var i = 0; i < fixtures.length; ++i)
                            items.push(fixtures[i]);
                        return items;
                        }
                    textRole: "name"
                    currentIndex: {
                        if (!ZCam.project || !ZCam.project.fixture)
                            return 0;  // the "---" entry
                        var idx = ZCam.project.fixtures.indexOf(ZCam.project.fixture);
                        return idx >= 0 ? idx + 1 : 0;  // +1 to account for "---"
                        }
                    onActivated: function (index) {
                        if (!ZCam.project)
                            return;
                        if (index === 0) {
                            // "---" selected — no fixture
                            // Only allow this if there are fixtures to deselect
                            // (otherwise there's nothing to change)
                            if (ZCam.project.fixture)
                                ZCam.project.fixture = null;
                            } else {
                            var fixtures = ZCam.project.fixtures;
                            var fi = index - 1;  // offset for "---" entry
                            if (fi >= 0 && fi < fixtures.length)
                                ZCam.project.fixture = fixtures[fi];
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

                // Tab 4 – Manual (MkDocs generated HTML via WebEngine)
                ManualPanel {
                    id: manualPanel
                    }
                }

            // ── AI side panel (toggled by the "AI" toolbar button) ──────
            AiPanel {
                id: aiPanel
                visible: actionShowAiPanel.checked
                SplitView.minimumWidth: 300
                SplitView.preferredWidth: 420
                SplitView.maximumWidth: 800
                }

            // -- Script Console side panel --
            ScriptPanel {
                id: scriptPanel
                visible: actionShowScriptPanel.checked
                SplitView.minimumWidth: 300
                SplitView.preferredWidth: 400
                SplitView.maximumWidth: 800
                }

            LaserPanel {
                id: laserPanel
                visible: actionShowLaserPanel.checked
                SplitView.minimumWidth: 300
                SplitView.maximumWidth: 300
                }
            }

        // ── Status bar ────────────────────────────────────────────────────────
        Rectangle {
            id: statusBar
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: Material.color(Material.BlueGrey, Material.Shade800)

            property string message: ""
            property color messageColor: Material.foreground

            function show(text, color) {
                statusBar.message = text;
                statusBar.messageColor = color !== undefined ? color : Material.foreground;
                statusTimer.restart();
                }

            Timer {
                id: statusTimer
                interval: 4000
                onTriggered: statusBar.message = ""
                }

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                text: statusBar.message
                color: statusBar.messageColor
                elide: Text.ElideRight
                }
            }
        }

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
            actionShowMediaBrowser.checked = true;
            }

        // Switch to the Recipes tab and select the requested recipe.
        function onRecipeEditorRequested(name) {
            tabBar.currentIndex = 1;   // Recipes tab
            configRecipes.selectRecipeByName(name);
            }

        // Status bar: project saved
        function onProjectSaved(path) {
            var name = path !== "" ? path.replace(/.*\//, "") : "project";
            statusBar.show(qsTr("%1 saved").arg(name), Material.color(Material.Green, Material.Shade400));
            }

        // Status bar: assets saved
        function onAssetsSaved() {
            statusBar.show(qsTr("Assets saved"), Material.color(Material.Green, Material.Shade400));
            }

        // Status bar: SVG exported
        function onSvgExported(path) {
            var name = path !== "" ? path.replace(/.*\//, "") : "svg";
            statusBar.show(qsTr("%1 exported").arg(name), Material.color(Material.Green, Material.Shade400));
            }

        // Status bar: Nest finished — reported by Nest::nest() *after* it
        // applied the packing results.  Also shown when nothing could be
        // packed (packed === 0) so "Run Nest" never looks like a no-op.
        function onNestFinished(packed, total) {
            if (packed > 0)
                statusBar.show(qsTr("Nest: %1 of %2 item(s) packed").arg(packed).arg(total),
                    Material.color(Material.Green, Material.Shade400));
            else
                statusBar.show(qsTr("Nest: no items fit into the bin"),
                    Material.color(Material.Red, Material.Shade300));
            }
        }
    }
