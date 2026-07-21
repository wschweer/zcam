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

#include "project.h"
#include "cad.h"
#include "cam.h"
#include "layer.h"
#include "laserlayer.h"
#include "element3d.h"
#include "rectangle.h"
#include "polygon.h"
#include "ellipse.h"
#include "text.h"
#include "grid.h"
#include "zcam.h"
#include "treemodel.h"
#include "undo.h"
#include "logger.h"

#include <QSet>
#include <QMetaProperty>
#include <QFileInfo>
#include <functional>

//---------------------------------------------------------
//   HandleDragCommand implementation
//---------------------------------------------------------

void HandleDragCommand::undo() {
      if (!_element)
            return;
      _element->setVertexPos(_handleIndex, _oldPos);
      }

void HandleDragCommand::redo() {
      if (!_element)
            return;
      _element->setVertexPos(_handleIndex, _newPos);
      }

//---------------------------------------------------------
//   Project
//---------------------------------------------------------

Project::Project(ZCam* z, Element* parent) : Element3d(z, parent) {
      Element::setName("project");
      // Switching the active fixture changes the machine-space
      // transformation chain (fixtureMatrix * camMatrix * ...),
      // so all CAM data must be recalculated.
      connect(this, &Project::fixtureChanged, [this] {
            // Cam display is NOT refreshed automatically when the fixture
            // changes.  The user triggers it manually via the refresh button.
            zcam->setCamDirty(true);
            emit updateFraming(-1);
            });
      }

Project::~Project() {
      }

//---------------------------------------------------------
//   projectName
//---------------------------------------------------------

QString Project::projectName() const {
      if (_projectPath.isEmpty())
            return QStringLiteral("Untitled");
      return QFileInfo(_projectPath).baseName();
      }

//---------------------------------------------------------
//   setDirty
//---------------------------------------------------------

void Project::setDirty(bool v) {
      if (v == _dirty)
            return;
      _dirty = v;
      emit dirtyChanged();
      }

//---------------------------------------------------------
//   setProjectPath
//---------------------------------------------------------

void Project::setProjectPath(const QString& v) {
      if (v == _projectPath)
            return;
      _projectPath = v;
      emit projectPathChanged();
      }

//---------------------------------------------------------
//   markDirty
//---------------------------------------------------------

void Project::markDirty() {
      setDirty(true);
      }

//---------------------------------------------------------
//   clearUndoStack
//---------------------------------------------------------

void Project::clearUndoStack() {
      _undoStack.clear();
      _undoIndex = 0;
      emit undoStateChanged();
      }

//---------------------------------------------------------
//   pushCommand
//    Push a new undo command onto the stack, discarding any
//    redo entries that come after the current index.
//---------------------------------------------------------

void Project::pushCommand(std::unique_ptr<UndoCommand> cmd) {
      // Discard any redo entries after the current index
      while (static_cast<int>(_undoStack.size()) > _undoIndex)
            _undoStack.pop_back();
      _undoStack.push_back(std::move(cmd));
      _undoIndex = static_cast<int>(_undoStack.size());
      emit undoStateChanged();
      }

//---------------------------------------------------------
//   changeProperty
//    Public API used by the Inspector to route a property change
//    through the undo system and set the dirty flag.
//---------------------------------------------------------

