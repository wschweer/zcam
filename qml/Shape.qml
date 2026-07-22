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

    // Bounding-box overlay: shown when this element is the current
    // selection or when it is being edited (text input mode).
    // Rendered as a sibling of the main model so it stays visible even
    // when the main model has no own geometry (e.g. Group elements whose
    // main geometry is the same selection rectangle).
    Model {
        id: bboxOverlay
        parent: model.parent
        property alias element: model.element
        geometry: element ? element.selectionGeometry : null
        visible: element && (ZCam.currentElement === element
               || (element.editing !== undefined && element.editing))
        position: model.position
        eulerRotation: model.eulerRotation
        scale: model.scale
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