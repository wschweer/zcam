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
#include <functional>
#include <QtQuick3D/private/qquick3dcamera_p.h>
#include <QtQuick3D/private/qquick3dnode_p.h>
#include <QtQuick3D/private/qquick3dviewport_p.h>
#include "project.h"
#include "cad.h"
#include "cameraelement.h"
#include "text.h"
#include "group.h"
#include "recipe.h"
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
#include "brepimport.h"
#include "importipc2581.h"

#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDebug>
#include <QCoreApplication>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <tuple>
#include <QQuaternion>
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
      _recipes  = new LaserReceipes(this);

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

      // When the active machine changes, update the recipe machine-type filter
      // so only recipes for the current machine type are shown.
      connect(this, &ZCam::projectChanged, this, [this]() {
            if (_project) {
                  // Disconnect any previous machineChanged handler from the
                  // old project, then connect to the new one.
                  disconnect(_project, &Project::machineChanged, this, nullptr);
                  connect(_project, &Project::machineChanged, this, [this]() {
                        if (_recipes && _project && _project->machine())
                              _recipes->set_machineType(_project->machine()->type());
                        });
                  }
            // Also apply immediately for the current project
            if (_recipes && _project && _project->machine())
                  _recipes->set_machineType(_project->machine()->type());
            });

      // Reload machines and recipes when their configured directory changes
      // at runtime (e.g. via the Config Panel).
      if (_config) {
            connect(_config, &Config::machinesDirectoryChanged, this, [this]() {
                  if (_machines)
                        _machines->loadFromDirectory(machinesDirectory());
                  });
            connect(_config, &Config::recipesDirectoryChanged, this, [this]() {
                  if (_recipes)
                        _recipes->loadFromDirectory(recipesDirectory(), _recipes->machineType());
                  });
            }

      // When a recipe's content changes (e.g. laser parameters edited,
      // layer added/removed), check whether any Recipe (LaserLayer) element
      // in the current project references that recipe.  If so, mark the
      // CAM data as dirty so the user knows a refresh is needed.
      connect(_recipes, &LaserReceipes::recipeChanged, this, [this](int idx) {
            if (!_project || !_recipes || !_rootElement)
                  return;
            LaserRecipe* changedRecipe = _recipes->recipePtr(idx);
            if (!changedRecipe)
                  return;
            // Traverse the project tree looking for Recipe (LaserLayer)
            // elements whose recipe pointer matches the changed recipe.
            bool found                         = false;
            std::function<void(Element*)> walk = [&](Element* e) {
                  if (found)
                        return;
                  auto* ll = qobject_cast<Recipe*>(e);
                  if (ll && ll->recipe() == changedRecipe) {
                        found = true;
                        return;
                        }
                  for (auto* c : e->children())
                        walk(c);
                  };
            walk(_rootElement);
            if (found)
                  setCamDirty(true);
            });

      // When the recipe model structure changes (recipes added, removed,
      // or reloaded from disk), recipe pointers held by Recipe elements
      // may become invalid or point to different recipes.  Mark CAM dirty
      // so the user knows a refresh is needed.
      connect(_recipes, &LaserReceipes::recipeModelChanged, this, [this]() { setCamDirty(true); });

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
      // Early return only if both are the same AND no multi-selection
      // needs to be cleared.  When el is null and _selectedElements
      // is non-empty, we must still proceed to clear the selection.
      if (el == oldElement && (el || _selectedElements.isEmpty()))
            return;
      // Clear segment selection on the old element when switching away.
      if (oldElement) {
            auto* oldPoly = qobject_cast<Polygon*>(oldElement);
            if (oldPoly && oldPoly->selectedSegment() >= 0)
                  oldPoly->clearSegmentSelection();
            }
      _currentElement = el;
      // When selecting a single element (not via lasso) or when
      // deselecting (el == nullptr, e.g. clicking on empty canvas),
      // clear the lasso multi-selection so the two selection modes
      // don't overlap.  When el is in _selectedElements (e.g. lassoSelect
      // sets the first element as currentElement), keep the selection.
      if (!_selectedElements.isEmpty() && (!el || !_selectedElements.contains(el))) {
            auto old = _selectedElements;
            _selectedElements.clear();
            for (auto* e : old)
                  emit e->curColorChanged();
            emit selectedElementsChanged();
            }
      emit currentElementChanged();
      // Signal color changes so the 3D view updates highlight colors.
      if (oldElement)
            emit oldElement->curColorChanged();
      if (el)
            emit el->curColorChanged();
      // When the new selection is a Group, refresh it now so its
      // selection bounding box is up-to-date when the bbox overlay
      // becomes visible.
      if (auto* group = qobject_cast<Group*>(el))
            group->update();
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
            _project->undo()->beginMacro();
            auto cmd = new PropertyChangeCommand(this, element, "pos", QVariant::fromValue(oldPos),
                                                 QVariant::fromValue(newPos));
            _project->undo()->push(cmd);
            _project->undo()->endMacro();
            }
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
//   defaultMachinesDirectory
//---------------------------------------------------------

QString ZCam::defaultMachinesDirectory() {
      return QStringLiteral("~/ZCam/machines");
      }

//---------------------------------------------------------
//   defaultRecipesDirectory
//---------------------------------------------------------

QString ZCam::defaultRecipesDirectory() {
      return QStringLiteral("~/ZCam/recipes");
      }

//---------------------------------------------------------
//   defaultArtworkDirectory
//---------------------------------------------------------

QString ZCam::defaultArtworkDirectory() {
      return QStringLiteral("~/ZCam/artwork");
      }

//---------------------------------------------------------
//   defaultIconDirectory
//---------------------------------------------------------

QString ZCam::defaultIconDirectory() {
      return QStringLiteral("~/ZCam/icons");
      }

//---------------------------------------------------------
//   expandPath
//    Expand a leading '~' to the user's home directory.
//    Returns the path unchanged if it does not start with '~'.
//---------------------------------------------------------

QString ZCam::expandPath(const QString& path) {
      if (path.startsWith('~'))
            return QDir::homePath() + path.mid(1);
      return path;
      }

//---------------------------------------------------------
//   machinesDirectory
//    Return the configured machines directory, or the default.
//    A leading '~' is expanded to the user's home directory.
//---------------------------------------------------------

QString ZCam::machinesDirectory() const {
      if (_config && !_config->machinesDirectory().isEmpty())
            return expandPath(_config->machinesDirectory());
      return expandPath(defaultMachinesDirectory());
      }

//---------------------------------------------------------
//   recipesDirectory
//    Return the configured recipes directory, or the default.
//---------------------------------------------------------

QString ZCam::recipesDirectory() const {
      if (_config && !_config->recipesDirectory().isEmpty())
            return expandPath(_config->recipesDirectory());
      return expandPath(defaultRecipesDirectory());
      }

//---------------------------------------------------------
//   loadAssets
//    Load config from assets.json in the AppDataLocation,
//    then load machines and recipes from their individual
//    directories.
//---------------------------------------------------------

