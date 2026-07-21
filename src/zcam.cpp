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
#include "laserlayer.h"
#include "rectangle.h"
#include "polygon.h"
#include "ellipse.h"
#include "element3d.h"
#include "treemodel.h"
#include "machines.h"
#include "laser.h"
#include "recipe.h"
#include "element.h"
#include "undo.h"
#include "propertyjson.h"
#include "geometryworker.h"
#include "grid.h"
#include "fixture.h"
#include "framing.h"
#include "stock.h"
#include "dxfimport.h"

#include <QSettings>
#include <QFileInfo>
#include <fstream>

#include <cmath>
#include <algorithm>
#include <vector>
#include <QQuaternion>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QCoreApplication>
#include <QMatrix4x4>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

//---------------------------------------------------------
//   ZCam
//---------------------------------------------------------

ZCam::ZCam(QObject* parent) : QObject(parent) {
      _config = new Config(this);

      // maintain two tree structures:
      //    - QObject tree
      //    - Element tree
      // Reason: we cannot easily manage the object order in QObject tree

      _machines = new Machines(this);
      _recipes  = new Recipes(this);

      loadAssets();

      _treeModel = new TreeModel(this);

      // Create an initial empty project as a valid default state.
      // The QML layer calls restoreLastProject() from Component.onCompleted
      // (after all signal handlers are connected) to replace this with the
      // previously-opened project, if any.
      //
      // IMPORTANT: newProject() must NOT clear the persisted lastPath here,
      // otherwise restoreLastProject() has nothing to read.
      newProject(false);

      // When the machines list changes (e.g. machines added/removed/renamed),
      // re-resolve the Project's Machine* from the stored machine name.
      connect(_machines, &Machines::machinesModelChanged, this, [this]() {
            if (_project)
                  _project->resolveMachine();
            });

      // Automatically save assets (machines, recipes) and stop the laser
      // when the application is about to quit so changes are not lost and
      // background threads are cleanly joined before the event loop stops.
      //
      // GeometryWorker is NOT shut down here because aboutToQuit fires
      // before the QML engine is destroyed.  QML destructors may enqueue
      // new geometry tasks whose callbacks would target dead objects.
      // The GeometryWorker singleton destructor handles final cleanup
      // with a bounded timeout.
      QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
            GeometryWorker::instance().shutdown();
            saveAssets();
            });
      }

//---------------------------------------------------------
//   setCurrentElement
//    Custom setter for currentElement.  Emits curColorChanged on
//    the old and new element so the 3D view updates highlight
//    colors regardless of whether the selection originated from
//    QML (TreeView click) or C++ (3D canvas pick).
//---------------------------------------------------------

void ZCam::setCurrentElement(Element3d* el) {
      Element3d* oldElement = _currentElement;
      if (el == oldElement)
            return;
      // Clear segment selection on the old element when switching away.
      if (oldElement) {
            auto* oldPoly = qobject_cast<Polygon*>(oldElement);
            if (oldPoly && oldPoly->selectedSegment() >= 0)
                  oldPoly->clearSegmentSelection();
            }
      _currentElement = el;
      emit currentElementChanged();
      // Signal color changes so the 3D view updates highlight colors.
      if (oldElement)
            emit oldElement->curColorChanged();
      if (el)
            emit el->curColorChanged();
      }

//---------------------------------------------------------
//   applyFontToCurrentText
//    Apply a font family to the currently selected Text
//    element. The change goes through the Project undo system so it
//    is undoable and marks the project dirty.
//---------------------------------------------------------

void ZCam::applyFontToCurrentText(const QString& family) {
      if (!_currentElement)
            return;
      auto* text = qobject_cast<Text*>(_currentElement);
      if (!text)
            return;
      if (!text->fontFamily().isEmpty() && text->fontFamily() == family)
            return;
      _project->changeProperty(text, "fontFamily", family);
      }

//---------------------------------------------------------
//   setCamDirty
//    Mark the cam data as out-of-date.  The QML "Cam" refresh
//    button becomes enabled when this is true.
//---------------------------------------------------------

void ZCam::setCamDirty(bool v) {
      if (v == _camDirty)
            return;
      _camDirty = v;
      emit camDirtyChanged();
      }

//---------------------------------------------------------
//   centerOnWorkspace
//    Center the given element on the workspace midpoint.
//    The workspace size is determined by the current machine's
//    maxTravel (X and Y).  Only Text, Polygon, Ellipse and
//    Rectangle elements are accepted; Z is always set to zero.
//    The operation is routed through the undo stack.
//---------------------------------------------------------

void ZCam::centerOnWorkspace(Element3d* element) {
      if (!element || !element->draggable())
            return;

      // Only accept Text, Polygon, Ellipse and Rectangle elements.
      // All four override draggable() to return true, so the draggable()
      // check above already filters non-shape elements.  But we also
      // check the concrete type to be explicit and safe.
      if (!isType<Text>(element) && !isType<Polygon>(element) && !isType<Ellipse>(element) &&
          !isType<Rectangle>(element))
            return;

      // Determine the workspace center from the current machine.
      double centerX = 0.0;
      double centerY = 0.0;
      if (_project && _project->machine()) {
            QVector3D travel = _project->machine()->maxTravel();
            centerX          = travel.x() / 2.0;
            centerY          = travel.y() / 2.0;
            }

      // Compute the element's bounding box center in world coordinates.
      // The bounding box is in local coordinates, so we transform it
      // through the element's globalMatrix() to get world coordinates.
      QRectF bbox = element->boundingBox();
      if (bbox.isNull() || bbox.isEmpty())
            return;

      // The center of the bbox in local coordinates.
      QPointF localCenter = bbox.center();

      // Transform the local center to world coordinates.
      QVector3D worldCenter = element->globalMatrix().map(
          QVector3D(static_cast<float>(localCenter.x()), static_cast<float>(localCenter.y()), 0.0f));

      // The new position must move the bbox center to the workspace center.
      // Since pos is in the parent's local coordinate system, we need
      // to convert the world-space displacement to parent-local space.
      QVector3D desiredWorldCenter(static_cast<float>(centerX), static_cast<float>(centerY), 0.0f);
      QVector3D worldDelta = desiredWorldCenter - worldCenter;

      // Convert world delta to parent-local delta.
      QVector3D localDelta = worldDelta;
      if (auto* p = qobject_cast<Element3d*>(element->parent())) {
            QMatrix4x4 parentGlobal = p->globalMatrix();
            bool ok                 = false;
            QMatrix4x4 inv          = parentGlobal.inverted(&ok);
            if (ok)
                  localDelta = inv.mapVector(worldDelta);
            }

      QVector3D oldPos = element->pos();
      QVector3D newPos = QVector3D(oldPos.x() + localDelta.x(), oldPos.y() + localDelta.y(), 0.0f);

      if ((newPos - oldPos).length() < 0.001)
            return;

      if (_project) {
            auto cmd = std::make_unique<PropertyChangeCommand>(
                element, QByteArrayLiteral("pos"), QVariant::fromValue(oldPos), QVariant::fromValue(newPos));
            cmd->redo(); // apply the new position immediately
            _project->pushCommand(std::move(cmd));
            _project->markDirty();
            }

      if (_project)
            setCamDirty(true);
      }

