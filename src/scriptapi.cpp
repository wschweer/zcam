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

#include "scriptapi.h"

#include "zcam.h"
#include "project.h"
#include "element.h"
#include "element3d.h"
#include "polygon.h"
#include "group.h"
#include "nest.h"
#include "cad.h"
#include "fixture.h"
#include "machine.h"
#include "laser.h"
#include "mop.h"
#include "machines.h"
#include "scriptengine.h"
#include "painterpath.h"
#include "clipper.h"
#include "logger.h"
#include "treemodel.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <QRectF>
#include <QMetaProperty>
#include <QVariant>
#include <cmath>
#include <functional>

//--------------------------------------------------------------------
//     ScriptApi — construction
//--------------------------------------------------------------------

ScriptApi::ScriptApi(QObject* parent) : QObject(parent) {
      }

//--------------------------------------------------------------------
//     resolveElement
//    Resolve a QVariant that is either a string (element name) or
//    a QObject* (Element) to an Element pointer.
//--------------------------------------------------------------------

Element* ScriptApi::resolveElement(const QVariant& nameOrObj) const {
      if (nameOrObj.isNull() || !nameOrObj.isValid())
            return nullptr;
      // If it's a QObject variant, try to cast directly.
      if (nameOrObj.canConvert<QObject*>()) {
            QObject* obj = nameOrObj.value<QObject*>();
            if (obj)
                  return qobject_cast<Element*>(obj);
            }
      // Otherwise treat as a string name.
      QString name = nameOrObj.toString();
      if (name.isEmpty())
            return nullptr;
      Element* el = Element::byName(name);
      if (el)
            return el;
      // Fallback: case-insensitive search.
      const auto& names = Element::namesMap();
      for (auto it = names.constBegin(); it != names.constEnd(); ++it)
            if (it.key().compare(name, Qt::CaseInsensitive) == 0)
                  return it.value();
      return nullptr;
      }

//--------------------------------------------------------------------
//     elementName
//    Extract a display name from a QVariant (string or QObject*).
//--------------------------------------------------------------------

QString ScriptApi::elementName(const QVariant& nameOrObj) {
      if (nameOrObj.canConvert<QObject*>()) {
            auto* el = qobject_cast<Element*>(nameOrObj.value<QObject*>());
            if (el)
                  return el->name();
            }
      return nameOrObj.toString();
      }

//--------------------------------------------------------------------
//     findTargetLayer
//    Find the target layer for new elements.  Walks the current
//    element's parent chain for a visible Group (Layer), or falls
//    back to the first visible layer under the Cad root.
//--------------------------------------------------------------------

Group* ScriptApi::findTargetLayer() const {
      if (!_zc || !_zc->project())
            return nullptr;
      // Walk up from the current element to find a containing layer.
      Element3d* cur = _zc->currentElement();
      if (cur) {
            for (Element* e = cur; e; e = e->parent()) {
                  if (qobject_cast<Cad*>(e))
                        break;
                  auto* layer = qobject_cast<Group*>(e);
                  if (layer && layer->show() && layer->ancestorsShow())
                        return layer;
                  }
            }
      // Fallback: first visible layer under Cad.
      Cad* cad = _zc->project()->cad();
      if (!cad)
            return nullptr;
      std::function<Group*(Element*)> walk = [&](Element* root) -> Group* {
            if (!root)
                  return nullptr;
            auto* layer = qobject_cast<Group*>(root);
            if (layer && layer->show() && layer->ancestorsShow())
                  return layer;
            for (Element* c : root->children()) {
                  auto* found = walk(c);
                  if (found)
                        return found;
                  }
            return nullptr;
            };
      return walk(cad);
      }

//--------------------------------------------------------------------
//     elementWorldPaths
//    Convert an Element3d's geometry to Clipper2 PathsD in
//    world (root) coordinates by transforming the pathList through
//    the element's globalMatrix and projecting onto z=0.
//--------------------------------------------------------------------

