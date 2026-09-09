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

#include "recipe.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include "logger.h"
#include <memory>
#include <map>
#include <functional>

static const std::string _uvLaserPassProperties = R"({
    "class": "Layer Setting",
    "rows": [
        {
            "label": "Name",
            "cells": [
                {
                    "name": "name",
                    "type": "singleline"
                }
            ]
        },
        {
            "label": " ",
            "cells": [
                {
                    "type": "bool",
                    "default": false,
                    "name": "enabled",
                    "sublabel": "enabled"
                },
                {
                    "type": "int",
                    "scriptable": true,
                    "min": 1,
                    "max": 10000,
                    "default": 1,
                    "name": "numPasses",
                    "sublabel": "passes"
                }
            ]
        },
        {
            "cells": [
                {
                    "name": "line",
                    "type": "line",
                    "label": "Laser"
                }
            ]
        },
        {
            "columns": 2,
            "cells": [
                {
                    "label": "Power",
                    "cells": [
                        {
                            "name": "frequency",
                            "sublabel": "Freq,",
                            "type": "float",
                            "scriptable": true,
                            "unit": "kHz",
                            "default": 40.0
                        },
                        {
                            "name": "speed",
                            "sublabel": "speed",
                            "type": "float",
                            "scriptable": true,
                            "unit": "mm/s",
                            "min": 0.0,
                            "max": 100000.0,
                            "default": 1000.0
                        }
                    ]
                },
                {
                    "label": "Pulse",
                    "cells": [
                        {
                            "type": "float",
                            "unit": "ns",
                            "name": "uvMinPulse",
                            "sublabel": "minPulse",
                            "default": 1.0
                        },
                        {
                            "type": "float",
                            "unit": "ns",
                            "name": "uvMaxPulse",
                            "sublabel": "maxPulse",
                            "default": 20.0
                        }
                    ]
                },
                {
                    "label": "FPK",
                    "cells": [
                        {
                            "type": "bool",
                            "name": "enableFPK",
                            "label": "enable"
                        },
                        {
                            "type": "float",
                            "name": "fpkStartPower",
                            "sublabel": "Start Power",
                            "enabled": "enableFPK"
                        },
                        {
                            "type": "float",
                            "name": "fpkIncrement",
                            "sublabel": "Increment",
                            "enabled": "enableFPK"
                        }
                    ]
                },
                {
                    "label": "Tickle",
                    "cells": [
                        {
                            "type": "bool",
                            "name": "enableTickle",
                            "label": "enable"
                        },
                        {
                            "type": "float",
                            "name": "ticklePulse",
                            "sublabel": "Pulse",
                            "unit": "µs",
                            "enabled": "enableTickle"
                        },
                        {
                            "type": "float",
                            "name": "tickleFrequence",
                            "sublabel": "Freq",
                            "unit": "Hz",
                            "enabled": "enableTickle"
                        }
                    ]
                },
                {
                    "name": "line",
                    "type": "line",
                    "colSpan": 2
                },
                {
                    "label": "Hatch",
                    "cells": [
                        {
                            "name": "interval",
                            "sublabel": " ",
                            "type": "float",
                            "scriptable": true,
                            "unit": "mm",
                            "min": 0.001,
                            "max": 100.0,
                            "default": 0.05
                        },
                        {
                            "name": "intervalLpi",
                            "sublabel": "Lpi",
                            "type": "float",
                            "scriptable": true
                        },
                        {
                            "name": "intervalLpmm",
                            "sublabel": "Lpmm",
                            "type": "float",
                            "scriptable": true
                        }
                      ]
                },
                {
                    "label": "Angle",
                    "cells": [
                        {
                            "type": "float",
                            "scriptable": true,
                            "unit": "°",
                            "min": -360.0,
                            "max": 360.0,
                            "default": 0.0,
                            "name": "startAngle",
                            "sublabel": "Start"
                        },
                        {
                            "type": "float",
                            "scriptable": true,
                            "unit": "°",
                            "min": -360.0,
                            "max": 360.0,
                            "default": 90.0,
                            "name": "angleIncrement",
                            "sublabel": "Incr"
                        }
                    ]
                },
                {
                    "label": " ",
                    "cells": [
                        {
                            "type": "bool",
                            "default": true,
                            "name": "zigzag",
                            "sublabel": "Zigzag"
                        },
                        {
                            "type": "int",
                            "scriptable": true,
                            "min": 1,
                            "max": 100,
                            "default": 1,
                            "name": "interleave",
                            "sublabel": "Interleave"
                        }
                    ]
                },
                {
                    "label": "Wobble",
                    "cells": [
                        {
                            "type": "bool",
                            "default": false,
                            "name": "wobble",
                            "sublabel": "enable"
                        },
                        {
                            "type": "float",
                            "scriptable": true,
                            "unit": "mm",
                            "min": 0.0,
                            "max": 10.0,
                            "default": 0.05,
                            "name": "wobbleStep",
                            "sublabel": "Step"
                        },
                        {
                            "type": "float",
                            "scriptable": true,
                            "unit": "mm",
                            "min": 0.0,
                            "max": 10.0,
                            "default": 0.1,
                            "name": "wobbleSize",
                            "sublabel": "Size"
                        }
                    ]
                },
                {
                    "name": "line",
                    "type": "line",
                    "colSpan": 2
                },
                {
                    "label": "Override",
                    "cells": [
                        {
                            "name": "overrideTimings",
                            "sublabel": "enable",
                            "type": "bool",
                            "default": false
                        },
                        {
                              "name": "leer",
                              "type": "empty"
                        },
                        {
                              "name": "leer",
                              "type": "empty"
                        }
                    ]
                },
                {
                    "label": "Jump",
                    "colSpan": 2,
                    "cells": [
                        {
                            "name": "jumpSpeed",
                            "sublabel": "speed",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "min": 0,
                            "max": 99999.0,
                            "default": 6000.0,
                            "unit": "mm/s²"
                        },
                        {
                            "name": "jumpDistanceLimit",
                            "sublabel": "limit",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "mm",
                            "min": 0.0,
                            "max": 100.0,
                            "default": 10.0
                        },
                        {
                            "name": "minJumpDelay",
                            "sublabel": "minDelay",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 200.0
                        },
                        {
                            "name": "maxJumpDelay",
                            "sublabel": "maxDelay",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 400.0
                        }
                    ]
                },
                {
                    "label": "Delay",
                    "colSpan": 2,
                    "cells": [
                        {
                            "name": "onDelay",
                            "sublabel": "on",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "min": -9999,
                            "max": 9999.0,
                            "default": 100.0,
                            "unit": "µs"
                        },
                        {
                            "name": "offDelay",
                            "sublabel": "off",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 100.0
                        },
                        {
                            "name": "endDelay",
                            "sublabel": "end",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 100.0
                        },
                        {
                            "name": "polygonDelay",
                            "sublabel": "polygon",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 100.0
                        }
                    ]
                }
            ]
        }
    ]
                  })";

