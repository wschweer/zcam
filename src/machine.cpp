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

#include "machine.h"
#include "propertyjson.h"
#include "logger.h"
#include "laser.h"

//---------------------------------------------------------
//   toJson
//    Serialize all user-editable properties declared in
//    properties() to JSON, using the shared propjson utilities.
//    Machine is a QObject, so gadget=false.
//---------------------------------------------------------

json Machine::toJson() const {
      json data;

      std::string_view propStr = properties();
      if (propStr.empty())
            return data;

      std::vector<std::pair<std::string, std::string>> propNames;
      try {
            propNames = propjson::parseAllPropertyNames(propStr);
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("Machine::toJson: JSON parse error: {}", err.what());
            return data;
            }

      const QMetaObject* meta = &Machine::staticMetaObject;
      for (const auto& [name, type] : propNames)
            propjson::writePropertyToJson(data, this, meta, false, name, type);

      return data;
      }

//---------------------------------------------------------
//   fromJson
//    Deserialize all properties from JSON using the shared
//    propjson utilities.  Machine is a QObject, so gadget=false.
//---------------------------------------------------------

bool Machine::fromJson(const json& data) {
      std::string_view propStr = properties();
      if (propStr.empty())
            return true;

      try {
            auto propNames          = propjson::parseAllPropertyNames(propStr);
            const QMetaObject* meta = &Machine::staticMetaObject;
            for (const auto& [name, type] : propNames)
                  propjson::readPropertyFromJson(data, this, meta, false, name, type);

            // Migration: rename legacy machine type names to current ones.
            if (type() == QStringLiteral("Fiber Laser"))
                  set_type(QStringLiteral("Q-switched Laser"));
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("Machine::fromJson: JSON parse error: {}", err.what());
            return false;
            }
      catch (...) {
            Warning("Machine::fromJson: unknown JSON error");
            return false;
            }
      delete _laser;
      _laser = new Laser(zcam, this, nullptr);
      return true;
      }