void Project::changeProperty(QObject* element, const QString& propName, const QVariant& newValue) {
      if (!element)
            return;
      QByteArray pn     = propName.toUtf8();
      QVariant oldValue = element->property(pn.constData());

      // If newValue represents a null pointer (invalid QVariant or a valid
      // but null value) and the property is a pointer type, we need to write
      // a typed null pointer directly.  setProperty() with an untyped null
      // (e.g. from QML) silently does nothing for Q_DECLARE_OPAQUE_POINTER
      // properties such as LaserLayer*.
      bool isNullPointerWrite = false;
      QMetaType propertyMetaType;
      if (!newValue.isValid() || newValue.isNull()) {
            int propIdx = element->metaObject()->indexOfProperty(pn.constData());
            if (propIdx >= 0) {
                  QMetaProperty mp = element->metaObject()->property(propIdx);
                  propertyMetaType = mp.metaType();
                  if (propertyMetaType.id() == QMetaType::QObjectStar
                      || (propertyMetaType.id() >= QMetaType::User && propertyMetaType.sizeOf() == sizeof(void*))) {
                        isNullPointerWrite = true;
                        }
                  }
            }

      if (isNullPointerWrite) {
            // Build a properly-typed null QVariant for the property's meta type.
            // This is required for Q_DECLARE_OPAQUE_POINTER types where a
            // generic QML null cannot be converted automatically.
            QVariant typedNull(propertyMetaType, nullptr);
            if (oldValue == typedNull)
                  return;
            // Write the null pointer directly.
            element->setProperty(pn.constData(), typedNull);
            // Create undo command for the change.
            auto cmd = std::make_unique<PropertyChangeCommand>(element, pn, oldValue, typedNull);
            pushCommand(std::move(cmd));
            setDirty(true);
            zcam->setCamDirty(true);
            return;
            }

      if (oldValue == newValue)
            return;
      // For the "name" property, use RenameElementCommand which stores
      // the actual de-duplicated name that Element::setName() assigns,
      // not the user-typed text.  This ensures undo/redo restores the
      // exact same name even when de-duplication occurred.
      if (pn == "name") {
            auto el = qobject_cast<Element*>(element);
            if (!el)
                  return;
            QString oldName = el->name();
            // Apply the new name first to capture the de-duplicated result
            el->setName(newValue.toString());
            QString actualName = el->name();
            auto cmd           = std::make_unique<RenameElementCommand>(el, oldName, actualName);
            pushCommand(std::move(cmd));
            setDirty(true);
            // mark cam data as stale so the Cam refresh button enables
            zcam->setCamDirty(true);
            return;
            }
      auto cmd = std::make_unique<PropertyChangeCommand>(element, pn, oldValue, newValue);
      cmd->redo(); // apply the new value immediately
      pushCommand(std::move(cmd));
      setDirty(true);
      // mark cam data as stale so the Cam refresh button enables
      zcam->setCamDirty(true);
      }

//---------------------------------------------------------
//   checkUnsavedChanges
//   Returns true if it is safe to proceed (no pending changes, or user
//   chose to discard).  When dirty the caller should show a dialog; this
//   stub always returns true so QML-level dialogs can take over.
//---------------------------------------------------------

bool Project::checkUnsavedChanges() {
      // The actual "Save changes?" dialog is driven from QML.
      // This method is kept as a synchronous fast-path for cases where the
      // project is already clean.
      return !_dirty;
      }

//---------------------------------------------------------
//   undo
//---------------------------------------------------------

void Project::undo() {
      if (!canUndo())
            return;
      --_undoIndex;
      _undoStack[_undoIndex]->undo();
      emit undoStateChanged();
      setDirty(true);
      zcam->setCamDirty(true);
      // Cam display is NOT refreshed automatically after undo.
      // The user triggers it manually via the refresh button.
      updateCadLayerVisibility();
      }

//---------------------------------------------------------
//   redo
//---------------------------------------------------------

void Project::redo() {
      if (!canRedo())
            return;
      _undoStack[_undoIndex]->redo();
      ++_undoIndex;
      emit undoStateChanged();
      setDirty(true);
      zcam->setCamDirty(true);
      // Cam display is NOT refreshed automatically after redo.
      // The user triggers it manually via the refresh button.
      updateCadLayerVisibility();
      }

//---------------------------------------------------------
//   updateCadLayerVisibility
//    Show only CAD Layers that are referenced by at least one
//    LaserLayer in the active fixture.  Layers not referenced
//    by the active fixture are hidden.
//---------------------------------------------------------

void Project::updateCadLayerVisibility() {
      if (!_cad)
            return;

      // Build a set of all Layer pointers that have at least one
      // LaserLayer referencing them via laserLayer property in the
      // active fixture.  Also show Layers that contain children which
      // have a laserLayer set to one of the fixture's LaserLayers.
      // For now, simply show all layers (the old baseElement logic is gone).
      QSet<Layer*> referencedLayers;
      if (_fixture) {
            for (const auto c : _fixture->children()) {
                  auto* ll = qobject_cast<LaserLayer*>(c);
                  if (ll) {
                        // Walk the Cad subtree to find elements referencing this LaserLayer
                        if (_cad) {
                              std::function<void(Element*)> walk = [&](Element* e) {
                                    auto* e3d = qobject_cast<Element3d*>(e);
                                    if (e3d && e3d->effectiveLaserLayer() == ll) {
                                          // Mark the nearest Layer ancestor as referenced
                                          Element* p = e3d;
                                          while (p && !isType<Layer>(p))
                                                p = p->parent();
                                          if (auto* layer = qobject_cast<Layer*>(p))
                                                referencedLayers.insert(layer);
                                          }
                                    for (auto* child : e->children())
                                          walk(child);
                                    };
                              walk(_cad);
                              }
                        }
                  }
            }

      for (const auto c : _cad->children()) {
            auto* layer = qobject_cast<Layer*>(c);
            if (!layer)
                  continue;
            // Set show to true only if this layer is referenced by
            // a LaserLayer in the active fixture.
            //            layer->set_show(referencedLayers.contains(layer));
            }
      }