//---------------------------------------------------------
//   refreshCam
//    Recalculate cam data and clear the dirty flag.
//---------------------------------------------------------

void ZCam::refreshCam() {
      if (!_project)
            return;
      Cam* cam = _project->cam();
      if (!cam)
            return;
      cam->updateCam();
      setCamDirty(false);
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
      if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = file.readAll();
            file.close();
            parseAssetsData(data);
            }

      // If the main file had no recipes/machines, try the backup file
      // created by previous editor sessions and merge missing sections.
      QString backupPath = QDir(dataDir).filePath(".assets.json,");
      QFile backup(backupPath);
      if (backup.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray bdata = backup.readAll();
            backup.close();
            try {
                  json bj = json::parse(bdata.toStdString());
                  if (bj.contains("recipes") && !bj["recipes"].empty()) {
                        if (_recipes && _recipes->recipeCount() == 0)
                              _recipes->fromJson(bj.at("recipes"));
                        }
                  if (bj.contains("machines") && !bj["machines"].empty()) {
                        if (_machines) {
                              QStringList model = _machines->machinesModel();
                              if (model.isEmpty())
                                    _machines->fromJson(bj.at("machines"));
                              }
                        }
                  }
            catch (json::parse_error&) {
                  // ignore backup parse errors
                  }
            }
      }

//---------------------------------------------------------
//   parseAssetsData
//---------------------------------------------------------

