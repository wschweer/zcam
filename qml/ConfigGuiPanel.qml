//=============================================================================
//  wcam
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
import QtQuick.Layouts
import Qt.labs.qmlmodels
import ZCam

Item {
    id: panel
//    property Element element: ZCam.config()

    Item {
        width: 400
        height: parent.height
/*
        ElementView {
            id: guiConfig
            wstyle: panel.wstyle
            element: panel.element
            anchors.topMargin: 20

            PropertyValue {
                wstyle: guiConfig.wstyle
                prop: {
                    panel.element.prop("handleSize")
                    }
                }
            }
*/
        }
    }
