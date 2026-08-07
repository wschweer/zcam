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

// Shape for BREP elements: shows the OCCT tessellated mesh
// plus a selection overlay when highlighted.
Model {
    id: model
    property Element element
    property alias color: material.baseColor
    property alias lineWidth: material.lineWidth
    geometry: element ? element.brepGeometry : null
    pickable: true
    materials: [
        PrincipledMaterial {
            id: material
            cullMode: PrincipledMaterial.NoCulling
            lineWidth: 3
            lighting: PrincipledMaterial.FragmentLighting
            // steel-blue is taken from element.curColor via the
            // color alias below (default set in BrepElement ctor)
        }
    ]

    // Black visible-feature edges (computed by OCCT HLR on the
    // tessellated shape).  Rendered on top of the solid mesh.
    // We draw them as a child of the solid model, not a sibling,
    // so the element transform (pos/rot/scale) is inherited and
    // the lines stay glued to the object.  depthBias pulls the
    // lines slightly towards the viewer so they do not z-fight
    // against the solid underneath.
    Model {
        parent: model
        geometry: element ? element.edgeGeometry : null
        visible: element && element.show
        pickable: false
        depthBias: -1.0
        materials: [
            PrincipledMaterial {
                cullMode: PrincipledMaterial.NoCulling
                lineWidth: 3
                lighting: PrincipledMaterial.NoLighting
                baseColor: Qt.rgba(0.0, 0.0, 0.0, 1.0) // black
            }
        ]
    }

    // Selection / hover overlay (yellow bounding-box outline).
    Model {
        id: bboxOverlay
        parent: model.parent
        property alias element: model.element
        geometry: element ? element.selectionGeometry : null
        visible: element && (ZCam.currentElement === element
               || (ZCam.selectedElements && ZCam.selectedElements.indexOf(element) >= 0)
               || (element.editing !== undefined && element.editing))
        // Re-evaluate the geometry whenever the visibility flips so the
        // C++ side had a chance to fill the selection lines in between
        // (the initial binding may have bound to the still-empty
        // geometry before updateSelectionGeometry() ran).
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
                baseColor: Qt.rgba(1.0, 0.8, 0.0, 1.0) // yellow outline
            }
        ]
    }
}
