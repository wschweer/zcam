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

#include "mop.h"
#include "laser.h"
#include "recipe.h"
#include "clipper.h"
#include "pathstrategy.h"

//---------------------------------------------------------
//   LaserMop
//    - Contains the laser parameters (recipe, overrides, etc.)
//    - Each Element3d in the project tree (from Cad
//      downward) can reference a LaserMop via its mop
//      property.  Elements that don't set it inherit the
//      LaserMop from their parent.
//    - material test can override up to two parameters
//---------------------------------------------------------

class LaserMop : public Mop
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      PROPV(LaserRecipe*, recipe, nullptr)

      // override types are defined in laser.h
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
    "class": "Mop",
    "rows": [
        {
            "label": "State",
            "cells": [
                {
                    "type": "bool",
                    "default": true,
                    "name": "show",
                    "sublabel": "Show"
                },
                {
                    "type": "bool",
                    "default": true,
                    "name": "burn",
                    "sublabel": "Burn"
                }
            ]
        },
        {
            "label": "Color",
            "cells": [
                {
                    "name": "colorIndex",
                    "type": "mopColor",
                    "default": 1
                }
            ]
        },
        {
            "label": "Mop",
            "cells": [
                {
                    "name": "recipe",
                    "type": "recipe"
                }
            ]
        },
        {
            "label": "Invert",
            "cells": [
                {
                    "name": "invert",
                    "type": "bool",
                    "default": false
                }
            ]
        },
        {
            "label": "Kerf",
            "cells": [
                {
                    "name": "kerfOffset",
                    "type": "float",
                    "scriptable": true,
                    "min": 0.0,
                    "max": 0.001,
                    "default": 0.03
                }
            ]
        },
        {
            "label": "Ovr1",
            "cells": [
                {
                    "type": "override",
                    "name": "overrideType1",
                    "sublabel": "type"
                },
                {
                    "type": "float",
                    "scriptable": true,
                    "default": 0.0,
                    "name": "overrideValue1",
                    "sublabel": "value"
                }
            ]
        },
        {
            "label": "Ovr2",
            "cells": [
                {
                    "type": "override",
                    "name": "overrideType2",
                    "sublabel": "type"
                },
                {
                    "type": "float",
                    "scriptable": true,
                    "default": 0.0,
                    "name": "overrideValue2",
                    "sublabel": "value"
                }
            ]
        },
        {
            "label": "Show",
            "cells": [
                {
                    "type": "bool",
                    "default": true,
                    "name": "showMarks",
                    "sublabel": "marks"
                },
                {
                    "type": "bool",
                    "default": true,
                    "name": "showMoves",
                    "sublabel": "moves"
                }
            ]
        }
    ]
                              })"};

      /// Process one tile's geometry through the recipe (fill, wobble, lines)
      /// and return raw line segments without panel-grid offsets.
      Clipper2Lib::PathsD processTileLines() const;

      /// Process one tile's geometry through the recipe and return
      /// lines grouped by hatch layer (angle).  No panel-grid offsets.
      LayeredLines processTileLinesLayered() const;

    public:
      LaserMop(ZCam*, Element* parent = nullptr);
      ~LaserMop() {}
      /// No-op: LaserMop no longer fills its own _geometry.
      /// Display geometry is collected by Cam::updateCam().
      void update(int flags = -1) override {}
      virtual QString typeName() override { return QStringLiteral("laserMop"); }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE bool deletable() const override { return true; }
      Q_INVOKABLE bool nameEditable() const override { return true; }
      PathsD spl;
      PathD glLines;
      Clipper2Lib::PathsD createFill(Clipper2Lib::PathsD& spdi) const;

      /// Fill polygon spdi with hatch pattern, returning lines grouped
      /// by hatch angle into the given LayeredLines.
      void createLayeredFill(Clipper2Lib::PathsD& spdi, LayeredLines& layered) const;

      /// Collect all Element3d items in the project tree (from Cad
      /// downward) whose effectiveLaserMop() equals this LaserMop.
      /// This replaces the old baseElement-based collection.
      std::vector<const Element3d*> collectElements() const;

      /// Return polygon list for a single tile (no panel-grid offsets).
      /// Used by Cam for the convex-hull / framing computation.
      PathsD collectLayerPath();
      /// Return display line segments (mark + move subsets) for a single tile.
      /// Used by Cam to build the combined panel-grid geometry.
      Clipper2Lib::PathsD collectDisplayLines() const;
      LaserPath collectLaserPath() const;

      /// Collect layered lines for all panel tiles, grouped by hatch angle.
      /// Used by doStartMarking for ebenausweise marking.
      LayeredLines collectLayeredLaserPath() const;

      /// Return display line segments (mark + move subsets) for all panel
      /// tiles, using the layered path strategy.
      Clipper2Lib::PathsD collectLayeredDisplayLines() const;
      };
