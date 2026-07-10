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

#include "zcam.h"
#include "project.h"
#include "cad.h"
#include "text.h"
#include "layer.h"
#include "treemodel.h"
#include "machines.h"
#include "laser.h"
#include "recipe.h"
#include "element.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QCoreApplication>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

//---------------------------------------------------------
//   ZCam
//---------------------------------------------------------

ZCam::ZCam(QObject* parent) : QObject(parent) {
      _style = new Style(this);

      // maintain two tree structures:
      //    - QObject tree
      //    - Element tree
      // Reason: we cannot easily manage the object order in QObject tree

      _machine  = new Machine(); // dummy
      _machines = new Machines(this);
      _laser    = new Laser(this, this);
      _recipes  = new Recipes(this);

      loadAssets();

      _treeModel      = new TreeModel(this);
      _projectManager = new ProjectManager(this, this);

      // Create an initial empty project as a valid default state.
      // The QML layer calls restoreLastProject() from Component.onCompleted
      // (after all signal handlers are connected) to replace this with the
      // previously-opened project, if any.
      //
      // IMPORTANT: newProject() must NOT clear the persisted lastPath here,
      // otherwise restoreLastProject() has nothing to read.
      _projectManager->newProject(false);

      // Automatically save assets (machines, recipes) when the
      // application is about to quit so changes are not lost.
      QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { saveAssets(); });
      }

//---------------------------------------------------------
//   create
//---------------------------------------------------------

ZCam* ZCam::create(QQmlEngine*, QJSEngine*) {
      return new ZCam();
      }

//---------------------------------------------------------
//   initAssets
//    initialize assets with dummies so the app will not
//    crash
//---------------------------------------------------------

void ZCam::initAssets() {
      // create recipes default
      if (_recipes) {
            json defaultRecipes = json::array();
            _recipes->fromJson(defaultRecipes);
            }
      if (_machines) {
            json defaultMachines = json::array();
            _machines->fromJson(defaultMachines);
            }
      }

//---------------------------------------------------------
//   loadAssets
//    load assets on application startup
//---------------------------------------------------------

void ZCam::loadAssets() {
      initAssets();

      QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      if (dataDir.isEmpty())
            return;

      QString filePath = QDir(dataDir).filePath("assets.json");
      QFile file(filePath);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;

      QByteArray data = file.readAll();
      file.close();

      try {
            json j = json::parse(data.toStdString());
            if (j.contains("recipes")) {
                  if (_recipes)
                        _recipes->fromJson(j.at("recipes"));
                  }
            if (j.contains("machines")) {
                  if (_machines)
                        _machines->fromJson(j.at("machines"));
                  }
            }
      catch (json::parse_error& e) {
            qWarning() << "Failed to parse assets.json:" << e.what();
            }
      }

//---------------------------------------------------------
//   saveAssets
//    assets are written in json format
//---------------------------------------------------------

//---------------------------------------------------------
//   dragged
//    Called from QML when an element is dragged in the 3D viewport.
//    Updates the element's position property, routing through the
//    ProjectManager so the change is recorded for undo/redo and the
//    project is marked dirty.
//---------------------------------------------------------

void ZCam::dragged(Element3d* element, const QVector3D& delta, int modifiers) {
      if (!element)
            return;
      QVector3D newPos = element->pos() + delta;
      if (_projectManager)
            _projectManager->changeProperty(element, QStringLiteral("pos"), QVariant::fromValue(newPos));
      else
            element->set_pos(newPos);
      }

//---------------------------------------------------------
//   rotated
//    Called from QML when an element is rotated in the 3D viewport.
//---------------------------------------------------------

void ZCam::rotated(Element3d* element, const QVector3D& deltaRotation, int modifiers) {
      if (!element)
            return;
      QVector3D newRot = element->rot() + deltaRotation;
      if (_projectManager)
            _projectManager->changeProperty(element, QStringLiteral("rot"), QVariant::fromValue(newRot));
      else
            element->set_rot(newRot);
      }

//---------------------------------------------------------
//   scaled
//    Called from QML when an element is scaled in the 3D viewport.
//    *scaleFactor* is a multiplicative delta per axis.
//---------------------------------------------------------