//---------------------------------------------------------
//   gridElement
//    Returns the Grid child element, or nullptr if none exists.
//---------------------------------------------------------

Element3d* Project::gridElement() const {
      for (Element* c : children()) {
            auto* g = qobject_cast<Grid*>(c);
            if (g)
                  return g;
            }
      return nullptr;
      }

//---------------------------------------------------------
//   set_machine
//    Set the active machine pointer.  Also updates the cached
//    _machineName so resolveMachine() can re-resolve after the
//    Machines asset is reloaded.
//---------------------------------------------------------

void Project::set_machine(Machine* m) {
      if (m == _machine)
            return;
      _machine     = m;
      _machineName = m ? m->name() : QString();
      emit machineChanged();
      }

//---------------------------------------------------------
//   resolveMachine
//    Re-resolve the Machine* from _machineName by looking it
//    up in the ZCam::machines asset.  Called after the Machines
//    asset is reloaded (add/remove/rename) so the pointer stays
//    valid.
//---------------------------------------------------------

void Project::resolveMachine() {
      ZCam* zc = zcamInstance();
      if (!zc || !zc->machines() || _machineName.isEmpty()) {
            if (_machine) {
                  _machine = nullptr;
                  emit machineChanged();
                  }
            return;
            }
      QStringList model   = zc->machines()->machinesModel();
      int idx             = model.indexOf(_machineName);
      Machine* newMachine = (idx >= 0) ? zc->machines()->machine(idx) : nullptr;
      if (newMachine != _machine) {
            _machine = newMachine;
            emit machineChanged();
            }
      }

//---------------------------------------------------------
//   addFixture
//    Register a Fixture in the project's fixture list and
//    make it the current fixture if none is active yet.
//---------------------------------------------------------

void Project::addFixture(Fixture* f) {
      if (!f || _fixtures.contains(f))
            return;
      _fixtures.append(f);
      emit fixturesChanged();
      if (!_fixture)
            set_fixture(f);
      }

//---------------------------------------------------------
//   removeFixture
//    Remove a Fixture from the project's fixture list.
//    If it was the active fixture, pick another one or null.
//---------------------------------------------------------

void Project::removeFixture(Fixture* f) {
      if (!f || !_fixtures.contains(f))
            return;
      _fixtures.removeAll(f);
      emit fixturesChanged();
      if (_fixture == f)
            set_fixture(_fixtures.isEmpty() ? nullptr : _fixtures.first());
      }

//---------------------------------------------------------
//   AddLayerCommand implementation
//---------------------------------------------------------

AddLayerCommand::AddLayerCommand(ZCam* zcam, Cad* cad, Fixture* fixture)
    : _cad(cad), _fixture(fixture), _zcam(zcam) {
      _layer = new Layer(zcam, nullptr);
      // No longer create a LaserLayer automatically.
      // The user creates LaserLayers explicitly and assigns
      // elements to them via the laserLayer property.
      }

void AddLayerCommand::redo() {
      if (!_cad || !_layer)
            return;
      // Insert Layer into Cad
      int row = _cad->children().size();
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginInsertChild(_cad, row);
      _cad->addChild(_layer);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endInsertChild();
      if (_zcam)
            emit _zcam->add3dElement(_layer);

      // Insert LaserLayer into Fixture
      if (_fixture && _laserLayer) {
            int llRow = _fixture->children().size();
            if (_zcam && _zcam->treeModel())
                  _zcam->treeModel()->beginInsertChild(_fixture, llRow);
            _fixture->addChild(_laserLayer);
            _llRow = llRow;
            if (_zcam && _zcam->treeModel())
                  _zcam->treeModel()->endInsertChild();
            if (_zcam)
                  emit _zcam->add3dElement(_laserLayer);
            }
      }

void AddLayerCommand::undo() {
      if (!_cad || !_layer)
            return;
      // Remove LaserLayer from Fixture first
      if (_fixture && _laserLayer) {
            int llRow = 0;
            for (const auto c : _fixture->children()) {
                  if (c == _laserLayer)
                        break;
                  ++llRow;
                  }
            if (_zcam)
                  emit _zcam->remove3dElement(_laserLayer);
            if (_zcam && _zcam->treeModel())
                  _zcam->treeModel()->beginRemoveChild(_fixture, llRow);
            _fixture->removeChild(_laserLayer);
            _llRow = llRow;
            if (_zcam && _zcam->treeModel())
                  _zcam->treeModel()->endRemoveChild();
            }

      // Remove Layer from Cad
      int row = 0;
      for (const auto c : _cad->children()) {
            if (c == _layer)
                  break;
            ++row;
            }
      if (_zcam)
            emit _zcam->remove3dElement(_layer);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginRemoveChild(_cad, row);
      _cad->removeChild(_layer);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endRemoveChild();
      }

