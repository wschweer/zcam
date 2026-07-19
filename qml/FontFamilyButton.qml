//=============================================================================
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
// import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root

    // ── Public interface ──────────────────────────────────────────────────────

    /// The currently displayed font family name.
    property string family: ""

    /// When true, clicking the "…" button toggles the media browser to the
    /// Fonts panel instead of opening the system font dialog.
    property bool useMediaBrowser: true

    /// Emitted when the user accepts the dialog or clicks Apply in the media browser.
    /// @param family  The newly chosen font family name.
    signal familySelected(string family)

    // ── Layout ────────────────────────────────────────────────────────────────

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    RowLayout {
        id: row
        anchors.fill: parent
        //        spacing: 4

        // Family name label – fills available width, clips long names
        Label {
            id: familyLabel
            Layout.fillWidth: true
            text: root.family.length > 0 ? root.family : "—"
            elide: Text.ElideRight
            font.family: root.family   // render in the chosen face for preview
            color: Material.foreground
            //            Layout.alignment: Qt.AlignVCenter
            }

        // "…" button opens the font selector (media browser or system dialog)
        Button {
            id: pickButton
            text: "…"
            flat: true
            implicitWidth: implicitHeight   // square
            //            Layout.alignment: Qt.AlignVCenter
            //            padding: 4
            implicitHeight: parent.height

            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: root.useMediaBrowser ? "Choose font family from media browser" : "Choose font family"

            onClicked: {
                if (root.useMediaBrowser)
                    ZCam.showFontMediaBrowserRequested();
                else
                    fontDialog.open();
                }
            }
        }

    // ── Font Dialog (fallback when useMediaBrowser is false) ──────────────────

    FontDialog {
        id: fontDialog
        flags: FontDialog.DontUseNativeDialog

        title: "Choose Font Family"

        /*  Material settings have no effect:
        Material.theme: Material.Light
        Material.accent: Material.Teal
        Material.primary: Material.BlueGrey
        Material.background: Material.color(Material.BlueGrey, Material.Shade900)
*/

        // Pre-select the current family so the dialog opens at the right place.
        // We keep the current point size neutral (12 pt) – only the family matters.
        currentFont: Qt.font({
            family: root.family,
            pointSize: 12
            })

        // ScalableFonts: show only outline fonts (no bitmap raster fonts)
        // MonospacedFonts / ProportionalFonts: show all
        options: FontDialog.ScalableFonts

        onAccepted: {
            root.familySelected(selectedFont.family);
            }
        }
    }