void ZCam::loadAssets() {
      // Load config from assets.json (still a single file)
      QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      json legacyAssets;
      if (!dataDir.isEmpty()) {
            QString filePath = QDir(dataDir).filePath("assets.json");
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                  QByteArray data = file.readAll();
                  file.close();
                  try {
                        legacyAssets = json::parse(data.toStdString());
                        if (legacyAssets.contains("config")) {
                              if (_config)
                                    _config->fromJson(legacyAssets.at("config"));
                              }
                        }
                  catch (json::parse_error& e) {
                        qWarning() << "Failed to parse assets.json:" << e.what();
                        }
                  }
            }

      // If the config did not specify explicit directories, populate
      // the Config properties with the default values so they are visible
      // in the Config Panel and persisted on save.
      if (_config) {
            if (_config->machinesDirectory().isEmpty())
                  _config->set_machinesDirectory(defaultMachinesDirectory());
            if (_config->recipesDirectory().isEmpty())
                  _config->set_recipesDirectory(defaultRecipesDirectory());
            if (_config->artworkDirectory().isEmpty())
                  _config->set_artworkDirectory(defaultArtworkDirectory());
            if (_config->iconDirectory().isEmpty())
                  _config->set_iconDirectory(defaultIconDirectory());
            }

      // Load machines from individual files in the machines directory
      if (_machines)
            _machines->loadFromDirectory(machinesDirectory());

      // Migrate: if the machines directory was empty but the legacy
      // assets.json contained machines, import them and save to the
      // directory so the migration is permanent.
      if (_machines && _machines->machinesModel().isEmpty() && legacyAssets.contains("machines")) {
            _machines->fromJson(legacyAssets.at("machines"));
            _machines->saveToDirectory(machinesDirectory());
            }

      // Load recipes from individual files in the recipes directory
      if (_recipes)
            _recipes->loadFromDirectory(recipesDirectory());

      // Migrate: if the recipes directory was empty but the legacy
      // assets.json contained recipes, import them and save to the
      // directory so the migration is permanent.
      if (_recipes && _recipes->recipeCount() == 0 && legacyAssets.contains("recipes")) {
            _recipes->fromJson(legacyAssets.at("recipes"));
            // Save migrated recipes under the root (no machineType filter)
            // so they are available before a machine is selected.
            _recipes->saveToDirectory(recipesDirectory());
            }
      }

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
      // (0,0) in local coords.  In world (root) space this maps to the
      // translation part of the element's globalMatrix().  The snap
      // computation is done in world space so the reference point lands
      // exactly on a grid line intersection, regardless of any parent
      // transforms (Layer position/rotation/scale).  The resulting world
      // position is then converted back to parent-local space for
      // element->set_pos().
      Grid* grid = nullptr;
      if (_project)
            grid = qobject_cast<Grid*>(_project->gridElement());
      if (grid && grid->snap()) {
            double spacing = grid->minorSpacing();
            if (spacing > 0.0) {
                  double halfSpacing = spacing / 2.0;

                  // Current world position of the reference point (0,0 local)
                  // = translation column of the element's globalMatrix().
                  QMatrix4x4 gm = element->globalMatrix();
                  QVector3D worldRef(gm(0, 3), gm(1, 3), gm(2, 3));

                  // Desired world position after applying the world delta.
                  double curWX = worldRef.x();
                  double curWY = worldRef.y();
                  double newWX = curWX + delta.x();
                  double newWY = curWY + delta.y();

                  // X axis snap (world space)
                  if (_snapState.activeX) {
                        _snapState.excessX += delta.x();
                        if (std::abs(_snapState.excessX) > halfSpacing) {
                              _snapState.activeX = false;
                              newWX              = curWX + delta.x();
                              }
                        else {
                              newWX = curWX;
                              }
                        }
                  else {
                        double oldLine = std::round(curWX / spacing);
                        double newLine = std::round(newWX / spacing);
                        if (newLine != oldLine) {
                              _snapState.activeX = true;
                              _snapState.excessX = newWX - newLine * spacing;
                              newWX              = newLine * spacing;
                              }
                        }

                  // Y axis snap (world space)
                  if (_snapState.activeY) {
                        _snapState.excessY += delta.y();
                        if (std::abs(_snapState.excessY) > halfSpacing) {
                              _snapState.activeY = false;
                              newWY              = curWY + delta.y();
                              }
                        else {
                              newWY = curWY;
                              }
                        }
                  else {
                        double oldLine = std::round(curWY / spacing);
                        double newLine = std::round(newWY / spacing);
                        if (newLine != oldLine) {
                              _snapState.activeY = true;
                              _snapState.excessY = newWY - newLine * spacing;
                              newWY              = newLine * spacing;
                              }
                        }

                  // Convert the snapped world position back to parent-local
                  // coordinates.  The element's local-to-world transform is:
                  //   world = parentGlobal * localMatrix(pos, rot, scale)
                  // The reference point (0,0,0) in local space maps to
                  // element->pos() through the local matrix (which only
                  // translates for the reference point since rotation and
                  // scale leave the origin unchanged).  So:
                  //   worldRef = parentGlobal.map(element->pos())
                  // Therefore:
                  //   newLocalPos = parentGlobalInv.map(snappedWorldRef)
                  QVector3D newWorldRef(newWX, newWY, worldRef.z() + delta.z());
                  QVector3D newLocalPos = newWorldRef;
                  if (auto* p = qobject_cast<Element3d*>(element->parent())) {
                        QMatrix4x4 parentGlobal = p->globalMatrix();
                        bool ok                 = false;
                        QMatrix4x4 parentInv    = parentGlobal.inverted(&ok);
                        if (ok)
                              newLocalPos = parentInv.map(newWorldRef);
                        }

                  // Derive the marker position from the exact same
                  // parent-local value that is assigned to pos below:
                  //   worldRefPos = parentGlobal * newLocalPos
                  // reproduces the future position of the element origin
                  // in world space — including the parent's scale,
                  // rotation and translation.  Computing the marker
                  // position from the element's own globalMatrix() would
                  // be wrong whenever the parent has a non-trivial
                  // rotation: the local matrix is applied BEFORE the
                  // parent rotation, so a world-space drag delta maps to
                  // a different pos delta direction and the marker drifts
                  // away from the element origin, eventually leaving the
                  // view ("cross disappears").
                  QVector3D worldRefPos = newWorldRef;
                  if (auto* p = qobject_cast<Element3d*>(element->parent()))
                        worldRefPos = p->globalMatrix().map(newLocalPos);
                  if (worldRefPos != _snapState.refPos) {
                        _snapState.refPos = worldRefPos;
                        emit snapRefPosChanged();
                        }
                  element->set_pos(newLocalPos);
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
      // Seed the marker position with the live world position of the
      // element origin so the cross appears at the element (not at the
      // world origin (0,0)) before the first snapped drag event.
      _snapState.refPos = element->globalMatrix().map(QVector3D(0, 0, 0));
      _snapDragActive   = true;
      emit snapDragActiveChanged();
      _project->undo()->beginMacro();
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
            //
            // CRITICAL: endMacro() must be called to balance the
            // beginMacro() from startElementDrag().  Without this,
            // curCmd remains active and the next beginMacro() fails
            // with Fatal("already active"), corrupting all subsequent
            // undo operations.
            applyPendingSegment();
            _elementDragElement = nullptr;
            _snapDragActive     = false;
            emit snapDragActiveChanged();
            // Roll back the empty macro (0 children → endMacro will
            // delete it instead of pushing it onto the list).
            _project->undo()->endMacro(true);
            return;
            }

      // A drag occurred — discard any pending segment selection so the
      // bounding box stays visible after the drag ends.
      _pendingSegmentElement = nullptr;

      // Use a single PropertyChangeCommand for pos; the undo command
      // records the old/new pos.  Rot and scale are captured in
      // additional commands pushed together.
      //
      // We push up to three commands atomically.  They are undone
      // and redone in reverse/forward order as a group.

      UndoCommand* cmd;
      cmd = new PropertyChangeCommand(this, el, "pos", QVariant::fromValue(_elementDragOrigPos),
                                      QVariant::fromValue(newPos));
      _project->undo()->push(cmd);
      cmd = new PropertyChangeCommand(this, el, "rot", QVariant::fromValue(_elementDragOrigRot),
                                      QVariant::fromValue(newRot));
      _project->undo()->push(cmd);
      cmd = new PropertyChangeCommand(this, el, "scale", QVariant::fromValue(_elementDragOrigScale),
                                      QVariant::fromValue(newScale));
      _project->undo()->push(cmd);

      _elementDragElement = nullptr;
      _snapDragActive     = false;
      emit snapDragActiveChanged();

      _project->undo()->endMacro();
      emit elementDragEnded();
      }

//---------------------------------------------------------
//   snapRefPos
//    World position used to place the snap reference-point cross.
//    While a drag with grid snap is in progress this returns the
//    recorded snap state position (derived from the same
//    parent-local pos that is assigned to the element — see
//    dragged()); otherwise the live position of the reference point
//    (0,0 local) of the element currently being dragged, or the
//    recorded value when idle.
//---------------------------------------------------------

QVector3D ZCam::snapRefPos() const {
      if (_elementDragElement) {
            if (_snapState.activeX || _snapState.activeY)
                  return _snapState.refPos;
            return _elementDragElement->globalMatrix().map(QVector3D(0, 0, 0));
            }
      return _snapState.refPos;
      }

