//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#pragma once

#include "clipper.h"
#include "laser.h"
#include "recipe.h"

#include <vector>

class Element3d;

//--------------------------------------------------------------------
//     HatchLayer
//--------------------------------------------------------------------
/// Linien einer einzelnen Hatch-Ebene (Winkel).
/// Alle Linien dieser Ebene stammen aus allen Elementen
/// des LaserMop, ggfs. ueber alle Panel-Tiles.
struct HatchLayer {
      double angle;              // Schraffurwinkel dieser Ebene [Grad]
      Clipper2Lib::PathsD lines; // alle Linien dieser Ebene
      const LaserPass* pass;     // zugehoeriger Recipe-Pass
      bool isOutline;            // true = Outline/Contour (kein Hatch)
      HatchLayer() : angle(0.0), pass(nullptr), isOutline(false) {}
      HatchLayer(double a, const LaserPass* p, bool outline = false)
          : angle(a), pass(p), isOutline(outline) {}
      };

//--------------------------------------------------------------------
//     LayeredLines
//--------------------------------------------------------------------
/// Linien segmentiert nach Hatch-Ebenen.
/// Die Ebenen werden in der Reihenfolge gelasert,
/// in der sie im Vektor stehen.
struct LayeredLines {
      std::vector<HatchLayer> layers;
      /// Alle Linien aller Ebenen als flache Liste (fuer Kompatibilitaet).
      Clipper2Lib::PathsD flatLines() const {
            Clipper2Lib::PathsD result;
            for (const auto& layer : layers)
                  result.append_range(layer.lines);
            return result;
            }
      };

//--------------------------------------------------------------------
//     PathStrategy
//--------------------------------------------------------------------
/// Strategie fuer die Pfad-Erstellung aus Hatch-Linien.
/// Sammelt Linien aller Elemente und gruppiert sie nach
/// Hatch-Ebene (Winkel), damit sie ebenenweise gelasert
/// werden koennen.
class PathStrategy
      {
    private:
      /// Normalisiere einen Winkel auf [0, 180)
      static double normalizeAngle(double a);

    public:
      /// Finde oder erstelle eine HatchLayer fuer den gegebenen Winkel.
      static HatchLayer& findOrCreateLayer(
          LayeredLines& layered, double angle, const LaserPass* pass, bool isOutline);

      /// Optimiere die Linien jeder Ebene unabhaengig und
      /// erzeuge einen LaserPath (ebenausweise).
      /// Die currentPos wird zwischen Ebenen weitergegeben.
      static LaserPath toLayeredLaserPath(const LayeredLines& layered);

      /// Wie toLayeredLaserPath, aber gibt zusaetzlich die
      /// Move-Linien (Jumps) und Mark-Linien getrennt zurueck
      /// fuer die Display-Geometrie.
      static void toDisplayLines(const LayeredLines& layered, Clipper2Lib::PathD& markLines,
          Clipper2Lib::PathD& moveLines, bool showMarks, bool showMoves);
      };