static constexpr std::string_view _properties[] = {
         {// Q-switched Laser
    R"json({
             "class": "Machine",
             "items": [
               {
                 "row": {
                   "name": {
                     "label": "Name",
                     "type": "string"
                   },
                   "type": {
                     "label": "Type",
                     "type": "machineType"
                   },
                   "boardType": {
                     "label": "Board",
                     "type": "boardType"
                   }
                 },
                 "label": " "
               },
               {
                 "name": "description",
                 "label": "Description",
                 "type": "multiline"
               },
               {
                 "name": "line",
                 "type": "line"
               },
               {
                 "columns": {
                   "count": 2,
                   "items": [
                     {
                       "name": "maxTravel",
                       "label": "Travel",
                       "type": "vector3d",
                       "unit": "mm",
                       "default": [
                         100.0,
                         100.0,
                         100.0
                       ]
                     },
                     {
                       "name": "travelSpeed",
                       "label": "Travel Speed",
                       "type": "float",
                       "unit": "mm/s",
                       "min": 0.0,
                       "max": 100000.0,
                       "default": 0.0
                     },
                     {
                       "name": "framingSpeed",
                       "label": "Framing Speed",
                       "type": "float",
                       "unit": "mm/s",
                       "min": 0.0,
                       "max": 100000.0,
                       "default": 0.0
                     },
                     {
                       "name": "maxFeed",
                       "label": "Max Feed",
                       "type": "vector3d",
                       "unit": "mm/s",
                       "default": [
                         0.0,
                         0.0,
                         0.0
                       ]
                     },
                     {
                       "name": "maxAcceleration",
                       "label": "Max Accel",
                       "type": "vector3d",
                       "unit": "mm/s²",
                       "default": [
                         0.0,
                         0.0,
                         0.0
                       ]
                     },
                     {
                       "row": {
                         "minSpindle": {
                           "label": "Min",
                           "type": "float",
                           "unit": "rpm",
                           "min": 0.0,
                           "max": 1000000.0,
                           "default": 0.0
                         },
                         "maxSpindle": {
                           "label": "Max",
                           "type": "float",
                           "unit": "rpm",
                           "min": 0.0,
                           "max": 1000000.0,
                           "default": 0.0
                         }
                       },
                       "label": "Spindle"
                     },
                     {
                       "name": "line",
                       "type": "line",
                       "colSpan": 2
                     },
                     {
                       "row": {
                         "precision": {
                           "label": "Prec",
                           "type": "float",
                           "unit": "mm",
                           "min": 0.001,
                           "max": 10.0,
                           "precision": 3,
                           "default": 0.001
                         },
                         "ncPrecision": {
                           "label": "NC Prec",
                           "type": "float",
                           "unit": "mm",
                           "min": 0.001,
                           "max": 10.0,
                           "precision": 3,
                           "default": 0.001
                         }
                       },
                       "label": "Precision"
                     },
                     {
                       "name": "circlePrecision",
                       "label": "Circle Prec",
                       "type": "float",
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
                     },
                     {
                       "row": {
                         "galvoP1": {
                           "label": "P1",
                           "type": "float",
                           "min": 0.0,
                           "max": 10.0,
                           "default": 0.0,
                           "precision": 4
                         },
                         "galvoP2": {
                           "label": "P2",
                           "type": "float",
                           "min": 0.0,
                           "max": 10.0,
                           "precision": 4,
                           "default": 0.0
                         },
                         "galvoP3": {
                           "label": "P3",
                           "type": "float",
                           "min": 0.0,
                           "max": 10.0,
                           "precision": 4,
                           "default": 0.0
                         }
                       },
                       "label": "Galvo"
                     },
                     {
                       "name": "galvoScale",
                       "label": "Galvo Scale",
                       "type": "vector2d",
                       "default": [
                         100.0,
                         100.0
                       ]
                     },
                     {
                       "row": {
                         "galvoShearX": {
                           "label": "Shear X",
                           "type": "float",
                           "min": -100.0,
                           "max": 100.0,
                           "precision": 3,
                           "default": 0.0
                         },
                         "galvoShearY": {
                           "label": "Shear Y",
                           "type": "float",
                           "min": -100.0,
                           "max": 100.0,
                           "precision": 3,
                           "default": 0.0
                         }
                       },
                       "label": "Galvo Shear"
                     },
                     {
                       "name": "galvoRotate",
                       "label": "Galvo Rotate",
                       "type": "float",
                       "unit": "°",
                       "min": 0.0,
                       "max": 360.0,
                       "default": 0.0,
                       "precision": 3
                     },
                     {
                       "name": "galvoSwapxy",
                       "label": "Galvo Swap XY",
                       "type": "bool",
                       "default": false
                     },
                     {
                       "name": "ethDevice",
                       "label": "Ethernet Device",
                       "type": "ethDevice",
                       "default": ""
                     }
                   ]
                 }
               }
             ]
                 })json"},
         {// MOPA laser
    R"json({
             "class": "Machine",
             "items": [
               {
                 "row": {
                   "name": {
                     "label": "Name",
                     "type": "string"
                   },
                   "type": {
                     "label": "Type",
                     "type": "machineType"
                   },
                   "boardType": {
                     "label": "Board",
                     "type": "boardType"
                   }
                 },
                 "label": " "
               },
               {
                 "name": "description",
                 "label": "Description",
                 "type": "multiline"
               },
               {
                 "name": "line",
                 "type": "line"
               },
               {
                 "columns": {
                   "count": 2,
                   "items": [
                     {
                       "name": "maxTravel",
                       "label": "Travel",
                       "type": "vector3d",
                       "unit": "mm",
                       "default": [
                         100.0,
                         100.0,
                         100.0
                       ]
                     },
                     {
                       "name": "travelSpeed",
                       "label": "Travel Speed",
                       "type": "float",
                       "unit": "mm/s",
                       "min": 0.0,
                       "max": 100000.0,
                       "default": 0.0
                     },
                     {
                       "name": "framingSpeed",
                       "label": "Framing Speed",
                       "type": "float",
                       "unit": "mm/s",
                       "min": 0.0,
                       "max": 100000.0,
                       "default": 0.0
                     },
                     {
                       "name": "maxFeed",
                       "label": "Max Feed",
                       "type": "vector3d",
                       "unit": "mm/s",
                       "default": [
                         0.0,
                         0.0,
                         0.0
                       ]
                     },
                     {
                       "name": "maxAcceleration",
                       "type": "empty"
                     },
                     {
                       "name": "line",
                       "type": "line",
                       "colSpan": 2
                     },
                     {
                       "row": {
                         "precision": {
                           "label": "Prec",
                           "type": "float",
                           "unit": "mm",
                           "min": 0.001,
                           "max": 10.0,
                           "precision": 3,
                           "default": 0.001
                         },
                         "ncPrecision": {
                           "label": "NC Prec",
                           "type": "float",
                           "unit": "mm",
                           "min": 0.001,
                           "max": 10.0,
                           "precision": 3,
                           "default": 0.001
                         }
                       },
                       "label": "Precision"
                     },
                     {
                       "name": "circlePrecision",
                       "label": "Circle Prec",
                       "type": "float",
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
                     },
                     {
                       "row": {
                         "galvoP1": {
                           "label": "P1",
                           "type": "float",
                           "min": 0.0,
                           "max": 10.0,
                           "default": 0.0,
                           "precision": 4
                         },
                         "galvoP2": {
                           "label": "P2",
                           "type": "float",
                           "min": 0.0,
                           "max": 10.0,
                           "precision": 4,
                           "default": 0.0
                         },
                         "galvoP3": {
                           "label": "P3",
                           "type": "float",
                           "min": 0.0,
                           "max": 10.0,
                           "precision": 4,
                           "default": 0.0
                         }
                       },
                       "label": "Galvo"
                     },
                     {
                       "name": "galvoScale",
                       "label": "Galvo Scale",
                       "type": "vector2d",
                       "default": [
                         100.0,
                         100.0
                       ]
                     },
                     {
                       "row": {
                         "galvoShearX": {
                           "label": "Shear X",
                           "type": "float",
                           "min": -100.0,
                           "max": 100.0,
                           "precision": 3,
                           "default": 0.0
                         },
                         "galvoShearY": {
                           "label": "Shear Y",
                           "type": "float",
                           "min": -100.0,
                           "max": 100.0,
                           "precision": 3,
                           "default": 0.0
                         }
                       },
                       "label": " "
                     },
                     {
                       "name": "galvoRotate",
                       "label": "Galvo Rotate",
                       "type": "float",
                       "unit": "°",
                       "min": 0.0,
                       "max": 360.0,
                       "default": 0.0,
                       "precision": 3
                     },
                     {
                       "name": "galvoSwapxy",
                       "label": "Galvo Swap XY",
                       "type": "bool",
                       "default": false
                     },
                     {
                       "name": "ethDevice",
                       "label": "Ethernet Device",
                       "type": "ethDevice",
                       "default": "",
                     }
                   ]
                 }
               }
             ]
                 })json"},
         {// UV-Laser
    R"json({
             "class": "Machine",
             "items": [
               {
                 "row": {
                   "name": {
                     "label": "Name",
                     "type": "string"
                   },
                   "type": {
                     "label": "Type",
                     "type": "machineType"
                   },
                   "boardType": {
                     "label": "Board",
                     "type": "boardType"
                   }
                 },
                 "label": " "
               },
               {
                 "name": "description",
                 "label": "Description",
                 "type": "multiline"
               },
               {
                 "name": "line",
                 "type": "line"
               },
               {
                 "columns": {
                   "count": 2,
                   "items": [
                     {
                       "name": "maxTravel",
                       "label": "Travel",
                       "type": "vector3d",
                       "unit": "mm",
                       "default": [
                         100.0,
                         100.0,
                         100.0
                       ]
                     },
                     {
                       "name": "travelSpeed",
                       "label": "Travel Speed",
                       "type": "float",
                       "unit": "mm/s",
                       "min": 0.0,
                       "max": 100000.0,
                       "default": 0.0
                     },
                     {
                       "name": "framingSpeed",
                       "label": "Framing Speed",
                       "type": "float",
                       "unit": "mm/s",
                       "min": 0.0,
                       "max": 100000.0,
                       "default": 0.0
                     },
                     {
                       "name": "maxFeed",
                       "label": "Max Feed",
                       "type": "vector3d",
                       "unit": "mm/s",
                       "default": [
                         0.0,
                         0.0,
                         0.0
                       ]
                     },
                     {
                       "name": "maxAcceleration",
                       "label": "Max Accel",
                       "type": "vector3d",
                       "unit": "mm/s²",
                       "default": [
                         0.0,
                         0.0,
                         0.0
                       ]
                     },
                     {
                       "row": {
                         "minSpindle": {
                           "label": "Min",
                           "type": "float",
                           "unit": "rpm",
                           "min": 0.0,
                           "max": 1000000.0,
                           "default": 0.0
                         },
                         "maxSpindle": {
                           "label": "Max",
                           "type": "float",
                           "unit": "rpm",
                           "min": 0.0,
                           "max": 1000000.0,
                           "default": 0.0
                         }
                       },
                       "label": "Spindle"
                     },
                     {
                       "name": "line",
                       "type": "line",
                       "colSpan": 2
                     },
                     {
                       "row": {
                         "precision": {
                           "label": "Prec",
                           "type": "float",
                           "unit": "mm",
                           "min": 0.001,
                           "max": 10.0,
                           "precision": 3,
                           "default": 0.001
                         },
                         "ncPrecision": {
                           "label": "NC Prec",
                           "type": "float",
                           "unit": "mm",
                           "min": 0.001,
                           "max": 10.0,
                           "precision": 3,
                           "default": 0.001
                         }
                       },
                       "label": "Precision"
                     },
                     {
                       "name": "circlePrecision",
                       "label": "Circle Prec",
                       "type": "float",
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
                     },
                     {
                       "row": {
                         "galvoP1": {
                           "label": "P1",
                           "type": "float",
                           "min": 0.0,
                           "max": 10.0,
                           "default": 0.0,
                           "precision": 4
                         },
                         "galvoP2": {
                           "label": "P2",
                           "type": "float",
                           "min": 0.0,
                           "max": 10.0,
                           "precision": 4,
                           "default": 0.0
                         },
                         "galvoP3": {
                           "label": "P3",
                           "type": "float",
                           "min": 0.0,
                           "max": 10.0,
                           "precision": 4,
                           "default": 0.0
                         }
                       },
                       "label": "Galvo"
                     },
                     {
                       "name": "galvoScale",
                       "label": "Galvo Scale",
                       "type": "vector2d",
                       "default": [
                         100.0,
                         100.0
                       ]
                     },
                     {
                       "row": {
                         "galvoShearX": {
                           "label": "Shear X",
                           "type": "float",
                           "min": -100.0,
                           "max": 100.0,
                           "precision": 3,
                           "default": 0.0
                         },
                         "galvoShearY": {
                           "label": "Shear Y",
                           "type": "float",
                           "min": -100.0,
                           "max": 100.0,
                           "precision": 3,
                           "default": 0.0
                         }
                       },
                       "label": "Galvo Shear"
                     },
                     {
                       "name": "galvoRotate",
                       "label": "Galvo Rotate",
                       "type": "float",
                       "unit": "°",
                       "min": 0.0,
                       "max": 360.0,
                       "default": 0.0,
                       "precision": 3
                     },
                     {
                       "name": "galvoSwapxy",
                       "label": "Galvo Swap XY",
                       "type": "bool",
                       "default": false
                     },
                     {
                       "name": "ethDevice",
                       "label": "Ethernet Device",
                       "type": "ethDevice",
                       "default": ""
                     }
                   ]
                 }
               }
             ]
                 })json"},

   //================================
   // GCode CNC:
   //================================
         {
      R"json({
               "class": "Machine",
               "items": [
                 {
                   "row": {
                     "name": {
                       "label": "Name",
                       "type": "string"
                     },
                     "type": {
                       "label": "Type",
                       "type": "machineType"
                     },
                     "boardType": {
                       "label": "Board",
                       "type": "boardType"
                     }
                   },
                   "label": " "
                 },
                 {
                   "name": "description",
                   "label": "Description",
                   "type": "multiline"
                 },
                 {
                   "name": "line",
                   "type": "line"
                 },
                 {
                   "columns": {
                     "count": 2,
                     "items": [
                       {
                         "name": "maxTravel",
                         "label": "Travel",
                         "type": "vector3d",
                         "unit": "mm",
                         "default": [
                           100.0,
                           100.0,
                           100.0
                         ]
                       },
                       {
                         "name": "travelSpeed",
                         "label": "Travel Speed",
                         "type": "float",
                         "unit": "mm/s",
                         "min": 0.0,
                         "max": 100000.0,
                         "default": 0.0
                       },
                       {
                         "name": "framingSpeed",
                         "label": "Framing Speed",
                         "type": "float",
                         "unit": "mm/s",
                         "min": 0.0,
                         "max": 100000.0,
                         "default": 0.0
                       },
                       {
                         "row": {
                           "safeDist1": {
                             "label": "Safe 1",
                             "type": "float",
                             "unit": "mm",
                             "min": 0.0,
                             "max": 1000.0,
                             "default": 0.0
                           },
                           "safeDist2": {
                             "label": "Safe 2",
                             "type": "float",
                             "unit": "mm",
                             "min": 0.0,
                             "max": 1000.0,
                             "default": 0.0
                           }
                         },
                         "label": "Safe Dist"
                       },
                       {
                         "name": "maxFeed",
                         "label": "Max Feed",
                         "type": "vector3d",
                         "unit": "mm/s",
                         "default": [
                           0.0,
                           0.0,
                           0.0
                         ]
                       },
                       {
                         "name": "maxAcceleration",
                         "label": "Max Accel",
                         "type": "vector3d",
                         "unit": "mm/s²",
                         "default": [
                           0.0,
                           0.0,
                           0.0
                         ]
                       },
                       {
                         "row": {
                           "minSpindle": {
                             "label": "Min",
                             "type": "float",
                             "unit": "rpm",
                             "min": 0.0,
                             "max": 1000000.0,
                             "default": 0.0
                           },
                           "maxSpindle": {
                             "label": "Max",
                             "type": "float",
                             "unit": "rpm",
                             "min": 0.0,
                             "max": 1000000.0,
                             "default": 0.0
                           }
                         },
                         "label": "Spindle"
                       },
                       {
                         "name": "line",
                         "type": "line",
                         "colSpan": 2
                       },
                       {
                         "row": {
                           "precision": {
                             "label": "Prec",
                             "type": "float",
                             "unit": "mm",
                             "min": 0.001,
                             "max": 10.0,
                             "precision": 3,
                             "default": 0.001
                           },
                           "ncPrecision": {
                             "label": "NC Prec",
                             "type": "float",
                             "unit": "mm",
                             "min": 0.001,
                             "max": 10.0,
                             "precision": 3,
                             "default": 0.001
                           }
                         },
                         "label": "Precision"
                       },
                       {
                         "name": "circlePrecision",
                         "label": "Circle Prec",
                         "type": "float",
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
                 }
               ]
                   })json"},
      };

//---------------------------------------------------------
//   properties
//---------------------------------------------------------

const std::string_view Machine::properties() const {
      QString t = type();
      if (t.isEmpty()) {
            // Return the first property set as a fallback for
            // not-yet-initialised machines (e.g. during construction).
            return _properties[0];
            }
      for (int i = 0; i < machineTypes.size(); ++i)
            if (t == QString::fromStdString(machineTypes[i]))
                  return _properties[i];
      // Migration: accept legacy type names that were renamed.
      if (t == QStringLiteral("Fiber Laser"))
            return _properties[0]; // maps to "Q-switched Laser"
      Critical("bad machine type <{}>", t);
      return _properties[0];
      }
