//=============================================================================
//  ZCam
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL.
//=============================================================================

// LockCheckBox
//
// A compact, right-aligned CheckBox used inside ValueBox cells for
// lockScale / lockSize mode selectors.  It compares modeValue against
// modeIndex and emits activated(index) when toggled on.
//
// Usage:
//   LockCheckBox {
//       modeIndex: index
//       modeValue: rowLockScale.propValue
//       onActivated: (idx) => rowLockScale.setModelValue(idx)
//   }

import QtQuick
import QtQuick.Controls

CheckBox {
    id: control

    // ── Public interface ─────────────────────────────────────────────

    /// The index of this checkbox within its Repeater.
    property int modeIndex: 0

    /// The current model value.  The checkbox is checked when
    /// Number(modeValue) === modeIndex.
    property var modeValue: undefined

    /// Emitted with modeIndex when the user toggles the checkbox on.
    signal activated(int index)

    // ── Styling ──────────────────────────────────────────────────────

    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
    anchors.right: parent ? parent.right : undefined
    width: 18
    text: ""

    checked: modeValue !== undefined ? Number(modeValue) === modeIndex : false

    onToggled: {
        if (checked)
            control.activated(control.modeIndex)
    }
}