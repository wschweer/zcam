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

    pickable: false
    geometry: element ? element.geometry : null
    position: element ? element.pos : Qt.vector3d(0, 0, 0)

    //=========================================================
    //  GridMaterial — CustomMaterial with SCREEN-SPACE
    //  line expansion.
    //
    //  The vertex shader (shaders/gridline.vert) projects every
    //  corner of the degenerate quad into clip space and
    //  displaces it perpendicular to the projected line direction
    //  by uHalfWidthPx * 2 / uViewportSize, multiplied by
    //  clip.w so the perspective division restores exact pixels.
    //  → the stroke keeps an EXACT pixel width in every view:
    //  zoom, pan and rotation no longer rebuild the geometry,
    //  there is no Z-fighting with the z=0 scene (the quad stays
    //  flat), and no woven/zig-zag artefacts at oblique angles.
    //=========================================================
    component GridMaterial: CustomMaterial {
        property color lineColor
        property real halfWidthPx: 1.0
        // Viewport size in physical pixels; updated from the
        // backgroundView below.  Initialised to a sensible default
        // so the very first frame (before the binding fires) still
        // shows the intended width.
        property vector2d vpSize: Qt.vector2d(900, 900)

        shadingMode: CustomMaterial.Unshaded
        cullMode: CustomMaterial.NoCulling
        // Alpha blending must be enabled explicitly — the default
        // (NoBlend) would discard the anti-aliased alpha produced
        // in the fragment shader and the transparent minor lines.
        // The fragment shader outputs PREMULTIPLIED colors
        // (rgb already multiplied by alpha), hence One /
        // OneMinusSrcAlpha instead of SrcAlpha / OneMinusSrcAlpha.
        sourceBlend: CustomMaterial.One
        destinationBlend: CustomMaterial.OneMinusSrcAlpha
        // Absolute qrc paths:  vertexShader/fragmentShader are read
        // via QQmlFile::urlToLocalFileOrQrc() — the most reliable
        // form is the full "qrc:" url instead of a relative one.
        vertexShader: "qrc:/ZCam/shaders/gridline.vert"
        fragmentShader: "qrc:/ZCam/shaders/gridline.frag"

        property vector4d uColor: Qt.vector4d(lineColor.r, lineColor.g, lineColor.b, lineColor.a)
        property real uHalfWidthPx: halfWidthPx
        // Two scalar uniforms instead of a vector2d — Qt's uniform
        // conversion for vector2d can silently fall back to a
        // vec4 with zero z/w in some versions, which would divide
        // the offset by 0 and make the grid invisible.
        property real uViewportWidth: vpSize.x
        property real uViewportHeight: vpSize.y
    }

    // Major (raster) lines: brighter, wider (2 px full width).
    materials: [
        GridMaterial {
            lineColor: Qt.rgba(0.60, 0.60, 0.60, 0.9)
            halfWidthPx: 1.0
            vpSize: Qt.vector2d(backgroundView.width, backgroundView.height)
        }
    ]

    // Minor (subraster) lines: darker, transparent, finer (1 px full width).
    Model {
        id: minorModel
        parent: model
        geometry: element ? element.minorGeometry : null
        position: Qt.vector3d(0, 0, 0)
        pickable: false
        materials: [
            GridMaterial {
                lineColor: Qt.rgba(0.58, 0.58, 0.58, 0.6)
                halfWidthPx: 0.5
                vpSize: Qt.vector2d(backgroundView.width, backgroundView.height)
            }
        ]
    }
}
