//=============================================================================
//  ZCam
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in the
//  file LICENSE.GPL
//=============================================================================

import QtQuick
import QtQuick3D
import ZCam

// Renders the Cam element with per-layer geometry subsets.
// Each LaserMop contributes two subsets:
//   even index — MarkTo segments (in the Mop's colour)
//   odd index   — MoveTo segments (in a dimmed version of that colour)
// The material list is rebuilt whenever element.layerColors changes.
Model {
    id: model
    property Element element
    property alias color: fallbackMarkMaterial.baseColor
    property alias lineWidth: fallbackMarkMaterial.lineWidth

    pickable: true

    // Track previously created materials for cleanup
    property var _dynamicMaterials: []

    // Rebuild the material list whenever the Cam element's layerColors
    // property changes (i.e. after updateCam() completes).
    function rebuildMaterials() {
        // Destroy previously created dynamic materials
        for (var d = 0; d < _dynamicMaterials.length; ++d) {
            if (_dynamicMaterials[d])
                _dynamicMaterials[d].destroy();
        }
        _dynamicMaterials = [];

        var cam = element;
        var colors = cam && cam.layerColors ? cam.layerColors : null;

        if (!colors || colors.length === 0) {
            // Fallback: two static materials with the old config colours
            materials = [fallbackMarkMaterial, fallbackMoveMaterial];
            return;
        }

        // Build two materials per layer: marks (full colour) and
        // moves (dimmed colour).
        var mats = [];
        for (var i = 0; i < colors.length; ++i) {
            var c = colors[i];

            var markMat = Qt.createQmlObject(
                "import QtQuick3D; PrincipledMaterial { " +
                "cullMode: PrincipledMaterial.NoCulling; " +
                "lineWidth: 3; " +
                "lighting: PrincipledMaterial.NoLighting; " +
                "baseColor: Qt.rgba(" + c.r + ", " + c.g + ", " + c.b + ", 1.0) " +
                "}",
                model, "markMaterial_" + i);
            mats.push(markMat);
            _dynamicMaterials.push(markMat);

            var moveMat = Qt.createQmlObject(
                "import QtQuick3D; PrincipledMaterial { " +
                "cullMode: PrincipledMaterial.NoCulling; " +
                "lineWidth: 3; " +
                "lighting: PrincipledMaterial.NoLighting; " +
                "baseColor: Qt.rgba(" + c.r + ", " + c.g + ", " + c.b + ", 0.4) " +
                "}",
                model, "moveMaterial_" + i);
            mats.push(moveMat);
            _dynamicMaterials.push(moveMat);
        }
        materials = mats;
    }

    Component.onCompleted: rebuildMaterials()

    Connections {
        target: element
        function onLayerColorsChanged() { rebuildMaterials() }
        ignoreUnknownSignals: true
    }

    // Fallback materials (only used when layerColors is empty)
    PrincipledMaterial {
        id: fallbackMarkMaterial
        cullMode: PrincipledMaterial.NoCulling
        lineWidth: 3
        lighting: PrincipledMaterial.NoLighting
        baseColor: ZCam.config.markColor
    }
    PrincipledMaterial {
        id: fallbackMoveMaterial
        cullMode: PrincipledMaterial.NoCulling
        lineWidth: 3
        lighting: PrincipledMaterial.NoLighting
        baseColor: ZCam.config.moveColor
    }
}