static const std::string _laserPassProperties = R"({
    "class": "Layer Setting",
    "rows": [
        {
            "label": "Name",
            "cells": [
                {
                    "name": "name",
                    "type": "singleline"
                }
            ]
        },
        {
            "label": " ",
            "cells": [
                {
                    "type": "bool",
                    "default": false,
                    "name": "enabled",
                    "sublabel": "enabled"
                },
                {
                    "type": "int",
                    "scriptable": true,
                    "min": 1,
                    "max": 10000,
                    "default": 1,
                    "name": "numPasses",
                    "sublabel": "passes"
                }
            ]
        },
        {
            "cells": [
                {
                    "name": "line",
                    "type": "line"
                }
            ]
        },
        {
            "columns": 2,
            "cells": [
                {
                    "label": "Laser",
                    "colSpan": 2,
                    "cells": [
                        {
                            "type": "float",
                            "scriptable": true,
                            "unit": "%",
                            "min": 0.0,
                            "max": 100.0,
                            "default": 20.0,
                            "name": "power",
                            "sublabel": "Power"
                        },
                        {
                            "type": "float",
                            "scriptable": true,
                            "unit": "kHz",
                            "default": 40.0,
                            "name": "frequency",
                            "sublabel": "Frequency"
                        },
                        {
                            "type": "pulsewidth",
                            "unit": "ns",
                            "name": "pulseWidth",
                            "sublabel": "Pulse"
                        },
                        {
                            "type": "float",
                            "scriptable": true,
                            "unit": "mm/s",
                            "min": 0.0,
                            "max": 100000.0,
                            "default": 1000.0,
                            "name": "speed",
                            "sublabel": "speed"
                        }
                    ]
                },
                {
                    "name": "line",
                    "type": "line",
                    "colSpan": 2
                },
                {
                    "label": "Hatch",
                    "cells": [
                        {
                            "name": "interval",
                            "sublabel": " ",
                            "type": "float",
                            "scriptable": true,
                            "unit": "mm",
                            "min": 0.001,
                            "max": 100.0,
                            "default": 0.05
                        },
                        {
                            "name": "intervalLpi",
                            "sublabel": "Lpi",
                            "type": "float",
                            "scriptable": true
                        },
                        {
                            "name": "intervalLpmm",
                            "sublabel": "Lpmm",
                            "type": "float",
                            "scriptable": true
                        }
                      ]
                },
                {
                    "label": "Angle",
                    "cells": [
                        {
                            "type": "float",
                            "scriptable": true,
                            "unit": "°",
                            "min": -360.0,
                            "max": 360.0,
                            "default": 0.0,
                            "name": "startAngle",
                            "sublabel": "Start"
                        },
                        {
                            "type": "float",
                            "scriptable": true,
                            "unit": "°",
                            "min": -360.0,
                            "max": 360.0,
                            "default": 90.0,
                            "name": "angleIncrement",
                            "sublabel": "Incr"
                        }
                    ]
                },
                {
                    "label": " ",
                    "cells": [
                        {
                            "type": "bool",
                            "default": true,
                            "name": "zigzag",
                            "sublabel": "Zigzag"
                        },
                        {
                            "type": "int",
                            "scriptable": true,
                            "min": 1,
                            "max": 100,
                            "default": 1,
                            "name": "interleave",
                            "sublabel": "Interleave"
                        }
                    ]
                },
                {
                    "label": "Wobble",
                    "cells": [
                        {
                            "type": "bool",
                            "default": false,
                            "name": "wobble",
                            "sublabel": "enable"
                        },
                        {
                            "type": "float",
                            "scriptable": true,
                            "unit": "mm",
                            "min": 0.0,
                            "max": 10.0,
                            "default": 0.05,
                            "name": "wobbleStep",
                            "sublabel": "Step"
                        },
                        {
                            "type": "float",
                            "scriptable": true,
                            "unit": "mm",
                            "min": 0.0,
                            "max": 10.0,
                            "default": 0.1,
                            "name": "wobbleSize",
                            "sublabel": "Size"
                        }
                    ]
                },
                {
                    "name": "line",
                    "type": "line",
                    "colSpan": 2
                },
                {
                    "label": "Override",
                    "cells": [
                        {
                            "name": "overrideTimings",
                            "sublabel": " ",
                            "type": "bool",
                            "default": false
                        },
                        {
                              "name": "leer",
                              "type": "empty"
                        },
                        {
                              "name": "leer",
                              "type": "empty"
                        }
                    ]
                },
                {
                    "label": "Jump",
                    "colSpan": 2,
                    "cells": [
                        {
                            "name": "jumpSpeed",
                            "sublabel": "speed",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "min": 0,
                            "max": 99999.0,
                            "default": 6000.0,
                            "unit": "mm/s²"
                        },
                        {
                            "name": "jumpDistanceLimit",
                            "sublabel": "limit",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "mm",
                            "min": 0.0,
                            "max": 100.0,
                            "default": 10.0
                        },
                        {
                            "name": "minJumpDelay",
                            "sublabel": "minDelay",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 200.0
                        },
                        {
                            "name": "maxJumpDelay",
                            "sublabel": "maxDelay",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 400.0
                        }
                    ]
                },
                {
                    "label": "Delay",
                    "colSpan": 2,
                    "cells": [
                        {
                            "name": "onDelay",
                            "sublabel": "on",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "min": -9999,
                            "max": 9999.0,
                            "default": 100.0,
                            "unit": "µs"
                        },
                        {
                            "name": "offDelay",
                            "sublabel": "off",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 100.0
                        },
                        {
                            "name": "endDelay",
                            "sublabel": "end",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 100.0
                        },
                        {
                            "name": "polygonDelay",
                            "sublabel": "polygon",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "scriptable": true,
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 100.0
                        }
                    ]
                }
            ]
        }
    ]
                  })";

