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

Item {
    id: root

    property real tileScale: 1.0
    property real baseTileSize: 120
    property string selectedFilePath: ""
    property string selectedFileType: ""

    // Checkerboard background for transparent images.
    Component {
        id: checkerboardComponent
        Canvas {
            property int sqSize: 10
            property color colLight: "#c8c8c8"
            property color colDark: "#787878"
            onPaint: {
                let ctx = getContext("2d")
                ctx.reset()
                let w = width
                let h = height
                let s = sqSize
                for (let y = 0; y < h; y += s) {
                    for (let x = 0; x < w; x += s) {
                        let odd = ((Math.floor(x / s) + Math.floor(y / s)) % 2) === 1
                        ctx.fillStyle = odd ? colDark : colLight
                        ctx.fillRect(x, y, s, s)
                    }
                }
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }
    }

    ArtworkTreeModel {
        id: artworkModel
        rootPath: ZCam.config ? ZCam.config.artworkDirectory : ""
        }

    Settings {
        id: artworkSettings
        category: "MediaArtworkPanel"
        property var splitState
        property string currentDir: ""
        }

    function restoreTreeSelection() {
        if (!currentDirPath)
            return
        let idx = artworkModel.findIndexForPath(currentDirPath)
        if (!idx.valid)
            return
        // Collect ancestors from root down to target
        let ancestors = []
        let p = idx
        while (p.valid) {
            ancestors.unshift(p)
            p = artworkModel.parent(p)
        }
        // Expand from root down, computing the TreeView *visible* row.
        // On startup nothing is expanded, so each child appears directly
        // after its parent:  treeViewRow = parentRow + 1 + childModelRow
        let tvRow = 0
        for (let i = 0; i < ancestors.length; ++i) {
            let a = ancestors[i]
            if (i === 0)
                tvRow = a.row
            else
                tvRow = tvRow + 1 + a.row
            if (!dirTree.isExpanded(tvRow))
                dirTree.toggleExpanded(tvRow)
        }
        dirTree.selectionModel.setCurrentIndex(idx, ItemSelectionModel.ClearAndSelect)
    }

    Component.onCompleted: {
        if (artworkSettings.splitState)
            splitView.restoreState(artworkSettings.splitState)
        if (artworkSettings.currentDir)
            currentDirPath = artworkSettings.currentDir
        Qt.callLater(restoreTreeSelection)
    }

    Connections {
        target: ZCam.config
        function onArtworkDirectoryChanged() {
            artworkModel.rootPath = ZCam.config ? ZCam.config.artworkDirectory : ""
            }
        ignoreUnknownSignals: true
        }

    property string currentDirPath: ""

    onCurrentDirPathChanged: {
        artworkSettings.currentDir = currentDirPath
        imageGrid.model = artworkModel.imageFiles(currentDirPath)
        }

    SplitView {
        id: splitView
        anchors.fill: parent
        orientation: Qt.Horizontal
        onResizingChanged: if (!resizing) artworkSettings.splitState = saveState()

        // ── Left: directory tree ──────────────────────────────────────────
        ColumnLayout {
            SplitView.preferredWidth: 200
            SplitView.minimumWidth: 120
            spacing: 0

            TreeView {
                id: dirTree
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 4
                model: artworkModel
                clip: true
                focus: true

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                selectionModel: ItemSelectionModel {
                    id: dirTreeSelModel
                    model: artworkModel
                    onCurrentChanged: function(current, previous) {
                        if (current.valid) {
                            var p = artworkModel.data(current, ArtworkTreeModel.PathRole)
                            root.currentDirPath = p !== undefined ? p : ""
                        }
                    }
                }

                delegate: ItemDelegate {
                    id: dirDelegate
                    required property TreeView treeView
                    required property bool isTreeNode
                    required property bool expanded
                    required property int hasChildren
                    required property int depth
                    required property int row
                    required property int column
                    required property bool current

                    implicitWidth: treeView.width
                    implicitHeight: 28
                    highlighted: current
                    clip: true
                    padding: 0
                    leftPadding: 8 + depth * 20
                    rightPadding: 8
                    topPadding: 2
                    bottomPadding: 2

                    background: Rectangle {
                        anchors.fill: parent
                        color: dirDelegate.highlighted ? Material.color(Material.Teal, Material.Shade700) : "transparent"
                        }

                    contentItem: RowLayout {
                        spacing: 4

                        Label {
                            text: dirDelegate.hasChildren ? (dirDelegate.expanded ? "▾" : "▸") : ""
                            font.pixelSize: 10
                            color: Material.foreground
                            Layout.preferredWidth: 12
                            verticalAlignment: Text.AlignVCenter
                            }

                        Label {
                            text: model.dirName
                            color: dirDelegate.highlighted ? Material.accentColor : Material.foreground
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            }
                        }

                    onClicked: {
                        treeView.selectionModel.setCurrentIndex(treeView.index(row, column), ItemSelectionModel.ClearAndSelect)
                        root.currentDirPath = model.dirPath
                        if (dirDelegate.hasChildren)
                            treeView.toggleExpanded(row)
                        dirTree.forceActiveFocus()
                        }
                    }
                }
            }

        // ── Right: image grid ─────────────────────────────────────────────
        ColumnLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 200
            spacing: 0

            // Header
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: Material.color(Material.BlueGrey, Material.Shade900)

                Label {
                    anchors.centerIn: parent
                    text: {
                        if (currentDirPath === "")
                            return qsTr("Images")
                        let root = ZCam.config ? ZCam.expandPath(ZCam.config.artworkDirectory) : ""
                        if (root !== "" && currentDirPath.startsWith(root + "/"))
                            return currentDirPath.substring(root.length + 1)
                        return currentDirPath
                    }
                    color: Material.accentColor
                    font.bold: true
                    elide: Text.ElideMiddle
                }
                }

            // Content area
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true

                    GridView {
                        id: imageGrid
                        width: parent.width
                        height: parent.height
                        clip: true

                        // Number of columns that fit based on the nominal tile width
                        readonly property int columnCount: {
                            if (width <= 0)
                                return 1
                            var nominal = root.baseTileSize * root.tileScale
                            return Math.max(1, Math.floor(width / nominal))
                        }

                        // Actual cell width: distribute the full grid width
                        // evenly across all columns so no empty margin remains
                        cellWidth: width > 0 && columnCount > 0
                                   ? width / columnCount
                                   : root.baseTileSize * root.tileScale
                        cellHeight: root.baseTileSize * root.tileScale

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Item {
                            width: imageGrid.cellWidth
                            height: imageGrid.cellHeight

                            property bool isSelected: false
                            // Cache DXF-to-SVG preview URL (file:// path to temp SVG file)
                            property string dxfPreviewUrl: {
                                if (modelData.fileType.toLowerCase() === "dxf")
                                    return artworkModel.dxfToSvgFile(modelData.filePath, ZCam.config ? ZCam.config.dxfScale : 72.0)
                                return ""
                                }

                            Rectangle {
                                id: tileBg
                                anchors.fill: parent
                                anchors.margins: 4
                                color: "transparent"
                                radius: 4
                                clip: true

                                // Checkerboard background for transparent images (not DXF)
                                Loader {
                                    anchors.fill: parent
                                    active: modelData.fileType.toLowerCase() !== "dxf"
                                    sourceComponent: checkerboardComponent
                                }

                                // Plain light gray background for DXF previews
                                Rectangle {
                                    anchors.fill: parent
                                    color: "#e0e0e0"
                                    visible: modelData.fileType.toLowerCase() === "dxf"
                                }

                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 0

                                    // Thumbnail
                                    Item {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        Layout.margins: 4

                                        Image {
                                            anchors.fill: parent
                                            property bool isDxf: modelData.fileType.toLowerCase() === "dxf"
                                            source: {
                                                var ft = modelData.fileType.toLowerCase()
                                                if (ft === "png" || ft === "svg")
                                                    return "file://" + modelData.filePath
                                                if (ft === "dxf" && dxfPreviewUrl.length > 0)
                                                    return dxfPreviewUrl
                                                return ""
                                                }
                                            fillMode: Image.PreserveAspectFit
                                            sourceSize.width: isDxf ? 1024 : 200
                                            sourceSize.height: isDxf ? 1024 : 200
                                            cache: false
                                            mipmap: true
                                            smooth: true
                                            }

                                        // DXF fallback label (shown when conversion fails)
                                        Label {
                                            anchors.centerIn: parent
                                            text: "DXF"
                                            visible: modelData.fileType.toLowerCase() === "dxf" && dxfPreviewUrl.length === 0
                                            color: "#333333"
                                            font.bold: true
                                            font.pixelSize: 24
                                            }
                                        }

                                    // Filename
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.margins: 2
                                        text: modelData.fileName
                                        elide: Text.ElideMiddle
                                        font.pixelSize: 10
                                        color: "#333333"
                                        horizontalAlignment: Text.AlignHCenter
                                        }
                                    }
                                }

                            // Selection border — drawn on top of all content
                            Rectangle {
                                anchors.fill: tileBg
                                color: "transparent"
                                border.width: isSelected ? 3 : 1
                                border.color: isSelected ? Material.accentColor : Material.color(Material.BlueGrey, Material.Shade300)
                                radius: 4
                                z: 100
                            }

                            // Click to select + drag to 3D canvas
                            MouseArea {
                                id: tileMouseArea
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                hoverEnabled: true

                                onClicked: {
                                    // Deselect all other tiles
                                    for (var i = 0; i < imageGrid.count; ++i) {
                                        var item = imageGrid.itemAtIndex(i)
                                        if (item)
                                            item.isSelected = false
                                        }
                                    isSelected = true
                                    root.selectedFilePath = modelData.filePath
                                    root.selectedFileType = modelData.fileType
                                    }

                                onPressed: {
                                    // Ensure this tile is selected before dragging
                                    for (var i = 0; i < imageGrid.count; ++i) {
                                        var item = imageGrid.itemAtIndex(i)
                                        if (item)
                                            item.isSelected = false
                                        }
                                    isSelected = true
                                    root.selectedFilePath = modelData.filePath
                                    root.selectedFileType = modelData.fileType
                                    }

                                // Drag support for drag&drop to 3D canvas
                                drag.target: parent
                                drag.threshold: 10
                                }

                            // QML Drag — provides mimeData to DropArea
                            Drag.active: tileMouseArea.pressed
                            Drag.dragType: Drag.Automatic
                            Drag.mimeData: {
                                "text/uri-list": "file://" + modelData.filePath
                                }
                            }
                        }
                    }

                // Label shown when no root path is configured
                Label {
                    anchors.centerIn: parent
                    text: qsTr("No artwork directory configured.\nGo to Config → Project → Artwork Directory")
                    color: Material.foreground
                    horizontalAlignment: Text.AlignHCenter
                    visible: ZCam.config && ZCam.config.artworkDirectory === ""
                    font.pixelSize: 14
                    }

                // Ctrl+wheel to scale tiles — placed after ScrollView so it is on top
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    onWheel: wheel => {
                        if (wheel.modifiers & Qt.ControlModifier) {
                            if (wheel.angleDelta.y > 0)
                                root.tileScale = Math.min(3.0, root.tileScale * 1.15)
                            else
                                root.tileScale = Math.max(0.3, root.tileScale / 1.15)
                            wheel.accepted = true
                            }
                        else {
                            wheel.accepted = false
                            }
                        }
                    }
                }
            }
        }
    }