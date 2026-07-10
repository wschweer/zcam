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
#include <QtQml/qqmlregistration.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include "macros.h"

//---------------------------------------------------------
//   LaserLayerSetting
//---------------------------------------------------------

class LaserLayerSetting
      {
      Q_GADGET
      QML_VALUE_TYPE(laserLayerSetting)

      PROP_GADGET(QString, name)
      PROPV_GADGET(bool, enabled, false)
      PROPV_GADGET(double, power, 20.0)
      PROPV_GADGET(double, speed, 1000.0)
      PROPV_GADGET(double, travelSpeed, 4000.0)
      PROPV_GADGET(double, frequency, 40.0)
      PROPV_GADGET(int, pulseWidth, 200)
      PROPV_GADGET(int, numPasses, 200)
      PROPV_GADGET(double, interval, 0.02)
      PROPV_GADGET(double, startAngle, 0)
      PROPV_GADGET(double, angleIncrement, 90.0)
      PROPV_GADGET(bool, zigzag, true)
      PROPV_GADGET(int, interleave, 1)
      PROPV_GADGET(bool, wobble, false)
      PROPV_GADGET(double, wobbleStep, 0.05)
      PROPV_GADGET(double, wobbleSize, 0.1)

      inline static constexpr std::string_view _properties {R"({
            "class": "Layer Setting",
            "items": [
                  { "name": "name", "label": "Name", "type": "singleline" },
                  { "row":
                        {
                              "enabled": { "label": "Enabled", "type": "bool", "default": false }
                        },
                        "label": " "
                     },
                  { "name": "line", "type": "line" },
                  { "columns": {
                        "count": 2,
                        "items": [
                              { "name": "power",       "label": "Power",       "type": "float", "unit": "%",  "min": 0.0,    "max": 100.0,    "default": 20.0 },
                              { "name": "speed",       "label": "Speed",       "type": "float", "unit": "mm/s","min": 0.0,    "max": 100000.0,  "default": 1000.0 },
                              { "name": "travelSpeed", "label": "Travel Speed","type": "float", "unit": "mm/s","min": 0.0,    "max": 100000.0,  "default": 4000.0 },
                              { "name": "frequency",   "label": "Frequency",  "type": "float", "unit": "kHz", "min": 0.0,    "max": 100000.0,  "default": 40.0 },
                              { "row":
                                    {
                                          "pulseWidth": { "label": "Pulse", "type": "int", "unit": "ns", "min": 0, "max": 1000, "default": 200 },
                                          "numPasses":  { "label": "Passes","type": "int", "min": 1, "max": 10000, "default": 200 }
                                    },
                                    "label": " "
                              },
                              { "name": "interval",       "label": "Interval",   "type": "float", "unit": "mm",  "min": 0.0,    "max": 100.0,   "default": 0.02 },
                              { "row":
                                    {
                                          "startAngle":     { "label": "Start", "type": "float", "unit": "°", "min": -360.0, "max": 360.0, "default": 0.0 },
                                          "angleIncrement": { "label": "Incr",  "type": "float", "unit": "°", "min": -360.0, "max": 360.0, "default": 90.0 }
                                    },
                                    "label": "Angle"
                              },
                              { "name": "line", "type": "line", "colSpan": 2 },
                              { "row":
                                    {
                                          "zigzag":     { "label": "Zigzag",    "type": "bool", "default": true },
                                          "interleave": { "label": "Interleave","type": "int",  "min": 1, "max": 100, "default": 1 }
                                    },
                                    "label": " "
                              },
                              { "row":
                                    {
                                          "wobble":     { "label": "Wobble", "type": "bool", "default": false },
                                          "wobbleStep": { "label": "Step",   "type": "float", "unit": "mm", "min": 0.0, "max": 10.0, "default": 0.05 },
                                          "wobbleSize": { "label": "Size",   "type": "float", "unit": "mm", "min": 0.0, "max": 10.0, "default": 0.1 }
                                    },
                                    "label": "Wobble"
                              }
                        ]
                        }
                  }
                  ]
                        })"};

    public:
      LaserLayerSetting() {}
      json toJson() const;
      void fromJson(const json&);
      const std::string_view properties() const { return _properties; }
      };

//---------------------------------------------------------
//   LaserLayersSettings
//---------------------------------------------------------

class LaserLayersSettings : public std::vector<LaserLayerSetting>
      {
      Q_GADGET

    public:
      json toJson() const;
      void fromJson(const json&);
      };

//---------------------------------------------------------
//   Recipe
//---------------------------------------------------------

class Recipe
      {
      Q_GADGET
      QML_VALUE_TYPE(recipe)

      PROP_GADGET(QString, name)
      PROP_GADGET(QString, description)
      PROPV_GADGET(int, numPasses, 1)

    public:
      LaserLayersSettings layer;
      Recipe() {}
      json toJson() const;
      void fromJson(const json&);
      };

//---------------------------------------------------------
//   Recipes
//---------------------------------------------------------

class Recipes : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      Q_PROPERTY(QStringList recipeModel READ recipeModel NOTIFY recipeModelChanged)

      std::vector<Recipe> recipes;

    public:
      Recipes(QObject* parent = nullptr) : QObject(parent) {}
      Q_INVOKABLE Recipe recipe(int idx) { return recipes[idx]; }
      int recipeCount() const { return static_cast<int>(recipes.size()); }
      Recipe* recipePtr(int idx) {
            if (idx >= 0 && idx < static_cast<int>(recipes.size()))
                  return &recipes[idx];
            return nullptr;
            }
      Q_INVOKABLE void updateRecipe(int idx, const Recipe& r);
      Q_INVOKABLE void addRecipe(const QString& name);
      Q_INVOKABLE void removeRecipe(int idx);

      Q_INVOKABLE LaserLayerSetting layer(int recipeIdx, int layerIdx);
      Q_INVOKABLE LaserLayerSetting* layerPtr(int recipeIdx, int layerIdx);
      Q_INVOKABLE void updateLayer(int recipeIdx, int layerIdx, const LaserLayerSetting& l);
      Q_INVOKABLE void addLayer(int recipeIdx, const QString& name);
      Q_INVOKABLE void removeLayer(int recipeIdx, int layerIdx);
      Q_INVOKABLE QStringList layerModel(int recipeIdx) const;

      QStringList recipeModel() const;

    signals:
      void recipeModelChanged();
      void recipeChanged(int idx);

    public:
      json toJson() const;
      void fromJson(const json&);
      };

Q_DECLARE_METATYPE(LaserLayerSetting)
Q_DECLARE_METATYPE(Recipe)