void ZCam::parseAssetsData(const QByteArray& data) {
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
            if (j.contains("config")) {
                  if (_config)
                        _config->fromJson(j.at("config"));
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
//    Updates the element's position property directly (live update).
//    The undo record is created once at endElementDrag().
//---------------------------------------------------------

void ZCam::dragged(Element3d* element, const QVector3D& delta, int modifiers) {
      if (!element || !element->draggable())
            return;
      // If startElementDrag() was not called before the first drag
      // event (e.g. because a segment was selected on the second
      // click, which skipped startElementDrag in QML), lazily
      // initialize the drag state here.
      if (!_elementDragElement) {
            startElementDrag(element);
            // A drag is now in progress — discard any pending segment
            // selection so the bounding box remains visible throughout
            // the drag and after release.
            _pendingSegmentElement = nullptr;
            // Clear any pre-existing segment selection so the bounding
            // box reappears during the drag.
            auto* poly = qobject_cast<Polygon*>(element);
            if (poly && poly->selectedSegment() >= 0)
                  poly->clearSegmentSelection();
            }
      // The delta from QML is in world (root) space coordinates, but
      // element->pos() is in the parent's local coordinate system.
      // If the parent (e.g. a Layer) has a non-identity scale, the
      // world-space delta must be converted to local space before
      // being applied.  Without this, an element under a layer with
      // scale 0.5 would move at half speed, because the parent's
      // scale also transforms the child's local translation.
      //
      // We use the inverse of the parent's globalMatrix() and
      // mapVector() (which applies only rotation+scale, not
      // translation) to convert the direction delta correctly.
      QVector3D localDelta = delta;
      if (auto* p = qobject_cast<Element3d*>(element->parent())) {
            QMatrix4x4 parentGlobal = p->globalMatrix();
            bool ok                 = false;
            QMatrix4x4 inv          = parentGlobal.inverted(&ok);
            if (ok)
                  localDelta = inv.mapVector(delta);
            }

      // Magnetic grid snap: when the project's Grid has snap enabled,
      // grid lines act magnetically.  The element's reference point is
      // (0,0) in local coords, which maps to element->pos() in parent
      // local space.  When the reference point crosses a grid line, the
      // element snaps to that line.  Further dragging accumulates in
      // _snapState.excess until it exceeds half the minor spacing, at
      // which point the element "breaks free" and moves freely until
      // the next line is crossed.
      Grid* grid = nullptr;
      if (_project)
            grid = qobject_cast<Grid*>(_project->gridElement());
      if (grid && grid->snap()) {
            double spacing = grid->minorSpacing();
            if (spacing > 0.0) {
                  double halfSpacing = spacing / 2.0;
                  double curX        = element->pos().x();
                  double curY        = element->pos().y();
                  double newX        = curX + localDelta.x();
                  double newY        = curY + localDelta.y();

                  // X axis snap
                  if (_snapState.activeX) {
                        _snapState.excessX += localDelta.x();
                        if (std::abs(_snapState.excessX) > halfSpacing) {
                              // Break free: snap point releases, move freely
                              _snapState.activeX = false;
                              newX               = curX + localDelta.x();
                              }
                        else {
                              // Hold snapped: keep X at the snap line
                              newX = curX;
                              }
                        }
                  else {
                        // Check if reference point crosses a grid line
                        double oldLine = std::round(curX / spacing);
                        double newLine = std::round(newX / spacing);
                        if (newLine != oldLine) {
                              _snapState.activeX = true;
                              _snapState.excessX = newX - newLine * spacing;
                              newX               = newLine * spacing;
                              }
                        }

                  // Y axis snap
                  if (_snapState.activeY) {
                        _snapState.excessY += localDelta.y();
                        if (std::abs(_snapState.excessY) > halfSpacing) {
                              _snapState.activeY = false;
                              newY               = curY + localDelta.y();
                              }
                        else {
                              newY = curY;
                              }
                        }
                  else {
                        double oldLine = std::round(curY / spacing);
                        double newLine = std::round(newY / spacing);
                        if (newLine != oldLine) {
                              _snapState.activeY = true;
                              _snapState.excessY = newY - newLine * spacing;
                              newY               = newLine * spacing;
                              }
                        }

                  QVector3D newPos(newX, newY, element->pos().z() + localDelta.z());
                  element->set_pos(newPos);
                  return;
                  }
            }

      QVector3D newPos = element->pos() + localDelta;
      element->set_pos(newPos);
      }

//---------------------------------------------------------
//   rotated
//    Called from QML when an element is rotated in the 3D viewport.
//    Updates the element's rotation property directly (live update).
//    The undo record is created once at endElementDrag().
//---------------------------------------------------------

void ZCam::rotated(Element3d* element, const QVector3D& deltaRotation, int modifiers) {
      if (!element || !element->draggable())
            return;
      // Lazily initialize drag state if startElementDrag() was skipped.
      if (!_elementDragElement)
            startElementDrag(element);
      QVector3D newRot = element->rot() + deltaRotation;
      element->set_rot(newRot);
      }

//---------------------------------------------------------
//   scaled
//    Called from QML when an element is scaled in the 3D viewport.
//    Updates the element's scale property directly (live update).
//    The undo record is created once at endElementDrag().
//---------------------------------------------------------

void ZCam::scaled(Element3d* element, const QVector3D& scaleFactor, int modifiers, const QVector3D& pivot) {
      if (!element || !element->draggable())
            return;
      // Lazily initialize drag state if startElementDrag() was skipped.
      if (!_elementDragElement)
            startElementDrag(element);

      // Compute the element's world-space origin BEFORE the scale
      // change.  The origin (0,0,0) in local coords maps to the
      // translation part of globalMatrix(); it does not depend on
      // the current scale value.
      QVector3D worldOrigin = element->globalMatrix().map(QVector3D(0.0f, 0.0f, 0.0f));

      // Use set_scaleAR() instead of set_scale() so that the lockScale
      // enforcement (Off / Lock / Square) is applied.  set_scale() bypasses
      // the lock constraints, which would allow non-uniform scaling even
      // when lockScale is set to Square or Lock.
      QVector3D cur = element->scale();
      QVector3D newScale(cur.x() * scaleFactor.x(), cur.y() * scaleFactor.y(), cur.z() * scaleFactor.z());
      element->set_scaleAR(newScale);

      // Determine the effective uniform scale factor.  set_scaleAR()
      // may adjust the requested scale when lockScale is Square or
      // Lock, so we compute the actual ratio from the resulting
      // scale values.
      QVector3D actualNew = element->scale();
      float sd            = (cur.x() != 0.0f) ? actualNew.x() / cur.x() : 1.0f;

      // To scale around the pivot point (in world coords), the
      // element's world-space origin must move so that the pivot
      // stays fixed.  The required world-space displacement is:
      //    worldDelta = (1 - sd) * (pivot - worldOrigin)
      // This is analogous to how the canvas zoom keeps the cursor
      // position fixed: root.position += cursorScenePos - root.position) * (1 - sd).
      QVector3D worldDelta = (1.0f - sd) * (pivot - worldOrigin);

      // Convert the world-space delta to the parent's local
      // coordinate system, analogous to dragged() and
      // centerOnWorkspace().  mapVector() applies only
      // rotation+scale, not translation, which is correct for
      // direction vectors.
      QVector3D localDelta = worldDelta;
      if (auto* p = qobject_cast<Element3d*>(element->parent())) {
            QMatrix4x4 parentGlobal = p->globalMatrix();
            bool ok                 = false;
            QMatrix4x4 inv          = parentGlobal.inverted(&ok);
            if (ok)
                  localDelta = inv.mapVector(worldDelta);
            }

      element->beginBatchUpdate();
      element->set_pos(element->pos() + localDelta);
      element->endBatchUpdate();
      }

//---------------------------------------------------------
//   startElementDrag
//    Called from QML when the user starts dragging/rotating/scaling
//    an element in the 3D viewport.  Records the original transform
//    values (pos, rot, scale) so that endElementDrag() can create
//    a single undo command for the entire drag operation.
//---------------------------------------------------------

void ZCam::startElementDrag(Element3d* element) {
      if (!element || !element->draggable())
            return;
      _elementDragElement   = element;
      _elementDragOrigPos   = element->pos();
      _elementDragOrigRot   = element->rot();
      _elementDragOrigScale = element->scale();
      _snapState.reset();
      }

//---------------------------------------------------------
//   endElementDrag
//    Called from QML when the user finishes dragging/rotating/scaling
//    an element.  Creates and pushes a single undo command that
//    captures all three transform properties (pos, rot, scale) in
//    one atomic operation.
//---------------------------------------------------------

void ZCam::endElementDrag() {
      // Helper lambda: apply the pending segment selection if any.
      // Called when no actual drag movement occurred (pure click).
      auto applyPendingSegment = [this]() {
            if (!_pendingSegmentElement)
                  return;
            auto* poly = qobject_cast<Polygon*>(_pendingSegmentElement);
            if (poly) {
                  if (_pendingSegmentToggleOff)
                        poly->clearSegmentSelection();
                  else {
                        int nearest = poly->findNearestSegment(_pendingSegmentClickPos);
                        if (nearest >= 0)
                              poly->setSelectedSegment(nearest);
                        }
                  }
            _pendingSegmentElement = nullptr;
            };

      if (!_elementDragElement) {
            // startElementDrag() was never called — apply pending
            // segment selection if any (e.g. click on already-selected
            // polygon without any movement).
            applyPendingSegment();
            return;
            }

      Element3d* el      = _elementDragElement;
      QVector3D newPos   = el->pos();
      QVector3D newRot   = el->rot();
      QVector3D newScale = el->scale();

      // Only create an undo command if something actually changed
      bool changed = (newPos - _elementDragOrigPos).length() > 0.001 ||
                     (newRot - _elementDragOrigRot).length() > 0.001 ||
                     (newScale - _elementDragOrigScale).length() > 0.001;

      if (!changed) {
            // No movement — this is a pure click.  Apply pending
            // segment selection (e.g. clicking on an already-selected
            // polygon to select a segment).
            applyPendingSegment();
            _elementDragElement = nullptr;
            _snapState.reset();
            return;
            }

      // A drag occurred — discard any pending segment selection so the
      // bounding box stays visible after the drag ends.
      _pendingSegmentElement = nullptr;

      if (_project) {
            // Use a single PropertyChangeCommand for pos; the undo command
            // records the old/new pos.  Rot and scale are captured in
            // additional commands pushed together.
            //
            // We push up to three commands atomically.  They are undone
            // and redone in reverse/forward order as a group.
                  {
                  auto cmd = std::make_unique<PropertyChangeCommand>(el, QByteArrayLiteral("pos"),
                                                                     QVariant::fromValue(_elementDragOrigPos),
                                                                     QVariant::fromValue(newPos));
                  _project->pushCommand(std::move(cmd));
                  }
                  {
                  auto cmd = std::make_unique<PropertyChangeCommand>(el, QByteArrayLiteral("rot"),
                                                                     QVariant::fromValue(_elementDragOrigRot),
                                                                     QVariant::fromValue(newRot));
                  _project->pushCommand(std::move(cmd));
                  }
                  {
                  auto cmd = std::make_unique<PropertyChangeCommand>(
                      el, QByteArrayLiteral("scale"), QVariant::fromValue(_elementDragOrigScale),
                      QVariant::fromValue(newScale));
                  _project->pushCommand(std::move(cmd));
                  }
            _project->markDirty();
            }

      // Cam display is NOT refreshed automatically at end of drag.
      // The user triggers it manually via the refresh button.
      if (_project)
            setCamDirty(true);

      _elementDragElement = nullptr;
      _snapState.reset();
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
      if (_config)
            j["config"] = _config->toJson();

      QTextStream out(&file);
      out << QString::fromStdString(j.dump(4));
      file.close();
      }

//---------------------------------------------------------
//   saveAssetsBackup
//    Save current assets to a backup file "assets-bu" in the
//    application data directory.  This is a developer convenience
//    to snapshot known-good assets before risky edits.
//---------------------------------------------------------

bool ZCam::saveAssetsBackup() {
      QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      if (dataDir.isEmpty()) {
            qWarning() << "saveAssetsBackup: no writable AppDataLocation";
            return false;
            }

      QDir dir(dataDir);
      if (!dir.exists())
            dir.mkpath(".");

      QString filePath = dir.filePath("assets-bu");
      QFile file(filePath);
      if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "saveAssetsBackup: failed to open" << filePath << "for writing";
            return false;
            }

      json j;
      if (_recipes)
            j["recipes"] = _recipes->toJson();
      if (_machines)
            j["machines"] = _machines->toJson();
      if (_config)
            j["config"] = _config->toJson();

      QTextStream out(&file);
      out << QString::fromStdString(j.dump(4));
      file.close();

      qDebug() << "saveAssetsBackup: assets saved to" << filePath;
      return true;
      }

//---------------------------------------------------------
//   restoreAssetsBackup
//    Restore assets from the backup file "assets-bu" in the
//    application data directory.  This is a developer convenience
//    to recover from corrupted assets.
//---------------------------------------------------------

bool ZCam::restoreAssetsBackup() {
      QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      if (dataDir.isEmpty()) {
            qWarning() << "restoreAssetsBackup: no writable AppDataLocation";
            return false;
            }

      QString filePath = QDir(dataDir).filePath("assets-bu");
      QFile file(filePath);
      if (!file.exists()) {
            qWarning() << "restoreAssetsBackup: backup file not found:" << filePath;
            return false;
            }
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "restoreAssetsBackup: failed to open" << filePath;
            return false;
            }

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
            if (j.contains("config")) {
                  if (_config)
                        _config->fromJson(j.at("config"));
                  }
            }
      catch (json::parse_error& e) {
            qWarning() << "restoreAssetsBackup: failed to parse" << filePath << ":" << e.what();
            return false;
            }

      qDebug() << "restoreAssetsBackup: assets restored from" << filePath;
      return true;
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
//   pickElement
//    Custom picking: traverse the element tree depth-first and
//    collect all visible Element3d whose world bounding box
//    contains the given world-space point (x, y).  Return the
//    one with the smallest area (innermost).  Only elements
//    that are visible (show == true, ancestorsShow == true),
//    selectable, and have a non-empty path list are considered.
//---------------------------------------------------------

