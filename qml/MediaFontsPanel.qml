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
    property string selectedFamily: ""

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
        fontListContainer.activate()
        }

    Connections {
        target: fontModel
        function onCurrentFamilyChanged() {
            fontSettings.currentFamily = fontModel.currentFamily
            if (fontModel.currentFamily !== root.selectedFamily)
                updateCurrentIndex()
            }
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
            Rectangle {
                id: fontListContainer
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"
                border.width: activeFocus ? 1 : 0
                border.color: Material.accentColor

                property alias currentIndex: fontList.currentIndex

                function activate() {
                    fontList.forceActiveFocus()
                    }

                Keys.forwardTo: [fontList]

                ListView {
                    id: fontList
                    anchors.fill: parent
                    anchors.margins: activeFocus ? 1 : 0
                    clip: true
                    model: fontModel
                    currentIndex: -1
                    focus: true
                    activeFocusOnTab: true
                    keyNavigationEnabled: true
                    keyNavigationWraps: false
                    highlightFollowsCurrentItem: true

                    Component.onCompleted: updateCurrentIndex()

                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    onCurrentIndexChanged: {
                        console.log("currentIndex changed:", currentIndex)
                        var family = fontModel.familyAt(currentIndex)
                        console.log("family from model:", family)
                        if (family === undefined || family === null)
                            family = ""
                        root.selectedFamily = family
                        console.log("selectedFamily:", root.selectedFamily)
                        if (family !== fontModel.currentFamily)
                            fontModel.currentFamily = family
                        }

                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Delete && currentIndex >= 0) {
                            var family = fontModel.familyAt(currentIndex)
                            if (fontModel.showFavorites)
                                fontModel.removeFavorite(family)
                            event.accepted = true
                            }
                        }

                    delegate: Rectangle {
                        id: fontDelegate
                        width: ListView.view.width
                        height: 32
                        color: fontDelegate.ListView.isCurrentItem ? Material.color(Material.Teal, Material.Shade700) : "transparent"

                        RowLayout {
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
                                color: fontDelegate.ListView.isCurrentItem ? Material.accentColor : Material.foreground
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                }
                            }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                console.log("mouse clicked index:", index, "model.family:", model.family)
                                fontList.currentIndex = index
                                fontList.forceActiveFocus()
                                }
                            }
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
                Layout.preferredHeight: 56
                color: Material.color(Material.BlueGrey, Material.Shade900)

                Column {
                    anchors.centerIn: parent
                    spacing: 2

                    Label {
                        text: root.selectedFamily
                        color: Material.accentColor
                        font.bold: true
                        }

                    Label {
                        text: "idx:" + fontList.currentIndex + " sel:" + root.selectedFamily + " fam:" + fontModel.currentFamily
                        color: Material.foreground
                        font.pixelSize: 10
                        }
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
                    model: fontModel.stylesForFamily(root.selectedFamily)
                    currentIndex: {
                        var styles = fontModel.stylesForFamily(root.selectedFamily)
                        var idx = styles.indexOf(fontModel.currentStyle)
                        return idx >= 0 ? idx : -1
                        }
                    onActivated: {
                        fontModel.currentStyle = styleCombo.model[index]
                        }
                    }

                // Add to favorites button
                Button {
                    text: fontModel.isFavorite(root.selectedFamily) ? "★" : "☆"
                    flat: true
                    ToolTip.visible: hovered
                    ToolTip.text: fontModel.isFavorite(root.selectedFamily) ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                    onClicked: {
                        if (fontModel.isFavorite(root.selectedFamily))
                            fontModel.removeFavorite(root.selectedFamily)
                        else
                            fontModel.addFavorite(root.selectedFamily)
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
                        font.family: root.selectedFamily
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