QString AddLayerCommand::description() const {
      return QStringLiteral("Add Layer");
      }

//---------------------------------------------------------
//   RenameElementCommand implementation
//---------------------------------------------------------

void RenameElementCommand::redo() {
      if (!_element)
            return;
      _element->setName(_newName);
      }

void RenameElementCommand::undo() {
      if (!_element)
            return;
      _element->setName(_oldName);
      }

//---------------------------------------------------------
//   AddFixtureCommand implementation
//---------------------------------------------------------

AddFixtureCommand::AddFixtureCommand(ZCam* zcam, Cam* cam) : _zcam(zcam), _cam(cam) {
      _fixture = new Fixture(zcam, nullptr);
      }

void AddFixtureCommand::redo() {
      if (!_cam || !_fixture)
            return;
      int row = _cam->children().size();
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginInsertChild(_cam, row);
      _cam->addChild(_fixture);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endInsertChild();
      if (_zcam)
            emit _zcam->add3dElement(_fixture);
      // Register in project fixture list and set as active
      if (_zcam && _zcam->project())
            _zcam->project()->addFixture(_fixture);
      }

void AddFixtureCommand::undo() {
      if (!_cam || !_fixture)
            return;
      int row = 0;
      for (const auto c : _cam->children()) {
            if (c == _fixture)
                  break;
            ++row;
            }
      if (_zcam)
            emit _zcam->remove3dElement(_fixture);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginRemoveChild(_cam, row);
      _cam->removeChild(_fixture);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endRemoveChild();
      // Remove from project fixture list
      if (_zcam && _zcam->project())
            _zcam->project()->removeFixture(_fixture);
      }

QString AddFixtureCommand::description() const {
      return QStringLiteral("Add Fixture");
      }

//---------------------------------------------------------
//   AddLaserLayerCommand implementation
//---------------------------------------------------------

AddLaserLayerCommand::AddLaserLayerCommand(ZCam* zcam, Fixture* fixture) : _zcam(zcam), _fixture(fixture) {
      _laserLayer = new LaserLayer(zcam, nullptr);
      // No longer auto-link to the first Cad Layer via baseElement.
      // The user assigns elements to this LaserLayer via the laserLayer property.
      _laserLayer->setName(QStringLiteral("LaserLayer"));
      }

void AddLaserLayerCommand::redo() {
      if (!_fixture || !_laserLayer)
            return;
      int row = _fixture->children().size();
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginInsertChild(_fixture, row);
      _fixture->addChild(_laserLayer);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endInsertChild();
      if (_zcam)
            emit _zcam->add3dElement(_laserLayer);
      }

void AddLaserLayerCommand::undo() {
      if (!_fixture || !_laserLayer)
            return;
      int row = 0;
      for (const auto c : _fixture->children()) {
            if (c == _laserLayer)
                  break;
            ++row;
            }
      if (_zcam)
            emit _zcam->remove3dElement(_laserLayer);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginRemoveChild(_fixture, row);
      _fixture->removeChild(_laserLayer);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endRemoveChild();
      }

QString AddLaserLayerCommand::description() const {
      return QStringLiteral("Add LaserLayer");
      }

//---------------------------------------------------------
//   AddRectangleCommand implementation
//---------------------------------------------------------

AddRectangleCommand::AddRectangleCommand(ZCam* zcam, Layer* layer, double x, double y)
    : _zcam(zcam), _layer(layer) {
      _rect = new Rectangle(zcam, nullptr);
      _rect->set_size(QVector2D(0.0, 0.0));
      _rect->set_pos(QVector3D(x, y, 0.0));
      _rect->setColor(QColor("cyan"));
      _rect->set_lineWidth(0.5);
      _rect->set_fill(true);
      _rect->update();
      }

void AddRectangleCommand::redo() {
      if (!_layer || !_rect)
            return;
      int row = _layer->children().size();
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginInsertChild(_layer, row);
      _layer->addChild(_rect);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endInsertChild();
      if (_zcam)
            emit _zcam->add3dElement(_rect);
      }

void AddRectangleCommand::undo() {
      if (!_layer || !_rect)
            return;
      int row = 0;
      for (const auto c : _layer->children()) {
            if (c == _rect)
                  break;
            ++row;
            }
      if (_zcam)
            emit _zcam->remove3dElement(_rect);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginRemoveChild(_layer, row);
      _layer->removeChild(_rect);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endRemoveChild();
      }