//---------------------------------------------------------
//   LaserLayerSetting
//---------------------------------------------------------

json LaserPass::toJson() const {
      json data;
      data["name"]              = _name.toStdString();
      data["enabled"]           = _enabled;
      data["power"]             = _power;
      data["speed"]             = _speed;
      data["travelSpeed"]       = _jumpSpeed;
      data["frequency"]         = _frequency;
      data["pulseWidth"]        = _pulseWidth;
      data["numPasses"]         = _numPasses;
      data["interval"]          = _interval;
      data["startAngle"]        = _startAngle;
      data["angleIncrement"]    = _angleIncrement;
      data["zigzag"]            = _zigzag;
      data["interleave"]        = _interleave;
      data["wobble"]            = _wobble;
      data["wobbleStep"]        = _wobbleStep;
      data["wobbleSize"]        = _wobbleSize;
      data["overrideTimings"]   = _overrideTimings;
      data["onDelay"]           = _onDelay;
      data["offDelay"]          = _offDelay;
      data["endDelay"]          = _endDelay;
      data["polygonDelay"]      = _polygonDelay;
      data["jumpSpeed"]         = _jumpSpeed;
      data["minJumpDelay"]      = _minJumpDelay;
      data["maxJumpDelay"]      = _maxJumpDelay;
      data["jumpDistanceLimit"] = _jumpDistanceLimit;
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void LaserPass::fromJson(const json& data) {
      if (data.contains("name"))
            _name = QString::fromStdString(data.at("name").get<std::string>());
      if (data.contains("enabled"))
            _enabled = data.at("enabled");
      if (data.contains("power"))
            _power = data.at("power");
      if (data.contains("speed"))
            _speed = data.at("speed");
      if (data.contains("travelSpeed"))
            _jumpSpeed = data.at("travelSpeed");
      if (data.contains("frequency"))
            _frequency = data.at("frequency");
      if (data.contains("pulseWidth"))
            _pulseWidth = data.at("pulseWidth");
      if (data.contains("numPasses"))
            _numPasses = data.at("numPasses");
      if (data.contains("interval"))
            _interval = data.at("interval");
      if (data.contains("startAngle"))
            _startAngle = data.at("startAngle");
      if (data.contains("angleIncrement"))
            _angleIncrement = data.at("angleIncrement");
      if (data.contains("zigzag"))
            _zigzag = data.at("zigzag");
      if (data.contains("interleave"))
            _interleave = data.at("interleave");
      if (data.contains("wobble"))
            _wobble = data.at("wobble");
      if (data.contains("wobbleStep"))
            _wobbleStep = data.at("wobbleStep");
      if (data.contains("wobbleSize"))
            _wobbleSize = data.at("wobbleSize");
      if (data.contains("overrideTimings"))
            _overrideTimings = data.at("overrideTimings");
      if (data.contains("onDelay"))
            _onDelay = data.at("onDelay");
      if (data.contains("offDelay"))
            _offDelay = data.at("offDelay");
      if (data.contains("endDelay"))
            _endDelay = data.at("endDelay");
      if (data.contains("polygonDelay"))
            _polygonDelay = data.at("polygonDelay");
      if (data.contains("jumpSpeed"))
            _jumpSpeed = data.at("jumpSpeed");
      if (data.contains("minJumpDelay"))
            _minJumpDelay = data.at("minJumpDelay");
      if (data.contains("maxJumpDelay"))
            _maxJumpDelay = data.at("maxJumpDelay");
      if (data.contains("jumpDistanceLimit"))
            _jumpDistanceLimit = data.at("jumpDistanceLimit");
      }

//---------------------------------------------------------
//   LaserLayersSettings
//---------------------------------------------------------

json LaserPasses::toJson() const {
      json data = json::array();
      for (const auto& layer : *this)
            data.push_back(layer.toJson());
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void LaserPasses::fromJson(const json& data) {
      clear();
      if (data.is_array()) {
            for (const auto& jlayer : data) {
                  LaserPass l;
                  l.fromJson(jlayer);
                  push_back(l);
                  }
            }
      }

//---------------------------------------------------------
//   Recipe
//---------------------------------------------------------

json LaserRecipe::toJson() const {
      json data;
      data["name"]        = _name.toStdString();
      data["description"] = _description.toStdString();
      data["numPasses"]   = _numPasses;
      data["machineType"] = std::string(machineTypeMap.name(_machineType));
      data["layer"]       = _passes.toJson();
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void LaserRecipe::fromJson(const json& data) {
      if (data.contains("name"))
            _name = QString::fromStdString(data.at("name").get<std::string>());
      if (data.contains("description"))
            _description = QString::fromStdString(data.at("description").get<std::string>());
      if (data.contains("numPasses"))
            _numPasses = data.at("numPasses");
      if (data.contains("machineType")) {
            if (data.at("machineType").is_string()) {
                  std::string nameStr = data.at("machineType").get<std::string>();
                  auto mt             = machineTypeMap.type(std::string_view(nameStr));
                  if (mt)
                        _machineType = *mt;
                  else
                        _machineType = MachineType::UNKNOWN;
                  }
            }
      if (data.contains("layer"))
            _passes.fromJson(data.at("layer"));
      }

//---------------------------------------------------------
//   properties
//    Returns the JSON description of the recipe properties for
//    the inspector / property editor.  Describes the top-level
//    recipe fields: name, description, numPasses and machineType.
//    The pass/layer settings are described by LaserPass::properties().
//---------------------------------------------------------

const std::string LaserRecipe::properties() const {
      return R"({
    "class": "Recipe",
    "rows": [
        {
            "label": "Name",
            "cells": [
                {
                    "type": "singleline",
                    "label": " ",
                    "name": "name"
                },
                {
                    "type": "int",
                    "name": "numPasses",
                    "sublabel": "Passes",
                    "min": 1,
                    "max": 1000,
                    "default": 1
                },
                {
                    "type": "machineType",
                    "sublabel": "type",
                    "name": "machineType"
                }
            ]
        },
        {
            "label": "Description",
            "cells": [
                {
                    "name": "description",
                    "type": "multiline"
                }
            ]
        }
    ]
                  })";
      }

//=========================================================
//   RecipeTreeModel
//=========================================================
struct RecipeTreeModel::Node {
      QString name;
      QString relativePath; // relative to recipes root
      bool isDir    = false;
      int recipeIdx = -1; // valid for leaf nodes only
      Node* parent  = nullptr;
      std::vector<std::unique_ptr<Node>> children;
      };

RecipeTreeModel::RecipeTreeModel(QObject* parent)
    : QAbstractItemModel(parent), _root(std::make_unique<Node>()) {
      _root->name  = QStringLiteral("root");
      _root->isDir = true;
      }

RecipeTreeModel::~RecipeTreeModel() = default;

//---------------------------------------------------------
//   nodeForIndex
//---------------------------------------------------------

RecipeTreeModel::Node* RecipeTreeModel::nodeForIndex(const QModelIndex& idx) const {
      if (!idx.isValid())
            return _root.get();
      return static_cast<Node*>(idx.internalPointer());
      }

//---------------------------------------------------------
//   indexForNode
//---------------------------------------------------------

QModelIndex RecipeTreeModel::indexForNode(Node* node) const {
      if (!node || node == _root.get())
            return {};
      Node* parent = node->parent;
      if (!parent)
            return {};
      int row = 0;
      for (const auto& c : parent->children) {
            if (c.get() == node)
                  return createIndex(row, 0, node);
            ++row;
            }
      return {};
      }

//---------------------------------------------------------
//   index
//---------------------------------------------------------

QModelIndex RecipeTreeModel::index(int row, int column, const QModelIndex& parent) const {
      if (!hasIndex(row, column, parent))
            return {};
      Node* parentNode = nodeForIndex(parent);
      if (parentNode && row >= 0 && row < static_cast<int>(parentNode->children.size()))
            return createIndex(row, column, parentNode->children[row].get());
      return {};
      }

//---------------------------------------------------------
//   parent
//---------------------------------------------------------

QModelIndex RecipeTreeModel::parent(const QModelIndex& child) const {
      if (!child.isValid())
            return {};
      Node* childNode = static_cast<Node*>(child.internalPointer());
      if (!childNode || !childNode->parent)
            return {};
      Node* parentNode = childNode->parent;
      if (parentNode == _root.get())
            return {};
      return indexForNode(parentNode);
      }

//---------------------------------------------------------
//   rowCount
//---------------------------------------------------------

int RecipeTreeModel::rowCount(const QModelIndex& parent) const {
      Node* node = nodeForIndex(parent);
      if (!node)
            return 0;
      return static_cast<int>(node->children.size());
      }

//---------------------------------------------------------
//   columnCount
//---------------------------------------------------------

int RecipeTreeModel::columnCount(const QModelIndex& parent) const {
      Q_UNUSED(parent)
      return 1;
      }

//---------------------------------------------------------
//   data
//---------------------------------------------------------

QVariant RecipeTreeModel::data(const QModelIndex& index, int role) const {
      Node* node = nodeForIndex(index);
      if (!node || node == _root.get())
            return {};
      switch (role) {
            case Qt::DisplayRole:
            case NameRole: return node->name;
            case IsDirRole: return node->isDir;
            case RecipeIdxRole: return node->recipeIdx;
            case PathRole: return node->relativePath;
            }
      return {};
      }

//---------------------------------------------------------
//   roleNames
//---------------------------------------------------------

QHash<int, QByteArray> RecipeTreeModel::roleNames() const {
      QHash<int, QByteArray> roles;
      roles[NameRole]      = "nodeName";
      roles[IsDirRole]     = "isDir";
      roles[RecipeIdxRole] = "recipeIdx";
      roles[PathRole]      = "nodePath";
      return roles;
      }

//---------------------------------------------------------
//   clear / beginBuild / endBuild
//---------------------------------------------------------

void RecipeTreeModel::clear() {
      beginResetModel();
      _root->children.clear();
      endResetModel();
      }

void RecipeTreeModel::beginBuild() {
      beginResetModel();
      _root->children.clear();
      }

void RecipeTreeModel::endBuild() {
      endResetModel();
      }

//---------------------------------------------------------
//   addDirNode
//---------------------------------------------------------

void* RecipeTreeModel::addDirNode(const QString& name, const QString& relativePath, void* parent) {
      Node* parentNode   = parent ? static_cast<Node*>(parent) : _root.get();
      auto node          = std::make_unique<Node>();
      node->name         = name;
      node->relativePath = relativePath;
      node->isDir        = true;
      node->parent       = parentNode;
      Node* raw          = node.get();
      parentNode->children.push_back(std::move(node));
      return raw;
      }

//---------------------------------------------------------
//   addRecipeNode
//---------------------------------------------------------

void* RecipeTreeModel::addRecipeNode(
    const QString& name, const QString& relativePath, int recipeIdx, void* parent) {
      Node* parentNode   = parent ? static_cast<Node*>(parent) : _root.get();
      auto node          = std::make_unique<Node>();
      node->name         = name;
      node->relativePath = relativePath;
      node->isDir        = false;
      node->recipeIdx    = recipeIdx;
      node->parent       = parentNode;
      Node* raw          = node.get();
      parentNode->children.push_back(std::move(node));
      return raw;
      }

//---------------------------------------------------------
//   removeNode
//---------------------------------------------------------

void RecipeTreeModel::removeNode(const QModelIndex& idx) {
      Node* node = nodeForIndex(idx);
      if (!node || node == _root.get())
            return;
      Node* parent = node->parent;
      if (!parent)
            return;
      int row = 0;
      for (auto& c : parent->children) {
            if (c.get() == node) {
                  beginRemoveRows(indexForNode(parent), row, row);
                  parent->children.erase(parent->children.begin() + row);
                  endRemoveRows();
                  return;
                  }
            ++row;
            }
      }

//---------------------------------------------------------
//   Q_INVOKABLE helpers
//---------------------------------------------------------

int RecipeTreeModel::recipeIndex(const QModelIndex& idx) const {
      Node* node = nodeForIndex(idx);
      if (!node || node->isDir)
            return -1;
      return node->recipeIdx;
      }

//---------------------------------------------------------
//   indexForRecipe
//    Depth-first search through the tree to find the leaf node
//    whose recipeIdx matches. Returns an invalid model index
//    if not found.
//---------------------------------------------------------

QModelIndex RecipeTreeModel::indexForRecipe(int recipeIdx) const {
      std::function<QModelIndex(Node*)> search = [&](Node* node) -> QModelIndex {
            for (const auto& child : node->children) {
                  if (!child->isDir && child->recipeIdx == recipeIdx)
                        return indexForNode(child.get());
                  if (child->isDir) {
                        auto idx = search(child.get());
                        if (idx.isValid())
                              return idx;
                        }
                  }
            return {};
            };
      return search(_root.get());
      }

bool RecipeTreeModel::isDir(const QModelIndex& idx) const {
      Node* node = nodeForIndex(idx);
      return node && node->isDir;
      }

QString RecipeTreeModel::path(const QModelIndex& idx) const {
      Node* node = nodeForIndex(idx);
      return node ? node->relativePath : QString();
      }

//---------------------------------------------------------
//   indexForPath
//    Depth-first search for the folder (dir) node whose relative
//    path matches \a relPath.  Returns an invalid model index when
//    no such node exists.
//---------------------------------------------------------

QModelIndex RecipeTreeModel::indexForPath(const QString& relPath) const {
      std::function<QModelIndex(Node*)> search = [&](Node* node) -> QModelIndex {
            for (const auto& child : node->children) {
                  if (child->isDir && child->relativePath == relPath)
                        return indexForNode(child.get());
                  if (child->isDir) {
                        auto idx = search(child.get());
                        if (idx.isValid())
                              return idx;
                        }
                  }
            return {};
            };
      return search(_root.get());
      }

//---------------------------------------------------------
//   topRowCount
//    Number of children of the root.  Lets QML enumerate the top level
//    without passing the (invalid) root QModelIndex, which does not
//    round-trip through the QML/C++ boundary.
//---------------------------------------------------------

int RecipeTreeModel::topRowCount() const {
      return static_cast<int>(_root->children.size());
      }

//---------------------------------------------------------
//   topIndexAt
//    Child of the root at \a row, or an invalid index when out of range.
//---------------------------------------------------------

QModelIndex RecipeTreeModel::topIndexAt(int row) const {
      if (row < 0 || row >= static_cast<int>(_root->children.size()))
            return {};
      return indexForNode(_root->children[row].get());
      }

//---------------------------------------------------------
//   sanitiseFileName
//---------------------------------------------------------

static QString sanitiseFileName(QString name) {
      if (name.isEmpty())
            name = QStringLiteral("unnamed");
      name.replace(QRegularExpression("[^a-zA-Z0-9_\\-]"), "_");
      return name;
      }

//---------------------------------------------------------
//   uniqueRecipeNameInDir
//    Returns a name that does not collide with an existing recipe
//    (or its .json file) in the given directory (relative path).
//    On duplicates the name is suffixed with "-NNN" starting at 1,
//    i.e. "New Recipe", "New Recipe-1", "New Recipe-2", ....
//    A name that already ends in "-NNN" (e.g. "foo-1") is treated
//    as base "foo", so it will continue as "foo-2", "foo-3", ....
//---------------------------------------------------------

static QString uniqueRecipeNameInDir(const std::vector<std::unique_ptr<LaserRecipe>>& recipes,
    const QString& name, const QString& relDir) {
      auto inDir = [&relDir](const LaserRecipe* r) -> bool {
            // A recipe "lives" in a directory when its relative file path
            // (the folder holding its .json) equals relDir exactly.
            return r->relativeFilePath() == relDir;
            };

      auto nameExists = [&](const QString& candidate) -> bool {
            for (const auto& r : recipes)
                  if (inDir(r.get()) &&
                      sanitiseFileName(r->name()) == sanitiseFileName(candidate))
                        return true;
            return false;
            };

      if (!nameExists(name))
            return name;

      // Strip a trailing "-NNN" counter so that "foo-1" yields "foo-2"
      // instead of "foo-1-2".
      QString base   = name;
      QString number;
      const QRegularExpression re(QStringLiteral("^(.*)-(\\d+)$"));
      QRegularExpressionMatch m = re.match(name);
      if (m.hasMatch()) {
            base   = m.captured(1);
            number = m.captured(2);
            }

      int counter = (number.isEmpty() ? 0 : number.toInt());
      while (nameExists(base + "-" + QString::number(++counter)))
            ;
      return base + "-" + QString::number(counter);
      }

//=========================================================
//   LaserReceipes
//=========================================================
Recipe::Recipe(QObject* parent) : QObject(parent), _treeModel(new RecipeTreeModel(this)) {
      // Register the opaque pointer metatypes so that QML can correctly
      // wrap and unwrap LaserRecipe* and LaserPass* values without
      // attempting to manage their lifetime.
      qRegisterMetaType<LaserRecipe*>("LaserRecipe*");
      qRegisterMetaType<LaserPass*>("LaserPass*");
      }

Recipe::~Recipe() = default;

//---------------------------------------------------------
//   set_machineType
//    Set the machine type filter and reload recipes from disk.
//---------------------------------------------------------

void Recipe::set_machineType(MachineType type) {
      if (_machineType == type)
            return;
      _machineType = type;
      emit machineTypeChanged();
      reload();
      }

//---------------------------------------------------------
//   reload
//    Reload recipes from disk using the current _rootDir and _machineType.
//---------------------------------------------------------

void Recipe::reload() {
      if (_rootDir.isEmpty())
            return;
      loadFromDirectory(_rootDir, machineType());
      }

void Recipe::updateRecipe(int idx, LaserRecipe* r) {
      if (idx >= 0 && idx < recipes.size() && r) {
            // LaserRecipe is a QObject (non-copyable), so copy
            // properties individually.
            recipes[idx]->set_name(r->name());
            recipes[idx]->set_description(r->description());
            recipes[idx]->set_numPasses(r->numPasses());
            recipes[idx]->set_machineType(r->machineType());
            recipes[idx]->passes() = r->passes();
            emit recipeModelChanged();
            emit recipeChanged(idx);
            }
      }

void Recipe::addRecipe(const QString& name) {
      auto r = std::make_unique<LaserRecipe>();
      r->set_name(name);
      recipes.push_back(std::move(r));
      emit recipeModelChanged();
      rebuildTreeModel();
      }

void Recipe::removeRecipe(int idx) {
      if (idx < 0 || idx >= static_cast<int>(recipes.size()))
            return;

      // Rename the corresponding .json file on disk by appending
      // a ".del" extension so it is not picked up on the next load.
      if (!_rootDir.isEmpty()) {
            const LaserRecipe* r = recipes[idx].get();
            QString relPath      = r->relativeFilePath();
            QString fileName     = sanitiseFileName(r->name()) + ".json";

            // The file lives under _rootDir/machineTypeName/...
            QString mtName  = QString::fromUtf8(machineTypeMap.name(_machineType));
            QString baseDir = _rootDir;
            if (!mtName.isEmpty())
                  baseDir = QDir(_rootDir).filePath(mtName);

            QString subDirPath = baseDir;
            if (!relPath.isEmpty())
                  subDirPath = QDir(baseDir).filePath(relPath);

            QString filePath = QDir(subDirPath).filePath(fileName);
            QFile file(filePath);
            if (file.exists()) {
                  QString delPath = filePath + ".del";
                  if (!file.rename(delPath))
                        Warning("LaserReceipes::removeRecipe: cannot rename {} to {}", filePath.toStdString(),
                            delPath.toStdString());
                  }
            }

      recipes.erase(recipes.begin() + idx);
      emit recipeModelChanged();
      rebuildTreeModel();
      }

LaserPass Recipe::layer(int recipeIdx, int layerIdx) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            const auto& r = *recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.passes().size())
                  return r.pass(layerIdx);
            }
      return LaserPass();
      }

