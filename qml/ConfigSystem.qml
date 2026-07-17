//=============================================================================
//  ZCam
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published in the file LICENSE.GPL
//=============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ZCam

Item {
    id: root

    Material.theme: Material.Dark

    Rectangle {
        anchors.fill: parent
        color: Material.color(Material.BlueGrey, Material.Shade900)
    }

    ConfigModel {
        id: configModel
        config: ZCam.config
    }

    SplitView {
        anchors.fill: parent
        anchors.margins: 8

        // Left Panel: Category List
        ColumnLayout {
            SplitView.preferredWidth: 180
            SplitView.minimumWidth: 140

            Label {
                text: "Categories"
                font.bold: true
                Layout.fillWidth: true
                Layout.bottomMargin: 4
                horizontalAlignment: Text.AlignHCenter
                color: Material.accentColor
            }

            // Thin accent divider
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Material.accentColor
                opacity: 0.4
                Layout.bottomMargin: 4
            }

            ListView {
                id: categoryList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: configModel.categories
                clip: true
                currentIndex: 0

                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: modelData
                    highlighted: ListView.isCurrentItem
                    onClicked: {
                        categoryList.currentIndex = index
                        configModel.categoryFilter = modelData
                    }
                }
            }

            Button {
                Layout.fillWidth: true
                text: "Save"
                onClicked: ZCam.saveAssets()
            }
        }

        // Right Panel: Property editor for the selected category
        ColumnLayout {
            SplitView.fillWidth: true

            // Title
            Label {
                text: configModel.title.length > 0 ? configModel.title : "Config"
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 2
                elide: Text.ElideRight
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                color: Material.accentColor
            }

            // Thin accent divider
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Material.accentColor
                opacity: 0.4
                Layout.bottomMargin: 2
            }

            // Property editor built from properties() JSON
            PropertyEditor {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: configModel
                propertiesJson: configModel.propertiesJson
                labelWidth: 140
            }
        }
    }

    Component.onCompleted: {
        if (configModel.categories.length > 0) {
            configModel.categoryFilter = configModel.categories[0]
        }
    }
}