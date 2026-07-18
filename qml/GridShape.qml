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
    property alias color: majorMaterial.baseColor
    property alias lineWidth: majorMaterial.lineWidth

    pickable: false
    geometry: element ? element.geometry : null
    position: element ? element.pos : Qt.vector3d(0, 0, 0)
//    color: element ? element.curColor : Qt.rgba(0.2, 0.2, 0.2, 1.0)
    color: Qt.rgba(0.7, 0.7, 0.7, 1.0)
    materials: [
        PrincipledMaterial {
            id: majorMaterial
            cullMode: PrincipledMaterial.NoCulling
            lineWidth: 1.0
            lighting: PrincipledMaterial.NoLighting
        }
    ]

    // Minor (subraster) lines — lighter colour
    Model {
        id: minorModel
        parent: model
        geometry: element ? element.minorGeometry : null
        position: Qt.vector3d(0, 0, 0)
        pickable: false
        materials: [
            PrincipledMaterial {
                cullMode: PrincipledMaterial.NoCulling
                lineWidth: 0.0
                lighting: PrincipledMaterial.NoLighting
                baseColor: Qt.rgba(0.4, 0.4, 0.4, 1.0)
            }
        ]
    }
}