LaserPass* Recipe::layerPtr(int recipeIdx, int layerIdx) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            auto& r = *recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.passes().size())
                  return &r.pass(layerIdx);
            }
      return nullptr;
      }

void Recipe::updateLayer(int recipeIdx, int layerIdx, const LaserPass& l) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            auto& r = *recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.passes().size()) {
                  r.pass(layerIdx) = l;
                  emit recipeChanged(recipeIdx);
                  }
            }
      }

void Recipe::addLayer(int recipeIdx, const QString& name) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            LaserPass l;
            l.set_name(name);
            recipes[recipeIdx]->passes().push_back(l);
            emit recipeChanged(recipeIdx);
            }
      }

void Recipe::removeLayer(int recipeIdx, int layerIdx) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            auto& r = *recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.passes().size()) {
                  r.passes().erase(r.passes().begin() + layerIdx);
                  emit recipeChanged(recipeIdx);
                  }
            }
      }

QStringList Recipe::layerModel(int recipeIdx) const {
      QStringList names;
      if (recipeIdx >= 0 && recipeIdx < recipes.size())
            for (const auto& l : recipes[recipeIdx]->passes())
                  names.append(l.name());
      return names;
      }

QStringList Recipe::recipeModel() const {
      QStringList names;
      for (const auto& r : recipes)
            names.append(r->name());
      return names;
      }

