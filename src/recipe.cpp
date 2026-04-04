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

//---------------------------------------------------------
//   LaserLayerSetting
//---------------------------------------------------------

json LaserLayerSetting::toJson() const {
      json data;
      data["name"]           = _name.toStdString();
      data["enabled"]        = _enabled;
      data["power"]          = _power;
      data["speed"]          = _speed;
      data["travelSpeed"]    = _travelSpeed;
      data["frequency"]      = _frequency;
      data["pulseWidth"]     = _pulseWidth;
      data["numPasses"]      = _numPasses;
      data["interval"]       = _interval;
      data["startAngle"]     = _startAngle;
      data["angleIncrement"] = _angleIncrement;
      data["zigzag"]         = _zigzag;
      data["interleave"]     = _interleave;
      data["wobble"]         = _wobble;
      data["wobbleStep"]     = _wobbleStep;
      data["wobbleSize"]     = _wobbleSize;
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void LaserLayerSetting::fromJson(const json& data) {
      if (data.contains("name"))
            _name = QString::fromStdString(data["name"]);
      if (data.contains("enabled"))
            _enabled = data["enabled"];
      if (data.contains("power"))
            _power = data["power"];
      if (data.contains("speed"))
            _speed = data["speed"];
      if (data.contains("travelSpeed"))
            _travelSpeed = data["travelSpeed"];
      if (data.contains("frequency"))
            _frequency = data["frequency"];
      if (data.contains("pulseWidth"))
            _pulseWidth = data["pulseWidth"];
      if (data.contains("numPasses"))
            _numPasses = data["numPasses"];
      if (data.contains("interval"))
            _interval = data["interval"];
      if (data.contains("startAngle"))
            _startAngle = data["startAngle"];
      if (data.contains("angleIncrement"))
            _angleIncrement = data["angleIncrement"];
      if (data.contains("zigzag"))
            _zigzag = data["zigzag"];
      if (data.contains("interleave"))
            _interleave = data["interleave"];
      if (data.contains("wobble"))
            _wobble = data["wobble"];
      if (data.contains("wobbleStep"))
            _wobbleStep = data["wobbleStep"];
      if (data.contains("wobbleSize"))
            _wobbleSize = data["wobbleSize"];
      }

//---------------------------------------------------------
//   LaserLayersSetting
//---------------------------------------------------------

json LaserLayersSetting::toJson() const {
      json data = json::array();
      for (const auto& layer : *this)
            data.push_back(layer.toJson());
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void LaserLayersSetting::fromJson(const json& data) {
      clear();
      if (data.is_array()) {
            for (const auto& jlayer : data) {
                  LaserLayerSetting l;
                  l.fromJson(jlayer);
                  push_back(l);
                  }
            }
      }

//---------------------------------------------------------
//   Recipe
//---------------------------------------------------------

json Recipe::toJson() const {
      json data;
      data["name"]        = _name.toStdString();
      data["description"] = _description.toStdString();
      data["numPasses"]   = _numPasses;
      data["layer"]       = layer.toJson();
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void Recipe::fromJson(const json& data) {
      if (data.contains("name"))
            _name = QString::fromStdString(data["name"]);
      if (data.contains("description"))
            _description = QString::fromStdString(data["description"]);
      if (data.contains("numPasses"))
            _numPasses = data["numPasses"];
      if (data.contains("layer"))
            layer.fromJson(data["layer"]);
      }

//---------------------------------------------------------
//   Recipes
//---------------------------------------------------------

void Recipes::updateRecipe(int idx, const Recipe& r) {
      if (idx >= 0 && idx < recipes.size()) {
            recipes[idx] = r;
            emit recipeModelChanged();
            emit recipeChanged(idx);
      }
}

void Recipes::addRecipe(const QString& name) {
      Recipe r;
      r.set_name(name);
      recipes.push_back(r);
      emit recipeModelChanged();
}

void Recipes::removeRecipe(int idx) {
      if (idx >= 0 && idx < recipes.size()) {
            recipes.erase(recipes.begin() + idx);
            emit recipeModelChanged();
      }
}

LaserLayerSetting Recipes::layer(int recipeIdx, int layerIdx) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            const auto& r = recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.layer.size()) {
                  return r.layer[layerIdx];
            }
      }
      return LaserLayerSetting();
}

void Recipes::updateLayer(int recipeIdx, int layerIdx, const LaserLayerSetting& l) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            auto& r = recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.layer.size()) {
                  r.layer[layerIdx] = l;
                  emit recipeChanged(recipeIdx);
            }
      }
}

void Recipes::addLayer(int recipeIdx, const QString& name) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            LaserLayerSetting l;
            l.set_name(name);
            recipes[recipeIdx].layer.push_back(l);
            emit recipeChanged(recipeIdx);
      }
}

void Recipes::removeLayer(int recipeIdx, int layerIdx) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            auto& r = recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.layer.size()) {
                  r.layer.erase(r.layer.begin() + layerIdx);
                  emit recipeChanged(recipeIdx);
            }
      }
}

QStringList Recipes::layerModel(int recipeIdx) const {
      QStringList names;
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            for (const auto& l : recipes[recipeIdx].layer)
                  names.append(l.name());
      }
      return names;
}

QStringList Recipes::recipeModel() const {
      QStringList names;
      for (const auto& r : recipes)
            names.append(r.name());
      return names;
      }

//---------------------------------------------------------
//   toJson
//---------------------------------------------------------

json Recipes::toJson() const {
      json data = json::array();
      for (const auto& r : recipes)
            data.push_back(r.toJson());
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void Recipes::fromJson(const json& data) {
      recipes.clear();
      if (data.is_array()) {
            for (const auto& r : data) {
                  Recipe recipe;
                  recipe.fromJson(r);
                  recipes.push_back(recipe);
                  }
            }
      emit recipeModelChanged();
      }
