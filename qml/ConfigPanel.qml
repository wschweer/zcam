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

// ── Config panel (placeholder) ────────────────────────────────────────────────
// This panel will host application-wide configuration settings.

Item {
    id: root

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "Configuration"
            font.pixelSize: 22
            font.bold: true
            color: Material.foreground
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "Application settings will appear here."
            color: Material.color(Material.BlueGrey, Material.Shade300)
        }
    }
}