static void collectPickCandidates(Element* root, double x, double y,
                                  std::vector<std::pair<double, Element3d*>>& candidates) {
      if (!root)
            return;
      auto* e3d = qobject_cast<Element3d*>(root);
      if (e3d && e3d->show() && e3d->ancestorsShow() && e3d->selectable()) {
            QRectF wb = e3d->worldBoundingBox();
            if (!wb.isNull() && !wb.isEmpty()) {
                  if (x >= wb.left() && x <= wb.right() && y >= wb.top() && y <= wb.bottom()) {
                        double area = wb.width() * wb.height();
                        candidates.emplace_back(area, e3d);
                        }
                  }
            }
      for (auto* child : root->children())
            collectPickCandidates(child, x, y, candidates);
      }

Element3d* ZCam::pickElement(double x, double y) {
      std::vector<std::pair<double, Element3d*>> candidates;
      collectPickCandidates(_rootElement, x, y, candidates);
      if (candidates.empty())
            return nullptr;
      // Return the element with the smallest area (innermost).
      auto it = std::min_element(candidates.begin(), candidates.end(),
                                 [](const auto& a, const auto& b) { return a.first < b.first; });
      return it->second;
      }

//---------------------------------------------------------
//   mousePress
//---------------------------------------------------------

void ZCam::mousePress(Element3d* element, int buttons, int modifiers, double x, double y) {
      Debug("{} x: {} y: {}", element ? element->name() : "--", x, y);
      // If the same polygon is already selected, defer segment selection
      // to endElementDrag().  This allows click+drag to move the polygon
      // (with bounding box visible) while a pure click (no drag) selects
      // the nearest segment.  Previously, segment selection happened
      // immediately here, which replaced the bounding box with a segment
      // line before the drag started, making the bounding box disappear.
      if (element && element == _currentElement) {
            auto* poly = qobject_cast<Polygon*>(element);
            if (poly) {
                  QVector3D worldPos(x, y, 0.0);
                  int nearest = poly->findNearestSegment(worldPos);
                  if (nearest < 0)
                        return;
                  // Remember the click for later.  If the same segment
                  // is already selected, we will toggle it off.
                  _pendingSegmentElement   = element;
                  _pendingSegmentClickPos  = worldPos;
                  _pendingSegmentToggleOff = (nearest == poly->selectedSegment());
                  // Do NOT select the segment here — let the drag proceed
                  // with the bounding box visible.  The segment will be
                  // selected in endElementDrag() if no drag occurred.
                  return;
                  }
            }
      setCurrentElement(element);
      }

//---------------------------------------------------------
//   startVertexDrag
//    Called from QML when the user starts dragging a handle.
//    Records the original handle position so that endVertexDrag()
//    can create an undo command with old and new positions.
//---------------------------------------------------------

void ZCam::startVertexDrag(Element3d* element, int vertexIndex) {
      if (!element || vertexIndex < 0 || vertexIndex >= element->vertexCount())
            return;
      _vertexDragElement = element;
      _vertexDragIndex   = vertexIndex;
      // Store original position in LOCAL coordinates for the undo command
      _vertexDragOrigPos = element->vertexPos(vertexIndex);
      }

//---------------------------------------------------------
//   dragVertexTo
//    Called from QML during dragging a handle.
//    Sets the handle to the given WORLD position by converting
//    it back to local coordinates via the inverse global matrix.
//---------------------------------------------------------

void ZCam::dragVertexTo(Element3d* element, int vertexIndex, const QVector3D& worldPos) {
      if (!element || vertexIndex < 0 || vertexIndex >= element->vertexCount())
            return;
      // Convert world position to local position
      QMatrix4x4 inv = element->globalMatrix();
      bool ok;
      inv = inv.inverted(&ok);
      if (!ok)
            return;
      QVector3D localPos = inv.map(worldPos);
      element->setVertexPos(vertexIndex, localPos);
      }

//---------------------------------------------------------
//   endVertexDrag
//    Called from QML when the user finishes dragging a handle.
//    Creates and pushes an undo command with the original and final
//    handle positions.
//---------------------------------------------------------

void ZCam::endVertexDrag(Element3d* element, int vertexIndex) {
      if (!element || vertexIndex < 0 || vertexIndex >= element->vertexCount())
            return;
      QVector3D newPos = element->vertexPos(vertexIndex);
      // Only create an undo command if the handle actually moved
      if (std::abs(newPos.x() - _vertexDragOrigPos.x()) < 0.001 &&
          std::abs(newPos.y() - _vertexDragOrigPos.y()) < 0.001) {
            _vertexDragElement = nullptr;
            return;
            }
      if (_project) {
            auto cmd = std::make_unique<HandleDragCommand>(element, vertexIndex, _vertexDragOrigPos, newPos);
            _project->pushCommand(std::move(cmd));
            _project->markDirty();
            }
      // Cam display is NOT refreshed automatically at end of drag.
      // The user triggers it manually via the refresh button.
      if (_project)
            setCamDirty(true);
      _vertexDragElement = nullptr;
      }

//---------------------------------------------------------
//   selectSegment
//    Select a segment of a Polygon element by segment index.
//    The segment is highlighted in the 3D viewport and only its
//    endpoint vertices show handles.  Pass -1 to clear.
//---------------------------------------------------------

