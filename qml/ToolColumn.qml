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
import QtQuick.Layouts

Item {
    default property alias content: internalLayout.data
    implicitWidth:  internalLayout.implicitWidth + 8
    implicitHeight: internalLayout.implicitHeight + 8

    Rectangle {
        anchors.fill: parent
        color: "#30000000"
        radius: 10
    }

    ColumnLayout {
        id: internalLayout
        anchors.fill: parent
        anchors.margins: 4
    }
}
