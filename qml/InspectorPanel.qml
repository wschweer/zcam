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

        // ── Multi-selection indicator ─────────────────────────────────────────
        //   When more than one element is selected, show a small label
        //   indicating the total count of selected elements.
        Label {
            visible: ZCam.selectedElements.length > 1
            text: qsTr("%1 elements selected").arg(ZCam.selectedElements.length)
            font.italic: true
            font.pointSize: Math.max(7, Qt.application.font.pointSize - 2)
            Layout.alignment: Qt.AlignHCenter
            color: Material.foreground
            opacity: 0.7
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