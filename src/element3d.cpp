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

#include "element3d.h"
#include "zcam.h"
#include "clipper2/clipper.h"
#include "propertyjson.h"
#include "layer.h"
#include "recipe.h"
#include <limits>
#include <QMetaProperty>
#include <QColor>
#include <QVector3D>

//---------------------------------------------------------
//   Element3d
//---------------------------------------------------------

Element3d::Element3d(ZCam* zcam, Element* parent) : Element(zcam, parent) {
      _selectionGeometry = new TessGeometry(this);
      QJSEngine::setObjectOwnership(_selectionGeometry, QJSEngine::CppOwnership);

      // Propagate ancestor visibility changes downward: when a parent's
      // show flag or its ancestorsShow changes, our own ancestorsShow
      // has also changed, so re-emit the signal for our descendants.
      if (auto* p = qobject_cast<Element3d*>(parent)) {
            connect(p, &Element3d::showChanged, this, &Element3d::ancestorsShowChanged);
            connect(p, &Element3d::ancestorsShowChanged, this, &Element3d::ancestorsShowChanged);
            }
      }

//---------------------------------------------------------
//   writeLayerOrRecipe
//    Handle Element3d-specific "layer" and "recipe" property types
//    that require access to the ZCam instance for name resolution.
//    Returns true if the property was handled.
//---------------------------------------------------------

