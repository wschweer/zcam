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

// Renders the Cam element with two geometry subsets:
//   subset 0 — MarkTo segments (markColor from Config, laser burns)
//   subset 1 — MoveTo segments (moveColor from Config, travel moves)
// The geometry is collected by Cam::updateCam() from all LaserLayer
// children, arranged in a panel grid.
Model {
    id: model
    property Element element
    property alias color: markMaterial.baseColor
    property alias lineWidth: markMaterial.lineWidth

    pickable: true
    materials: [
        PrincipledMaterial {
            id: markMaterial
            cullMode: PrincipledMaterial.NoCulling
            lineWidth: 3
            lighting: PrincipledMaterial.NoLighting
            baseColor: ZCam.config.markColor
        },
        PrincipledMaterial {
            id: moveMaterial
            cullMode: PrincipledMaterial.NoCulling
            lineWidth: 3
            lighting: PrincipledMaterial.NoLighting
            baseColor: ZCam.config.moveColor
        }
    ]
}