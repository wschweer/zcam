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

#include "logger.h"
#include "machine.h"
#include "machinegcode.h"

//---------------------------------------------------------
//   _properties
//---------------------------------------------------------

static const std::string propertiesGCodeLaser =
    //
    R"json(
      {
        "columns": 2,
        "cells": [
          {
            "name": "maxTravel",
            "label": "Travel",
            "type": "vector3d",
            "scriptable": true,
            "unit": "mm",
            "default": [
              100.0,
              100.0,
              100.0
            ]
          },
          {
            "name": "maxFeed",
            "label": "maxFeed",
            "type": "vector3d",
            "scriptable": true,
            "unit": "mm/s",
            "default": [
              1000.0,
              1000.0,
              1000.0
            ]
          },
          {
            "name": "maxAcceleration",
            "label": "max. Accel.",
            "type": "vector3d",
            "scriptable": true,
            "unit": "mm/s²",
            "default": [
              1000.0,
              1000.0,
              1000.0
            ]
          },
          {
            "name": "line",
            "type": "line",
            "colSpan": 2
          },
          {
            "label": "Precision",
            "cells": [
              {
                "name": "precision",
                "sublabel": "Prec",
                "type": "float",
                "scriptable": true,
                "unit": "mm",
                "min": 0.001,
                "max": 10.0,
                "precision": 3,
                "default": 0.001
              },
              {
                "name": "ncPrecision",
                "sublabel": "NC Prec",
                "type": "float",
                "scriptable": true,
                "unit": "mm",
                "min": 0.001,
                "max": 10.0,
                "precision": 3,
                "default": 0.001
              }
            ]
          },
          {
            "name": "circlePrecision",
            "label": "Circle Prec",
            "type": "float",
            "scriptable": true,
            "unit": "mm",
            "min": 0.001,
            "max": 10.0,
            "precision": 3,
            "default": 0.001
          },
          {
            "name": "line",
            "type": "line",
            "colSpan": 2
          }
        ]
      }

    )json";

static const std::string propertiesGCodeMill =
    //
    R"json(
      {
        "columns": 2,
        "cells": [
          {
            "name": "maxTravel",
            "label": "Travel",
            "type": "vector3d",
            "scriptable": true,
            "unit": "mm",
            "default": [
              100.0,
              100.0,
              100.0
            ]
          },
          {
            "name": "maxFeed",
            "label": "maxFeed",
            "type": "vector3d",
            "scriptable": true,
            "unit": "mm/s",
            "default": [
              1000.0,
              1000.0,
              1000.0
            ]
          },
          {
            "name": "maxAcceleration",
            "label": "max. Accel.",
            "type": "vector3d",
            "scriptable": true,
            "unit": "mm/s²",
            "default": [
              1000.0,
              1000.0,
              1000.0
            ]
          },
          {
            "label": "Safe Dist",
            "cells": [
              {
                "name": "safeDist1",
                "sublabel": "Safe 1",
                "type": "float",
                "scriptable": true,
                "unit": "mm",
                "min": 0.0,
                "max": 1000.0,
                "default": 0.0
              },
              {
                "name": "safeDist2",
                "sublabel": "Safe 2",
                "type": "float",
                "scriptable": true,
                "unit": "mm",
                "min": 0.0,
                "max": 1000.0,
                "default": 0.0
              }
            ]
          },
          {
            "label": "Spindle",
            "cells": [
              {
                "name": "minSpindle",
                "sublabel": "Min",
                "type": "float",
                "scriptable": true,
                "unit": "rpm",
                "min": 0.0,
                "max": 1000000.0,
                "default": 0.0
              },
              {
                "name": "maxSpindle",
                "sublabel": "Max",
                "type": "float",
                "scriptable": true,
                "unit": "rpm",
                "min": 0.0,
                "max": 1000000.0,
                "default": 0.0
              }
            ]
          },
          {
            "name": "line",
            "type": "line",
            "colSpan": 2
          },
          {
            "label": "Precision",
            "cells": [
              {
                "name": "precision",
                "sublabel": "Prec",
                "type": "float",
                "scriptable": true,
                "unit": "mm",
                "min": 0.001,
                "max": 10.0,
                "precision": 3,
                "default": 0.001
              },
              {
                "name": "ncPrecision",
                "sublabel": "NC Prec",
                "type": "float",
                "scriptable": true,
                "unit": "mm",
                "min": 0.001,
                "max": 10.0,
                "precision": 3,
                "default": 0.001
              }
            ]
          },
          {
            "name": "circlePrecision",
            "label": "Circle Prec",
            "type": "float",
            "scriptable": true,
            "unit": "mm",
            "min": 0.001,
            "max": 10.0,
            "precision": 3,
            "default": 0.001
          },
          {
            "name": "line",
            "type": "line",
            "colSpan": 2
          }
        ]
      }

    )json";

//---------------------------------------------------------
//   properties
//---------------------------------------------------------

const std::string MachineGCode::properties() const {
      std::string head = "{\n\"class\": \"Machine\",\"rows\": \[";
      std::string foot = "]\n}";

      if (machine()->type() == MachineType::GCODE_LASER)
            return head + propertiesMachine + propertiesGCodeLaser + foot;
      else if (machine()->type() == MachineType::GCODE_MILL)
            return head + propertiesMachine + propertiesGCodeMill + foot;
      else  {
            Fatal("bad machine type");
            return "";
            }
      }
