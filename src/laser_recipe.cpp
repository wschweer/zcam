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
#include <QRegularExpression>
#include <QTextStream>
#include "logger.h"
//---------------------------------------------------------
//   LaserLayerSetting
//---------------------------------------------------------

json LaserPass::toJson() const {
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
            _travelSpeed = data.at("travelSpeed");
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
      if (data.contains("layer"))
            _passes.fromJson(data.at("layer"));
}
//---------------------------------------------------------
//   Recipes
//---------------------------------------------------------

void LaserReceipes::updateRecipe(int idx, const LaserRecipe& r) {
      if (idx >= 0 && idx < recipes.size()) {
            recipes[idx] = r;
            emit recipeModelChanged();
            emit recipeChanged(idx);
      }
}
void LaserReceipes::addRecipe(const QString& name) {
      LaserRecipe r;
      r.set_name(name);
      recipes.push_back(r);
      emit recipeModelChanged();
}
void LaserReceipes::removeRecipe(int idx) {
      if (idx >= 0 && idx < recipes.size()) {
            recipes.erase(recipes.begin() + idx);
            emit recipeModelChanged();
      }
}
LaserPass LaserReceipes::layer(int recipeIdx, int layerIdx) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            const auto& r = recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.layers()->size())
                  return *r.layer(layerIdx);
      }
      return LaserPass();
}
LaserPass* LaserReceipes::layerPtr(int recipeIdx, int layerIdx) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            auto& r = recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.passes()->size())
                  return r.pass(layerIdx);
      }
      return nullptr;
}
void LaserReceipes::updateLayer(int recipeIdx, int layerIdx, const LaserPass& l) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            auto& r = recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.passes()->size()) {
                  *r.pass(layerIdx) = l;
                  emit recipeChanged(recipeIdx);
            }
      }
}
void LaserReceipes::addLayer(int recipeIdx, const QString& name) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            LaserPass l;
            l.set_name(name);
            recipes[recipeIdx].passes()->push_back(l);
            emit recipeChanged(recipeIdx);
      }
}
void LaserReceipes::removeLayer(int recipeIdx, int layerIdx) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            auto& r = recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.passes()->size()) {
                  r.passes()->erase(r.passes()->begin() + layerIdx);
                  emit recipeChanged(recipeIdx);
            }
      }
}
QStringList LaserReceipes::layerModel(int recipeIdx) const {
      QStringList names;
      if (recipeIdx >= 0 && recipeIdx < recipes.size())
            for (const auto& l : *recipes[recipeIdx].layers())
                  names.append(l.name());
      return names;
}
QStringList LaserReceipes::recipeModel() const {
      QStringList names;
      for (const auto& r : recipes)
            names.append(r.name());
      return names;
}
//---------------------------------------------------------
//   toJson
//---------------------------------------------------------

json LaserReceipes::toJson() const {
      json data = json::array();
      for (const auto& r : recipes)
            data.push_back(r.toJson());
      return data;
}
//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void LaserReceipes::fromJson(const json& data) {
      recipes.clear();
      if (data.is_array()) {
            for (const auto& r : data) {
                  LaserRecipe recipe;
                  recipe.fromJson(r);
                  recipes.push_back(recipe);
            }
      }
      emit recipeModelChanged();
}
//---------------------------------------------------------
//   loadFromDirectory
//    Load all .json recipe files from the given directory.
//    Each file contains a single recipe JSON object.
//---------------------------------------------------------

void LaserReceipes::loadFromDirectory(const QString& dir) {
      recipes.clear();

      QDir d(dir);
      if (!d.exists()) {
            emit recipeModelChanged();
            return;
      }

      const auto files = d.entryList({"*.json"}, QDir::Files, QDir::Name);
      for (const QString& fileName : files) {
            QString filePath = d.filePath(fileName);
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                  Warning("LaserReceipes::loadFromDirectory: cannot open {}", filePath.toStdString());
                  continue;
            }
            QByteArray data = file.readAll();
            file.close();
            try {
                  json jr = json::parse(data.toStdString());
                  LaserRecipe recipe;
                  recipe.fromJson(jr);
                  recipes.push_back(recipe);
            }
            catch (const json::parse_error& e) {
                  Warning("LaserReceipes::loadFromDirectory: parse error in {}: {}", filePath.toStdString(),
                          e.what());
            }
      }
      emit recipeModelChanged();
}
//---------------------------------------------------------
//   saveToDirectory
//    Save each recipe as an individual .json file in dir.
//    The file name is derived from the recipe name (sanitised).
//---------------------------------------------------------

void LaserReceipes::saveToDirectory(const QString& dir) const {
      QDir d(dir);
      if (!d.exists())
            d.mkpath(".");

      for (const auto& r : recipes) {
            QString name = r.name();
            if (name.isEmpty())
                  name = QStringLiteral("unnamed");
            // Sanitise: replace characters that are problematic in file names
            name.replace(QRegularExpression("[^a-zA-Z0-9_\\-]"), "_");
            QString filePath = d.filePath(name + ".json");
            QFile file(filePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                  Warning("LaserReceipes::saveToDirectory: cannot open {} for writing",
                          filePath.toStdString());
                  continue;
            }
            json jr = r.toJson();
            QTextStream out(&file);
            out << QString::fromStdString(jr.dump(4));
            file.close();
      }
}
