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
#include "zcam.h"
#include "projectmanager.h"
#include "treemodel.h"
#include "undo.h"
#include "logger.h"

//---------------------------------------------------------
//   Project
//---------------------------------------------------------

Project::Project(ZCam* zcam, Element* parent) : Element3d(zcam, parent) {
      Element::setName("project");
      }

//---------------------------------------------------------
//   AddLayerCommand implementation
//---------------------------------------------------------

AddLayerCommand::AddLayerCommand(ZCam* zcam, Cad* cad, Fixture* fixture)
    : _cad(cad), _fixture(fixture), _zcam(zcam) {
      _layer = new Layer(zcam, nullptr);
      if (_fixture) {
            _laserLayer = new LaserLayer(zcam, nullptr);
            _laserLayer->set_baseElement(_layer);
            _laserLayer->setName(QString("LL-%1").arg(_layer->name()));
            }
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
      auto pm          = zc->projectManager();
      if (!pm) {
            Critical("Project::addLayer: no ProjectManager");
            return;
            }
      auto cmd = std::make_unique<AddLayerCommand>(zc, cad, fixture);
      cmd->redo(); // apply immediately
      pm->pushCommand(std::move(cmd));
      pm->markDirty();
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
      }

QString RemoveElementCommand::description() const {
      return QStringLiteral("Remove Element");
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
      auto pm = zc->projectManager();
      if (!pm) {
            Critical("Project::removeElement: no ProjectManager");
            return;
            }
      auto cmd = std::make_unique<RemoveElementCommand>(zc, parent, el, row);

      // If the element is a Layer, find all LaserLayers that reference it
      auto layer = qobject_cast<Layer*>(el);
      if (layer && _fixture) {
            for (const auto c : _fixture->children()) {
                  auto ll = qobject_cast<LaserLayer*>(c);
                  if (ll && ll->baseElement() == layer) {
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
      pm->pushCommand(std::move(cmd));
      pm->markDirty();
      }