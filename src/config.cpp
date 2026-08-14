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

#include "config.h"
#include "propertyjson.h"
#include "element.h"
#include "scriptengine.h"
#include <QJSEngine>

//---------------------------------------------------------
//   _properties
//---------------------------------------------------------

const std::string_view Config::_properties {
   R"json({
            "class": "Config",
            "rows": [
              {
                "columns": 2,
                "cat": "GUI",
                "cells": [
                  {
                    "name": "iconSize",
                    "label": "Icon Size",
                    "type": "int",
                    "cat": "GUI",
                    "min": 16,
                    "max": 128,
                    "default": 32,
                    "tooltip": "Size of toolbar and navigation icons in pixels"
                  },
                  {
                    "name": "navCubeSize",
                    "label": "Nav Cube Size",
                    "type": "int",
                    "cat": "GUI",
                    "min": 80,
                    "max": 400,
                    "default": 200,
                    "tooltip": "Size of the 3D navigation cube in the viewport"
                  },
                  {
                    "name": "handleSize",
                    "label": "Handle Size",
                    "type": "float",
                    "cat": "GUI",
                    "min": 0.01,
                    "max": 1.0,
                    "default": 0.2,
                    "precision": 2,
                    "step": 0.05,
                    "bigStep": 0.5,
                    "tooltip": "Relative size of selection and transform handles in the 3D viewport"
                  },
                  {
                    "name": "dragThreshold",
                    "label": "Drag Threshold",
                    "type": "float",
                    "cat": "GUI",
                    "unit": "mm",
                    "min": 0.0,
                    "max": 10.0,
                    "default": 0.5,
                    "precision": 2,
                    "step": 0.1,
                    "bigStep": 1.0,
                    "tooltip":
                        "Minimum movement distance before a drag operation is initiated, in millimetres"
                  },
                  {
                    "label": "Font",
                    "colSpan": 2,
                    "cells": [
                      {
                        "type": "font",
                        "cat": "GUI",
                        "default": "NotoSans",
                        "name": "font",
                        "sublabel": "Font"
                      },
                      {
                        "type": "int",
                        "cat": "GUI",
                        "min": 6,
                        "max": 72,
                        "default": 12,
                        "name": "fontSize",
                        "sublabel": "Font Size"
                      }
                    ]
                  }
                ]
              },
              {
                "columns": 2,
                "cat": "View",
                "cells": [
                  {
                    "name": "showGrid",
                    "label": "Show Grid",
                    "type": "bool",
                    "cat": "View",
                    "default": true
                  },
                  {
                    "name": "gridSpacing",
                    "label": "Grid Spacing",
                    "type": "float",
                    "cat": "View",
                    "unit": "mm",
                    "min": 1.0,
                    "max": 100.0,
                    "default": 10.0,
                    "precision": 1,
                    "step": 0.5,
                    "bigStep": 5.0
                  }
                ]
              },
              {
                "columns": 2,
                "cat": "Colors",
                "cells": [
                  {
                    "name": "panelBG",
                    "label": "Panel BG",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "canvasBG",
                    "label": "Canvas BG",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "accentColor",
                    "label": "Accent Color",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "gridColor",
                    "label": "Grid Color",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "framingColor",
                    "label": "Framing Color",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "markColor",
                    "label": "Mark Color",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "moveColor",
                    "label": "Move Color",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "type": "line",
                    "name": "mopColorLine",
                    "label": "Mop",
                    "colSpan": 2
                  }
                ]
              },
              {
                "columns": 8,
                "labelWidth": 50,
                "cat": "Colors",
                "cells": [
                  {
                    "name": "mopColor0",
                    "label": "#00",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor1",
                    "label": "#01",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor2",
                    "label": "#02",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor3",
                    "label": "#03",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor4",
                    "label": "#04",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor5",
                    "label": "#05",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor6",
                    "label": "#06",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor7",
                    "label": "#07",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor8",
                    "label": "#08",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor9",
                    "label": "#09",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor10",
                    "label": "#10",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor11",
                    "label": "#11",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor12",
                    "label": "#12",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor13",
                    "label": "#13",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor14",
                    "label": "#14",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor15",
                    "label": "#15",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor16",
                    "label": "#16",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor17",
                    "label": "#17",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor18",
                    "label": "#18",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor19",
                    "label": "#19",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor20",
                    "label": "#20",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor21",
                    "label": "#21",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor22",
                    "label": "#22",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor23",
                    "label": "#23",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor24",
                    "label": "#24",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor25",
                    "label": "#25",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor26",
                    "label": "#26",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor27",
                    "label": "#27",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor28",
                    "label": "#28",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor29",
                    "label": "#29",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor30",
                    "label": "#30",
                    "type": "color",
                    "cat": "Colors"
                  },
                  {
                    "name": "mopColor31",
                    "label": "#31",
                    "type": "color",
                    "cat": "Colors"
                  }
                ]
              },
              {
                "columns": 2,
                "cat": "Project",
                "cells": [
                  {
                    "name": "defaultMachine",
                    "label": "Default Machine",
                    "type": "machineName",
                    "cat": "Project",
                    "default": ""
                  },
                  {
                    "type": "empty"
                  },
                  {
                    "type": "line",
                    "name": "line",
                    "colSpan": 2
                  },
                  {
                    "name": "zcamDirectory",
                    "label": "ZCam",
                    "type": "path",
                    "cat": "Project",
                    "scriptable": true
                  },
                  {
                    "name": "projectsDirectory",
                    "label": "Projects",
                    "type": "path",
                    "cat": "Project",
                    "scriptable": true
                  },
                  {
                    "name": "artworkDirectory",
                    "label": "Artwork Path",
                    "type": "path",
                    "cat": "Project",
                    "default": "",
                    "scriptable": true
                  },
                  {
                    "name": "iconDirectory",
                    "label": "Icon Path",
                    "type": "path",
                    "cat": "Project",
                    "default": "~/ZCam/icons",
                    "scriptable": true
                  },
                  {
                    "name": "machinesDirectory",
                    "label": "Machines Path",
                    "type": "path",
                    "cat": "Project",
                    "default": "",
                    "scriptable": true
                  },
                  {
                    "name": "recipesDirectory",
                    "label": "Recipes Path",
                    "type": "path",
                    "cat": "Project",
                    "default": "",
                    "scriptable": true
                  },
                  {
                    "type": "line",
                    "name": "line",
                    "colSpan": 2
                  },
                  {
                    "name": "dxfScale",
                    "label": "DXF Scale",
                    "type": "float",
                    "cat": "Project",
                    "unit": "dpmm",
                    "min": 0.001,
                    "max": 1000.0,
                    "default": 72.0,
                    "precision": 3,
                    "step": 0.1,
                    "bigStep": 1.0
                  },
                  {
                    "name": "dxfCircleResolution",
                    "label": "DXF Circle Resolution",
                    "type": "int",
                    "cat": "Project",
                    "unit": "segments",
                    "min": 8,
                    "max": 2048,
                    "default": 360,
                    "step": 8,
                    "bigStep": 90
                  },
                  {
                    "name": "dxfCurveResolution",
                    "label": "DXF Curve Resolution",
                    "type": "int",
                    "cat": "Project",
                    "unit": "segments",
                    "min": 4,
                    "max": 1024,
                    "default": 100,
                    "step": 4,
                    "bigStep": 25
                  }
                ]
              },
              {
                "columns": 2,
                "cat": "SpaceMouse",
                "cells": [
                  {
                    "name": "smPanX",
                    "label": "Pan Left/Right",
                    "type": "float",
                    "cat": "SpaceMouse",
                    "min": 0.1,
                    "max": 50.0,
                    "default": 4.0,
                    "precision": 1,
                    "step": 0.5,
                    "bigStep": 2.0
                  },
                  {
                    "name": "smPanY",
                    "label": "Pan Up/Down",
                    "type": "float",
                    "cat": "SpaceMouse",
                    "min": 0.1,
                    "max": 50.0,
                    "default": 4.0,
                    "precision": 1,
                    "step": 0.5,
                    "bigStep": 2.0
                  },
                  {
                    "name": "smZoom",
                    "label": "Zoom",
                    "type": "float",
                    "cat": "SpaceMouse",
                    "min": 0.1,
                    "max": 50.0,
                    "default": 12.0,
                    "precision": 1,
                    "step": 0.5,
                    "bigStep": 2.0
                  },
                  {
                    "name": "smPitch",
                    "label": "Tilt Up/Down",
                    "type": "float",
                    "cat": "SpaceMouse",
                    "min": 0.1,
                    "max": 10.0,
                    "default": 1.0,
                    "precision": 1,
                    "step": 0.1,
                    "bigStep": 0.5
                  },
                  {
                    "name": "smYaw",
                    "label": "Turn Left/Right",
                    "type": "float",
                    "cat": "SpaceMouse",
                    "min": 0.1,
                    "max": 10.0,
                    "default": 1.0,
                    "precision": 1,
                    "step": 0.1,
                    "bigStep": 0.5
                  },
                  {
                    "name": "smRoll",
                    "label": "Twist",
                    "type": "float",
                    "cat": "SpaceMouse",
                    "min": 0.1,
                    "max": 10.0,
                    "default": 1.0,
                    "precision": 1,
                    "step": 0.1,
                    "bigStep": 0.5
                  }
                ]
              }
            ]
                })json"};