Rectangle* AddRectangleCommand::rectangle() const {
      return _rect;
      }

//---------------------------------------------------------
//   AddPolygonCommand implementation
//---------------------------------------------------------

AddPolygonCommand::AddPolygonCommand(ZCam* zcam, Layer* layer, double x, double y)
    : _zcam(zcam), _layer(layer) {
      _poly = new Polygon(zcam, nullptr);
      _poly->set_pos(QVector3D(x, y, 0.0));
      _poly->setColor(QColor("cyan"));
      _poly->set_lineWidth(0.5);
      _poly->set_fill(true);
      _poly->update();
      }

void AddPolygonCommand::redo() {
      if (!_layer || !_poly)
            return;
      int row = _layer->children().size();
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginInsertChild(_layer, row);
      _layer->addChild(_poly);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endInsertChild();
      if (_zcam)
            emit _zcam->add3dElement(_poly);
      }

void AddPolygonCommand::undo() {
      if (!_layer || !_poly)
            return;
      int row = 0;
      for (const auto c : _layer->children()) {
            if (c == _poly)
                  break;
            ++row;
            }
      if (_zcam)
            emit _zcam->remove3dElement(_poly);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginRemoveChild(_layer, row);
      _layer->removeChild(_poly);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endRemoveChild();
      }

Polygon* AddPolygonCommand::polygon() const {
      return _poly;
      }

//---------------------------------------------------------
//   AddEllipseCommand implementation
//---------------------------------------------------------

AddEllipseCommand::AddEllipseCommand(ZCam* zcam, Layer* layer, double x, double y)
    : _zcam(zcam), _layer(layer) {
      _ellipse = new Ellipse(zcam, nullptr);
      _ellipse->set_size(QVector2D(0.0, 0.0));
      _ellipse->set_pos(QVector3D(x, y, 0.0));
      _ellipse->setColor(QColor("cyan"));
      _ellipse->set_lineWidth(0.5);
      _ellipse->set_fill(true);
      _ellipse->update();
      }

void AddEllipseCommand::redo() {
      if (!_layer || !_ellipse)
            return;
      int row = _layer->children().size();
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginInsertChild(_layer, row);
      _layer->addChild(_ellipse);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endInsertChild();
      if (_zcam)
            emit _zcam->add3dElement(_ellipse);
      }

void AddEllipseCommand::undo() {
      if (!_layer || !_ellipse)
            return;
      int row = 0;
      for (const auto c : _layer->children()) {
            if (c == _ellipse)
                  break;
            ++row;
            }
      if (_zcam)
            emit _zcam->remove3dElement(_ellipse);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginRemoveChild(_layer, row);
      _layer->removeChild(_ellipse);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endRemoveChild();
      }

Ellipse* AddEllipseCommand::ellipse() const {
      return _ellipse;
      }

//---------------------------------------------------------
//   AddTextCommand implementation
//---------------------------------------------------------

AddTextCommand::AddTextCommand(ZCam* zcam, Layer* layer, double x, double y) : _zcam(zcam), _layer(layer) {
      _text = new Text(zcam, nullptr);
      _text->set_text("");
      _text->set_pos(QVector3D(x, y, 0.0));
      _text->setColor(QColor("green"));
      _text->set_fill(true);
      _text->update();
      }

void AddTextCommand::redo() {
      if (!_layer || !_text)
            return;
      int row = _layer->children().size();
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginInsertChild(_layer, row);
      _layer->addChild(_text);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endInsertChild();
      if (_zcam)
            emit _zcam->add3dElement(_text);
      }

void AddTextCommand::undo() {
      if (!_layer || !_text)
            return;
      int row = 0;
      for (const auto c : _layer->children()) {
            if (c == _text)
                  break;
            ++row;
            }
      if (_zcam)
            emit _zcam->remove3dElement(_text);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginRemoveChild(_layer, row);
      _layer->removeChild(_text);
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endRemoveChild();
      }

Text* AddTextCommand::text() const {
      return _text;
      }

//---------------------------------------------------------
//   Project::addFixtureCmd
//    Create a new Fixture as child of the Cam element.
//    The operation is routed through the undo stack so it can
//    be undone/redone.
//---------------------------------------------------------

void Project::addFixtureCmd() {
      ZCam* zc = zcamInstance();
      if (!zc) {
            Critical("Project::addFixtureCmd: no ZCam instance");
            return;
            }
      Cam* cam = _cam;
      if (!cam) {
            Critical("Project::addFixtureCmd: no Cam element");
            return;
            }
      auto cmd = std::make_unique<AddFixtureCommand>(zc, cam);
      cmd->redo(); // apply immediately
      pushCommand(std::move(cmd));
      markDirty();
      zc->setCamDirty(true);
      }

