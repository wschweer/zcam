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

import QtQuick
import QtQuick3D
import ZCam

Model {
    id: model
    property Element element
    property alias color: material.baseColor
    property alias lineWidth: material.lineWidth
    position: Qt.vector3d(60, 60, 60)
    scale: Qt.vector3d(6, 6, 6)

    pickable: true
    materials: [
        PrincipledMaterial {
            id: material
            cullMode: PrincipledMaterial.NoCulling
            lineWidth: 3
            lighting: PrincipledMaterial.NoLighting
        }
    ]

    // Bounding-box overlay: shown when this element is the current selection.
    // The C++ side keeps element.selectionGeometry up to date with a rectangle
    // built from the element's path data.
    Model {
        id: bboxOverlay
        parent: model
        geometry: element ? element.selectionGeometry : null
        visible: element && ZCam.currentElement === element
        pickable: false
        materials: [
            PrincipledMaterial {
                cullMode: PrincipledMaterial.NoCulling
                lineWidth: 2
                lighting: PrincipledMaterial.NoLighting
                baseColor: Qt.rgba(1.0, 0.8, 0.0, 1.0) // yellow outline
            }
        ]
    }
}