//---------------------------------------------------------
//   toJson
//---------------------------------------------------------

json Recipe::toJson() const {
      json data = json::array();
      for (const auto& r : recipes)
            data.push_back(r->toJson());
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void Recipe::fromJson(const json& data) {
      recipes.clear();
      if (data.is_array()) {
            for (const auto& r : data) {
                  auto recipe = std::make_unique<LaserRecipe>();
                  recipe->fromJson(r);
                  recipes.push_back(std::move(recipe));
                  }
            }
      emit recipeModelChanged();
      }

//---------------------------------------------------------
//   loadFromDirectory
//    Recursively load all .json recipe files from dir,
//    descending into subdirectories.  Builds the tree model.
//    If mt is non-default, only recipes under dir/machineTypeName/
//    are loaded.
//---------------------------------------------------------

void Recipe::loadFromDirectory(const QString& dir, MachineType mt) {
      recipes.clear();
      _rootDir     = dir;
      _machineType = mt;

      _treeModel->beginBuild();

      // Convert the MachineType enum to its string name for the
      // directory path.
      QString mtName = QString::fromUtf8(machineTypeMap.name(mt));

      // If a machineType filter is set, only load recipes under
      // dir/machineTypeName/ and strip the machineType prefix from
      // relative paths.
      QString baseDir   = dir;
      void* machineNode = nullptr; // top-level machine-type node (if any)
      if (!mtName.isEmpty()) {
            baseDir = QDir(dir).filePath(mtName);
            // Add a top-level node showing the machine type so the user
            // can see for which machine these recipes apply.
            machineNode = _treeModel->addDirNode(mtName, QString(), nullptr);
            }

      QDir d(baseDir);
      if (d.exists()) {
            // Recursive lambda
            std::function<void(const QString& absPath, const QString& relPath, void* parentNode)> recurse =
                [&](const QString& absPath, const QString& relPath, void* parentNode) {
                      QDir d2(absPath);
                      // Process subdirectories first
                      const auto dirs = d2.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                      for (const QString& subDir : dirs) {
                            QString subAbs = d2.filePath(subDir);
                            QString subRel = relPath.isEmpty() ? subDir : (relPath + "/" + subDir);
                            void* dirNode  = _treeModel->addDirNode(subDir, subRel, parentNode);
                            recurse(subAbs, subRel, dirNode);
                            }
                      // Process recipe files
                      const auto files = d2.entryList({"*.json"}, QDir::Files, QDir::Name);
                      for (const QString& fileName : files) {
                            QString filePath = d2.filePath(fileName);
                            QFile file(filePath);
                            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                                  Warning("LaserReceipes::loadFromDirectory: cannot open {}",
                                      filePath.toStdString());
                                  continue;
                                  }
                            QByteArray data = file.readAll();
                            file.close();
                            try {
                                  json jr     = json::parse(data.toStdString());
                                  auto recipe = std::make_unique<LaserRecipe>();
                                  recipe->fromJson(jr);
                                  recipe->setRelativeFilePath(relPath);
                                  int idx = static_cast<int>(recipes.size());
                                  recipes.push_back(std::move(recipe));
                                  // Display name: use recipe name if available, otherwise file base name
                                  QString displayName = recipes.back()->name().isEmpty()
                                                            ? QFileInfo(fileName).baseName()
                                                            : recipes.back()->name();
                                  _treeModel->addRecipeNode(displayName, relPath, idx, parentNode);
                                  }
                            catch (const json::parse_error& e) {
                                  Warning("LaserReceipes::loadFromDirectory: parse error in {}: {}",
                                      filePath.toStdString(), e.what());
                                  }
                            }
                      };
            recurse(baseDir, QString(), machineNode);
            }

      _treeModel->endBuild();
      emit recipeModelChanged();
      }