//---------------------------------------------------------
//   Project::addLayer
//    Create a new Layer as child of the Cad element and a
//    corresponding LaserLayer as child of the Fixture element.
//    The operation is routed through the undo stack so it can
//    be undone/redone.
//---------------------------------------------------------

void Project::addLayer() {
      ZCam* zc = zcamInstance();
      if (!zc) {
            Critical("Project::addLayer: no ZCam instance");
            return;
            }
      Cad* cad = _cad;
      if (!cad) {
            Critical("Project::addLayer: no Cad element");
            return;
            }
      Fixture* fixture = _fixture;
      auto cmd         = std::make_unique<AddLayerCommand>(zc, cad, fixture);
      cmd->redo(); // apply immediately
      pushCommand(std::move(cmd));
      markDirty();
      zc->setCamDirty(true);
      // The new Layer is referenced by a LaserLayer in the active fixture,
      // so make it visible on the 3D canvas.
      updateCadLayerVisibility();
      }

//---------------------------------------------------------
//   Project::addLaserLayerCmd
//    Create a new LaserLayer as child of the given Fixture
//    element.  The LaserLayer is auto-linked to the first
//    available Cad Layer.  The operation is routed through
//    the undo stack so it can be undone/redone.
//---------------------------------------------------------

void Project::addLaserLayerCmd(Fixture* fixture) {
      ZCam* zc = zcamInstance();
      if (!zc) {
            Critical("Project::addLaserLayerCmd: no ZCam instance");
            return;
            }
      if (!fixture) {
            Critical("Project::addLaserLayerCmd: no Fixture element");
            return;
            }
      auto cmd = std::make_unique<AddLaserLayerCommand>(zc, fixture);
      cmd->redo(); // apply immediately
      pushCommand(std::move(cmd));
      markDirty();
      zc->setCamDirty(true);
      updateCadLayerVisibility();
      }

//---------------------------------------------------------
//   RemoveElementCommand implementation
//---------------------------------------------------------

void RemoveElementCommand::redo() {
      if (!_parent || !_child)
            return;
      // find the current row of the child in parent's children
      int row = 0;
      for (const auto c : _parent->children()) {
            if (c == _child)
                  break;
            ++row;
            }
      _row = row;
      if (_zcam)
            emit _zcam->remove3dElement(qobject_cast<Element3d*>(_child.data()));
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginRemoveChild(_parent, _row);
      _parent->removeChild(_child);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endRemoveChild();

      // If the element is a Fixture, remove it from the project fixture
      // list so the fixture selector combo box updates and the active
      // fixture pointer is adjusted (pick another or set to nullptr).
      if (!_removedFixture)
            _removedFixture = qobject_cast<Fixture*>(_child.data());
      if (_removedFixture && _zcam && _zcam->project())
            _zcam->project()->removeFixture(_removedFixture);

      // Remove all linked LaserLayers
      for (auto& link : _linkedLaserLayers) {
            if (!link.parent || !link.ll)
                  continue;
            int llRow = 0;
            for (const auto c : link.parent->children()) {
                  if (c == link.ll)
                        break;
                  ++llRow;
                  }
            link.row = llRow;
            if (_zcam)
                  emit _zcam->remove3dElement(qobject_cast<Element3d*>(link.ll.data()));
            if (_zcam && _zcam->treeModel())
                  _zcam->treeModel()->beginRemoveChild(link.parent, llRow);
            link.parent->removeChild(link.ll);
            if (_zcam && _zcam->treeModel())
                  _zcam->treeModel()->endRemoveChild();
            }
      }

