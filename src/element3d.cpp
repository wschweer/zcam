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
#include "machine.h"
#include "machines.h"
#include "geometryworker.h"
#include <limits>
#include <cmath>
#include <QMetaProperty>
#include <QColor>
#include <QVector3D>
#include <QQuaternion>
#include <QPointer>

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

      // Invalidate the cached matrix whenever position, rotation or scale changes.
      // During batch updates, suppress vertexRevisionChanged to avoid
      // redundant signal cascades; endBatchUpdate() will emit it once.
      connect(this, &Element3d::posChanged, this, [this] {
            _matrixDirty = true;
            if (!_batching) {
                  ++_vertexRevision;
                  emit vertexRevisionChanged();
                  }
            });
      connect(this, &Element3d::rotChanged, this, [this] {
            _matrixDirty = true;
            if (!_batching) {
                  ++_vertexRevision;
                  emit vertexRevisionChanged();
                  }
            });
      connect(this, &Element3d::scaleChanged, this, [this] {
            _matrixDirty = true;
            if (!_batching) {
                  ++_vertexRevision;
                  emit vertexRevisionChanged();
                  }
            });
      connect(this, &Element3d::mirrorXChanged, this, [this] {
            _matrixDirty = true;
            if (!_batching) {
                  ++_vertexRevision;
                  emit vertexRevisionChanged();
                  }
            });
      connect(this, &Element3d::mirrorYChanged, this, [this] {
            _matrixDirty = true;
            if (!_batching) {
                  ++_vertexRevision;
                  emit vertexRevisionChanged();
                  }
            });
      connect(this, &Element3d::fillChanged, [this] { update(); });
      connect(this, &Element3d::lineWidthChanged, [this] { update(); });
      connect(this, &Element3d::endTypeChanged, [this] { update(); });
      connect(this, &Element3d::joinTypeChanged, [this] { update(); });
      }

//---------------------------------------------------------
//   writeLayerOrRecipe
//    Handle Element3d-specific "layer" and "recipe" property types
//    that require access to the ZCam instance for name resolution.
//    Returns true if the property was handled.
//---------------------------------------------------------