void ZCam::selectSegment(Element3d* element, int segmentIndex) {
      if (!element)
            return;
      auto* poly = qobject_cast<Polygon*>(element);
      if (!poly)
            return;
      poly->setSelectedSegment(segmentIndex);
      }

//---------------------------------------------------------
//   clearSegmentSelection
//---------------------------------------------------------

void ZCam::clearSegmentSelection(Element3d* element) {
      if (!element)
            return;
      auto* poly = qobject_cast<Polygon*>(element);
      if (!poly)
            return;
      poly->clearSegmentSelection();
      }

//---------------------------------------------------------
//   selectNearestSegment
//    Find and select the segment closest to the given world position.
//    Returns the selected segment index, or -1 on failure.
//---------------------------------------------------------

int ZCam::selectNearestSegment(Element3d* element, const QVector3D& worldPos) {
      if (!element)
            return -1;
      auto* poly = qobject_cast<Polygon*>(element);
      if (!poly)
            return -1;
      int idx = poly->findNearestSegment(worldPos);
      if (idx >= 0)
            poly->setSelectedSegment(idx);
      return idx;
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
            const Recipe* r = _recipes->recipePtr(i);
            if (r->name() == name)
                  return _recipes->recipePtr(i);
            }
      return nullptr;
      }

//---------------------------------------------------------
//   Config::toJson
//    Serialise all config properties to a JSON object using
//    the propertyjson utility.
//---------------------------------------------------------

nlohmann::json Config::toJson() const {
      nlohmann::json data     = nlohmann::json::object();
      const QMetaObject* meta = this->metaObject();

      auto propNames = propjson::parseAllPropertyNames(_properties);
      for (const auto& [name, type] : propNames)
            propjson::writePropertyToJson(data, this, meta, false, name, type);
      return data;
      }

//---------------------------------------------------------
//   Config::fromJson
//    Deserialise config properties from a JSON object.
//---------------------------------------------------------

bool Config::fromJson(const nlohmann::json& data) {
      const QMetaObject* meta = this->metaObject();
      auto propNames          = propjson::parseAllPropertyNames(_properties);
      for (const auto& [name, type] : propNames)
            propjson::readPropertyFromJson(data, this, meta, false, name, type);
      return true;
      }

//---------------------------------------------------------
//   findFirstVisibleLayer
//    Recursively traverse the element tree to find the first
//    Layer that is currently visible (show == true).
//---------------------------------------------------------

Layer* ZCam::findFirstVisibleLayer(Element* root) const {
      if (!root)
            return nullptr;
      if (auto* layer = qobject_cast<Layer*>(root)) {
            if (layer->show() && layer->ancestorsShow())
                  return layer;
            }
      for (Element* child : root->children()) {
            auto* found = findFirstVisibleLayer(child);
            if (found)
                  return found;
            }
      return nullptr;
      }

//---------------------------------------------------------
//   findCurrentLayer
//    Find the Layer that is the current element itself, or the
//    nearest Layer ancestor of the current element, walking up
//    the parent chain until Cad is reached.
//
//    The search starts at _currentElement. If _currentElement is
//    itself a Layer, it is returned (provided it is visible).
//    Otherwise we walk up through parent() until we either find
//    a Layer or reach the Cad element (which is the container of
//    all layers and therefore the stop marker).
//
//    Returns nullptr if:
//      - there is no current element
//      - no Layer is found in the parent chain before reaching Cad
//      - the found Layer is not visible (show == false or an
//        ancestor has show == false)
//---------------------------------------------------------

Layer* ZCam::findCurrentLayer() const {
      if (!_currentElement)
            return nullptr;

      // Walk up the parent chain from the current element.
      // Stop when we reach a Cad element (the container of layers)
      // or when there are no more parents.
      for (Element* e = _currentElement; e; e = e->parent()) {
            // If we reach Cad, the layers are direct children of Cad,
            // so there is no Layer in this chain.
            if (isType<Cad>(e))
                  return nullptr;

            if (auto* layer = qobject_cast<Layer*>(e)) {
                  if (layer->show() && layer->ancestorsShow())
                        return layer;
                  return nullptr;
                  }
            }
      return nullptr;
      }

//---------------------------------------------------------
//   createRectangle
//    Create a new Rectangle element with size (0, 0) at the
//    given world position and add it to the current Layer (the
//    Layer of the selected element) or the first visible Layer
//    as fallback.  The new rectangle is set as the current element
//    so that vertex handles are displayed.  Returns the new
//    Rectangle or nullptr if no suitable layer was found.
//    The operation is routed through the undo stack so it can
//    be undone/redone.
//---------------------------------------------------------

Element3d* ZCam::createRectangle(double x, double y) {
      if (!_project || !_project->cad())
            return nullptr;

      // Find the layer to host the new rectangle: prefer the layer
      // of the currently selected element, fall back to the first
      // visible layer in the tree.
      Layer* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd = std::make_unique<AddRectangleCommand>(this, layer, x, y);
      cmd->redo(); // apply immediately
      Element3d* rect = cmd->rectangle();
      _project->pushCommand(std::move(cmd));

      // Select the new rectangle so vertex handles appear.
      setCurrentElement(rect);

      _project->markDirty();
      setCamDirty(true);

      return rect;
      }

//---------------------------------------------------------
//   createPolygon
//    Create a new Polygon element at the given world position
//    and add it to the current Layer (the Layer of the selected
//    element) or the first visible Layer as fallback.  The new
//    polygon is set as the current element so that vertex handles
//    are displayed.  Returns the new Polygon or nullptr if no
//    suitable layer was found.  The operation is routed through
//    the undo stack so it can be undone/redone.
//---------------------------------------------------------

Element3d* ZCam::createPolygon(double x, double y) {
      if (!_project || !_project->cad())
            return nullptr;

      Layer* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd = std::make_unique<AddPolygonCommand>(this, layer, x, y);
      cmd->redo(); // apply immediately
      Element3d* poly = cmd->polygon();
      _project->pushCommand(std::move(cmd));

      setCurrentElement(poly);

      _project->markDirty();
      setCamDirty(true);

      return poly;
      }

//---------------------------------------------------------
//   createEllipse
//    Create a new Ellipse element with size (0, 0) at the
//    given world position and add it to the current Layer (the
//    Layer of the selected element) or the first visible Layer
//    as fallback.  The new ellipse is set as the current element
//    so that vertex handles are displayed.  Returns the new Ellipse
//    or nullptr if no suitable layer was found.
//    The operation is routed through the undo stack so it can
//    be undone/redone.
//---------------------------------------------------------

Element3d* ZCam::createEllipse(double x, double y) {
      if (!_project || !_project->cad())
            return nullptr;

      Layer* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd = std::make_unique<AddEllipseCommand>(this, layer, x, y);
      cmd->redo(); // apply immediately
      Element3d* ell = cmd->ellipse();
      _project->pushCommand(std::move(cmd));

      setCurrentElement(ell);

      _project->markDirty();
      setCamDirty(true);

      return ell;
      }