void RemoveElementCommand::undo() {
      if (!_parent || !_child)
            return;
      // Re-insert all linked LaserLayers first (in reverse order)
      for (auto it = _linkedLaserLayers.rbegin(); it != _linkedLaserLayers.rend(); ++it) {
            auto& link = *it;
            if (!link.parent || !link.ll)
                  continue;
            if (_zcam && _zcam->treeModel())
                  _zcam->treeModel()->beginInsertChild(link.parent, link.row);
            link.parent->addChild(link.ll);
            auto& kids  = const_cast<QList<Element*>&>(link.parent->children());
            int lastRow = kids.size() - 1;
            if (link.row >= 0 && link.row < lastRow)
                  kids.move(lastRow, link.row);
            if (_zcam && _zcam->treeModel())
                  _zcam->treeModel()->endInsertChild();
            if (_zcam)
                  emit _zcam->add3dElement(qobject_cast<Element3d*>(link.ll.data()));
            }

      // Re-insert the child
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginInsertChild(_parent, _row);
      _parent->addChild(_child);
      auto& kids  = const_cast<QList<Element*>&>(_parent->children());
      int lastRow = kids.size() - 1;
      if (_row >= 0 && _row < lastRow)
            kids.move(lastRow, _row);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endInsertChild();
      if (_zcam)
            emit _zcam->add3dElement(qobject_cast<Element3d*>(_child.data()));

      // If the element was a Fixture, re-add it to the project fixture list.
      // addFixture() will also set it as active if no fixture is currently set.
      if (_removedFixture && _zcam && _zcam->project())
            _zcam->project()->addFixture(_removedFixture);
      }

QString RemoveElementCommand::description() const {
      return QStringLiteral("Remove Element");
      }

//---------------------------------------------------------
//   MoveElementCommand implementation
//---------------------------------------------------------

void MoveElementCommand::redo() {
      if (!_element || !_oldParent || !_newParent)
            return;

      // Find the current row of the element in old parent's children
      int oldRow = 0;
      for (const auto c : _oldParent->children()) {
            if (c == _element)
                  break;
            ++oldRow;
            }
      _oldRow = oldRow;

      // Clamp target row
      int newRow = _newRow;
      if (newRow < 0 || newRow > _newParent->children().size())
            newRow = _newParent->children().size();
      _newRow = newRow;

      // If same parent and target row is after the old row, decrement
      // because the element will be removed first, shifting later rows down.
      bool sameParent = (_oldParent == _newParent);
      int adjustedRow = newRow;
      if (sameParent && newRow > oldRow)
            --adjustedRow;

      // Remove from old parent
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginRemoveChild(_oldParent, oldRow);
      _oldParent->removeChild(_element);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endRemoveChild();

      // If the parent changes, notify the 3D scene about the removal
      if (!sameParent && _zcam)
            emit _zcam->remove3dElement(qobject_cast<Element3d*>(_element.data()));

      // Insert into new parent at the adjusted position
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginInsertChild(_newParent, adjustedRow);
      _newParent->addChild(_element);
      auto& kids  = const_cast<QList<Element*>&>(_newParent->children());
      int lastRow = kids.size() - 1;
      if (adjustedRow >= 0 && adjustedRow < lastRow)
            kids.move(lastRow, adjustedRow);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endInsertChild();

      // If the parent changes, notify the 3D scene about the addition
      if (!sameParent && _zcam)
            emit _zcam->add3dElement(qobject_cast<Element3d*>(_element.data()));
      }

void MoveElementCommand::undo() {
      if (!_element || !_oldParent || !_newParent)
            return;

      // Find the current row in the new parent
      int curRow = 0;
      for (const auto c : _newParent->children()) {
            if (c == _element)
                  break;
            ++curRow;
            }

      bool sameParent = (_oldParent == _newParent);
      int oldRow      = _oldRow;
      // If same parent and old row was after current row, decrement
      if (sameParent && oldRow > curRow)
            --oldRow;

      // Remove from new parent
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginRemoveChild(_newParent, curRow);
      _newParent->removeChild(_element);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endRemoveChild();

      if (!sameParent && _zcam)
            emit _zcam->remove3dElement(qobject_cast<Element3d*>(_element.data()));

      // Insert back into old parent at original position
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginInsertChild(_oldParent, oldRow);
      _oldParent->addChild(_element);
      auto& kids  = const_cast<QList<Element*>&>(_oldParent->children());
      int lastRow = kids.size() - 1;
      if (oldRow >= 0 && oldRow < lastRow)
            kids.move(lastRow, oldRow);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endInsertChild();

      if (!sameParent && _zcam)
            emit _zcam->add3dElement(qobject_cast<Element3d*>(_element.data()));
      }

//---------------------------------------------------------
//   Project::removeElement
//    Remove an element from the project tree.  If the element
//    is a Layer, all LaserLayers referencing it as baseElement
//    are also removed.  The operation is undoable.
//---------------------------------------------------------

