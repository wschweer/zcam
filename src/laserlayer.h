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
#include "layer.h"
#include "laserengine.h"
#include "recipe.h"
#include "clipper.h"

//---------------------------------------------------------
//   LaserLayer
//    - corresponds to a Layer
//    - all children of Layer are burned with parameters from
//      an LaserLayerTemplate
//    - material test can override up to two parameters
//---------------------------------------------------------

class LaserLayer : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      PROPV(Layer*, baseElement, nullptr)
      PROPV(Recipe*, recipe, nullptr)
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

                  { "name": "baseElement", "label": "CadLayer", "type": "layer" },
                  { "name": "recipe",      "label": "Recipe", "type": "recipe" },
                  { "name": "invert",      "label": "Invert", "type": "bool",  "default": false },
                  { "name": "kerfOffset",  "label": "Kerf",   "type": "float", "min": 0.0, "max": 0.001, "default": 0.03 },
                  { "row":
                        {
                              "showMarks": { "label": "marks", "type": "bool", "default": true },
                              "showMoves": { "label": "moves", "type": "bool", "default": true }
                        },
                        "label": "Show"
                     }
                  ]
                  })"};

    public:
      LaserLayer(ZCam*, Element* parent = nullptr);
      ~LaserLayer();
      void update(int flags = -1) override;
      virtual QString typeName() override { return QStringLiteral("laserLayer"); }
      virtual const std::string_view properties() const override { return _properties; }
      PathsD spl;
      PathD glLines;
      Clipper2Lib::PathsD createFill(Clipper2Lib::PathsD& spdi) const;

      PathsD collectLayerPath();
      LaserPath collectLaserPath() const;
      };