//---------------------------------------------------------
//   saveAssets
//    Save config to assets.json and machines/recipes to their
//    individual directories.
//---------------------------------------------------------

void ZCam::saveAssets() {
      // Save config to assets.json (still a single file)
      QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      if (!dataDir.isEmpty()) {
            QDir dir(dataDir);
            if (!dir.exists())
                  dir.mkpath(".");

            QString filePath = dir.filePath("assets.json");
            QFile file(filePath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                  json j;
                  if (_config)
                        j["config"] = _config->toJson();
                  QTextStream out(&file);
                  out << QString::fromStdString(j.dump(4));
                  file.close();
                  }
            }

      // Save machines to individual files
      if (_machines)
            _machines->saveToDirectory(machinesDirectory());

      // Save recipes to individual files
      if (_recipes)
            _recipes->saveToDirectory(recipesDirectory(), _recipes->machineType());

      emit assetsSaved();
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
//   collectPickCandidates
//    Depth-first traversal collecting all Element3d whose world
//    bounding box contains the point (x, y).  A candidate must be
//    visible on the 3D canvas: show, ancestorsShow, selectable and
//    visible() (has an on-canvas representation).  Non-rendered
//    containers (Project, Fixture, ...) are excluded.  Each
//    candidate stores its tree depth so that, when several
//    elements have the same bounding-box area (a container whose
//    bounding box is derived from its single child), the deepest
//    element — the actual shape — wins over its ancestors.
//---------------------------------------------------------

static void collectPickCandidates(Element* root, double x, double y, int depth,
                                  std::vector<std::tuple<double, int, Element3d*>>& candidates) {
      if (!root)
            return;
      auto* e3d = qobject_cast<Element3d*>(root);
      if (e3d && e3d->show() && e3d->ancestorsShow() && e3d->selectable() && e3d->visible()) {
            QRectF wb = e3d->worldBoundingBox();
            if (!wb.isNull() && !wb.isEmpty()) {
                  if (x >= wb.left() && x <= wb.right() && y >= wb.top() && y <= wb.bottom()) {
                        double area = wb.width() * wb.height();
                        candidates.emplace_back(area, depth, e3d);
                        }
                  }
            }
      for (auto* child : root->children())
            collectPickCandidates(child, x, y, depth + 1, candidates);
      }

//---------------------------------------------------------
//   pickElement
//    Return the innermost candidate (smallest bounding-box area).
//    On equal areas the deepest tree node wins, then selection
//    cycling: clicking again on the already-selected element
//    cycles to the next-larger candidate (usually its parent
//    group), wrapping around at the outermost one.
//---------------------------------------------------------

Element3d* ZCam::pickElement(double x, double y) {
      std::vector<std::tuple<double, int, Element3d*>> candidates;
      collectPickCandidates(_rootElement, x, y, 0, candidates);
      if (candidates.empty())
            return nullptr;
      // Sort by area ascending (innermost first).  On equal areas
      // the deeper tree node (larger depth) comes first so the
      // innermost shape wins over its ancestor containers.
      std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            if (std::get<0>(a) != std::get<0>(b))
                  return std::get<0>(a) < std::get<0>(b);
            return std::get<1>(a) > std::get<1>(b);
            });
      if (_currentElement) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                  if (std::get<2>(candidates[i]) == _currentElement) {
                        if (i + 1 < candidates.size())
                              return std::get<2>(candidates[i + 1]);
                        return std::get<2>(candidates[0]);
                        }
                  }
            }
      return std::get<2>(candidates[0]);
      }

//---------------------------------------------------------
//   collectRayPickCandidates
//    Depth-first traversal collecting every visible, selectable
//    Element3d whose world 3D bounding box is intersected by the
//    pick ray, using the slab method.  The hit parameter t
//    (distance along the ray) is stored with each candidate.
//---------------------------------------------------------

static void collectRayPickCandidates(Element* root, const QVector3D& origin, const QVector3D& dir, int depth,
                                     std::vector<std::tuple<float, int, Element3d*>>& candidates) {
      if (!root)
            return;
      auto* e3d = qobject_cast<Element3d*>(root);
      if (e3d && e3d->show() && e3d->ancestorsShow() && e3d->selectable() && e3d->visible()) {
            QVector3D bMin, bMax;
            e3d->worldBoundingBox3D(bMin, bMax);
            if (bMin.x() <= bMax.x()) {
                  // Slab test: intersect the ray with the three axis
                  // planes pairs; the box is hit iff the largest entry
                  // parameter does not exceed the smallest exit one.
                  float tNear = std::numeric_limits<float>::lowest();
                  float tFar  = std::numeric_limits<float>::max();
                  bool hit    = true;
                  for (int axis = 0; axis < 3; ++axis) {
                        float o = origin[axis], d = dir[axis];
                        float lo = bMin[axis], hi = bMax[axis];
                        if (qFuzzyIsNull(d)) {
                              if (o < lo || o > hi) {
                                    hit = false;
                                    break;
                                    }
                              continue;
                              }
                        float t0 = (lo - o) / d;
                        float t1 = (hi - o) / d;
                        if (t0 > t1)
                              std::swap(t0, t1);
                        tNear = std::max(tNear, t0);
                        tFar  = std::min(tFar, t1);
                        if (tNear > tFar) {
                              hit = false;
                              break;
                              }
                        }
                  if (hit && tFar >= 0.0f)
                        candidates.emplace_back(std::max(tNear, 0.0f), depth, e3d);
                  }
            }
      for (auto* child : root->children())
            collectRayPickCandidates(child, origin, dir, depth + 1, candidates);
      }

//---------------------------------------------------------
//   debugRayPick
//    Diagnosis helper: log all elements along the pick ray —
//    which are skipped by the visibility gate, which boxes are
//    hit and at what ray parameter.
//---------------------------------------------------------

void ZCam::debugRayPick(const QVector3D& origin, const QVector3D& dir) {
      std::function<void(Element*, int)> walk = [&](Element* e, int depth) {
            if (!e)
                  return;
            if (auto* e3d = qobject_cast<Element3d*>(e)) {
                  QVector3D bMin, bMax;
                  e3d->worldBoundingBox3D(bMin, bMax);
                  Debug("  [{}] vis={}/{} sel={} show={}/{} box=({:.1f},{:.1f},{:.1f})..({:.1f},{:.1f},{:.1f})", depth,
                        e3d->visible(), e3d->draggable(), e3d->selectable(), e3d->show(), e3d->ancestorsShow(),
                        bMin.x(), bMin.y(), bMin.z(), bMax.x(), bMax.y(), bMax.z());
                  }
            for (auto* c : e->children())
                  walk(c, depth + 1);
            };
      Debug("debugRayPick: origin=({:.1f},{:.1f},{:.1f}) dir=({:.3f},{:.3f},{:.3f})", origin.x(), origin.y(),
            origin.z(), dir.x(), dir.y(), dir.z());
      walk(_rootElement, 0);
      std::vector<std::tuple<float, int, Element3d*>> candidates;
      collectRayPickCandidates(_rootElement, origin, dir.normalized(), 0, candidates);
      std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return std::get<0>(a) < std::get<0>(b);
            });
      Debug("  {} candidate(s):", candidates.size());
      for (const auto& [t, depth, el] : candidates)
            Debug("    t={:.1f} depth={} name={}", t, depth, el->name().toUtf8().constData());
      }

//---------------------------------------------------------
//   pickAt
//    Convenience helper for QML: unproject the viewport point
//    (x, y in pixels) through the view's camera and root node
//    and pick with the resulting ray.  The unprojection uses
//    the official QQuick3D C++ API so the pick ray is the same
//    ray the internal picker would use — including the y-flip
//    between View3D viewport coordinates and scene coordinates.
//---------------------------------------------------------