void Project::removeElement(Element* el) {
      ZCam* zc = zcamInstance();
      if (!zc) {
            Critical("Project::removeElement: no ZCam instance");
            return;
            }
      if (!el) {
            Critical("Project::removeElement: null element");
            return;
            }
      Element* parent = el->parent();
      if (!parent) {
            Critical("Project::removeElement: element has no parent");
            return;
            }
      int row = 0;
      for (const auto c : parent->children()) {
            if (c == el)
                  break;
            ++row;
            }
      auto cmd = std::make_unique<RemoveElementCommand>(zc, parent, el, row);

      // If the element is a Layer, find all elements in the Cad tree
      // whose effectiveLaserLayer references a LaserLayer that is a child
      // of the current fixture, so we can also remove those LaserLayers.
      auto layer = qobject_cast<Layer*>(el);
      if (layer && _fixture) {
            // Find LaserLayers in the fixture whose collectElements()
            // includes any element under the removed Layer.
            for (const auto c : _fixture->children()) {
                  auto ll = qobject_cast<LaserLayer*>(c);
                  if (!ll)
                        continue;
                  // Check if any element in the LaserLayer's collection
                  // is a descendant of the removed Layer.
                  auto elements = ll->collectElements();
                  bool hasDescendant = false;
                  for (const auto* elem : elements) {
                        const Element* p = elem;
                        while (p) {
                              if (p == layer) {
                                    hasDescendant = true;
                                    break;
                                    }
                              p = p->parent();
                              }
                        if (hasDescendant)
                              break;
                        }
                  if (hasDescendant) {
                        int llRow = 0;
                        for (const auto cc : _fixture->children()) {
                              if (cc == ll)
                                    break;
                              ++llRow;
                              }
                        cmd->_linkedLaserLayers.push_back({_fixture, ll, llRow});
                        }
                  }
            }

      cmd->redo(); // apply immediately
      pushCommand(std::move(cmd));
      markDirty();
      zc->setCamDirty(true);
      // Update CAD layer visibility since removing a Layer or LaserLayer
      // may change which layers are referenced by the active fixture.
      updateCadLayerVisibility();
      }

//---------------------------------------------------------
//   Project::moveElement
//    Move an element to a new parent (or a new position within
//    the same parent).  The operation is undoable.
//---------------------------------------------------------

void Project::moveElement(Element* element, Element* newParent, int newRow) {
      ZCam* zc = zcamInstance();
      if (!zc) {
            Critical("Project::moveElement: no ZCam instance");
            return;
            }
      if (!element || !newParent) {
            Critical("Project::moveElement: null element or newParent");
            return;
            }
      Element* oldParent = element->parent();
      if (!oldParent) {
            Critical("Project::moveElement: element has no parent");
            return;
            }
      // Prevent moving an element into itself or one of its descendants
      Element* p = newParent;
      while (p) {
            if (p == element) {
                  Critical("Project::moveElement: cannot move element into itself or a descendant");
                  return;
                  }
            p = p->parent();
            }
      int oldRow = 0;
      for (const auto c : oldParent->children()) {
            if (c == element)
                  break;
            ++oldRow;
            }
      if (oldParent == newParent) {
            // Same parent: check if the row actually changes
            int adjustedNew = newRow;
            if (adjustedNew < 0 || adjustedNew > oldParent->children().size())
                  adjustedNew = oldParent->children().size();
            // If moving within same parent to same position (adjusted), no-op
            if (adjustedNew == oldRow || adjustedNew == oldRow + 1)
                  return;
            }
      auto cmd = std::make_unique<MoveElementCommand>(zc, element, oldParent, oldRow, newParent, newRow);
      cmd->redo(); // apply immediately
      pushCommand(std::move(cmd));
      markDirty();
      zc->setCamDirty(true);
      updateCadLayerVisibility();
      }

//---------------------------------------------------------
//   InsertElementCommand implementation
//---------------------------------------------------------

void InsertElementCommand::redo() {
      if (!_parent || !_element)
            return;
      int row = _row;
      if (row < 0 || row > _parent->children().size())
            row = _parent->children().size();
      _row = row;
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginInsertChild(_parent, row);
      _parent->addChild(_element);
      // addChild appends at end; move to the requested row if needed
      auto& kids  = const_cast<QList<Element*>&>(_parent->children());
      int lastRow = kids.size() - 1;
      if (row >= 0 && row < lastRow)
            kids.move(lastRow, row);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endInsertChild();
      if (_zcam)
            emit _zcam->add3dElement(qobject_cast<Element3d*>(_element.data()));
      }

void InsertElementCommand::undo() {
      if (!_parent || !_element)
            return;
      int row = 0;
      for (const auto c : _parent->children()) {
            if (c == _element)
                  break;
            ++row;
            }
      _row = row;
      if (_zcam)
            emit _zcam->remove3dElement(qobject_cast<Element3d*>(_element.data()));
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->beginRemoveChild(_parent, row);
      _parent->removeChild(_element);
      if (_zcam && _zcam->treeModel())
            _zcam->treeModel()->endRemoveChild();
      }