Clipper2Lib::PathsD ScriptApi::elementWorldPaths(Element3d* el) const {
      Clipper2Lib::PathsD result;
      if (!el)
            return result;
      QMatrix4x4 gm      = el->globalMatrix();
      const PathList& pl = el->pathList();
      for (const auto& path : pl) {
            Clipper2Lib::PathD clipperPath;
            for (const auto& pt : path) {
                  QVector3D world = gm.map(QVector3D(float(pt.x()), float(pt.y()), 0.0f));
                  clipperPath.push_back({world.x(), world.y()});
                  }
            if (clipperPath.size() >= 3)
                  result.push_back(clipperPath);
            }
      // Also consider the fill path list (unmodified geometry before
      // stroke-and-fill inflation) for containment-type tests.
      const PathList& fpl = el->fillPathList();
      if (result.empty() && !fpl.empty()) {
            for (const auto& path : fpl) {
                  Clipper2Lib::PathD clipperPath;
                  for (const auto& pt : path) {
                        QVector3D world = gm.map(QVector3D(float(pt.x()), float(pt.y()), 0.0f));
                        clipperPath.push_back({world.x(), world.y()});
                        }
                  if (clipperPath.size() >= 3)
                        result.push_back(clipperPath);
                  }
            }
      return result;
      }

//--------------------------------------------------------------------
//     createPolygonFromWorldPaths
//    Create a new Polygon from Clipper2 PathsD expressed in world
//    (root) coordinates, add it to the first visible layer, and
//    return the new Polygon.  The paths are transformed from world
//    space to the target layer's local space.
//--------------------------------------------------------------------

Element3d* ScriptApi::createPolygonFromWorldPaths(const Clipper2Lib::PathsD& worldPaths) const {
      if (!_zc || !_zc->project() || worldPaths.empty())
            return nullptr;

      Group* layer = findTargetLayer();
      if (!layer) {
            Debug("ScriptApi: no target layer for new polygon");
            return nullptr;
            }

      // Transform world paths → layer-local space.
      QMatrix4x4 layerGlobal    = layer->globalMatrix();
      bool ok                   = false;
      QMatrix4x4 layerGlobalInv = layerGlobal.inverted(&ok);
      if (!ok)
            return nullptr;

      PainterPath pp;
      for (const auto& path : worldPaths) {
            if (path.empty())
                  continue;
            QVector3D w0 = QVector3D(float(path[0].x), float(path[0].y), 0.0f);
            QVector3D l0 = layerGlobalInv.map(w0);
            Vec2d firstPt(l0.x(), l0.y());
            pp.moveTo(firstPt);
            for (size_t i = 1; i < path.size(); ++i) {
                  QVector3D w = QVector3D(float(path[i].x), float(path[i].y), 0.0f);
                  QVector3D l = layerGlobalInv.map(w);
                  pp.lineTo(Vec2d(l.x(), l.y()));
                  }
            Vec2d lastPt(path.back().x, path.back().y);
            // Close subpath explicitly if not already closed.
            if (std::abs(firstPt.x() - lastPt.x()) > 0.0001 || std::abs(firstPt.y() - lastPt.y()) > 0.0001)
                  pp.lineTo(firstPt);
            }

      auto* newPoly = new Polygon(_zc, nullptr);
      newPoly->setName("");
      newPoly->set_pos(QVector3D(0.0, 0.0, 0.0));
      newPoly->setPainterPath(pp);
      newPoly->update();

      int row = layer->children().size();
      if (_zc->treeModel())
            _zc->treeModel()->beginInsertChild(layer, row);
      layer->addChild(newPoly);
      if (_zc->treeModel())
            _zc->treeModel()->endInsertChild();
      emit _zc->add3dElement(newPoly);
      _zc->setCamDirty(true);

      return newPoly;
      }

//--------------------------------------------------------------------
//     Element creation / deletion
//--------------------------------------------------------------------

