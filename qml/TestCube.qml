//=============================================================================
//  wcam
//
//  Copyright (C) 2025/2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

import QtCore
import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import QtQuick.Controls.Material
import QtQuick.Layouts

// ─────────────────────────────────────────────────────────
//  TEST: Bunter 3-D Würfel  (Kantenlänge 100 Units)
//  Jede der 6 Flächen ist ein eigenes Rechteck-Mesh mit
//  einer eigenen Farbe, so dass alle Seiten unterschiedlich
//  eingefärbt sind.
// ─────────────────────────────────────────────────────────

Node {
    id: testCube

    // --- Vorderseite  (rot)   z = +50 ---
    Model {
        source: "#Rectangle"
        position: Qt.vector3d(0, 0, 50)
        eulerRotation: Qt.vector3d(0, 0, 0)
        scale: Qt.vector3d(1, 1, 1)
        materials: DefaultMaterial {
            diffuseColor: "#e74c3c"   // Rot
            lighting: DefaultMaterial.FragmentLighting
        }
    }

    // --- Rückseite   (blau)   z = -50 ---
    Model {
        source: "#Rectangle"
        position: Qt.vector3d(0, 0, -50)
        eulerRotation: Qt.vector3d(0, 180, 0)
        scale: Qt.vector3d(1, 1, 1)
        materials: DefaultMaterial {
            diffuseColor: "#2980b9"   // Blau
            lighting: DefaultMaterial.FragmentLighting
        }
    }

    // --- Linke Seite (grün)   x = -50 ---
    Model {
        source: "#Rectangle"
        position: Qt.vector3d(-50, 0, 0)
        eulerRotation: Qt.vector3d(0, -90, 0)
        scale: Qt.vector3d(1, 1, 1)
        materials: DefaultMaterial {
            diffuseColor: "#27ae60"   // Grün
            lighting: DefaultMaterial.FragmentLighting
        }
    }

    // --- Rechte Seite (gelb)  x = +50 ---
    Model {
        source: "#Rectangle"
        position: Qt.vector3d(50, 0, 0)
        eulerRotation: Qt.vector3d(0, 90, 0)
        scale: Qt.vector3d(1, 1, 1)
        materials: DefaultMaterial {
            diffuseColor: "#f1c40f"   // Gelb
            lighting: DefaultMaterial.FragmentLighting
        }
    }

    // --- Oberseite  (orange)  y = +50 ---
    Model {
        source: "#Rectangle"
        position: Qt.vector3d(0, 50, 0)
        eulerRotation: Qt.vector3d(-90, 0, 0)
        scale: Qt.vector3d(1, 1, 1)
        materials: DefaultMaterial {
            diffuseColor: "#e67e22"   // Orange
            lighting: DefaultMaterial.FragmentLighting
        }
    }

    // --- Unterseite (lila)    y = -50 ---
    Model {
        source: "#Rectangle"
        position: Qt.vector3d(0, -50, 0)
        eulerRotation: Qt.vector3d(90, 0, 0)
        scale: Qt.vector3d(1, 1, 1)
        materials: DefaultMaterial {
            diffuseColor: "#8e44ad"   // Lila
            lighting: DefaultMaterial.FragmentLighting
            }
        }
    }