//---------------------------------------------------------
//   createText
//    Create a new Text element at the given world position
//    and add it to the current Layer (the Layer of the selected
//    element) or the first visible Layer as fallback.
//
//    If the currently selected element is itself a Text, its
//    font and appearance properties (fontFamily, pointSize, weight,
//    stretch, letterSpacing, wordSpacing, lineSpacing, align, bold,
//    italic, underline, fill, color, burn, show, mirrorX, mirrorY,
//    lockScale, lineWidth, endType, joinType, scale, rot) are
//    copied to the new Text so that the user gets visual continuity.
//    The "text" string itself is not copied – the new Text starts
//    empty.
//
//    The new text is set as the current element.  Returns the new
//    Text or nullptr if no suitable layer was found.
//    The operation is routed through the undo stack so it can
//    be undone/redone.
//---------------------------------------------------------

Element3d* ZCam::createText(double x, double y) {
      if (!_project || !_project->cad())
            return nullptr;

      Layer* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd = std::make_unique<AddTextCommand>(this, layer, x, y);
      cmd->redo(); // apply immediately
      Element3d* text = cmd->text();

      // If the currently selected element is a Text, copy its font
      // and appearance properties to the new Text.
      if (_currentElement && isType<Text>(_currentElement)) {
            auto* src = qobject_cast<Text*>(_currentElement);
            auto* dst = qobject_cast<Text*>(text);
            if (src && dst) {
                  dst->set_fontFamily(src->fontFamily());
                  dst->set_pointSize(src->pointSize());
                  dst->set_weight(src->weight());
                  dst->set_stretch(src->stretch());
                  dst->set_letterSpacing(src->letterSpacing());
                  dst->set_wordSpacing(src->wordSpacing());
                  dst->set_lineSpacing(src->lineSpacing());
                  dst->set_align(src->align());
                  dst->set_bold(src->bold());
                  dst->set_italic(src->italic());
                  dst->set_underline(src->underline());
                  dst->set_fill(src->fill());
                  dst->setColor(src->color());
                  dst->set_burn(src->burn());
                  dst->set_show(src->show());
                  dst->set_mirrorX(src->mirrorX());
                  dst->set_mirrorY(src->mirrorY());
                  dst->set_lockScale(src->lockScale());
                  dst->set_lineWidth(src->lineWidth());
                  dst->set_endType(src->endType());
                  dst->set_joinType(src->joinType());
                  dst->set_scale(src->scale());
                  dst->set_rot(src->rot());
                  dst->update();
                  }
            }

      _project->pushCommand(std::move(cmd));

      setCurrentElement(text);

      _project->markDirty();
      setCamDirty(true);

      return text;
      }

//---------------------------------------------------------
//   reparentElement
//    Re-parent an element to a new parent Element3d.  The element's
//    local pos/rot/scale are adjusted so that its world-space
//    transform stays the same (the visual position doesn't jump).
//    This is the core of the drag-&-drop grouping mechanism:
//    when the user drops one draggable element onto another, the
//    dropped element becomes a child of the target element.
//
//    Coordinate transformation:
//      Before:  worldPos = oldParentGlobal * localPos
//      After:   worldPos = newParentGlobal * newLocalPos
//      So:      newLocalPos = inverse(newParentGlobal) * oldWorldPos
//
//    The same logic applies to scale and rotation, which are
//    encoded in the matrix and extracted back via decompose().
//---------------------------------------------------------

void ZCam::reparentElement(Element3d* element, Element3d* newParent) {
      if (!element || !newParent || element == newParent)
            return;
      // Prevent re-parenting into one's own descendant
      Element* p = newParent;
      while (p) {
            if (p == element)
                  return;
            p = p->parent();
            }
      // Only Element3d parents can carry transforms
      if (!qobject_cast<Element3d*>(newParent))
            return;

      Element* oldParent = element->parent();
      if (!oldParent)
            return;

      // Compute the element's current world (global) matrix.
      QMatrix4x4 oldGlobal = element->globalMatrix();

      // Compute the new parent's global matrix.
      QMatrix4x4 newParentGlobal = newParent->globalMatrix();
      bool ok                    = false;
      QMatrix4x4 newParentInv    = newParentGlobal.inverted(&ok);
      if (!ok)
            return;

      // The new local matrix maps points from the element's local
      // space to the new parent's local space.
      QMatrix4x4 newLocal = newParentInv * oldGlobal;

      // Decompose the new local matrix into pos, rot, scale.
      // Extract translation: last column (x, y, z)
      QVector3D newPos(newLocal(0, 3), newLocal(1, 3), newLocal(2, 3));

      // Extract scale: length of the first three column vectors.
      // Column 0 = (m(0,0), m(1,0), m(2,0))
      // Column 1 = (m(0,1), m(1,1), m(2,1))
      // Column 2 = (m(0,2), m(1,2), m(2,2))
      float sx = QVector3D(newLocal(0, 0), newLocal(1, 0), newLocal(2, 0)).length();
      float sy = QVector3D(newLocal(0, 1), newLocal(1, 1), newLocal(2, 1)).length();
      float sz = QVector3D(newLocal(0, 2), newLocal(1, 2), newLocal(2, 2)).length();
      QVector3D newScale(sx, sy, sz);

      // Extract rotation: build a pure rotation matrix by dividing
      // out the scale, then convert to euler angles via quaternion.
      QMatrix3x3 rotMat;
      if (sx > 1e-9) {
            rotMat(0, 0) = newLocal(0, 0) / sx;
            rotMat(1, 0) = newLocal(1, 0) / sx;
            rotMat(2, 0) = newLocal(2, 0) / sx;
            }
      if (sy > 1e-9) {
            rotMat(0, 1) = newLocal(0, 1) / sy;
            rotMat(1, 1) = newLocal(1, 1) / sy;
            rotMat(2, 1) = newLocal(2, 1) / sy;
            }
      if (sz > 1e-9) {
            rotMat(0, 2) = newLocal(0, 2) / sz;
            rotMat(1, 2) = newLocal(1, 2) / sz;
            rotMat(2, 2) = newLocal(2, 2) / sz;
            }
      QQuaternion quat = QQuaternion::fromRotationMatrix(rotMat);
      QVector3D newRot = quat.toEulerAngles();

      // Apply the new transforms BEFORE moving the element in the tree.
      // This ensures that when the MoveElementCommand's redo() emits
      // add3dElement, the scene graph rebuilds the element with the
      // correct local transform.
      //
      // Record undo commands for the transform changes so that undo/redo
      // restores the correct pre-reparent transforms.
      QVector3D origPos   = element->pos();
      QVector3D origRot   = element->rot();
      QVector3D origScale = element->scale();

      element->beginBatchUpdate();
      element->set_pos(newPos);
      element->set_rot(newRot);
      element->set_scale(newScale);
      element->endBatchUpdate();

      // Push undo commands for the transform changes.
      // These must be pushed BEFORE the MoveElementCommand so that
      // undo undoes the move first (restoring the old parent), then
      // the transforms (restoring the old pos/rot/scale).
      if (_project) {
                  {
                  auto cmd = std::make_unique<PropertyChangeCommand>(element, QByteArrayLiteral("pos"),
                                                                     QVariant::fromValue(origPos),
                                                                     QVariant::fromValue(newPos));
                  _project->pushCommand(std::move(cmd));
                  }
                  {
                  auto cmd = std::make_unique<PropertyChangeCommand>(element, QByteArrayLiteral("rot"),
                                                                     QVariant::fromValue(origRot),
                                                                     QVariant::fromValue(newRot));
                  _project->pushCommand(std::move(cmd));
                  }
                  {
                  auto cmd = std::make_unique<PropertyChangeCommand>(element, QByteArrayLiteral("scale"),
                                                                     QVariant::fromValue(origScale),
                                                                     QVariant::fromValue(newScale));
                  _project->pushCommand(std::move(cmd));
                  }
            }

      // Reset the drag state since we bypassed endElementDrag().
      _elementDragElement = nullptr;

      // Now move the element in the tree via the undoable command.
      if (_project) {
            int oldRow = 0;
            for (const auto c : oldParent->children()) {
                  if (c == element)
                        break;
                  ++oldRow;
                  }
            Debug("reparentElement: calling moveElement: element={} oldParent={} newParent={}",
                  element->name(), oldParent ? oldParent->name() : "null", newParent->name());
            _project->moveElement(element, newParent, -1);
            }
      else {
            Debug("reparentElement: no project");
            }

      _project->markDirty();
      setCamDirty(true);
      }