QObject* ScriptApi::createElement(const QString& type, double x, double y, const QString& parent) {
      if (!_zc || !_zc->project())
            return nullptr;

      // If a parent is specified, select that element first so the
      // create* helpers find the right layer.  We do this by temporarily
      // setting the current element.
      Element3d* savedCurrent = _zc->currentElement();
      if (!parent.isEmpty()) {
            Element* p = resolveElement(parent);
            if (p) {
                  auto* e3d = qobject_cast<Element3d*>(p);
                  if (e3d)
                        _zc->setCurrentElement(e3d);
                  }
            }

      Element3d* el = nullptr;
      if (type == QStringLiteral("rectangle"))
            el = _zc->createRectangle(x, y);
      else if (type == QStringLiteral("polygon"))
            el = _zc->createPolygon(x, y);
      else if (type == QStringLiteral("ellipse"))
            el = _zc->createEllipse(x, y);
      else if (type == QStringLiteral("text"))
            el = _zc->createText(x, y);
      else if (type == QStringLiteral("group")) {
            Group* layer = findTargetLayer();
            if (!layer)
                  return nullptr;
            auto* g = new Group(_zc, layer);
            int row = layer->children().size();
            if (_zc->treeModel())
                  _zc->treeModel()->beginInsertChild(layer, row);
            layer->addChild(g);
            if (_zc->treeModel())
                  _zc->treeModel()->endInsertChild();
            emit _zc->add3dElement(g);
            _zc->setCamDirty(true);
            el = g;
            }
      else if (type == QStringLiteral("nest")) {
            Group* layer = findTargetLayer();
            if (!layer)
                  return nullptr;
            auto* n = new Nest(_zc, layer);
            int row = layer->children().size();
            if (_zc->treeModel())
                  _zc->treeModel()->beginInsertChild(layer, row);
            layer->addChild(n);
            if (_zc->treeModel())
                  _zc->treeModel()->endInsertChild();
            emit _zc->add3dElement(n);
            _zc->setCamDirty(true);
            el = n;
            }

      // Restore the saved current element.
      if (!parent.isEmpty() && savedCurrent)
            _zc->setCurrentElement(savedCurrent);

      return el;
      }

bool ScriptApi::deleteElement(const QString& name) {
      if (!_zc || !_zc->project())
            return false;
      Element* el = resolveElement(name);
      if (!el)
            return false;
      auto* el3d = qobject_cast<Element3d*>(el);
      if (!el3d || !el3d->deletable())
            return false;
      _zc->setCurrentElement(el3d);
      _zc->deleteCurrentElement();
      return true;
      }

QString ScriptApi::renameElement(const QString& name, const QString& newName) {
      if (!_zc || !_zc->project())
            return {};
      Element* el = resolveElement(name);
      if (!el)
            return {};
      return _zc->project()->renameElement(el, newName);
      }

bool ScriptApi::moveElement(const QString& name, const QString& newParent, int row) {
      if (!_zc || !_zc->project())
            return false;
      Element* el     = resolveElement(name);
      Element* parent = resolveElement(newParent);
      if (!el || !parent)
            return false;
      _zc->project()->moveElement(el, parent, row);
      return true;
      }

//--------------------------------------------------------------------
//     Property access
//--------------------------------------------------------------------

QVariant ScriptApi::getProperty(const QString& name, const QString& property) {
      if (!_zc || !_zc->project())
            return {};
      Element* el = resolveElement(name);
      if (!el)
            return {};
      QByteArray pn = property.toUtf8();
      int idx       = el->metaObject()->indexOfProperty(pn.constData());
      if (idx < 0)
            return {};
      QMetaProperty mp = el->metaObject()->property(idx);
      QVariant v       = mp.read(el);
      // Convert vector types to lists for JS convenience.
      if (v.metaType().id() == QMetaType::QVector2D) {
            QVector2D vec = v.value<QVector2D>();
            return QVariantList {vec.x(), vec.y()};
            }
      if (v.metaType().id() == QMetaType::QVector3D) {
            QVector3D vec = v.value<QVector3D>();
            return QVariantList {vec.x(), vec.y(), vec.z()};
            }
      if (v.metaType().id() == QMetaType::QColor)
            return v.value<QColor>().name(QColor::HexArgb);
      return v;
      }

