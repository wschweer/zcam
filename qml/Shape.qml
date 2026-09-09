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

    // Bezier control-point association lines: dashed lines connecting each
    // cubic segment's start/end anchor to its control points (S→C1 and
    // E→C2).  Rendered in red and shown only while this polygon is the
    // current selection — exactly when the control-point handles are
    // visible — so the user can see which control points belong to which
    // segment.  The resulting curve itself is drawn by the main model
    // above (as a solid line / filled polygon).  controlHandleGeometry is
    // non-null only for Polygon (and empty when it has no bezier
    // segments), so this overlay is a no-op for every other element type.
    Model {
        id: controlHandleModel
        parent: model.parent
        property alias element: model.element
        geometry: element ? element.controlHandleGeometry : null
        visible: element
                  && element.controlHandleGeometry
                  && (ZCam.currentElement === element
                      || (element.editing !== undefined && element.editing))
        position: model.position
        eulerRotation: model.eulerRotation
        scale: model.scale
        pickable: false
        materials: [
            PrincipledMaterial {
                cullMode: PrincipledMaterial.NoCulling
                lineWidth: 3
                lighting: PrincipledMaterial.NoLighting
                // Force alpha blending so this overlay is routed to the
                // TRANSPARENT pass (rendered after the opaque polygon
                // fill).  Without this the dashed handle lines sit at the
                // same z=0 plane as the filled polygon and lose the
                // LessOrEqual depth-test tie, so the fill hides them.
                alphaMode: PrincipledMaterial.Blend
                baseColor: Qt.rgba(0.7, 0.0, 0.0, 1.0) // dark red association lines
            }
        ]
    }

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
        // Depend on ZCam.selectedElements (Q_PROPERTY with NOTIFY) so the
        // binding re-evaluates when the lasso selection changes.  Using
        // ZCam.isSelected() (a Q_INVOKABLE method) would NOT trigger
        // re-evaluation because QML cannot track its dependency on
        // _selectedElements.
        visible: element && (ZCam.currentElement === element
               || (ZCam.selectedElements && ZCam.selectedElements.indexOf(element) >= 0)
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
                // Force alpha blending so the selected-segment highlight
                // (and the bounding-box rectangle) is routed to the
                // TRANSPARENT pass, i.e. rendered after the opaque polygon
                // fill.  Both share the z=0 plane; without this the fill
                // wins the LessOrEqual depth-test tie and the highlight
                // line is drawn on top of (hidden by) the polygon body.
                alphaMode: PrincipledMaterial.Blend
                baseColor: Qt.rgba(1.0, 0.8, 0.0, 1.0) // yellow outline
            }
        ]
    }
}