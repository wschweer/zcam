//=============================================================================
//  ZCam
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

import QtQuick
import QtQuick3D
import ZCam

// Shape for ImageElement: displays a textured quad on the XY plane.
// The geometry is a unit quad (1×1, centered at origin) that is
// scaled by the element's scale property to the physical mm size.
// The texture comes from ImageTextureData which loads the image file.
Model {
    id: model
    property Element element
    property alias color: material.baseColor
    geometry: element ? element.planeGeometry : null
    pickable: true
    materials: [
        PrincipledMaterial {
            id: material
            cullMode: PrincipledMaterial.NoCulling
            lighting: PrincipledMaterial.NoLighting
            baseColor: "white"
            opacity: element ? element.opacity : 1.0
            baseColorMap: Texture {
                textureData: element ? element.textureData : null
            }
        }
    ]

    // Selection / hover overlay (yellow bounding-box outline).
    Model {
        id: bboxOverlay
        parent: model.parent
        property alias element: model.element
        geometry: element ? element.selectionGeometry : null
        visible: element && (ZCam.currentElement === element
               || (ZCam.selectedElements && ZCam.selectedElements.indexOf(element) >= 0))
        onVisibleChanged: {
            if (visible)
                geometry = element ? element.selectionGeometry : null;
            }
        position: model.position
        eulerRotation: model.eulerRotation
        scale: model.scale
        pickable: false
        materials: [
            PrincipledMaterial {
                cullMode: PrincipledMaterial.NoCulling
                lineWidth: 2
                lighting: PrincipledMaterial.NoLighting
                baseColor: Qt.rgba(1.0, 0.8, 0.0, 1.0)  // yellow outline
            }
        ]
    }
}