static bool writeLayerOrRecipe(nlohmann::json& data, const Element3d* element, const std::string& name,
                               const std::string& type) {
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
      else if (type == "machine") {
            // machine is stored as a Machine*, serialized as its name string.
            // A null pointer is serialized as JSON null.
            Machine* machine = value.value<Machine*>();
            if (machine)
                  data[name] = machine->name().toStdString();
            else
                  data[name] = nullptr;
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

static bool readLayerOrRecipe(const nlohmann::json& data, Element3d* element, const std::string& name,
                              const std::string& type) {
      if (!data.contains(name))
            return false;
      const nlohmann::json& jval = data.at(name);
      const QMetaObject* meta    = element->metaObject();
      QByteArray propName        = QByteArray::fromStdString(name);
      int idx                    = meta->indexOfProperty(propName.constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);

      // A null JSON value means the property was not set (e.g. no
      // machine assigned).  Skip gracefully instead of throwing a
      // nlohmann::json::type_error which would abort fromJson().
      if (jval.is_null())
            return true;

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
      else if (type == "machine") {
            // machine is stored as a Machine* pointer; resolve from name
            QString machineName = QString::fromStdString(jval.get<std::string>());
            Machine* machine    = nullptr;
            ZCam* zc            = element->zcamInstance();
            if (zc && zc->machines() && !machineName.isEmpty()) {
                  QStringList model = zc->machines()->machinesModel();
                  int idx           = model.indexOf(machineName);
                  if (idx >= 0)
                        machine = zc->machines()->machine(idx);
                  }
            mp.write(element, QVariant::fromValue(machine));
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
      for (const auto& [name, type] : propNames)
            if (type == "layer" || type == "recipe" || type == "machine")
                  writeLayerOrRecipe(data, this, name, type);
            else
                  propjson::writePropertyToJson(data, this, meta, false, name, type);

      return data;
      }

//---------------------------------------------------------
//   fromJson
//    Deserialize all properties from JSON using the shared
//    propjson utilities, with Element3d-specific handling
//    for "layer" and "recipe" types.
//---------------------------------------------------------

void Element3d::fromJson(const json& json) {
      // Process children in their own try-catch block so that an
      // exception in a child does NOT prevent this element's own
      // properties (e.g. show, burn) from being read.  Previously,
      // a single try-catch wrapped both steps; if a child threw,
      // the parent's show property was never restored from JSON,
      // leaving it at the constructor default (e.g. false for Cam).
      try {
            Element::fromJson(json);
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("Element3d::fromJson: JSON parse error in children: {}", err.what());
            }
      catch (...) {
            Warning("====json bug in children");
            }

      try {
            std::string_view propStr = properties();
            if (propStr.empty())
                  return;

            auto propNames          = propjson::parseAllPropertyNames(propStr);
            const QMetaObject* meta = this->metaObject();

            // Two-pass loading: lock constraints (lockScale, lockSize) must
            // be restored BEFORE the constrained values (scale, size) so that
            // the custom setters (set_scale, set_size) use the correct lock
            // mode instead of the constructor default (Square).  Without this,
            // a non-square rectangle saved with lockSize=Off would be loaded
            // with lockSize still at its default Square, causing set_size()
            // to force width==height, turning the rectangle into a square.
            for (const auto& [name, type] : propNames)
                  if (type == "lockScale" || type == "lockSize")
                        propjson::readPropertyFromJson(json, this, meta, false, name, type);
            for (const auto& [name, type] : propNames) {
                  if (type == "lockScale" || type == "lockSize")
                        continue; // already loaded in first pass
                  if (type == "layer" || type == "recipe" || type == "machine")
                        readLayerOrRecipe(json, this, name, type);
                  else
                        propjson::readPropertyFromJson(json, this, meta, false, name, type);
                  }
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("Element3d::fromJson: JSON parse error in properties: {}", err.what());
            }
      catch (...) {
            Warning("====json bug in properties");
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
//   adjustColorTone
//    Shift a colour towards white (tone > 0, lighten) or
//    towards black (tone < 0, darken) by blending a fixed
//    fraction of the target tone into the original colour.
//
//    Unlike QColor::lighter()/darker() which multiply or
//    divide RGB values, this linear blend always produces a
//    visible change – even for extreme colours like pure
//    black or pure white where the Qt routines are no-ops.
//
//    A |tone| of 0.2 shifts the colour 20 % of the way
//    toward white or black, which is clearly perceptible
//    yet preserves the hue identity.
//---------------------------------------------------------
static QColor adjustColorTone(const QColor& c, double tone) {
      constexpr double blend = 0.2;   // 20 % shift
      int r = c.red();
      int g = c.green();
      int b = c.blue();
      if (tone > 0.0) {
            // Blend toward white
            r = static_cast<int>(std::round(r + (255 - r) * blend * tone));
            g = static_cast<int>(std::round(g + (255 - g) * blend * tone));
            b = static_cast<int>(std::round(b + (255 - b) * blend * tone));
            }
      else {
            // Blend toward black
            double f = blend * (-tone);
            r = static_cast<int>(std::round(r * (1.0 - f)));
            g = static_cast<int>(std::round(g * (1.0 - f)));
            b = static_cast<int>(std::round(b * (1.0 - f)));
            }
      return QColor(
            std::clamp(r, 0, 255),
            std::clamp(g, 0, 255),
            std::clamp(b, 0, 255),
            c.alpha()
            );
      }

//---------------------------------------------------------
//   curColor
//    Returns the element's colour, adjusted for hover or
//    current-selection state.  Light colours are darkened,
//    dark colours are lightened so the element stands out.
//---------------------------------------------------------

QColor Element3d::curColor() const {
      if (zcam->hoverElement() == this) {
            // Light colours → darken, dark colours → lighten
            double lum = 0.299 * _color.redF()
                       + 0.587 * _color.greenF()
                       + 0.114 * _color.blueF();
            return adjustColorTone(_color, lum >= 0.5 ? -1.0 : 1.0);
            }
      if (zcam->currentElement() == this) {
            double lum = 0.299 * _color.redF()
                       + 0.587 * _color.greenF()
                       + 0.114 * _color.blueF();
            return adjustColorTone(_color, lum >= 0.5 ? -1.0 : 1.0);
            }
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

//---------------------------------------------------------
//   set_scaleAR
//    Custom setter for the scale property that enforces the
//    lockScale mode:
//      Off    – accept the value as-is
//      Lock   – preserve the current aspect ratio; the axis
//               that changed the most drives the others
//      Square – force x == y == z using the most-changed axis
//---------------------------------------------------------

void Element3d::set_scaleAR(QVector3D v) {
      if (v == _scale)
            return;
      auto mode = static_cast<LockScaleMode>(lockScale());
      if (mode == LockScaleMode::Square) {
            // Determine which axis changed the most and use its
            // new value for all three axes.
            double dx = std::abs(v.x() - _scale.x());
            double dy = std::abs(v.y() - _scale.y());
            double dz = std::abs(v.z() - _scale.z());
            double s;
            if (dx >= dy && dx >= dz)
                  s = v.x();
            else if (dy >= dz)
                  s = v.y();
            else
                  s = v.z();
            v = QVector3D(s, s, s);
            }
      else if (mode == LockScaleMode::Lock) {
            // Preserve the current aspect ratio: the axis that
            // changed the most determines a uniform scale factor
            // that is applied to all three axes.
            double dx = std::abs(v.x() - _scale.x());
            double dy = std::abs(v.y() - _scale.y());
            double dz = std::abs(v.z() - _scale.z());
            double factor;
            if (dx >= dy && dx >= dz) {
                  if ((qAbs(_scale.x()) < 1e-12))
                        return;
                  factor = v.x() / _scale.x();
                  }
            else if (dy >= dz) {
                  if ((qAbs(_scale.y()) < 1e-12))
                        return;
                  factor = v.y() / _scale.y();
                  }
            else {
                  if ((qAbs(_scale.z()) < 1e-12))
                        return;
                  factor = v.z() / _scale.z();
                  }
            v = QVector3D(_scale.x() * factor, _scale.y() * factor, _scale.z() * factor);
            }
      if (v == scale())
            return;
      set_scale(v);
      if (!_batching) {
            ++_vertexRevision;
            emit vertexRevisionChanged();
            }
      }

//---------------------------------------------------------
//   matrix
//    Returns the cached local transformation matrix.
//    The matrix is recomputed lazily from pos, rot and scale
//    whenever the matrixDirty flag is set (i.e. after any
//    change to position, rotation or scale).
//---------------------------------------------------------

const QMatrix4x4& Element3d::matrix() const {
      if (_matrixDirty) {
            _matrix.setToIdentity();
            _matrix.translate(_pos);
            _matrix.rotate(QQuaternion::fromEulerAngles(_rot));
            // Apply mirror as negative scale to match the QML
            // visualisation in ProjectTree.qml which multiplies
            // scale by (mirrorX ? -1 : 1) and (mirrorY ? -1 : 1).
            // Without this, globalMatrix() would not reflect the
            // actual on-screen transform, causing incorrect world-
            // to-local conversions (e.g. drag direction reversal).
            QVector3D s(_scale);
            if (_mirrorX)
                  s.setX(-s.x());
            if (_mirrorY)
                  s.setY(-s.y());
            _matrix.scale(s);
            _matrixDirty = false;
            }
      return _matrix;
      }

//---------------------------------------------------------
//   globalMatrix
//    Returns the full transformation matrix from this element's
//    local coordinate system to the root coordinate system.
//    The parent matrices are multiplied left-to-right so that
//    a point in local space is transformed by:
//       v_root = globalMatrix * v_local
//    This implements the standard scene-graph convention where
//    the outermost (parent) transform is applied last:
//       v_root = rootMatrix * ... * parentMatrix * localMatrix * v_local
//---------------------------------------------------------

QMatrix4x4 Element3d::globalMatrix() const {
      QMatrix4x4 result = matrix();
      Element* p        = parent();
      while (p) {
            if (auto* e3d = qobject_cast<Element3d*>(p))
                  result = e3d->matrix() * result;
            p = p->parent();
            }
      return result;
      }

//---------------------------------------------------------
//   strokeAndFill
//    if lineWidth != 0 then stroke _pathList
//---------------------------------------------------------

void Element3d::strokeAndFill() {
      double lw     = lineWidth();
      bool doStroke = !qFuzzyCompare(lw, 0.0);

      if (doStroke) {
            // Offload the expensive InflatePaths computation to a
            // background thread.  The result (inflated PathList) is
            // applied on the main thread, then setPolygons is called.
            lw                     *= .5;
            Clipper2Lib::PathsD cl  = _pathList.clipper();
            int jt                  = joinType();
            int et                  = endType();
            bool f                  = fill();

            QPointer<Element3d> guard(this);
            GeometryWorker::instance().requestStroke(cl, lw, jt, et, f,
                                                     [this, guard](const GeometryWorker::StrokeResult& r) {
                                                           if (!guard || !r.valid)
                                                                 return;
                                                           _pathList = r.pathList;
                                                           _geometry->setPolygons(_pathList);
                                                           });
            }
      else {
            _pathList.setFill(fill());
            _geometry->setPolygons(_pathList);
            }
      }

//---------------------------------------------------------
//   closePath
//---------------------------------------------------------

void closePath(PathList& pl) {
      for (auto& p : pl)
            if (p.size() > 2 && p.front() != p.back())
                  p.push_back(p.front());
      }
