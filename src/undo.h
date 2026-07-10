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

#pragma once

#include <QObject>
#include <QVariant>
#include <QByteArray>
#include <QPointer>
#include <QString>
#include <memory>
#include <vector>

class ZCam;
class Cad;
class Layer;
class Element;
class Fixture;
class LaserLayer;

//---------------------------------------------------------
//   UndoCommand
//    Abstract base class for all undoable operations.
//---------------------------------------------------------

class UndoCommand
      {
    public:
      virtual ~UndoCommand() = default;
      virtual void undo()    = 0;
      virtual void redo()    = 0;
      virtual QString description() const { return {}; }
      };

//---------------------------------------------------------
//   PropertyChangeCommand
//    Records a single property change on a QObject so that it
//    can be reverted (undo) or re-applied (redo).
//---------------------------------------------------------

class PropertyChangeCommand : public UndoCommand
      {
      QPointer<QObject> _element;
      QByteArray _propName;
      QVariant _oldValue;
      QVariant _newValue;

    public:
      PropertyChangeCommand(QObject* el, const QByteArray& prop, const QVariant& oldVal,
                            const QVariant& newVal)
          : _element(el), _propName(prop), _oldValue(oldVal), _newValue(newVal) {}
      void undo() override {
            if (_element)
                  _element->setProperty(_propName.constData(), _oldValue);
            }
      void redo() override {
            if (_element)
                  _element->setProperty(_propName.constData(), _newValue);
            }
      QString description() const override {
            return QStringLiteral("Change %1").arg(QString::fromUtf8(_propName));
            }
      };

//---------------------------------------------------------
//   RenameElementCommand
//    Undoable command that renames an Element.  Unlike
//    PropertyChangeCommand, this stores the actual de-duplicated
//    name that Element::setName() assigned, not the user-typed
//    text.  This ensures undo/redo restores the exact same name.
//---------------------------------------------------------

class RenameElementCommand : public UndoCommand
      {
      QPointer<Element> _element;
      QString _oldName;
      QString _newName; // actual name after de-duplication

    public:
      RenameElementCommand(Element* el, const QString& oldName, const QString& newName)
          : _element(el), _oldName(oldName), _newName(newName) {}
      void undo() override;
      void redo() override;
      QString description() const override { return QStringLiteral("Rename element"); }
      };

//---------------------------------------------------------
//   AddLayerCommand
//    Undoable command that creates a new Layer, inserts it as
//    a child of the Cad element, updates the tree model and
//    notifies the 3D scene.
//    undo() removes the Layer from the tree (and scene); redo()
//    re-inserts it.
//---------------------------------------------------------

class AddLayerCommand : public UndoCommand
      {
      QPointer<Cad> _cad;
      QPointer<Layer> _layer;
      QPointer<Fixture> _fixture;
      QPointer<LaserLayer> _laserLayer;
      QPointer<ZCam> _zcam;
      int _row {-1};   ///< position within cad's children
      int _llRow {-1}; ///< position within fixture's children

    public:
      AddLayerCommand(ZCam* zcam, Cad* cad, Fixture* fixture);

      void undo() override;
      void redo() override;
      QString description() const override;
      };

//---------------------------------------------------------
//   RemoveElementCommand
//    Undoable command that removes an Element from its parent.
//    undo() re-inserts it at the original position; redo()
//    removes it again.  Both operations update the tree model
//    and notify the 3D scene.
//---------------------------------------------------------

class RemoveElementCommand : public UndoCommand
      {
      QPointer<Element> _parent;
      QPointer<Element> _child;
      QPointer<ZCam> _zcam;
      int _row {-1}; ///< original position within parent's children

    public:
      ///< Associated LaserLayers that reference the Layer as baseElement.
      ///< Stored so they can be removed/restored together with the Layer.
      struct LinkedLaserLayer {
            QPointer<Fixture> parent;
            QPointer<LaserLayer> ll;
            int row {-1};
            };
      std::vector<LinkedLaserLayer> _linkedLaserLayers;
      RemoveElementCommand(ZCam* zcam, Element* parent, Element* child, int row)
          : _parent(parent), _child(child), _zcam(zcam), _row(row) {}
      void undo() override;
      void redo() override;
      QString description() const override;
      };