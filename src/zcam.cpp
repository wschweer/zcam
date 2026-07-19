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

#include <cmath>
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
            if (_laser)
                  _laser->shutdown();
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
//    element. The change goes through ProjectManager so it
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
      _projectManager->changeProperty(text, "fontFamily", family);
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

      if (_projectManager) {
            auto cmd = std::make_unique<PropertyChangeCommand>(
                element, QByteArrayLiteral("pos"), QVariant::fromValue(oldPos), QVariant::fromValue(newPos));
            cmd->redo(); // apply the new position immediately
            _projectManager->pushCommand(std::move(cmd));
            _projectManager->markDirty();
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
      QVector3D newRot = element->rot() + deltaRotation;
      element->set_rot(newRot);
      }

//---------------------------------------------------------
//   scaled
//    Called from QML when an element is scaled in the 3D viewport.
//    Updates the element's scale property directly (live update).
//    The undo record is created once at endElementDrag().
//---------------------------------------------------------

void ZCam::scaled(Element3d* element, const QVector3D& scaleFactor, int modifiers) {
      if (!element || !element->draggable())
            return;
      // The lockScale enforcement is handled by Element3d::set_scale(),
      // so we just compute the new scale and let set_scale() apply
      // the lock constraints.
      QVector3D cur = element->scale();
      QVector3D newScale(cur.x() * scaleFactor.x(), cur.y() * scaleFactor.y(), cur.z() * scaleFactor.z());
      element->set_scale(newScale);
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
      if (!_elementDragElement)
            return;
      Element3d* el      = _elementDragElement;
      QVector3D newPos   = el->pos();
      QVector3D newRot   = el->rot();
      QVector3D newScale = el->scale();

      // Only create an undo command if something actually changed
      bool changed = (newPos - _elementDragOrigPos).length() > 0.001 ||
                     (newRot - _elementDragOrigRot).length() > 0.001 ||
                     (newScale - _elementDragOrigScale).length() > 0.001;

      if (changed && _projectManager) {
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
                  _projectManager->pushCommand(std::move(cmd));
                  }
                  {
                  auto cmd = std::make_unique<PropertyChangeCommand>(el, QByteArrayLiteral("rot"),
                                                                     QVariant::fromValue(_elementDragOrigRot),
                                                                     QVariant::fromValue(newRot));
                  _projectManager->pushCommand(std::move(cmd));
                  }
                  {
                  auto cmd = std::make_unique<PropertyChangeCommand>(
                      el, QByteArrayLiteral("scale"), QVariant::fromValue(_elementDragOrigScale),
                      QVariant::fromValue(newScale));
                  _projectManager->pushCommand(std::move(cmd));
                  }
            _projectManager->markDirty();
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
//   mousePress
//---------------------------------------------------------

void ZCam::mousePress(Element3d* element, int buttons, int modifiers, double x, double y) {
      Debug("{} x: {} y: {}", element ? element->name() : "--", x, y);
      // If the same polygon is already selected, select the nearest
      // segment instead of re-selecting the whole polygon.
      if (element && element == _currentElement) {
            auto* poly = qobject_cast<Polygon*>(element);
            if (poly) {
                  QVector3D worldPos(x, y, 0.0);
                  int nearest = poly->findNearestSegment(worldPos);
                  if (nearest < 0)
                        return;
                  // If the same segment is already selected, toggle it off.
                  if (nearest == poly->selectedSegment())
                        poly->clearSegmentSelection();
                  else
                        poly->setSelectedSegment(nearest);
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
      if (_projectManager) {
            auto cmd = std::make_unique<HandleDragCommand>(element, vertexIndex, _vertexDragOrigPos, newPos);
            _projectManager->pushCommand(std::move(cmd));
            _projectManager->markDirty();
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
      _projectManager->pushCommand(std::move(cmd));

      // Select the new rectangle so vertex handles appear.
      setCurrentElement(rect);

      _projectManager->markDirty();
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
      _projectManager->pushCommand(std::move(cmd));

      setCurrentElement(poly);

      _projectManager->markDirty();
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
      _projectManager->pushCommand(std::move(cmd));

      setCurrentElement(ell);

      _projectManager->markDirty();
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

      _projectManager->pushCommand(std::move(cmd));

      setCurrentElement(text);

      _projectManager->markDirty();
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
      if (_projectManager) {
                  {
                  auto cmd = std::make_unique<PropertyChangeCommand>(element, QByteArrayLiteral("pos"),
                                                                     QVariant::fromValue(origPos),
                                                                     QVariant::fromValue(newPos));
                  _projectManager->pushCommand(std::move(cmd));
                  }
                  {
                  auto cmd = std::make_unique<PropertyChangeCommand>(element, QByteArrayLiteral("rot"),
                                                                     QVariant::fromValue(origRot),
                                                                     QVariant::fromValue(newRot));
                  _projectManager->pushCommand(std::move(cmd));
                  }
                  {
                  auto cmd = std::make_unique<PropertyChangeCommand>(element, QByteArrayLiteral("scale"),
                                                                     QVariant::fromValue(origScale),
                                                                     QVariant::fromValue(newScale));
                  _projectManager->pushCommand(std::move(cmd));
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

      _projectManager->markDirty();
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
