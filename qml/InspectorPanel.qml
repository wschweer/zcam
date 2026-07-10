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

    InspectorModel {
        id: inspectorModel
        element: ZCam.currentElement
        }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // ── Title ─────────────────────────────────────────────────────────────
        Label {
            text: inspectorModel.title.length > 0 ? inspectorModel.title : "No Selection"
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 2
            elide: Text.ElideRight
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            color: Material.accentColor
            }

        // ── Thin accent divider ───────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Material.accentColor
            opacity: 0.4
            Layout.bottomMargin: 2
            }

        // ── Property editor ───────────────────────────────────────────────────
        PropertyEditor {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: inspectorModel
            propertiesJson: inspectorModel.propertiesJson
            }
        }
    }