//--------------------------------------------------------------------
//     Config::Config
//--------------------------------------------------------------------

Config::Config(ZCam* zc)
      : Element(zc, nullptr) {
      setName(QStringLiteral("config"));
      }

//---------------------------------------------------------
//   Config::toJson
//    Serialise all config properties to a JSON object using
//    the propertyjson utility.  Also includes script bindings
//    from the Element base class.
//---------------------------------------------------------

nlohmann::json Config::toJson() const {
      nlohmann::json data     = nlohmann::json::object();
      const QMetaObject* meta = this->metaObject();

      auto propNames = propjson::parseAllPropertyNames(_properties);
      for (const auto& [name, type] : propNames)
            propjson::writePropertyToJson(
                data, this, meta, false, name, type, propjson::precisionForName(_properties, name));

      // ── Scripting ────────────────────────────────────────────────
      if (hasScript()) {
            nlohmann::json scripts = nlohmann::json::array();
            for (auto it = _scripts.constBegin(); it != _scripts.constEnd(); ++it) {
                  nlohmann::json s;
                  s["prop"]   = it.key().toStdString();
                  s["script"] = it.value().script.toStdString();
                  s["active"] = it.value().active;
                  scripts.push_back(s);
                  }
            data["scripts"] = scripts;
            }
      nlohmann::json comps = nlohmann::json::array();
      for (int i = 0; i < 3; ++i) {
            if (hasScriptComp(i)) {
                  nlohmann::json s;
                  s["prop"]   = scriptCompProp(i).toStdString();
                  s["comp"]   = i;
                  s["script"] = scriptComp(i).toStdString();
                  s["active"] = _scriptCompActive[i];
                  comps.push_back(s);
                  }
            }
      if (!comps.empty())
            data["scriptComp"] = comps;
      return data;
      }