static bool writeLayerOrRecipe(nlohmann::json& data, const Element3d* element,
                               const std::string& name, const std::string& type) {
      const QMetaObject* meta = element->metaObject();
      QByteArray propName     = QByteArray::fromStdString(name);
      int idx                 = meta->indexOfProperty(propName.constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      QVariant value   = mp.read(element);

      if (type == "layer") {
            Layer* layer = value.value<Layer*>();
            data[name]   = layer ? layer->name().toStdString() : "";
            return true;
            }
      else if (type == "recipe") {
            Recipe* recipe = value.value<Recipe*>();
            data[name]     = recipe ? recipe->name().toStdString() : "";
            return true;
            }
      return false;
      }

//---------------------------------------------------------
//   readLayerOrRecipe
//    Handle Element3d-specific "layer" and "recipe" property types
//    that require access to the ZCam instance for pointer resolution.
//    Returns true if the property was handled.
//---------------------------------------------------------

static bool readLayerOrRecipe(const nlohmann::json& data, Element3d* element,
                              const std::string& name, const std::string& type) {
      if (!data.contains(name))
            return false;
      const nlohmann::json& jval = data.at(name);
      const QMetaObject* meta    = element->metaObject();
      QByteArray propName        = QByteArray::fromStdString(name);
      int idx                    = meta->indexOfProperty(propName.constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);

      if (type == "layer") {
            QString layerName = QString::fromStdString(jval.get<std::string>());
            Layer* layer      = element->zcamInstance()->layerPtr(layerName);
            mp.write(element, QVariant::fromValue(layer));
            return true;
            }
      else if (type == "recipe") {
            QString recipeName = QString::fromStdString(jval.get<std::string>());
            Recipe* recipe     = element->zcamInstance()->recipePtr(recipeName);
            mp.write(element, QVariant::fromValue(recipe));
            return true;
            }
      return false;
      }

//---------------------------------------------------------
//   toJson
//    Serialize all user-editable properties declared in properties().
//    Uses the shared propjson utilities for common types, with
//    Element3d-specific handling for "layer" and "recipe" types.
//---------------------------------------------------------

json Element3d::toJson() const {
      nlohmann::json data = Element::toJson();

      std::string_view propStr = properties();
      if (propStr.empty())
            return data;

      propjson::PropNameList propNames;
      try {
            propNames = propjson::parseAllPropertyNames(propStr);
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("Element3d::toJson: JSON parse error: {}", err.what());
            return data;
            }

      const QMetaObject* meta = this->metaObject();
      for (const auto& [name, type] : propNames) {
            if (type == "layer" || type == "recipe")
                  writeLayerOrRecipe(data, this, name, type);
            else
                  propjson::writePropertyToJson(data, this, meta, false, name, type);
            }

      return data;
      }

//---------------------------------------------------------
//   fromJson
//    Deserialize all properties from JSON using the shared
//    propjson utilities, with Element3d-specific handling
//    for "layer" and "recipe" types.
//---------------------------------------------------------

void Element3d::fromJson(const json& json) {
      try {
            Element::fromJson(json);

            std::string_view propStr = properties();
            if (propStr.empty())
                  return;

            auto propNames = propjson::parseAllPropertyNames(propStr);
            const QMetaObject* meta = this->metaObject();

            for (const auto& [name, type] : propNames) {
                  if (type == "layer" || type == "recipe")
                        readLayerOrRecipe(json, this, name, type);
                  else
                        propjson::readPropertyFromJson(json, this, meta, false, name, type);
                  }
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("Element3d::fromJson: JSON parse error: {}", err.what());
            }
      catch (...) {
            Warning("====json bug");
            }
      }

//---------------------------------------------------------
//   ancestorsShow
//    Returns true when every ancestor Element3d has show == true.
//    The element's own show flag is NOT considered here.
//---------------------------------------------------------

bool Element3d::ancestorsShow() const {
      Element* p = parent();
      while (p) {
            if (auto* e3d = qobject_cast<Element3d*>(p)) {
                  if (!e3d->show())
                        return false;
                  }
            p = p->parent();
            }
      return true;
      }

//---------------------------------------------------------
//   boundingBox
//    compute the axis-aligned bounding box of all path vertices
//---------------------------------------------------------

QRectF Element3d::boundingBox() const {
      if (_pathList.empty())
            return {};
      double minX = std::numeric_limits<double>::max();
      double minY = std::numeric_limits<double>::max();
      double maxX = std::numeric_limits<double>::lowest();
      double maxY = std::numeric_limits<double>::lowest();
      for (const auto& path : _pathList) {
            for (const auto& pt : path) {
                  minX = std::min(minX, pt.x());
                  minY = std::min(minY, pt.y());
                  maxX = std::max(maxX, pt.x());
                  maxY = std::max(maxY, pt.y());
                  }
            }
      if (minX > maxX || minY > maxY)
            return {};
      return QRectF(minX, minY, maxX - minX, maxY - minY);
      }

//---------------------------------------------------------
//   updateSelectionGeometry
//    build a line rectangle for the bounding box so the QML
//    layer can render it when this element is selected
//---------------------------------------------------------

void Element3d::updateSelectionGeometry() {
      if (!_selectionGeometry)
            return;
      QRectF bbox = boundingBox();
      if (bbox.isNull() || bbox.isEmpty()) {
            Clipper2Lib::PathsD empty;
            _selectionGeometry->setLines(empty);
            return;
            }
      // Lines primitive draws pairs of vertices as independent segments.
      // Emit the four edges of the bounding-box rectangle.
      Clipper2Lib::PathD rect;
      rect.push_back({bbox.left(), bbox.top()});     // P0
      rect.push_back({bbox.right(), bbox.top()});    // P1  (top edge)
      rect.push_back({bbox.right(), bbox.top()});    // P1
      rect.push_back({bbox.right(), bbox.bottom()}); // P2  (right edge)
      rect.push_back({bbox.right(), bbox.bottom()}); // P2
      rect.push_back({bbox.left(), bbox.bottom()});  // P3  (bottom edge)
      rect.push_back({bbox.left(), bbox.bottom()});  // P3
      rect.push_back({bbox.left(), bbox.top()});     // P0  (left edge)
      Clipper2Lib::PathsD lines;
      lines.push_back(rect);
      _selectionGeometry->setLines(lines);
      }

//---------------------------------------------------------
//   curColor
//---------------------------------------------------------

QColor Element3d::curColor() const {
      if (zcam->hoverElement() == this)
            return _color.darker();
      if (zcam->currentElement() == this)
            return _color.lighter();
      return _color;
      }

//---------------------------------------------------------
//   setColor
//---------------------------------------------------------

void Element3d::setColor(const QColor& c) {
      if (_color != c) {
            _color = c;
            emit colorChanged();
            emit curColorChanged();
            }
      }