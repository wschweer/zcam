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
import QtQuick.Controls.Material
import QtQuick.Layouts
import Qt.labs.qmlmodels
import ZCam

Rectangle {
    Material.theme: Material.Dark
    color: Material.background

    SplitView {
        orientation: Qt.Horizontal
        anchors.fill: parent
        ListView {
            id: categories
            SplitView.preferredWidth: 300
            currentIndex: 0
            model: ListModel {
                ListElement { category: "Gui" }
                ListElement { category: "Colors" }
                }
            delegate: ItemDelegate {
                text: category
                font: ZCam.style.font
                Layout.margins: 10
                width: categories.width
                highlighted: ListView.isCurrentItem
                onClicked: {
                    categories.currentIndex = index
                    }
                }
            }
        StackLayout {
            id: stack
            currentIndex: categories.currentIndex
            ConfigGuiPanel {}
            ConfigColorsPanel {}
            }
        }
    }