//---------------------------------------------------------
//   deleteCurrentElement
//    Delete the current element if it is deletable.
//    The operation is routed through the undo stack via
//    Project::removeElement() so it can be undone/redone.
//    The current element is cleared before deletion to avoid
//    dangling pointer dereferences in QML bindings.
//---------------------------------------------------------

void ZCam::deleteCurrentElement() {
      if (!_currentElement || !_project)
            return;
      // deletable() is defined on Element3d; _currentElement is already
      // an Element3d* so no cast is needed.
      if (!_currentElement->deletable())
            return;
      auto* el = qobject_cast<Element*>(_currentElement);
      if (!el)
            return;
      // Clear the current element BEFORE removing it so QML
      // bindings don't dereference a dangling pointer.
      setCurrentElement(nullptr);
      _project->removeElement(el);
      }

//=========================================================
//  Project lifecycle methods (moved from ProjectManager)
//=========================================================

//---------------------------------------------------------
//   saveLastProjectPath / lastProjectPath
//    Persist the current project path in QSettings so it can be
//    restored on the next application start.
//---------------------------------------------------------

static void saveLastProjectPath(const QString& path) {
      QSettings settings;
      settings.setValue("project/lastPath", path);
      }

static QString lastProjectPath() {
      QSettings settings;
      return settings.value("project/lastPath").toString();
      }

//---------------------------------------------------------
//   newProject
//---------------------------------------------------------

void ZCam::newProject(bool clearPersistedPath) {
      startNewProject(clearPersistedPath);

      auto project = this->project();
      auto fixture = project->fixture();
      auto cam     = project->cam();
      auto cad     = project->cad();
      auto ll      = new LaserLayer(this, fixture);
      auto recipes = this->recipes();
      if (recipes && recipes->recipeCount() > 0)
            ll->set_recipe(recipes->recipePtr(0));
      auto stock = new Stock(this, cam);
      auto layer = new Layer(this, cad);
      auto text  = new Text(this, layer);
      text->setColor("yellow");
      text->set_text("ZCam");

      auto rectangle = new Rectangle(this, layer);
      rectangle->set_size(QVector2D(40.0, 30.0));
      rectangle->set_pos(QVector3D(50.0, 50.0, 0.0));
      rectangle->setColor(QColor("blue"));
      rectangle->set_corner(5.0);
      rectangle->set_lineWidth(1.0);
      rectangle->set_fill(false);

      auto poly = new Polygon(this, layer);
      poly->set_pos(QVector3D(10.0, 25.0, 0.0));
      poly->setColor(QColor("green"));
      poly->set_lineWidth(1.0);
      poly->moveTo({0.0, 0.0});
      poly->lineTo({20.0, 20.0});
      poly->lineTo({10.0, 5.0});
      poly->set_fill(true);

      auto ell = new Ellipse(this, layer);
      ell->set_size(QVector2D(25.0, 25.0));
      ell->set_pos(QVector3D(-30.0, 40.0, 0.0));
      ell->setColor(QColor("magenta"));
      ell->set_lineWidth(1.0);
      ell->set_fill(false);

      ll->setName(QString("LL-%1").arg(layer->name()));
      ll->set_baseElement(layer);

      auto grid = new Grid(this, project);

      // build project tree
      cad->addChild(layer);
      layer->addChild(text);
      layer->addChild(rectangle);
      layer->addChild(poly);
      layer->addChild(ell);
      project->addChild(grid);
      cam->addChild(stock);
      fixture->addChild(ll);

      endNewProject();
      }

//---------------------------------------------------------
//   startNewProject
//---------------------------------------------------------

void ZCam::startNewProject(bool clearPersistedPath) {
      // Caller is responsible for checking unsaved changes via QML dialog
      // before invoking this method.
      if (_project)
            _project->clearUndoStack();
      Element::clearProject(); // clear global name hash

      // Clear the current/hover element pointers BEFORE destroying the old
      // tree.  If these are left pointing at soon-to-be-deleted Element3d
      // objects, any subsequent QML access (e.g. the InspectorPanel binding
      //   element: ZCam.currentElement
      // or the Shape.qml binding
      //   visible: element && ZCam.currentElement === element
      // ) will dereference a dangling pointer and crash in
      // QQmlData::wasDeleted() inside QObjectWrapper::wrap().
      setCurrentElement(nullptr);
      set_hoverElement(nullptr);

      // Synchronously destroy the old element tree before creating new
      // elements.  TreeModel::setRoot(nullptr) schedules deleteLater() on
      // the old root; we must flush those deferred deletes NOW, otherwise
      // the old elements' destructors would run later (after new elements
      // with the same names have been created) and accidentally remove
      // the new elements from the Element::names hash.
      _treeModel->setRoot(nullptr);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      set_rootElement(nullptr);
      set_project(nullptr);
      //
      // construct a demo project
      //
      if (clearPersistedPath)
            saveLastProjectPath(QString());

      auto root = new RootElement(this, nullptr);
      set_rootElement(root);
      auto top = new Project(this, root);
      set_project(top);

      top->setProjectPath(QString());
      top->setDirty(false);

      // Set the default machine from the Config, if configured.
      if (_config && !_config->defaultMachine().isEmpty() && _machines) {
            QStringList model = _machines->machinesModel();
            int idx           = model.indexOf(_config->defaultMachine());
            if (idx >= 0)
                  top->set_machine(_machines->machine(idx));
            }

      auto cad     = new Cad(this, top);
      auto cam     = new Cam(this, top);
      auto fixture = new Fixture(this, cam);
      auto framing = new Framing(this, cam);
      top->addChild(cad);
      top->addChild(cam);
      cam->addChild(fixture);
      cam->addChild(framing);
      connect(top, &Project::updateFraming, framing, &Framing::update);
      }

