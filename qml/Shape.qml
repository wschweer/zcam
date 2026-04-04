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
import QtQuick3D
import ZCam

Model {
    id: model
    property Element element
    property alias color: material.baseColor
    property alias lineWidth: material.lineWidth
    position: Qt.vector3d(60,60,60)
    scale:    Qt.vector3d(6,6,6)

    pickable: true
    materials: [
       PrincipledMaterial {
            id: material
            cullMode: PrincipledMaterial.NoCulling
            lineWidth: 3
            lighting: PrincipledMaterial.NoLighting
            }
       ]
    }
