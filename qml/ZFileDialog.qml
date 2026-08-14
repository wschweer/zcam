//=============================================================================
//  ZCam
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

// ZFileDialog — thin wrapper around QtQuick.Dialogs.FileDialog.
//
// Sets Material.theme: Material.Dark so the dialog background is dark.
// The sidebar text color is fixed by SideBarColorFixer (C++ event filter
// in main.cpp) which walks the dialog's item tree after it opens and
// sets every QQuickIconLabel with a default (black) color to white.
//
// API-compatible with FileDialog: same properties and signals.
// Usage: replace `FileDialog { }` with `ZFileDialog { }`.

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Dialogs

FileDialog {
    Material.theme: Material.Dark
}