Element3d* ZCam::pickAt(QObject* view3d, QObject* rootNode, double x, double y) {
      auto* view = qobject_cast<QQuick3DViewport*>(view3d);
      auto* root = qobject_cast<QQuick3DNode*>(rootNode);
      if (!view || !root)
            return nullptr;
      auto* cam = view->camera();
      if (!cam || view->width() <= 0.0 || view->height() <= 0.0)
            return nullptr;
      const float nx = float(x / view->width());
      const float ny = float(y / view->height());
      const QVector3D nearPos = root->mapPositionFromScene(cam->mapFromViewport(QVector3D(nx, ny, 0.0f)));
      const QVector3D farPos  = root->mapPositionFromScene(cam->mapFromViewport(QVector3D(nx, ny, 1.0f)));
      QVector3D dir           = farPos - nearPos;
      if (dir.isNull())
            return nullptr;
      dir.normalize();

      // Pick in the QML root node's local coordinate space: the ray
      // (nearPos/dir) already lives there.  The element C++ hierarchy
      // starts at Cad, so globalMatrix() misses the QML root node's
      // own transform (scale / rotation / position) — collect the
      // candidates' globalMatrix boxes and pre-multiply the QML root
      // scene transform so both sides share the same coordinate frame.
      const QMatrix4x4 qmlRoot = root->sceneTransform();

      std::vector<std::tuple<float, int, Element3d*>> candidates;
      std::function<void(Element*, int)> walk = [&](Element* e, int depth) {
            if (!e)
                  return;
            if (auto* e3d = qobject_cast<Element3d*>(e)) {
                  if (e3d->show() && e3d->ancestorsShow() && e3d->selectable() && e3d->visible()) {
                        QVector3D lMin, lMax;
                        e3d->boundingBox3D(lMin, lMax);
                        if (lMin.x() <= lMax.x()) {
                              // 8 local corners through (qmlRoot * globalMatrix).
                              QMatrix4x4 m = qmlRoot * e3d->globalMatrix();
                              QVector3D bMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                                             std::numeric_limits<float>::max());
                              QVector3D bMax(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                                             std::numeric_limits<float>::lowest());
                              for (int i = 0; i < 8; ++i) {
                                    QVector3D c = m.map(QVector3D((i & 1) ? lMax.x() : lMin.x(),
                                                                  (i & 2) ? lMax.y() : lMin.y(),
                                                                  (i & 4) ? lMax.z() : lMin.z()));
                                    bMin.setX(std::min(bMin.x(), c.x()));
                                    bMin.setY(std::min(bMin.y(), c.y()));
                                    bMin.setZ(std::min(bMin.z(), c.z()));
                                    bMax.setX(std::max(bMax.x(), c.x()));
                                    bMax.setY(std::max(bMax.y(), c.y()));
                                    bMax.setZ(std::max(bMax.z(), c.z()));
                                    }
                              // Slab test.
                              float tNear = std::numeric_limits<float>::lowest();
                              float tFar  = std::numeric_limits<float>::max();
                              bool hit    = true;
                              for (int axis = 0; axis < 3; ++axis) {
                                    float o = nearPos[axis], d = dir[axis];
                                    float lo = bMin[axis], hi = bMax[axis];
                                    if (qFuzzyIsNull(d)) {
                                          if (o < lo || o > hi) {
                                                hit = false;
                                                break;
                                                }
                                          continue;
                                          }
                                    float t0 = (lo - o) / d;
                                    float t1 = (hi - o) / d;
                                    if (t0 > t1)
                                          std::swap(t0, t1);
                                    tNear = std::max(tNear, t0);
                                    tFar  = std::min(tFar, t1);
                                    if (tNear > tFar) {
                                          hit = false;
                                          break;
                                          }
                                    }
                              if (hit && tFar >= 0.0f)
                                    candidates.emplace_back(std::max(tNear, 0.0f), depth, e3d);
                              }
                        }
                  }
            for (auto* c : e->children())
                  walk(c, depth + 1);
            };
      walk(_rootElement, 0);
      if (candidates.empty())
            return nullptr;
      std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            if (std::get<0>(a) != std::get<0>(b))
                  return std::get<0>(a) < std::get<0>(b);
            return std::get<1>(a) > std::get<1>(b);
            });
      if (_currentElement) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                  if (std::get<2>(candidates[i]) == _currentElement) {
                        if (i + 1 < candidates.size())
                              return std::get<2>(candidates[i + 1]);
                        return std::get<2>(candidates[0]);
                        }
                  }
            }
      return std::get<2>(candidates[0]);
      }

//---------------------------------------------------------
//   pickElementAtRay
//    Return the candidate hit closest to the ray origin; on equal
//    distances the deepest tree node wins so innermost shapes are
//    preferred over their ancestor containers.  Selection cycling
//    behaves like in pickElement(): clicking again on the already
//    selected element returns the next-farther candidate.
//---------------------------------------------------------

Element3d* ZCam::pickElementAtRay(const QVector3D& origin, const QVector3D& dir) {
      if (dir.isNull())
            return nullptr;
      std::vector<std::tuple<float, int, Element3d*>> candidates;
      collectRayPickCandidates(_rootElement, origin, dir.normalized(), 0, candidates);
      if (candidates.empty())
            return nullptr;
      std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            if (std::get<0>(a) != std::get<0>(b))
                  return std::get<0>(a) < std::get<0>(b);
            return std::get<1>(a) > std::get<1>(b);
            });
      if (_currentElement) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                  if (std::get<2>(candidates[i]) == _currentElement) {
                        if (i + 1 < candidates.size())
                              return std::get<2>(candidates[i + 1]);
                        return std::get<2>(candidates[0]);
                        }
                  }
            }
      return std::get<2>(candidates[0]);
      }

//---------------------------------------------------------
//   pickDragTarget
//    Picking helper used when the user starts a left-button drag.
//
//    When a lasso multi-selection is active, the method first
//    checks if the click falls inside any selected element's
//    bounding box.  If so, it returns the smallest such element
//    directly so the user can drag it without pickElement()
//    cycling to the parent (which would deselect the lasso
//    selection via setCurrentElement).
//
//    If the currently selected element is draggable, has children,
//    and the drag point lies inside its world bounding box, return
//    the element itself.  This makes the visible selection bounding
//    box act as a drag handle: dragging anywhere inside the box moves
//    the whole element together with its children.  This applies to
//    Groups and any other draggable element that has children (e.g.
//    a Rectangle with Text sub-elements).
//    If the currently selected element has no children, or the click
//    is outside its bounding box, this delegates to pickElement(),
//    which implements selection cycling (clicking again on the same
//    spot cycles to the parent element).
//---------------------------------------------------------

Element3d* ZCam::pickDragTarget(double x, double y) {
      // When a lasso multi-selection is active, check if the click
      // falls inside any selected element's bounding box.  If so,
      // return the smallest such element directly so it can be
      // dragged without cycling to its parent (which would
      // deselect the lasso selection).
      if (!_selectedElements.isEmpty()) {
            Element3d* best = nullptr;
            double bestArea = 0.0;
            for (auto* el : _selectedElements) {
                  if (!el || !el->draggable() || !el->show() || !el->ancestorsShow())
                        continue;
                  QRectF wb = el->worldBoundingBox();
                  if (!wb.isNull() && !wb.isEmpty() && x >= wb.left() && x <= wb.right() && y >= wb.top() &&
                      y <= wb.bottom()) {
                        double area = wb.width() * wb.height();
                        if (!best || area < bestArea) {
                              bestArea = area;
                              best     = el;
                              }
                        }
                  }
            if (best)
                  return best;
            }
      // If the currently selected element is draggable, has children,
      // and the drag point lies inside its world bounding box, return
      // the element itself.  This makes the visible selection bounding
      // box act as a drag handle: dragging anywhere inside the box moves
      // the whole element together with its children.  This applies to
      // Groups and any other draggable element that has children (e.g.
      // a Rectangle with Text sub-elements).  In all other cases the
      // behaviour is identical to pickElement().
      if (_currentElement && _currentElement->draggable() && _currentElement->show() &&
          _currentElement->ancestorsShow()) {
            bool hasChildren = false;
            for (auto* c : _currentElement->children()) {
                  if (qobject_cast<Element3d*>(c)) {
                        hasChildren = true;
                        break;
                        }
                  }
            if (hasChildren) {
                  QRectF wb = _currentElement->worldBoundingBox();
                  if (!wb.isNull() && !wb.isEmpty()) {
                        if (x >= wb.left() && x <= wb.right() && y >= wb.top() && y <= wb.bottom())
                              return _currentElement;
                        }
                  }
            }
      return pickElement(x, y);
      }

