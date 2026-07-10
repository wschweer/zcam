//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#pragma once

#include <QObject>
#include <QVector3D>
#include <QVector2D>
#include <QtQml/qqmlregistration.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "types.h"
#include "macros.h"

class ZCam;
class Machines;

inline const std::vector<std::string> machineTypes {
      "Fiber Laser", "UV-Laser", "Co2-Laser", "GCode CNC"
      };

//---------------------------------------------------------
//   Machine
//---------------------------------------------------------

class Machine
      {
      Q_GADGET

      PROP_GADGET(QString, name)
      PROP_GADGET(QString, type)
      PROP_GADGET(QString, description)
      PROPV_GADGET(QVector3D, maxTravel, QVector3D(100.0, 100.0, 100.0))
      PROP_GADGET(double, travelSpeed)
      PROP_GADGET(double, framingSpeed)
      PROP_GADGET(double, safeDist1)
      PROP_GADGET(double, safeDist2)
      PROP_GADGET(QVector3D, maxFeed)
      PROP_GADGET(QVector3D, maxAcceleration)
      PROP_GADGET(double, minSpindle)
      PROP_GADGET(double, maxSpindle)
      PROP_GADGET(double, precision)
      PROP_GADGET(double, ncPrecision)
      PROP_GADGET(double, circlePrecision)

      // galvolaser
      PROP_GADGET(double, galvoP1)
      PROP_GADGET(double, galvoP2)
      PROP_GADGET(double, galvoP3)
      PROP_GADGET(QVector2D, galvoScale)
      PROP_GADGET(double, galvoShearX)
      PROP_GADGET(double, galvoShearY)
      PROP_GADGET(double, galvoRotate)
      PROP_GADGET(bool,   galvoSwapxy)

      inline static constexpr std::string_view _properties {R"({
            "class": "Machine",
            "items": [
                  { "row":
                        {
                              "name": { "label": "Name", "type": "string" },
                              "type": { "label": "Type", "type": "machineType" }
                        },
                        "label": " "
                     },
                  { "name": "description", "label": "Description", "type": "multiline" },
                  { "name": "line", "type": "line" },
                  { "columns": {
                        "count": 2,
                        "items": [
                              { "name": "maxTravel",    "label": "Travel", "type": "vector3d", "unit": "mm", "default": [100.0, 100.0, 100.0] },
                              { "name": "travelSpeed",  "label": "Travel Speed", "type": "float", "unit": "mm/s", "min": 0.0, "max": 100000.0, "default": 0.0 },
                              { "name": "framingSpeed", "label": "Framing Speed", "type": "float", "unit": "mm/s", "min": 0.0, "max": 100000.0, "default": 0.0 },
                              { "row":
                                    {
                                          "safeDist1": { "label": "Safe 1", "type": "float", "unit": "mm", "min": 0.0, "max": 1000.0, "default": 0.0 },
                                          "safeDist2": { "label": "Safe 2", "type": "float", "unit": "mm", "min": 0.0, "max": 1000.0, "default": 0.0 }
                                    },
                                    "label": "Safe Dist"
                              },
                              { "name": "maxFeed", "label": "Max Feed", "type": "vector3d", "unit": "mm/s", "default": [0.0, 0.0, 0.0] },
                              { "name": "maxAcceleration", "label": "Max Accel", "type": "vector3d", "unit": "mm/s²", "default": [0.0, 0.0, 0.0] },
                              { "row":
                                    {
                                          "minSpindle": { "label": "Min", "type": "float", "unit": "rpm", "min": 0.0, "max": 1000000.0, "default": 0.0 },
                                          "maxSpindle": { "label": "Max", "type": "float", "unit": "rpm", "min": 0.0, "max": 1000000.0, "default": 0.0 }
                                    },
                                    "label": "Spindle"
                              },
                              { "name": "line", "type": "line", "colSpan": 2 },
                              { "row":
                                    {
                                          "precision":       { "label": "Prec",    "type": "float", "unit": "mm", "min": 0.0, "max": 10.0, "default": 0.0 },
                                          "ncPrecision":     { "label": "NC Prec", "type": "float", "unit": "mm", "min": 0.0, "max": 10.0, "default": 0.0 }
                                    },
                                    "label": "Precision"
                              },
                              { "name": "circlePrecision", "label": "Circle Prec", "type": "float", "unit": "mm", "min": 0.0, "max": 10.0, "default": 0.0 },
                              { "name": "line", "type": "line", "colSpan": 2 },
                              { "row":
                                    {
                                          "galvoP1": { "label": "P1", "type": "float", "min": 0.0, "max": 100000.0, "default": 0.0 },
                                          "galvoP2": { "label": "P2", "type": "float", "min": 0.0, "max": 100000.0, "default": 0.0 },
                                          "galvoP3": { "label": "P3", "type": "float", "min": 0.0, "max": 100000.0, "default": 0.0 }
                                    },
                                    "label": "Galvo"
                              },
                              { "name": "galvoScale", "label": "Galvo Scale", "type": "vector2d", "default": [100.0, 100.0] },
                              { "row":
                                    {
                                          "galvoShearX": { "label": "Shear X", "type": "float", "min": -100.0, "max": 100.0, "default": 0.0 },
                                          "galvoShearY": { "label": "Shear Y", "type": "float", "min": -100.0, "max": 100.0, "default": 0.0 }
                                    },
                                    "label": "Galvo Shear"
                              },
                              { "name": "galvoRotate", "label": "Galvo Rotate", "type": "float", "unit": "°", "min": 0.0, "max": 360.0, "default": 0.0 },
                              { "name": "galvoSwapxy", "label": "Galvo Swap XY", "type": "bool", "default": false }
                        ]
                        }
                  }
                  ]
                  })"};

    public:
      Machine() {}
      json toJson() const;
      bool fromJson(const json&);
      const std::string_view properties() const { return _properties; }
      };