//---------------------------------------------------------
//   saveToDirectory
//    Save all recipes as individual .json files into dir,
//    preserving subdirectory structure.  If mt is non-default,
//    recipes are saved under dir/machineTypeName/.
//---------------------------------------------------------

void Recipe::saveToDirectory(const QString& dir, MachineType mt) const {
      // Convert the MachineType enum to its string name for the
      // directory path.
      QString mtName = QString::fromUtf8(machineTypeMap.name(mt));

      // If machineType is set, save under dir/machineTypeName/
      QString baseDir = dir;
      if (!mtName.isEmpty())
            baseDir = QDir(dir).filePath(mtName);

      QDir rootDir(baseDir);
      if (!rootDir.exists())
            rootDir.mkpath(".");

      for (const auto& r : recipes) {
            QString relPath  = r->relativeFilePath();
            QString fileName = sanitiseFileName(r->name()) + ".json";

            // Ensure the subdirectory exists
            QString subDirPath = baseDir;
            if (!relPath.isEmpty())
                  subDirPath = QDir(baseDir).filePath(relPath);

            QDir subDir(subDirPath);
            if (!subDir.exists())
                  subDir.mkpath(".");

            QString filePath = QDir(subDirPath).filePath(fileName);
            QFile file(filePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                  Warning(
                      "LaserReceipes::saveToDirectory: cannot open {} for writing", filePath.toStdString());
                  continue;
                  }
            json jr = r->toJson();
            QTextStream out(&file);
            out << QString::fromStdString(jr.dump(4));
            file.close();
            }
      }

