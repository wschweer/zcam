//=============================================================================
//  ZCam
//
//  Copyright (C) 2024-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Dialogs
import ZCam

Button {
    id: configColor
    property alias color: background.color

    implicitWidth: 150
    Rectangle {
        id: background
        anchors.fill: parent
        border.width: 2
        border.color: "white"
        radius: 5
        }
    onClicked: {
        colorDialog.selectedColor = background.color
        colorDialog.open()
        }
    ColorDialog {
        id: colorDialog
        options: ColorDialog.DontUseNativeDialog
        onAccepted: {
            background.color = selectedColor
            }
        }
    }