//---------------------------------------------------------
//   endNewProject
//---------------------------------------------------------

void ZCam::endNewProject() {
      rootElement()->addChild(project());
      update();

      // Notify QML that the grid element may have changed so the
      // background View3D can re-evaluate its GridShape binding.
      if (project())
            emit project() -> gridElementChanged();

      // Re-resolve the Project's Machine* after the project is replaced.
      if (project())
            project()->resolveMachine();

      emit projectCreated();
      // Update CAD layer visibility based on the active fixture.
      // The CAM data is NOT refreshed automatically here because the
      // processTileLines() call inside Cam::updateCam() may need to
      // run createFill() for filled elements (e.g. Text), which can
      // be extremely expensive on the main thread when the recipe
      // has many passes or a small interval.  Instead, the camDirty
      // flag is set so the user can trigger a refresh manually via
      // the Cam refresh button when ready.
      if (project())
            project()->updateCadLayerVisibility();
      setCamDirty(true);
      }

//---------------------------------------------------------
//   update
//    updates the tree view and triggers update of 3DCanvas
//---------------------------------------------------------

void ZCam::update() {
      _treeModel->setRoot(rootElement()); // update project tree view
      set_rootElement(project());         // build and show the scene
      }

//---------------------------------------------------------
//   openProject
//---------------------------------------------------------

bool ZCam::openProject(const QString& path, bool skipCamUpdate) {
      if (path.isEmpty()) {
            Warning("ZCam::openProject: empty path");
            return false;
            }
      if (!readProjectFile(path.toStdString(), skipCamUpdate)) {
            Warning("ZCam::openProject: failed to read", path);
            return false;
            }
      _project->setProjectPath(path);
      _project->clearUndoStack();
      _project->setDirty(false);
      saveLastProjectPath(path);
      emit projectLoaded(path);
      // cam data is fresh after loading a project, unless the CAM update
      // was skipped (e.g. at startup) — in that case, mark it as dirty
      // so the refresh button is enabled and the user can update manually.
      setCamDirty(skipCamUpdate);
      return true;
      }

//---------------------------------------------------------
//   save
//---------------------------------------------------------

bool ZCam::save() {
      if (!_project || _project->projectPath().isEmpty())
            return false; // QML should call saveAs with a chosen path
      if (!writeProjectFile(_project->projectPath().toStdString()))
            return false;
      _project->setDirty(false);
      saveLastProjectPath(_project->projectPath());
      emit projectSaved(_project->projectPath());
      return true;
      }

//---------------------------------------------------------
//   saveAs
//---------------------------------------------------------

bool ZCam::saveAs(const QString& path) {
      if (path.isEmpty() || !_project)
            return false;
      if (!writeProjectFile(path.toStdString()))
            return false;
      _project->setProjectPath(path);
      _project->setDirty(false);
      saveLastProjectPath(path);
      emit projectSaved(path);
      return true;
      }

//---------------------------------------------------------
//   importFile
//---------------------------------------------------------

bool ZCam::importFile(const QString& path) {
      if (path.isEmpty())
            return false;
      QFileInfo fi(path);
      QString suffix = fi.suffix().toLower();
      if (suffix == QStringLiteral("svg"))
            importSvg(path);
      else if (suffix == QStringLiteral("dxf") || suffix == QStringLiteral("dwg"))
            return DxfImport::import(this, path);
      else {
            Warning("ZCam::importFile: unsupported file type: {}", suffix);
            return false;
            }
      _project->markDirty();
      setCamDirty(true);
      return true;
      }

//---------------------------------------------------------
//   restoreLastProject
//    Called at startup to re-open the project that was open when
//    the application was last closed.
//---------------------------------------------------------

bool ZCam::restoreLastProject() {
      QString path = lastProjectPath();
      if (path.isEmpty())
            return false;
      QFileInfo fi(path);
      if (!fi.exists() || !fi.isFile())
            return false;
      // Skip CAM data update at startup — the user can trigger a
      // refresh manually via the Cam button when needed.
      return openProject(path, /*skipCamUpdate=*/true);
      }

//---------------------------------------------------------
//   writeProjectFile
//---------------------------------------------------------

bool ZCam::writeProjectFile(const std::string& path) {
      Project* tl = _project;
      if (!tl) {
            Warning("no toplevel");
            return false;
            }
      std::ofstream f(path);
      if (!f.is_open()) {
            Warning("ZCam: cannot open for writing:", path);
            return false;
            }
      // Minimal JSON skeleton – replace with full scene serialisation
      nlohmann::ordered_json root;
      root["version"]     = "1.0";
      root["application"] = "zcam";
      root["toplevel"]    = tl->toJson();
      f << root.dump(4);
      return true;
      }

//---------------------------------------------------------
//   readProjectFile
//---------------------------------------------------------

bool ZCam::readProjectFile(const std::string& path, bool skipCamUpdate) {
      std::ifstream f(path);
      if (!f.is_open()) {
            Warning("ZCam: cannot open for reading:", path);
            return false;
            }

      json jdata;
      try {
            f >> jdata;
            std::string version = jdata.value("version", "unknown");
            Debug("ZCam: loaded project version <{}>", version);
            //
            //  destroy old project
            //
            //  Clear the global name registry first, then synchronously
            //  delete the old element tree.  We cannot rely on
            //  deleteLater() here because it is asynchronous: the old
            //  elements would still be alive (and registered in the
            //  Element::names hash) when the new tree is built below,
            //  causing spurious name collisions.
            //
            Element::clearProject();

            // Clear the current/hover element pointers BEFORE destroying
            // the old tree to avoid dangling-pointer dereferences in QML.
            // See newProject() for a detailed explanation.
            setCurrentElement(nullptr);
            set_hoverElement(nullptr);

            _treeModel->setRoot(nullptr);
            set_rootElement(nullptr); // detach scene
            set_project(nullptr);

            // Process pending deleteLater() calls so the old tree is
            // truly gone before we build the new one.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

            auto root = new RootElement(this, nullptr);
            auto top  = new Project(this, root);
            root->addChild(top);
            set_project(top);
            top->fromJson(jdata.at("toplevel"));
            _treeModel->setRoot(root);
            set_rootElement(project()); // build and show the scene

            // Notify QML that the grid element may have changed.
            emit top->gridElementChanged();

            // Re-resolve the Project's Machine* after loading.
            if (project())
                  project()->resolveMachine();

            // Initial CAD layer visibility update and CAM refresh so
            // display geometry is populated after load.
            // When skipCamUpdate is true (e.g. at startup when restoring
            // the last project), the expensive Cam::updateCam() call is
            // skipped — the user can trigger it manually via the Cam
            // refresh button.
            if (project()) {
                  project()->updateCadLayerVisibility();
                  if (!skipCamUpdate && project()->cam())
                        project()->cam()->updateCam();
                  }
            auto func = [](this auto& self, Element* e) -> void {
                  for (auto c : e->children()) {
                        c->fixup();
                        self(c);
                        }
                  };
            func(project());
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("JSON parse error:", err.what());
            return false;
            }

      return true;
      }