//---------------------------------------------------------
//   addRecipeInDir
//    Create a new recipe in the given subdirectory (relative
//    to root).  If relDir is empty, the recipe is created in
//    the root.
//---------------------------------------------------------

//---------------------------------------------------------
//   rebuildTreeModel
//    Rebuild the tree model from the in-memory recipes list.
//    Creates directory nodes for each unique relative path
//    and recipe leaf nodes under their respective directories.
//---------------------------------------------------------

void Recipe::rebuildTreeModel() {
      _treeModel->beginBuild();

      // Create a top-level node for the current machine type so
      // the user can see which machine the recipes belong to.
      QString mtName    = QString::fromUtf8(machineTypeMap.name(_machineType));
      void* machineNode = nullptr;
      if (!mtName.isEmpty())
            machineNode = _treeModel->addDirNode(mtName, QString(), nullptr);

      // Collect unique directory paths and build folder nodes
      std::map<QString, void*> dirNodes; // relPath -> node ptr
      dirNodes[""] = machineNode;        // root (may be null if no machineType)

      // First pass: create all directory nodes
      for (const auto& r : recipes) {
            QString relPath = r->relativeFilePath();
            if (relPath.isEmpty())
                  continue;
            // Create intermediate directories
            QStringList parts = relPath.split("/", Qt::SkipEmptyParts);
            QString current;
            for (int i = 0; i < parts.size(); ++i) {
                  QString parent = current;
                  current        = parent.isEmpty() ? parts[i] : (parent + "/" + parts[i]);
                  if (dirNodes.find(current) == dirNodes.end()) {
                        void* parentNode  = dirNodes[parent];
                        void* dirNode     = _treeModel->addDirNode(parts[i], current, parentNode);
                        dirNodes[current] = dirNode;
                        }
                  }
            }

      // Second pass: add recipe nodes
      for (int i = 0; i < static_cast<int>(recipes.size()); ++i) {
            const auto& r       = recipes[i];
            QString relPath     = r->relativeFilePath();
            void* parentNode    = dirNodes[relPath];
            QString displayName = r->name().isEmpty() ? QStringLiteral("unnamed") : r->name();
            _treeModel->addRecipeNode(displayName, relPath, i, parentNode);
            }

      _treeModel->endBuild();
      }