//---------------------------------------------------------
//   pointInPolygon
//    Ray-casting point-in-polygon test.  Returns true if the
//    point (x, y) lies inside the polygon defined by the given
//    list of world-space vertices.
//---------------------------------------------------------

static bool pointInPolygon(double x, double y, const QList<QVector3D>& polygon) {
      int n = polygon.size();
      if (n < 3)
            return false;
      bool inside = false;
      for (int i = 0, j = n - 1; i < n; j = i++) {
            double xi = polygon[i].x(), yi = polygon[i].y();
            double xj = polygon[j].x(), yj = polygon[j].y();
            if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi))
                  inside = !inside;
            }
      return inside;
      }

//---------------------------------------------------------
//   collectLassoCandidates
//    Depth-first traversal collecting all Element3d whose world
//    bounding-box center lies inside the given polygon.  Only
//    visible, selectable elements with a non-empty bounding box
//    are considered.  Groups are included as individual candidates
//    so a lasso can select a whole group at once.
//---------------------------------------------------------

static void collectLassoCandidates(Element* root, const QList<QVector3D>& polygon,
                                   QList<Element3d*>& candidates) {
      if (!root)
            return;
      auto* e3d = qobject_cast<Element3d*>(root);
      if (e3d && e3d->show() && e3d->ancestorsShow() && e3d->selectable() && e3d->visible()) {
            QRectF wb = e3d->worldBoundingBox();
            if (!wb.isNull() && !wb.isEmpty()) {
                  double cx   = wb.center().x();
                  double cy   = wb.center().y();
                  bool inside = pointInPolygon(cx, cy, polygon);
                  if (inside)
                        candidates.append(e3d);
                  }
            }
      for (auto* child : root->children())
            collectLassoCandidates(child, polygon, candidates);
      }

//---------------------------------------------------------
//   lassoSelect
//    Select all visible, selectable elements whose world
//    bounding-box center lies inside the given polygon (in
//    world/root coordinates).  The first element becomes the
//    currentElement.  Clears any previous lasso selection.
//---------------------------------------------------------

void ZCam::lassoSelect(const QList<QVector3D>& polygon) {
      QList<Element3d*> old = _selectedElements;
      _selectedElements.clear();

      if (polygon.size() < 3) {
            if (!old.isEmpty()) {
                  for (auto* e : old)
                        emit e->curColorChanged();
                  emit selectedElementsChanged();
                  }
            return;
            }

      collectLassoCandidates(_rootElement, polygon, _selectedElements);

      // Remove ancestor elements whose children are already in the
      // selection: when a Group's bounding-box center lies inside the
      // lasso polygon alongside some of its children, the Group itself
      // should not be selected — the user wants the individual children,
      // not the container.
      for (int i = _selectedElements.size() - 1; i >= 0; --i) {
            Element3d* el   = _selectedElements[i];
            bool isAncestor = false;
            for (Element3d* other : _selectedElements) {
                  if (other == el)
                        continue;
                  // Walk up other's parent chain; if we encounter el,
                  // then el is an ancestor of other and should be removed.
                  Element* p = other->parent();
                  while (p) {
                        if (p == el) {
                              isAncestor = true;
                              break;
                              }
                        p = p->parent();
                        }
                  if (isAncestor)
                        break;
                  }
            if (isAncestor)
                  _selectedElements.removeAt(i);
            }

      // Emit curColorChanged for all elements that changed selection state.
      for (auto* e : old)
            if (!_selectedElements.contains(e))
                  emit e->curColorChanged();
      for (auto* e : _selectedElements)
            if (!old.contains(e))
                  emit e->curColorChanged();

      if (!_selectedElements.isEmpty())
            setCurrentElement(_selectedElements.first());
      else
            setCurrentElement(nullptr);

      emit selectedElementsChanged();
      }

//---------------------------------------------------------
//   clearSelection
//    Clear the multi-selection list and reset currentElement.
//    This is called from QML (e.g. on Escape) to deselect
//    everything — both the lasso multi-selection and the
//    current (primary) element.
//---------------------------------------------------------

void ZCam::clearSelection() {
      QList<Element3d*> old = _selectedElements;
      _selectedElements.clear();
      for (auto* e : old)
            emit e->curColorChanged();
      if (!old.isEmpty())
            emit selectedElementsChanged();
      // Also clear the primary current element so Escape truly
      // deselects everything when a multi-selection is active.
      setCurrentElement(nullptr);
      }

//--------------------------------------------------------------------
//   clearSelectionList
//    Clear only the multi-selection list without changing currentElement.
//    Used when switching to a new single selection (e.g. plain click in
//    the TreeView) to avoid an intermediate null state that would cause
//    the InspectorModel to clear its data while delegates are still
//    being torn down, leading to null-access runtime errors.
//--------------------------------------------------------------------

void ZCam::clearSelectionList() {
      QList<Element3d*> old = _selectedElements;
      _selectedElements.clear();
      for (auto* e : old)
            emit e->curColorChanged();
      if (!old.isEmpty())
            emit selectedElementsChanged();
      }

//---------------------------------------------------------
//   isSelected
//    Returns true if the given element is in the lasso selection.
//---------------------------------------------------------

bool ZCam::isSelected(const Element3d* el) const {
      for (auto* e : _selectedElements)
            if (e == el)
                  return true;
      return false;
      }

//--------------------------------------------------------------------
//   addToSelection
//    Add an element to the multi-selection list.  The element
//    becomes the current (primary) element shown in the inspector.
//    If the element is already in the list, it just becomes current.
//    Emits selectedElementsChanged and curColorChanged as needed.
//--------------------------------------------------------------------

void ZCam::addToSelection(Element3d* el) {
      if (!el)
            return;
      bool wasInList = _selectedElements.contains(el);
      if (!wasInList) {
            _selectedElements.append(el);
            emit el->curColorChanged();
            emit selectedElementsChanged();
            }
      // Set as current element without clearing the multi-selection.
      // We bypass setCurrentElement() because it clears _selectedElements
      // when the new element is not already in the list (but we just
      // added it, so it is).  Still, calling setCurrentElement() here
      // is safe because el IS in _selectedElements now.
      setCurrentElement(el);
      }

//--------------------------------------------------------------------
//   removeFromSelection
//    Remove an element from the multi-selection list.  If it was
//    the current element, the next remaining element (or nullptr)
//    becomes current.
//--------------------------------------------------------------------

void ZCam::removeFromSelection(Element3d* el) {
      if (!el)
            return;
      if (!_selectedElements.contains(el))
            return;
      _selectedElements.removeOne(el);
      emit el->curColorChanged();
      emit selectedElementsChanged();
      // If the removed element was the current element, pick a new one.
      if (_currentElement == el) {
            if (!_selectedElements.isEmpty())
                  setCurrentElement(_selectedElements.last());
            else
                  setCurrentElement(nullptr);
            }
      }

//--------------------------------------------------------------------
//   toggleSelection
//    Toggle the selection state of an element.  If the element is
//    not yet selected, it is added and becomes current.  If it is
//    already selected, it is removed; if it was current, the next
//    remaining element (or nullptr) becomes current.
//--------------------------------------------------------------------