void ZCam::scaled(Element3d* element, const QVector3D& scaleFactor, int modifiers) {
      if (!element)
            return;
      // If scale-locked, scale uniformly using the X component.
      if (element->scaleLocked()) {
            double s = element->scale().x() * scaleFactor.x();
            QVector3D newScale(s, s, s);
            if (_projectManager)
                  _projectManager->changeProperty(element, QStringLiteral("scale"),
                                                  QVariant::fromValue(newScale));
            else
                  element->set_scale(newScale);
            }
      else {
            QVector3D cur = element->scale();
            QVector3D newScale(cur.x() * scaleFactor.x(), cur.y() * scaleFactor.y(),
                               cur.z() * scaleFactor.z());
            if (_projectManager)
                  _projectManager->changeProperty(element, QStringLiteral("scale"),
                                                  QVariant::fromValue(newScale));
            else
                  element->set_scale(newScale);
            }
      }

//---------------------------------------------------------
//   saveAssets
//---------------------------------------------------------

void ZCam::saveAssets() {
      QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      if (dataDir.isEmpty())
            return;

      QDir dir(dataDir);
      if (!dir.exists())
            dir.mkpath(".");

      QString filePath = dir.filePath("assets.json");
      QFile file(filePath);
      if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "Failed to open assets.json for writing";
            return;
            }

      json j;
      if (_recipes)
            j["recipes"] = _recipes->toJson();
      if (_machines)
            j["machines"] = _machines->toJson();

      QTextStream out(&file);
      out << QString::fromStdString(j.dump(4));
      file.close();
      }

//---------------------------------------------------------
//   hover
//---------------------------------------------------------

void ZCam::hover(Element3d* element) {
      Element3d* oldElement = hoverElement();
      set_hoverElement(element);
      // if an element changes its hover status, signal
      // a color change
      if (oldElement != element) {
            if (element)
                  emit element->curColorChanged();
            if (oldElement)
                  emit oldElement->curColorChanged();
            }
      }

//---------------------------------------------------------
//   mousePress
//---------------------------------------------------------

void ZCam::mousePress(Element3d* element, int buttons, int modifiers, double x, double y) {
      Debug("{}", element ? element->name() : "--");
      Element3d* oldElement = currentElement();
      set_currentElement(element);
      // if an element changes its hover status, signal
      // a color change
      if (oldElement != element) {
            if (element)
                  emit element->curColorChanged();
            if (oldElement)
                  emit oldElement->curColorChanged();
            }
      }

//---------------------------------------------------------
//   collectLayers (static helper)
//    Recursively traverse the element tree and collect all
//    Layer element names.
//---------------------------------------------------------

static void collectLayers(Element* root, QStringList& names) {
      if (!root)
            return;
      if (isType<Layer>(root))
            names.append(root->name());
      for (Element* child : root->children())
            collectLayers(child, names);
      }

//---------------------------------------------------------
//   layerNames
//    Collect all Layer element names by traversing the project tree.
//---------------------------------------------------------

QStringList ZCam::layerNames() const {
      QStringList names;
      collectLayers(rootElement(), names);
      return names;
      }

//---------------------------------------------------------
//   layerPtr
//    Return the Layer* for a given name, or nullptr.
//---------------------------------------------------------

Layer* ZCam::layerPtr(const QString& name) const {
      Element* e = Element::byName(name);
      if (!e)
            return nullptr;
      return qobject_cast<Layer*>(e);
      }

//---------------------------------------------------------
//   recipeNames
//    Return all recipe names from ZCam::recipes.
//---------------------------------------------------------

QStringList ZCam::recipeNames() const {
      if (!_recipes)
            return {};
      return _recipes->recipeModel();
      }

//---------------------------------------------------------
//   recipePtr
//    Return a pointer to the Recipe with the given name.
//    NOTE: Recipes stores std::vector<Recipe> by value, so we
//    return a pointer into that vector.  The pointer is valid
//    until recipeModelChanged is emitted.
//---------------------------------------------------------

Recipe* ZCam::recipePtr(const QString& name) const {
      if (!_recipes)
            return nullptr;
      for (int i = 0; i < _recipes->recipeCount(); ++i) {
            Recipe r = _recipes->recipe(i);
            if (r.name() == name)
                  return _recipes->recipePtr(i);
            }
      return nullptr;
      }