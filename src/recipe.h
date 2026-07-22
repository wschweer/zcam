//=============================================================================
//  wcam
//    CAM tool for gcode and fiber laser machines.
//
//  Copyright (C) 2025 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#pragma once

#include "element3d.h"
#include "laserengine.h"
#include "laser_recipe.h"
#include "clipper.h"

//---------------------------------------------------------
//   LaserLayer
//    - Contains the laser parameters (recipe, overrides, etc.)
//    - No longer references a specific Layer as baseElement.
//    - Instead, each Element3d in the project tree (from Cad
//      downward) can reference a LaserLayer via its laserLayer
//      property.  Elements that don't set it inherit the
//      LaserLayer from their parent.
//    - material test can override up to two parameters
//---------------------------------------------------------

class Recipe : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      PROPV(const LaserRecipe*, recipe, nullptr)

      // override types are defined in laserengine.h
      // as ParameterType

      PROPV(int, overrideType1, 0)
      PROPV(int, overrideType2, 0)
      PROPV(double, overrideValue1, 0.0)
      PROPV(double, overrideValue2, 0.0)
      PROPV(double, kerfOffset, 0.0)
      PROPV(bool, burn, true)
      PROPV(bool, invert, false)

      PROPV(bool, showMarks, true)
      PROPV(bool, showMoves, true)

      inline static constexpr std::string_view _properties {R"({
            "class": "LaserLayer",
            "items": [
                  { "row":
                        {
                              "show": { "label": "Show", "type": "bool", "default": true },
                              "burn": { "label": "Burn", "type": "bool", "default": true }
                        },
                        "label": "State"
                     },

                  { "name": "recipe",      "label": "Recipe", "type": "recipe" },
                  { "name": "invert",      "label": "Invert", "type": "bool",  "default": false },
                  { "name": "kerfOffset",  "label": "Kerf",   "type": "float", "min": 0.0, "max": 0.001, "default": 0.03 },
                  { "row":
                        {
                              "overrideType1": { "label": "type", "type": "override" },
                              "overrideValue1": { "label": "value", "type": "float", "default": 0.0 }
                        },
                        "label": "Ovr1"
                     },
                  { "row":
                        {
                              "overrideType2": { "label": "type", "type": "override" },
                              "overrideValue2": { "label": "value", "type": "float", "default": 0.0 }
                        },
                        "label": "Ovr2"
                     },
                  { "row":
                        {
                              "showMarks": { "label": "marks", "type": "bool", "default": true },
                              "showMoves": { "label": "moves", "type": "bool", "default": true }
                        },
                        "label": "Show"
                     }
                  ]
                              })"};

      /// Process one tile's geometry through the recipe (fill, wobble, lines)
      /// and return raw line segments without panel-grid offsets.
      Clipper2Lib::PathsD processTileLines() const;

    public:
      Recipe(ZCam*, Element* parent = nullptr);
      ~Recipe();
      /// No-op: LaserLayer no longer fills its own _geometry.
      /// Display geometry is collected by Cam::updateCam().
      void update(int flags = -1) override {}
      virtual QString typeName() override { return QStringLiteral("laserLayer"); }
      virtual const std::string_view properties() const override { return _properties; }
      // LaserLayer elements can be deleted from the project tree.
      static constexpr bool s_deletable = true;
      Q_INVOKABLE bool deletable() const override { return s_deletable; }
      PathsD spl;
      PathD glLines;
      Clipper2Lib::PathsD createFill(Clipper2Lib::PathsD& spdi) const;

      /// Collect all Element3d items in the project tree (from Cad
      /// downward) whose effectiveLaserLayer() equals this LaserLayer.
      /// This replaces the old baseElement-based collection.
      std::vector<const Element3d*> collectElements() const;

      /// Return polygon list for a single tile (no panel-grid offsets).
      /// Used by Cam for the convex-hull / framing computation.
      PathsD collectLayerPath();
      /// Return display line segments (mark + move subsets) for a single tile.
      /// Used by Cam to build the combined panel-grid geometry.
      Clipper2Lib::PathsD collectDisplayLines() const;
      LaserPath collectLaserPath() const;
      };