//---------------------------------------------------------
//   addRecipeInDir
//    Create a new recipe in the given subdirectory (relative
//    to root).  If relDir is empty, the recipe is created in
//    the root.  When sourceIdx is valid, the new recipe is a
//    clone of that recipe.  The machine type is always set to
//    the type intended for the current directory (directories
//    are named after their machine type).
//    Returns the index of the new recipe, or -1 on failure.
//---------------------------------------------------------

int Recipe::addRecipeInDir(const QString& name, const QString& relDir, int sourceIdx) {
      auto r = std::make_unique<LaserRecipe>();

      // Optionally clone the settings of an existing (selected) recipe.
      bool hasSource = (sourceIdx >= 0 && sourceIdx < static_cast<int>(recipes.size()));
      const LaserRecipe* src = hasSource ? recipes[sourceIdx].get() : nullptr;
      if (src) {
            r->set_description(src->description());
            r->set_numPasses(src->numPasses());
            r->set_machineType(src->machineType());
            r->passes() = src->passes();
      }

      // Base name: when cloning, clone the source recipe's name as well (a
      // non-empty source name takes precedence over the default \a name).
      QString baseName = (src && !src->name().isEmpty()) ? src->name() : name;
      // Assign a unique name within the target directory so that creating
      // several recipes does not produce name (and therefore file) clashes.
      r->set_name(uniqueRecipeNameInDir(recipes, baseName, relDir));

      // In any case initialise with the machine type intended for the
      // current directory (the directory is named after its machine type).
      r->set_machineType(_machineType);

      r->setRelativeFilePath(relDir);
      int newIdx = static_cast<int>(recipes.size());
      recipes.push_back(std::move(r));
      emit recipeModelChanged();

      // Rebuild the tree model from the in-memory recipes list.
      // We don't reload from disk because the new recipe hasn't been saved yet.
      rebuildTreeModel();
      emit recipeChanged(newIdx);
      return newIdx;
      }

//---------------------------------------------------------
//   addFolder
//    Create a new subdirectory under the given parent directory
//    (relative to root).  If parentRelDir is empty, the folder
//    is created in the root.
//---------------------------------------------------------

bool Recipe::addFolder(const QString& folderName, const QString& parentRelDir) {
      if (_rootDir.isEmpty() || folderName.isEmpty())
            return false;

      QString sanitised = sanitiseFileName(folderName);

      // Create the folder under _rootDir/machineTypeName/...
      QString mtName  = QString::fromUtf8(machineTypeMap.name(_machineType));
      QString baseDir = _rootDir;
      if (!mtName.isEmpty())
            baseDir = QDir(_rootDir).filePath(mtName);

      QString dirPath = parentRelDir.isEmpty()
                            ? QDir(baseDir).filePath(sanitised)
                            : QDir(QDir(baseDir).filePath(parentRelDir)).filePath(sanitised);

      QDir d(dirPath);
      if (d.exists())
            return true; // already exists, no error
      if (!d.mkpath("."))
            return false;

      // Reload from disk to show the new (possibly empty) folder in the tree
      reload();
      return true;
      }

//---------------------------------------------------------
//   properties
//---------------------------------------------------------

const std::string LaserRecipe::laserPassProperties() const {
      if (machineType() == MachineType::UV_LASER)
            return _uvLaserPassProperties;
      return _laserPassProperties;
      }

//---------------------------------------------------------
//   removeFolder
//    Remove a folder and all recipes inside it.
//---------------------------------------------------------

bool Recipe::removeFolder(const QString& relDir) {
      if (_rootDir.isEmpty() || relDir.isEmpty())
            return false;

      // The folder lives under _rootDir/machineTypeName/...
      QString mtName  = QString::fromUtf8(machineTypeMap.name(_machineType));
      QString baseDir = _rootDir;
      if (!mtName.isEmpty())
            baseDir = QDir(_rootDir).filePath(mtName);

      QString dirPath = QDir(baseDir).filePath(relDir);
      QDir d(dirPath);
      if (!d.exists())
            return false;

      // Remove all recipes that have this relativePath or a subdirectory of it
      auto it = recipes.begin();
      while (it != recipes.end()) {
            QString rp = (*it)->relativeFilePath();
            if (rp == relDir || rp.startsWith(relDir + "/"))
                  it = recipes.erase(it);
            else
                  ++it;
            }

      // Remove the directory on disk
      d.removeRecursively();

      emit recipeModelChanged();
      rebuildTreeModel();
      return true;
      }