bool ScriptApi::setProperty(const QString& name, const QString& property, const QVariant& value) {
      if (!_zc || !_zc->project())
            return false;
      Element* el = resolveElement(name);
      if (!el)
            return false;
      QByteArray pn = property.toUtf8();
      int idx       = el->metaObject()->indexOfProperty(pn.constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = el->metaObject()->property(idx);
      if (!mp.isWritable())
            return false;

      // Convert the QVariant to the property's type.
      QVariant converted = value;
      int typeId         = mp.metaType().id();
      if (typeId == QMetaType::QVector2D) {
            if (value.metaType().id() == QMetaType::QVariantList) {
                  QVariantList lst = value.toList();
                  if (lst.size() >= 2)
                        converted = QVariant::fromValue(QVector2D(lst[0].toFloat(), lst[1].toFloat()));
                  }
            else
                  converted = value;
            }
      else if (typeId == QMetaType::QVector3D) {
            if (value.metaType().id() == QMetaType::QVariantList) {
                  QVariantList lst = value.toList();
                  if (lst.size() >= 3)
                        converted = QVariant::fromValue(
                            QVector3D(lst[0].toFloat(), lst[1].toFloat(), lst[2].toFloat()));
                  else if (lst.size() == 2)
                        converted = QVariant::fromValue(QVector3D(lst[0].toFloat(), lst[1].toFloat(), 0.0f));
                  }
            else
                  converted = value;
            }
      else if (typeId == QMetaType::QColor) {
            converted = QVariant::fromValue(QColor(value.toString()));
            }
      else
            converted.convert(QMetaType(typeId));

      _zc->project()->changeProperty(el, property, converted);
      return true;
      }

//--------------------------------------------------------------------
//     Element queries
//--------------------------------------------------------------------

QVariantList ScriptApi::listElements(int maxDepth) {
      if (!_zc || !_zc->project())
            return {};
      QVariantList result;
      std::function<void(Element*, int, const QString&)> walk = [&](Element* e, int depth,
                                                                    const QString& path) {
            if (!e || (maxDepth >= 0 && depth > maxDepth))
                  return;
            QString name = e->name();
            if (!name.isEmpty()) {
                  QVariantMap entry;
                  entry["name"] = name;
                  entry["type"] = e->typeName();
                  entry["path"] = path.isEmpty() ? name : path + "." + name;
                  result.append(entry);
                  }
            QString childPath = path.isEmpty() ? name : path + "." + name;
            for (Element* c : e->children())
                  walk(c, depth + 1, childPath);
            };
      walk(_zc->project(), 0, QString());
      return result;
      }

QObject* ScriptApi::findElement(const QString& name) {
      if (!_zc || !_zc->project())
            return nullptr;
      return resolveElement(name);
      }

QObject* ScriptApi::currentElement() {
      if (!_zc)
            return nullptr;
      return _zc->currentElement();
      }

bool ScriptApi::selectElement(const QString& name) {
      if (!_zc)
            return false;
      if (name.isEmpty()) {
            _zc->setCurrentElement(nullptr);
            return true;
            }
      Element* el = resolveElement(name);
      if (!el)
            return false;
      auto* e3d = qobject_cast<Element3d*>(el);
      if (!e3d)
            return false;
      _zc->setCurrentElement(e3d);
      return true;
      }

void ScriptApi::clearSelection() {
      if (_zc)
            _zc->clearSelection();
      }

//--------------------------------------------------------------------
//     invokeElementMethod
//    Dispatch a Q_INVOKABLE method call on the named element through
//    its runtime meta-object.  JS wrappers for elements expose only
//    the static Element3d interface, so subclass methods (Nest::nest,
//    Polygon::optimize, ...) cannot be called directly from scripts.
//--------------------------------------------------------------------

bool ScriptApi::invokeElementMethod(const QString& name, const QString& method, const QVariantList& args) {
      if (!_zc)
            return false;
      Element* el = resolveElement(name);
      if (!el)
            return false;
      auto* el3d = qobject_cast<Element3d*>(el);
      if (!el3d)
            return false;
      return _zc->invokeElementMethod(el3d, method, args);
      }

//--------------------------------------------------------------------
//     Geometry queries (element-based)
//--------------------------------------------------------------------

QVariantList ScriptApi::worldBoundingBox(const QString& name) {
      if (!_zc)
            return {};
      Element* el = resolveElement(name);
      auto* e3d   = qobject_cast<Element3d*>(el);
      if (!e3d)
            return {};
      QRectF bb = e3d->worldBoundingBox();
      return {bb.left(), bb.top(), bb.right(), bb.bottom()};
      }

QVariantList ScriptApi::boundingBox(const QString& name) {
      if (!_zc)
            return {};
      Element* el = resolveElement(name);
      auto* e3d   = qobject_cast<Element3d*>(el);
      if (!e3d)
            return {};
      QRectF bb = e3d->boundingBox();
      return {bb.left(), bb.top(), bb.right(), bb.bottom()};
      }

QVariantList ScriptApi::vertexWorldPos(const QString& name, int index) {
      if (!_zc)
            return {};
      Element* el = resolveElement(name);
      auto* e3d   = qobject_cast<Element3d*>(el);
      if (!e3d)
            return {};
      QVector3D v = e3d->vertexWorldPos(index);
      return {v.x(), v.y(), v.z()};
      }

int ScriptApi::vertexCount(const QString& name) {
      if (!_zc)
            return 0;
      Element* el = resolveElement(name);
      auto* e3d   = qobject_cast<Element3d*>(el);
      if (!e3d)
            return 0;
      return e3d->vertexCount();
      }

bool ScriptApi::containsWorldPoint(const QString& name, double x, double y) {
      if (!_zc)
            return false;
      Element* el = resolveElement(name);
      auto* e3d   = qobject_cast<Element3d*>(el);
      if (!e3d)
            return false;
      return e3d->containsWorldPoint(x, y);
      }

bool ScriptApi::isInside(const QString& inner, const QString& outer) {
      if (!_zc)
            return false;
      Element* innerEl = resolveElement(inner);
      Element* outerEl = resolveElement(outer);
      auto* inner3d    = qobject_cast<Element3d*>(innerEl);
      auto* outer3d    = qobject_cast<Element3d*>(outerEl);
      if (!inner3d || !outer3d)
            return false;
      Clipper2Lib::PathsD innerPaths = elementWorldPaths(inner3d);
      Clipper2Lib::PathsD outerPaths = elementWorldPaths(outer3d);
      if (innerPaths.empty() || outerPaths.empty())
            return false;
      // If the difference (inner minus outer) is empty, inner is
      // entirely contained within outer.
      Clipper2Lib::PathsD diff = Clipper2Lib::BooleanOp(
          Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, innerPaths, outerPaths, 4);
      return diff.empty();
      }

//--------------------------------------------------------------------
//     Geometry operations (element-based)
//--------------------------------------------------------------------

QString ScriptApi::unionPolygons(const QString& a, const QString& b) {
      if (!_zc)
            return {};
      Element* elA = resolveElement(a);
      Element* elB = resolveElement(b);
      auto* a3d    = qobject_cast<Element3d*>(elA);
      auto* b3d    = qobject_cast<Element3d*>(elB);
      if (!a3d || !b3d)
            return {};
      Clipper2Lib::PathsD pathsA = elementWorldPaths(a3d);
      Clipper2Lib::PathsD pathsB = elementWorldPaths(b3d);
      if (pathsA.empty() || pathsB.empty())
            return {};
      Clipper2Lib::PathsD result = Clipper2Lib::BooleanOp(
          Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, pathsA, pathsB, 4);
      if (result.empty())
            return {};
      Element3d* newPoly = createPolygonFromWorldPaths(result);
      return newPoly ? newPoly->name() : QString();
      }

QString ScriptApi::differencePolygons(const QString& a, const QString& b) {
      if (!_zc)
            return {};
      Element* elA = resolveElement(a);
      Element* elB = resolveElement(b);
      auto* a3d    = qobject_cast<Element3d*>(elA);
      auto* b3d    = qobject_cast<Element3d*>(elB);
      if (!a3d || !b3d)
            return {};
      Clipper2Lib::PathsD pathsA = elementWorldPaths(a3d);
      Clipper2Lib::PathsD pathsB = elementWorldPaths(b3d);
      if (pathsA.empty() || pathsB.empty())
            return {};
      Clipper2Lib::PathsD result = Clipper2Lib::BooleanOp(
          Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, pathsA, pathsB, 4);
      if (result.empty())
            return {};
      Element3d* newPoly = createPolygonFromWorldPaths(result);
      return newPoly ? newPoly->name() : QString();
      }

QString ScriptApi::intersectPolygons(const QString& a, const QString& b) {
      if (!_zc)
            return {};
      Element* elA = resolveElement(a);
      Element* elB = resolveElement(b);
      auto* a3d    = qobject_cast<Element3d*>(elA);
      auto* b3d    = qobject_cast<Element3d*>(elB);
      if (!a3d || !b3d)
            return {};
      Clipper2Lib::PathsD pathsA = elementWorldPaths(a3d);
      Clipper2Lib::PathsD pathsB = elementWorldPaths(b3d);
      if (pathsA.empty() || pathsB.empty())
            return {};
      Clipper2Lib::PathsD result = Clipper2Lib::BooleanOp(
          Clipper2Lib::ClipType::Intersection, Clipper2Lib::FillRule::NonZero, pathsA, pathsB, 4);
      if (result.empty())
            return {};
      Element3d* newPoly = createPolygonFromWorldPaths(result);
      return newPoly ? newPoly->name() : QString();
      }

QString ScriptApi::offsetPolygon(const QString& name, double delta) {
      if (!_zc)
            return {};
      Element* el = resolveElement(name);
      auto* e3d   = qobject_cast<Element3d*>(el);
      if (!e3d)
            return {};
      Clipper2Lib::PathsD paths = elementWorldPaths(e3d);
      if (paths.empty())
            return {};
      Clipper2Lib::PathsD result = Clipper2Lib::InflatePaths(
          paths, delta, Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Polygon, 2.0, 4, 0.0);
      if (result.empty())
            return {};
      Element3d* newPoly = createPolygonFromWorldPaths(result);
      return newPoly ? newPoly->name() : QString();
      }

//--------------------------------------------------------------------
//     Polygon vertex manipulation
//--------------------------------------------------------------------

bool ScriptApi::addVertex(const QString& polygonName, double x, double y) {
      if (!_zc)
            return false;
      Element* el = resolveElement(polygonName);
      auto* poly  = qobject_cast<Polygon*>(el);
      if (!poly)
            return false;
      poly->lineTo(Vec2d(x, y));
      poly->update();
      _zc->setCamDirty(true);
      return true;
      }

bool ScriptApi::addBezier(
    const QString& polygonName, double c1x, double c1y, double c2x, double c2y, double ex, double ey) {
      if (!_zc)
            return false;
      Element* el = resolveElement(polygonName);
      auto* poly  = qobject_cast<Polygon*>(el);
      if (!poly)
            return false;
      poly->cubicTo(Vec2d(c1x, c1y), Vec2d(c2x, c2y), Vec2d(ex, ey));
      poly->update();
      _zc->setCamDirty(true);
      return true;
      }

bool ScriptApi::setVertexPos(const QString& name, int index, double x, double y, double z) {
      if (!_zc)
            return false;
      Element* el = resolveElement(name);
      auto* e3d   = qobject_cast<Element3d*>(el);
      if (!e3d)
            return false;
      e3d->setVertexPos(index, QVector3D(x, y, z));
      e3d->update();
      _zc->setCamDirty(true);
      return true;
      }

bool ScriptApi::optimizePolygon(const QString& name) {
      if (!_zc)
            return false;
      Element* el = resolveElement(name);
      auto* poly  = qobject_cast<Polygon*>(el);
      if (!poly)
            return false;
      poly->optimize();
      return true;
      }

//--------------------------------------------------------------------
//     Transform
//--------------------------------------------------------------------

bool ScriptApi::setPos(const QString& name, double x, double y, double z) {
      return setProperty(name, QStringLiteral("pos"), QVariantList {x, y, z});
      }

bool ScriptApi::setRot(const QString& name, double x, double y, double z) {
      return setProperty(name, QStringLiteral("rot"), QVariantList {x, y, z});
      }

bool ScriptApi::setScale(const QString& name, double x, double y, double z) {
      return setProperty(name, QStringLiteral("scale"), QVariantList {x, y, z});
      }

//--------------------------------------------------------------------
//     App control
//--------------------------------------------------------------------

void ScriptApi::newProject() {
      if (_zc)
            _zc->newProject();
      }

bool ScriptApi::saveProject() {
      if (!_zc)
            return false;
      return _zc->save();
      }

void ScriptApi::undo() {
      if (_zc && _zc->project())
            _zc->project()->doUndo();
      }

void ScriptApi::redo() {
      if (_zc && _zc->project())
            _zc->project()->doRedo();
      }

void ScriptApi::beginBatch() {
      if (_zc && _zc->project() && _zc->project()->undo())
            _zc->project()->undo()->beginMacro();
      }

void ScriptApi::endBatch() {
      if (_zc && _zc->project() && _zc->project()->undo())
            _zc->project()->undo()->endMacro();
      }

void ScriptApi::refreshCam() {
      if (_zc)
            _zc->refreshCam();
      }

bool ScriptApi::exportSvg(const QString& path) {
      if (!_zc)
            return false;
      return _zc->exportSvg(path);
      }

bool ScriptApi::exportDxf(const QString& path) {
      if (!_zc)
            return false;
      return _zc->exportDxf(path);
      }

bool ScriptApi::importFile(const QString& path) {
      if (!_zc)
            return false;
      return _zc->importFile(path);
      }

bool ScriptApi::importSvgAt(const QString& path, double x, double y) {
      if (!_zc)
            return false;
      _zc->importSvgAt(path, x, y);
      return true;
      }

QString ScriptApi::screenshot() {
      if (!_zc)
            return {};
      return _zc->saveCanvasScreenshot();
      }

//--------------------------------------------------------------------
//     Machine / laser
//--------------------------------------------------------------------

QString ScriptApi::machineName() {
      if (!_zc || !_zc->project())
            return {};
      return _zc->project()->machineName();
      }

bool ScriptApi::setMachine(const QString& name) {
      if (!_zc || !_zc->project() || !_zc->machines())
            return false;
      Machine* m = _zc->machines()->machine(name);
      if (!m)
            return false;
      _zc->project()->set_machine(m);
      return true;
      }

bool ScriptApi::startFraming() {
      if (!_zc || !_zc->project() || !_zc->project()->machine())
            return false;
      Laser* laser = _zc->project()->machine()->laserEngine();
      if (!laser)
            return false;
      laser->startFraming();
      return true;
      }

bool ScriptApi::startMarking() {
      if (!_zc || !_zc->project() || !_zc->project()->machine())
            return false;
      Laser* laser = _zc->project()->machine()->laserEngine();
      if (!laser)
            return false;
      laser->startMarking();
      return true;
      }

void ScriptApi::stopLaser() {
      if (!_zc || !_zc->project() || !_zc->project()->machine())
            return;
      Laser* laser = _zc->project()->machine()->laserEngine();
      if (laser)
            laser->stop();
      }

//--------------------------------------------------------------------
//     Layer / fixture
//--------------------------------------------------------------------

QString ScriptApi::addLayer() {
      if (!_zc || !_zc->project())
            return {};
      _zc->project()->addLayer();
      // Return the name of the most recently added layer.
      Cad* cad = _zc->project()->cad();
      if (!cad || cad->children().isEmpty())
            return {};
      Element* last = cad->children().last();
      return last ? last->name() : QString();
      }

QString ScriptApi::addFixture() {
      if (!_zc || !_zc->project())
            return {};
      _zc->project()->addFixtureCmd();
      const auto& fixtures = _zc->project()->fixtures();
      if (fixtures.isEmpty())
            return {};
      return fixtures.last()->name();
      }

QString ScriptApi::addLaserMop(const QString& fixtureName) {
      if (!_zc || !_zc->project())
            return {};
      Element* el   = resolveElement(fixtureName);
      auto* fixture = qobject_cast<Fixture*>(el);
      if (!fixture)
            return {};
      _zc->project()->addLaserMopCmd(fixture);
      // Return the name of the most recently added MOP.
      const auto& children = fixture->children();
      for (int i = children.size() - 1; i >= 0; --i)
            if (children[i]->typeName() == QStringLiteral("laserMop"))
                  return children[i]->name();
      return {};
      }

//--------------------------------------------------------------------
//     setMops
//    Assign a named laser MOP (laser layer) to an element by setting
//    its ``mop`` property.  The MOP name is resolved via
//    ZCam::mopPtr(); pass an empty string to clear the assignment
//    (inherit from parent).  The change is routed through
//    Project::changeProperty() so it is recorded on the undo stack.
//--------------------------------------------------------------------

bool ScriptApi::setMops(const QString& elementName, const QString& mopsName) {
      if (!_zc || !_zc->project())
            return false;
      Element* el = resolveElement(elementName);
      if (!el)
            return false;
      auto* el3d = qobject_cast<Element3d*>(el);
      if (!el3d)
            return false;
      QVariant value;
      if (!mopsName.isEmpty()) {
            Mop* mop = _zc->mopPtr(mopsName);
            if (!mop)
                  return false;
            value = QVariant::fromValue(mop);
            }
      else {
            // Clear: write a typed null for the Mop* property.
            int propIdx = el3d->metaObject()->indexOfProperty("mop");
            if (propIdx < 0)
                  return false;
            QMetaProperty mp = el3d->metaObject()->property(propIdx);
            value            = QVariant(mp.metaType(), nullptr);
            }
      _zc->project()->changeProperty(el3d, QStringLiteral("mop"), value);
      return true;
      }

//--------------------------------------------------------------------
//     Utility
//--------------------------------------------------------------------

void ScriptApi::log(const QString& msg) {
      Debug("script: {}", msg.toUtf8().constData());
      }

QVariant ScriptApi::eval(const QString& script) {
      auto* se = ScriptEngine::instance();
      if (!se)
            return {};
      auto r = se->eval(script);
      if (!r.error.isEmpty()) {
            Warning("script eval error: {}", r.error.toUtf8().constData());
            return {};
            }
      return r.value;
      }