void ZCam::toggleSelection(Element3d* el) {
      if (!el)
            return;
      if (_selectedElements.contains(el))
            removeFromSelection(el);
      else
            addToSelection(el);
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
      // Ctrl-click on the 3D canvas toggles multi-selection.
      if (element && (modifiers & Qt::ControlModifier)) {
            toggleSelection(element);
            return;
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
            _project->undo()->beginMacro();
            auto cmd = new HandleDragCommand(this, element, vertexIndex, _vertexDragOrigPos, newPos);
            _project->undo()->push(cmd);
            _project->undo()->endMacro();
            }
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
      if (isType<Group>(root))
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

Group* ZCam::layerPtr(const QString& name) const {
      Element* e = Element::byName(name);
      if (!e)
            return nullptr;
      return qobject_cast<Group*>(e);
      }

//---------------------------------------------------------
//   laserLayerNames
//    Collect all LaserLayer element names by traversing the project tree.
//---------------------------------------------------------

static void collectLaserLayers(Element* root, QStringList& names) {
      if (!root)
            return;
      if (isType<Recipe>(root))
            names.append(root->name());
      for (Element* child : root->children())
            collectLaserLayers(child, names);
      }

QStringList ZCam::laserLayerNames() const {
      QStringList names;
      collectLaserLayers(rootElement(), names);
      return names;
      }

//---------------------------------------------------------
//   laserLayerPtr
//    Return the LaserLayer* for a given name, or nullptr.
//---------------------------------------------------------

Recipe* ZCam::laserLayerPtr(const QString& name) const {
      Element* e = Element::byName(name);
      if (!e)
            return nullptr;
      return qobject_cast<Recipe*>(e);
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

LaserRecipe* ZCam::recipePtr(const QString& name) const {
      if (!_recipes)
            return nullptr;
      for (int i = 0; i < _recipes->recipeCount(); ++i) {
            const LaserRecipe* r = _recipes->recipePtr(i);
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

Group* ZCam::findFirstVisibleLayer(Element* root) const {
      if (!root)
            return nullptr;
      if (auto* layer = qobject_cast<Group*>(root)) {
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

Group* ZCam::findCurrentLayer() const {
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

            if (auto* layer = qobject_cast<Group*>(e)) {
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
      Group* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd        = new AddRectangleCommand(this, layer, x, y);
      Element3d* rect = cmd->rectangle();
      _project->undo()->beginMacro();
      _project->undo()->push(cmd);
      _project->undo()->endMacro();

      // Select the new rectangle so vertex handles appear.
      setCurrentElement(rect);

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

      Group* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd        = new AddPolygonCommand(this, layer, x, y);
      Element3d* poly = cmd->polygon();
      _project->undo()->beginMacro();
      _project->undo()->push(cmd);
      _project->undo()->endMacro();

      setCurrentElement(poly);

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

      Group* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd       = new AddEllipseCommand(this, layer, x, y);
      Element3d* ell = cmd->ellipse();
      _project->undo()->beginMacro();
      _project->undo()->push(cmd);
      _project->undo()->endMacro();

      setCurrentElement(ell);

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

      Group* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd        = new AddTextCommand(this, layer, x, y);
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

      _project->undo()->beginMacro();
      _project->undo()->push(cmd);
      _project->undo()->endMacro();

      setCurrentElement(text);

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
      _project->undo()->beginMacro();
            {
            auto cmd = new PropertyChangeCommand(this, element, "pos", QVariant::fromValue(origPos),
                                                 QVariant::fromValue(newPos));
            _project->undo()->push(cmd);
            }
            {
            auto cmd = new PropertyChangeCommand(this, element, "rot", QVariant::fromValue(origRot),
                                                 QVariant::fromValue(newRot));
            _project->undo()->push(cmd);
            }
            {
            auto cmd = new PropertyChangeCommand(this, element, "scale", QVariant::fromValue(origScale),
                                                 QVariant::fromValue(newScale));
            _project->undo()->push(cmd);
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
            // Refresh the new and old parent's selection geometry so
            // the Group bounding box updates to include/exclude the
            // moved element.
            if (auto* np = qobject_cast<Element3d*>(newParent))
                  np->update();
            if (auto* op = qobject_cast<Element3d*>(oldParent))
                  op->update();
            _project->undo()->endMacro();
            }
      else {
            Debug("reparentElement: no project");
            }
      }

//--------------------------------------------------------------------
//     groupSelectedElements
//--------------------------------------------------------------------
//   Group the currently selected elements (lasso multi-selection or
//   single current element) into a new Group element.
//
//   The new Group is created as a child of the first selected element's
//   parent Layer (or the Cad root if the parent is not a Layer).  All
//   selected elements are re-parented into the new Group, preserving
//   their world-space transforms so nothing visually jumps.
//
//   The operation is wrapped in a single undo macro that contains:
//     1. AddGroupCommand  — creates and inserts the new Group
//     2. MoveElementCommand (one per element) — moves each element into
//        the new Group, with preceding PropertyChangeCommands for
//        pos/rot/scale adjustments (handled by reparentElement logic
//        inlined here).
//
//   After grouping, the new Group becomes the current element and the
//   lasso selection is cleared.
//---------------------------------------------------------

void ZCam::groupSelectedElements() {
      if (!_project || !_project->cad())
            return;

      // Build the list of elements to group.
      QList<Element3d*> toGroup;
      if (!_selectedElements.isEmpty())
            toGroup = _selectedElements;
      else if (_currentElement)
            toGroup.append(_currentElement);

      // Need at least two elements to form a group.
      if (toGroup.size() < 2)
            return;

      // Filter: keep only draggable elements that have a parent.
      // Skip elements that are already inside one of the other
      // selected elements (descendant) — they will be moved together
      // with their ancestor.
      QList<Element3d*> filtered;
      for (auto* el : toGroup) {
            if (!el || !el->draggable() || !el->parent())
                  continue;
            bool isDescendant = false;
            for (auto* other : toGroup) {
                  if (other == el)
                        continue;
                  // Walk up el's parent chain; if we encounter other,
                  // then el is a descendant of other.
                  Element* p = el->parent();
                  while (p) {
                        if (p == other) {
                              isDescendant = true;
                              break;
                              }
                        p = p->parent();
                        }
                  if (isDescendant)
                        break;
                  }
            if (!isDescendant)
                  filtered.append(el);
            }

      if (filtered.size() < 2)
            return;

      // Determine the common parent: use the parent of the first
      // filtered element.  All filtered elements should share the
      // same parent (they come from a lasso selection at the same
      // tree level), but if they don't we use the first element's
      // parent as the Group's parent.
      Element* groupParent = filtered.first()->parent();
      if (!groupParent)
            return;

      // Find the Layer ancestor for the new Group.  If groupParent
      // is itself a Group/Layer, add the new Group as its child.
      // Otherwise add it to the Cad root.
      Group* targetLayer = nullptr;
      if (auto* gp = qobject_cast<Group*>(groupParent))
            targetLayer = gp;
      else {
            // Walk up to find the nearest Group ancestor.
            for (Element* e = groupParent; e; e = e->parent()) {
                  if (auto* g = qobject_cast<Group*>(e)) {
                        targetLayer = g;
                        break;
                        }
                  if (isType<Cad>(e))
                        break;
                  }
            }
      if (!targetLayer)
            targetLayer = findFirstVisibleLayer(_project->cad());
      if (!targetLayer) {
            Debug("groupSelectedElements: no target layer");
            return;
            }

      // Create the new Group element.
      auto* newGroup = new Group(this, nullptr);
      newGroup->setName("group");

      // Compute the world-space bounding box of all filtered elements
      // to position the new Group at the center of the bounding box.
      // This keeps the Group's local origin near the visual center of
      // its children, making subsequent transforms intuitive.
      QRectF bbox = filtered.first()->worldBoundingBox();
      for (int i = 1; i < filtered.size(); ++i) {
            QRectF wb = filtered[i]->worldBoundingBox();
            bbox      = bbox.united(wb);
            }
      QVector3D groupPos;
      if (!bbox.isNull() && !bbox.isEmpty()) {
            // The Group is placed in the target Layer's coordinate space.
            // Convert the world-space center back to the target Layer's
            // local space.
            QMatrix4x4 layerGlobal = targetLayer->globalMatrix();
            bool ok                = false;
            QMatrix4x4 layerInv    = layerGlobal.inverted(&ok);
            if (ok) {
                  QVector3D worldCenter(bbox.center().x(), bbox.center().y(), 0.0);
                  QVector3D localCenter = layerInv.map(worldCenter);
                  groupPos              = localCenter;
                  }
            }
      newGroup->set_pos(groupPos);

      // Begin the undo macro.
      _project->undo()->beginMacro();

      // Insert the new Group into the target Layer.
            {
            int row = targetLayer->children().size();
            if (treeModel())
                  treeModel()->beginInsertChild(targetLayer, row);
            targetLayer->addChild(newGroup);
            if (treeModel())
                  treeModel()->endInsertChild();
            emit add3dElement(newGroup);
            newGroup->update();
            }

      // Re-parent each filtered element into the new Group.
      // We inline the reparent logic (coordinate transform) here
      // and use MoveElementCommand for the actual tree move.
      for (auto* el : filtered) {
            Element* oldParent = el->parent();
            if (!oldParent)
                  continue;

            // Compute the element's current world (global) matrix.
            QMatrix4x4 oldGlobal = el->globalMatrix();

            // Compute the new parent's (newGroup) global matrix.
            QMatrix4x4 newParentGlobal = newGroup->globalMatrix();
            bool ok                    = false;
            QMatrix4x4 newParentInv    = newParentGlobal.inverted(&ok);
            if (!ok)
                  continue;

            QMatrix4x4 newLocal = newParentInv * oldGlobal;

            // Decompose into pos/rot/scale.
            QVector3D newPos(newLocal(0, 3), newLocal(1, 3), newLocal(2, 3));
            float sx = QVector3D(newLocal(0, 0), newLocal(1, 0), newLocal(2, 0)).length();
            float sy = QVector3D(newLocal(0, 1), newLocal(1, 1), newLocal(2, 1)).length();
            float sz = QVector3D(newLocal(0, 2), newLocal(1, 2), newLocal(2, 2)).length();
            QVector3D newScale(sx, sy, sz);

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

            // Record original transforms for undo.
            QVector3D origPos   = el->pos();
            QVector3D origRot   = el->rot();
            QVector3D origScale = el->scale();

            el->beginBatchUpdate();
            el->set_pos(newPos);
            el->set_rot(newRot);
            el->set_scale(newScale);
            el->endBatchUpdate();

            // Push undo commands for transform changes.
            _project->undo()->push(new PropertyChangeCommand(this, el, "pos", QVariant::fromValue(origPos),
                                                             QVariant::fromValue(newPos)));
            _project->undo()->push(new PropertyChangeCommand(this, el, "rot", QVariant::fromValue(origRot),
                                                             QVariant::fromValue(newRot)));
            _project->undo()->push(new PropertyChangeCommand(
                this, el, "scale", QVariant::fromValue(origScale), QVariant::fromValue(newScale)));

            // Move the element in the tree.
            int oldRow = 0;
            for (const auto c : oldParent->children()) {
                  if (c == el)
                        break;
                  ++oldRow;
                  }
            auto moveCmd = new MoveElementCommand(this, el, oldParent, oldRow, newGroup, -1);
            _project->undo()->push(moveCmd);
            }

      _project->undo()->endMacro();

      // Update the new Group and old parents.
      newGroup->update();
      if (auto* tp = qobject_cast<Element3d*>(targetLayer))
            tp->update();

      // Select the new Group and clear the lasso selection.
      _selectedElements.clear();
      emit selectedElementsChanged();
      setCurrentElement(newGroup);

      setCamDirty(true);
      }

//--------------------------------------------------------------------
//     combineSelectedPolygons
//--------------------------------------------------------------------
//   Combine all selected Polygon elements that share the same parent
//   (same tree level) into a single new Polygon.
//
//   For each selected Polygon, its path data (PainterPath) is converted
//   to a PathList, each path is transformed from the polygon's local
//   coordinate space to the common parent's local coordinate space
//   using the polygon's globalMatrix and the parent's inverse global
//   matrix, and all transformed paths are unioned via Clipper2.
//
//   The union result is converted back to a PainterPath (as a series
//   of MoveTo / LineTo elements) and assigned to a new Polygon that
//   is inserted as a child of the common parent.  All original selected
//   polygons are then deleted.
//
//   The operation is wrapped in a single undo macro containing:
//     1. AddPolygonCommand — creates and inserts the new combined Polygon
//     2. RemoveElementCommand (one per original polygon) — deletes each
//
//   After combining, the new Polygon becomes the current element and
//   the lasso selection is cleared.
//---------------------------------------------------------

void ZCam::combineSelectedPolygons() {
      if (!_project || !_project->cad())
            return;

      // Build the list of selected elements.
      QList<Element3d*> toCombine;
      if (!_selectedElements.isEmpty())
            toCombine = _selectedElements;
      else if (_currentElement)
            toCombine.append(_currentElement);

      // Filter: keep only Polygon elements that have a parent.
      QList<Polygon*> polygons;
      for (auto* el : toCombine) {
            auto* poly = qobject_cast<Polygon*>(el);
            if (poly && poly->parent())
                  polygons.append(poly);
            }

      // Need at least two polygons to combine.
      if (polygons.size() < 2)
            return;

      // Verify all polygons share the same parent (same tree level).
      Element* commonParent = polygons.first()->parent();
      for (int i = 1; i < polygons.size(); ++i) {
            if (polygons[i]->parent() != commonParent) {
                  Debug("combineSelectedPolygons: selected polygons are not on the same tree level");
                  return;
                  }
            }

      auto* parent3d = qobject_cast<Element3d*>(commonParent);
      if (!parent3d)
            return;

      // Collect all paths in the common parent's local coordinate space.
      // For each polygon, transform its pathList from its local space
      // to the parent's local space via:
      //   parentLocal = parentGlobalInv * polyGlobal * polyLocal
      // We use the polygon's globalMatrix() to map local points to
      // world (root) space, then the parent's inverse globalMatrix to
      // map back to parent-local space.
      QMatrix4x4 parentGlobal = parent3d->globalMatrix();
      bool ok                  = false;
      QMatrix4x4 parentGlobalInv = parentGlobal.inverted(&ok);
      if (!ok)
            return;

      Clipper2Lib::PathsD allPaths;
      for (auto* poly : polygons) {
            // Ensure the polygon's pathList is up to date.
            // toPathList() converts the PainterPath (with bezier
            // flattening) to a list of 2D point paths.
            PathList pl = poly->painterPathData().toPathList();
            QMatrix4x4 polyGlobal = poly->globalMatrix();
            for (const auto& path : pl) {
                  Clipper2Lib::PathD clipperPath;
                  for (const auto& pt : path) {
                        QVector3D worldPt = polyGlobal.map(QVector3D(float(pt.x()), float(pt.y()), 0.0f));
                        QVector3D parentPt = parentGlobalInv.map(worldPt);
                        clipperPath.push_back({parentPt.x(), parentPt.y()});
                        }
                  if (clipperPath.size() >= 3)
                        allPaths.push_back(clipperPath);
                  }
            }

      if (allPaths.empty())
            return;

      // Determine nesting depth for each path.
      // A path at odd nesting depth (inside one other path) must be
      // reversed so that it becomes a hole in the NonZero union.
      // Without this, two same-orientation paths where one is inside
      // the other would simply merge into the outer contour — the
      // inner path would be absorbed and no hole would appear.
      for (int i = 0; i < static_cast<int>(allPaths.size()); ++i) {
            int depth = 0;
            // Use the first point of path i as a representative point.
            Clipper2Lib::PointD testPt = allPaths[i][0];
            for (int j = 0; j < static_cast<int>(allPaths.size()); ++j) {
                  if (i == j)
                        continue;
                  if (Clipper2Lib::PointInPolygon(testPt, allPaths[j])
                      == Clipper2Lib::PointInPolygonResult::IsInside)
                        ++depth;
                  }
            if (depth % 2 == 1)
                  std::reverse(allPaths[i].begin(), allPaths[i].end());
            }

      // Union all paths via Clipper2.
      // NonZero fill rule with correct orientations (outer contours CCW,
      // holes CW) produces a result where holes are preserved as separate
      // paths with opposite orientation.
      Clipper2Lib::ClipperD clipper(4);
      clipper.AddSubject(allPaths);
      Clipper2Lib::PathsD unioned;
      clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, unioned);

      if (unioned.empty())
            return;

      // Build a PainterPath from the union result.
      // Each Clipper2 path becomes a subpath: MoveTo to the first point,
      // then LineTo for each subsequent point.  The subpath is closed
      // by adding a final LineTo back to the first point (unless the
      // path is already closed).
      //
      // We do NOT use PainterPath::closeSubpath() because that method
      // always closes to front() of the entire PainterPath, which is
      // wrong when multiple subpaths exist.
      PainterPath combinedPath;
      for (const auto& path : unioned) {
            if (path.empty())
                  continue;
            Vec2d firstPt(path[0].x, path[0].y);
            combinedPath.moveTo(firstPt);
            for (size_t i = 1; i < path.size(); ++i)
                  combinedPath.lineTo(Vec2d(path[i].x, path[i].y));
            // Close the subpath explicitly.
            Vec2d lastPt(path.back().x, path.back().y);
            if (std::abs(firstPt.x() - lastPt.x()) > 0.0001
                || std::abs(firstPt.y() - lastPt.y()) > 0.0001)
                  combinedPath.lineTo(firstPt);
            }

      // Find the target Layer for the new Polygon.
      Group* targetLayer = nullptr;
      if (auto* gp = qobject_cast<Group*>(commonParent))
            targetLayer = gp;
      else {
            for (Element* e = commonParent; e; e = e->parent()) {
                  if (auto* g = qobject_cast<Group*>(e)) {
                        targetLayer = g;
                        break;
                        }
                  if (isType<Cad>(e))
                        break;
                  }
            }
      if (!targetLayer)
            targetLayer = findFirstVisibleLayer(_project->cad());
      if (!targetLayer) {
            Debug("combineSelectedPolygons: no target layer");
            return;
            }

      // Create the new combined Polygon.
      auto* newPoly = new Polygon(this, nullptr);
      newPoly->setName("");
      newPoly->set_pos(QVector3D(0.0, 0.0, 0.0));
      // Copy visual properties from the first polygon.
      newPoly->setColor(polygons.first()->color());
      newPoly->set_lineWidth(polygons.first()->lineWidth());
      newPoly->set_fill(polygons.first()->fill());
      newPoly->set_endType(polygons.first()->endType());
      newPoly->set_joinType(polygons.first()->joinType());
      // Set the combined painter path.
      newPoly->setPainterPath(combinedPath);
      newPoly->update();

      // Begin the undo macro.
      _project->undo()->beginMacro();

      // Insert the new Polygon into the target Layer.
      {
      int row = targetLayer->children().size();
      if (treeModel())
            treeModel()->beginInsertChild(targetLayer, row);
      targetLayer->addChild(newPoly);
      if (treeModel())
            treeModel()->endInsertChild();
      emit add3dElement(newPoly);
      }

      // Remove all original polygons.
      for (auto* poly : polygons) {
            Element* p = poly->parent();
            if (!p)
                  continue;
            int row = 0;
            for (const auto c : p->children()) {
                  if (c == poly)
                        break;
                  ++row;
                  }
            auto cmd = new RemoveElementCommand(this, p, poly, row);
            _project->undo()->push(cmd);
            }

      _project->undo()->endMacro();

      // Update the parent's selection geometry.
      parent3d->update();

      // Select the new Polygon and clear the lasso selection.
      _selectedElements.clear();
      emit selectedElementsChanged();
      setCurrentElement(newPoly);

      setCamDirty(true);
      }

//---------------------------------------------------------
//   deleteCurrentElement
//    Delete the current element and/or all lasso-selected
//    elements.  When _selectedElements is non-empty, all
//    selected elements are deleted in a single undo macro.
//    Hierarchical de-duplication: if an element is a
//    descendant of another selected element, the descendant
//    is skipped (it will be removed together with its
//    ancestor).  The current element is included in the
//    deletion set even if it is not in _selectedElements
//    (e.g. when the user clicked a single element without
//    lasso).  Non-deletable elements are silently skipped.
//    The operation is routed through the undo stack via
//    Project::removeElement() so it can be undone/redone.
//---------------------------------------------------------

void ZCam::deleteCurrentElement() {
      if (!_project)
            return;

      // Build the set of elements to delete.
      // Start with _selectedElements (lasso multi-selection).
      // If _selectedElements is empty, fall back to just
      // _currentElement (single selection mode).
      QList<Element3d*> toDelete;
      if (!_selectedElements.isEmpty())
            toDelete = _selectedElements;
      else if (_currentElement)
            toDelete.append(_currentElement);

      if (toDelete.isEmpty())
            return;

      // Filter: keep only deletable elements.
      // Also remove elements that are descendants of other
      // elements in the deletion set — deleting the ancestor
      // will already remove the descendant from the tree.
      QList<Element3d*> filtered;
      for (auto* el : toDelete) {
            if (!el || !el->deletable())
                  continue;
            // Walk up the parent chain; if any ancestor is
            // also in the deletion set, skip this element.
            bool isDescendant = false;
            for (Element* p = el->parent(); p; p = p->parent()) {
                  auto* p3d = qobject_cast<Element3d*>(p);
                  if (!p3d)
                        continue;
                  if (toDelete.contains(p3d)) {
                        isDescendant = true;
                        break;
                        }
                  }
            if (!isDescendant)
                  filtered.append(el);
            }

      if (filtered.isEmpty())
            return;

      // Clear selection BEFORE deleting so QML bindings don't
      // dereference dangling pointers.
      auto oldSelected = _selectedElements;
      _selectedElements.clear();
      for (auto* e : oldSelected)
            emit e->curColorChanged();
      if (!oldSelected.isEmpty())
            emit selectedElementsChanged();
      setCurrentElement(nullptr);
      // Pre-compute the parent and row for each element BEFORE
      // any deletion happens.  Since push() executes redo()
      // immediately, deleting an element shifts the children list
      // of its parent.  To avoid stale indices, we sort the
      // deletion list by (parent, descending row) so that within
      // the same parent we delete from highest to lowest index,
      // preserving the validity of lower indices.
      struct DelInfo {
            Element3d* element;
            Element* parent;
            int row;
            };
      QList<DelInfo> delList;
      for (auto* el : filtered) {
            Element* parent = el->parent();
            if (!parent)
                  continue;
            int row = 0;
            for (const auto c : parent->children()) {
                  if (c == el)
                        break;
                  ++row;
                  }
            delList.append({el, parent, row});
            }
      std::sort(delList.begin(), delList.end(), [](const DelInfo& a, const DelInfo& b) {
            if (a.parent != b.parent)
                  return a.parent < b.parent;
            return a.row > b.row; // descending row within same parent
            });

      // Delete all filtered elements in a single undo macro.
      // We create RemoveElementCommand objects directly instead of
      // calling Project::removeElement() because removeElement()
      // creates its own beginMacro/endMacro pair, and macro nesting
      // is not supported by the undo stack.
      _project->undo()->beginMacro();
      for (const auto& info : delList) {
            auto cmd = new RemoveElementCommand(this, info.parent, info.element, info.row);
            _project->undo()->push(cmd);
            }
      _project->undo()->endMacro();
      // Update CAD layer visibility since removing a Layer or
      // LaserLayer may change which layers are referenced by the
      // active fixture.
      _project->updateCadLayerVisibility();
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
      auto ll      = new Recipe(this, fixture);
      auto recipes = this->recipes();
      if (recipes && recipes->recipeCount() > 0)
            ll->set_recipe(recipes->recipePtr(0));
      auto stock = new Stock(this, cam);
      auto layer = new Group(this, cad);
      // Set the LaserLayer on the Layer so all children inherit it.
      layer->set_laserLayer(ll);
      auto text = new Text(this, layer);
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
      // Create a CameraElement for new projects before the scene is built.
      if (project())
            project()->ensureCameraElement();

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
      _project->undo()->setClean();
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
      _project->undo()->setClean();
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
      else if (ImportIpc2581::isIpc2581File(path))
            return ImportIpc2581::import(this, path);
      else if (suffix == QStringLiteral("brep"))
            return BrepElementInterface::import(this, path);
      else {
            Warning("ZCam::importFile: unsupported file type: {}", suffix);
            return false;
            }
      setCamDirty(true);
      return true;
      }

//---------------------------------------------------------
//   dxfBoundingBox
//    Compute the bounding box of a DXF/DWG file in millimetres.
//---------------------------------------------------------

QRectF ZCam::dxfBoundingBox(const QString& path) {
      return DxfImport::boundingBox(this, path);
      }

//---------------------------------------------------------
//   importDxfAt
//    Import a DXF/DWG file and position it so the bounding
//    box's bottom-left corner is at (x, y) in scene coordinates.
//---------------------------------------------------------

bool ZCam::importDxfAt(const QString& path, double x, double y) {
      return DxfImport::importAt(this, path, x, y);
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

            // Migration: create a CameraElement if the loaded project
            // doesn't have one (projects saved before the camera element
            // was introduced).
            if (project())
                  project()->ensureCameraElement();

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