//---------------------------------------------------------
//   Config::fromJson
//    Deserialise config properties from a JSON object.
//    Also restores script bindings from the Element base class.
//---------------------------------------------------------

void Config::fromJson(const nlohmann::json& data) {
      const QMetaObject* meta = this->metaObject();
      auto propNames          = propjson::parseAllPropertyNames(_properties);
      for (const auto& [name, type] : propNames)
            propjson::readPropertyFromJson(data, this, meta, false, name, type);

      // ── Scripting ────────────────────────────────────────────────
      // New format: "scripts" is an array of {prop, script, active}.
      if (data.contains("scripts") && data.at("scripts").is_array()) {
            for (const auto& s : data.at("scripts")) {
                  if (!s.contains("prop") || !s.contains("script"))
                        continue;
                  QString prop = QString::fromStdString(s.at("prop").get<std::string>());
                  ScriptEntry entry;
                  entry.script = QString::fromStdString(s.at("script").get<std::string>());
                  entry.active = s.value("active", true);
                  _scripts.insert(prop, entry);
                  }
            }
      // Legacy format: single "script" object.
      else if (data.contains("script")) {
            const nlohmann::json& s = data.at("script");
            if (s.contains("prop") && s.contains("script")) {
                  QString prop = QString::fromStdString(s.at("prop").get<std::string>());
                  ScriptEntry entry;
                  entry.script = QString::fromStdString(s.at("script").get<std::string>());
                  entry.active = s.value("active", true);
                  _scripts.insert(prop, entry);
                  }
            }
      if (data.contains("scriptComp")) {
            for (const auto& s : data.at("scriptComp")) {
                  if (!s.contains("prop") || !s.contains("script") || !s.contains("comp"))
                        continue;
                  int comp = s.at("comp").get<int>();
                  if (comp >= 0 && comp < 3) {
                        setScriptCompProp(comp, QString::fromStdString(s.at("prop").get<std::string>()));
                        setScriptComp(comp, QString::fromStdString(s.at("script").get<std::string>()));
                        _scriptCompActive[comp] = s.value("active", true);
                        }
                  }
            }
      }
