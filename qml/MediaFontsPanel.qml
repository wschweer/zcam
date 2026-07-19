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

    property real previewScale: 1.0

    FontModel {
        id: fontModel
        }

    Settings {
        id: fontSettings
        category: "MediaFontsPanel"
        property string currentFamily: ""
        property string currentStyle: ""
        property bool showFavorites: false
        property string sampleText: "The quick brown fox jumps over the lazy dog"
        property real previewScale: 1.0
        property var splitState
        }

    function updateCurrentIndex() {
        var idx = -1
        for (var i = 0; i < fontModel.rowCount(); ++i) {
            if (fontModel.data(fontModel.index(i, 0), FontModel.FamilyRole) === fontModel.currentFamily) {
                idx = i
                break
                }
            }
        fontList.currentIndex = idx
        if (idx >= 0)
            fontList.positionViewAtIndex(idx, ListView.Contain)
        }

    Component.onCompleted: {
        fontModel.currentFamily = fontSettings.currentFamily || (fontModel.allFamilies().length > 0 ? fontModel.allFamilies()[0] : "")
        fontModel.currentStyle = fontSettings.currentStyle
        fontModel.showFavorites = fontSettings.showFavorites
        root.previewScale = fontSettings.previewScale
        if (fontSettings.splitState)
            splitView.restoreState(fontSettings.splitState)
        updateCurrentIndex()
        }

    Connections {
        target: fontModel
        function onCurrentFamilyChanged() { fontSettings.currentFamily = fontModel.currentFamily; updateCurrentIndex() }
        function onCurrentStyleChanged() { fontSettings.currentStyle = fontModel.currentStyle }
        function onShowFavoritesChanged() { fontSettings.showFavorites = fontModel.showFavorites }
        function onFavoritesChanged() { updateCurrentIndex() }
        }

    onPreviewScaleChanged: fontSettings.previewScale = root.previewScale

    SplitView {
        id: splitView
        anchors.fill: parent
        orientation: Qt.Horizontal
        onResizingChanged: if (!resizing) fontSettings.splitState = saveState()

        // ── Left: font list ────────────────────────────────────────────────
        ColumnLayout {
            SplitView.preferredWidth: 200
            SplitView.minimumWidth: 120
            spacing: 0

            // Toggle between all fonts and favorites
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 4
                spacing: 4

                Button {
                    text: qsTr("All")
                    flat: true
                    checkable: true
                    checked: !fontModel.showFavorites
                    onClicked: fontModel.showFavorites = false
                    Material.foreground: checked ? Material.accentColor : Material.foreground
                    Layout.fillWidth: true
                    }
                Button {
                    text: qsTr("Favs")
                    flat: true
                    checkable: true
                    checked: fontModel.showFavorites
                    onClicked: fontModel.showFavorites = true
                    Material.foreground: checked ? Material.accentColor : Material.foreground
                    Layout.fillWidth: true
                    }
                }

            // Font list
            ListView {
                id: fontList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: fontModel
                currentIndex: -1

                Component.onCompleted: updateCurrentIndex()

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: ItemDelegate {
                    width: ListView.view.width
                    height: 32

                    background: Rectangle {
                        color: ListView.isCurrentItem ? Material.color(Material.Teal, Material.Shade700) : "transparent"
                        }

                    contentItem: RowLayout {
                        anchors.fill: parent
                        spacing: 4

                        // Favorite star
                        Label {
                            text: "★"
                            color: Material.accentColor
                            font.pixelSize: 14
                            visible: model.isFavorite
                            Layout.preferredWidth: 20
                            }

                        // Font family name rendered in the actual font
                        Label {
                            text: model.family
                            font.family: model.family
                            color: ListView.isCurrentItem ? Material.accentColor : Material.foreground
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            }
                        }

                    onClicked: {
                        fontModel.currentFamily = model.family
                        }

                    Keys.onDeletePressed: {
                        if (fontModel.showFavorites)
                            fontModel.removeFavorite(model.family)
                        }
                    }
                }
            }

        // ── Right: font preview ────────────────────────────────────────────
        ColumnLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 200
            spacing: 4

            // Current font name as title
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: Material.color(Material.BlueGrey, Material.Shade900)

                Label {
                    anchors.centerIn: parent
                    text: fontModel.currentFamily
                    color: Material.accentColor
                    font.bold: true
                    }
                }

            // Style selection row
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 4
                spacing: 8

                Label {
                    text: qsTr("Style:")
                    color: Material.foreground
                    }

                ComboBox {
                    id: styleCombo
                    Layout.fillWidth: true
                    model: fontModel.stylesForFamily(fontModel.currentFamily)
                    currentIndex: {
                        var styles = fontModel.stylesForFamily(fontModel.currentFamily)
                        var idx = styles.indexOf(fontModel.currentStyle)
                        return idx >= 0 ? idx : -1
                        }
                    onActivated: {
                        fontModel.currentStyle = styleCombo.model[index]
                        }
                    }

                // Add to favorites button
                Button {
                    text: fontModel.isFavorite(fontModel.currentFamily) ? "★" : "☆"
                    flat: true
                    ToolTip.visible: hovered
                    ToolTip.text: fontModel.isFavorite(fontModel.currentFamily) ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                    onClicked: {
                        if (fontModel.isFavorite(fontModel.currentFamily))
                            fontModel.removeFavorite(fontModel.currentFamily)
                        else
                            fontModel.addFavorite(fontModel.currentFamily)
                        }
                    }
                }

            // Sample text editor
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                clip: true

                TextArea {
                    id: sampleTextEdit
                    text: fontSettings.sampleText
                    placeholderText: qsTr("Enter sample text…")
                    wrapMode: TextArea.Wrap
                    color: Material.foreground
                    onTextChanged: fontSettings.sampleText = text
                    }
                }

            // Font preview area
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScrollView {
                    anchors.fill: parent
                    clip: true

                    TextArea {
                        id: previewText
                        readOnly: true
                        text: fontSettings.sampleText
                        wrapMode: TextArea.Wrap
                        color: Material.foreground
                        font.family: fontModel.currentFamily
                        font.pointSize: 24 * root.previewScale
                        font.styleName: fontModel.currentStyle
                        font.bold: fontModel.currentStyle.toLowerCase().indexOf("bold") >= 0
                        font.italic: fontModel.currentStyle.toLowerCase().indexOf("italic") >= 0 || fontModel.currentStyle.toLowerCase().indexOf("oblique") >= 0
                        background: Rectangle {
                            color: Material.color(Material.BlueGrey, Material.Shade900)
                            radius: 4
                            }
                        }
                    }

                // Ctrl+wheel to scale the preview font size
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    onWheel: wheel => {
                        if (wheel.modifiers & Qt.ControlModifier) {
                            if (wheel.angleDelta.y > 0)
                                root.previewScale = Math.min(6.0, root.previewScale * 1.15)
                            else
                                root.previewScale = Math.max(0.2, root.previewScale / 1.15)
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