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

// FontFamilyButton
//
// Displays the current font family name and opens a FontDialog on click.
// Only the family name is written back – weight, size, etc. are untouched.
//
// Usage:
//   FontFamilyButton {
//       family: model.propValue      // current family string
//       onFamilySelected: model.propValue = family
//   }

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root

    // ── Public interface ──────────────────────────────────────────────────────

    /// The currently displayed font family name.
    property string family: ""

    /// Emitted when the user accepts the dialog.
    /// @param family  The newly chosen font family name.
    signal familySelected(string family)

    // ── Layout ────────────────────────────────────────────────────────────────

    implicitWidth:  row.implicitWidth
    implicitHeight: row.implicitHeight

    RowLayout {
        id: row
        anchors.fill: parent
        spacing: 4

        // Family name label – fills available width, clips long names
        Label {
            id: familyLabel
            Layout.fillWidth: true
            text: root.family.length > 0 ? root.family : "—"
            elide: Text.ElideRight
            font.family: root.family   // render in the chosen face for preview
            color: Material.foreground
            Layout.alignment: Qt.AlignVCenter
        }

        // "…" button opens the dialog
        Button {
            id: pickButton
            text: "…"
            flat: true
            implicitWidth:  implicitHeight   // square
            Layout.alignment: Qt.AlignVCenter
            padding: 4

            ToolTip.visible: hovered
            ToolTip.delay:   600
            ToolTip.text:    "Choose font family"

            onClicked: fontDialog.open()
        }
    }

    // ── Font Dialog ───────────────────────────────────────────────────────────

    FontDialog {
        id: fontDialog

        title: "Choose Font Family"

        // Pre-select the current family so the dialog opens at the right place.
        // We keep the current point size neutral (12 pt) – only the family matters.
        currentFont: Qt.font({ family: root.family, pointSize: 12 })

        // ScalableFonts: show only outline fonts (no bitmap raster fonts)
        // MonospacedFonts / ProportionalFonts: show all
        options: FontDialog.ScalableFonts

        onAccepted: {
            root.familySelected(selectedFont.family)
        }
    }
}
