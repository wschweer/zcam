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
#include <QVector3D>
#include <QByteArray>
#include <QPointer>
#include <QString>
#include <memory>
#include <vector>

class ZCam;
class Cad;
class Cam;
class Group;
class Element;
class Element3d;
class Fixture;
class Recipe;
class Rectangle;
class Polygon;
class Ellipse;
class Text;

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
      /// Returns true when *this command can absorb the information
      /// from \a other (e.g. same element + same property), so that
      /// no new history entry needs to be created.
      virtual bool canCoalesceWith(const UndoCommand& other) const {
            (void)other;
            return false;
            }
      /// Merge the new value from \a other into this command.
      /// Called only after canCoalesceWith() returned true.
      virtual void coalesceFrom(const UndoCommand& other) { (void)other; }
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
      /// Coalesce when both commands target the same live element and
      /// the same property name.  The old value of the first command is
      /// kept; only the new value is updated from @a other.
      bool canCoalesceWith(const UndoCommand& other) const override {
            const auto* o = dynamic_cast<const PropertyChangeCommand*>(&other);
            if (!o)
                  return false;
            return _element != nullptr && _element == o->_element && _propName == o->_propName;
            }
      void coalesceFrom(const UndoCommand& other) override {
            const auto* o = dynamic_cast<const PropertyChangeCommand*>(&other);
            if (o)
                  _newValue = o->_newValue;
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
      QPointer<Group> _layer;
      QPointer<Fixture> _fixture;
      QPointer<Recipe> _laserLayer;
      QPointer<ZCam> _zcam;
      int _row {-1};   ///< position within cad's children
      int _llRow {-1}; ///< position within fixture's children

    public:
      AddLayerCommand(ZCam* zcam, Cad* cad, Fixture* fixture);

      void undo() override;
      void redo() override;
      QString description() const override;
      };

class AddFixtureCommand : public UndoCommand
      {
      QPointer<ZCam> _zcam;
      QPointer<Cam> _cam;
      QPointer<Fixture> _fixture;
      int _row {-1}; ///< position within cam's children

    public:
      AddFixtureCommand(ZCam* zcam, Cam* cam);

      void undo() override;
      void redo() override;
      QString description() const override;
      };

//---------------------------------------------------------
//   AddLaserLayerCommand
//    Undoable command that creates a new LaserLayer and inserts
//    it as a child of a Fixture element.  The LaserLayer is
//    auto-linked to the first available Cad Layer (if any).
//    undo() removes the LaserLayer from the tree (and scene);
//    redo() re-inserts it.
//---------------------------------------------------------

class AddLaserLayerCommand : public UndoCommand
      {
      QPointer<ZCam> _zcam;
      QPointer<Fixture> _fixture;
      QPointer<Recipe> _laserLayer;
      int _row {-1}; ///< position within fixture's children

    public:
      AddLaserLayerCommand(ZCam* zcam, Fixture* fixture);

      void undo() override;
      void redo() override;
      QString description() const override;
      };

//---------------------------------------------------------
//   AddRectangleCommand
//    Undoable command that creates a new Rectangle, inserts it
//    as a child of a Layer, updates the tree model and notifies
//    the 3D scene.
//    undo() removes the Rectangle from the tree (and scene);
//    redo() re-inserts it.
//---------------------------------------------------------

class AddRectangleCommand : public UndoCommand
      {
      QPointer<ZCam> _zcam;
      QPointer<Group> _layer;
      QPointer<Rectangle> _rect;
      int _row {-1}; ///< position within layer's children

    public:
      AddRectangleCommand(ZCam* zcam, Group* layer, double x, double y);

      void undo() override;
      void redo() override;
      QString description() const override { return QStringLiteral("Add Rectangle"); }
      Rectangle* rectangle() const;
      };

//---------------------------------------------------------
//   AddPolygonCommand
//    Undoable command that creates a new Polygon, inserts it
//    as a child of a Layer, updates the tree model and notifies
//    the 3D scene.
//    undo() removes the Polygon from the tree (and scene);
//    redo() re-inserts it.
//---------------------------------------------------------

class AddPolygonCommand : public UndoCommand
      {
      QPointer<ZCam> _zcam;
      QPointer<Group> _layer;
      QPointer<Polygon> _poly;
      int _row {-1}; ///< position within layer's children

    public:
      AddPolygonCommand(ZCam* zcam, Group* layer, double x, double y);

      void undo() override;
      void redo() override;
      QString description() const override { return QStringLiteral("Add Polygon"); }
      Polygon* polygon() const;
      };

//---------------------------------------------------------
//   AddEllipseCommand
//    Undoable command that creates a new Ellipse, inserts it
//    as a child of a Layer, updates the tree model and notifies
//    the 3D scene.
//    undo() removes the Ellipse from the tree (and scene);
//    redo() re-inserts it.
//---------------------------------------------------------

class AddEllipseCommand : public UndoCommand
      {
      QPointer<ZCam> _zcam;
      QPointer<Group> _layer;
      QPointer<Ellipse> _ellipse;
      int _row {-1}; ///< position within layer's children

    public:
      AddEllipseCommand(ZCam* zcam, Group* layer, double x, double y);

      void undo() override;
      void redo() override;
      QString description() const override { return QStringLiteral("Add Ellipse"); }
      Ellipse* ellipse() const;
      };

//---------------------------------------------------------
//   AddTextCommand
//    Undoable command that creates a new Text element, inserts it
//    as a child of a Layer, updates the tree model and notifies
//    the 3D scene.
//---------------------------------------------------------

class AddTextCommand : public UndoCommand
      {
      QPointer<ZCam> _zcam;
      QPointer<Group> _layer;
      QPointer<Text> _text;
      int _row {-1}; ///< position within layer's children

    public:
      AddTextCommand(ZCam* zcam, Group* layer, double x, double y);

      void undo() override;
      void redo() override;
      QString description() const override { return QStringLiteral("Add Text"); }
      Text* text() const;
      };

//---------------------------------------------------------
//   HandleDragCommand
//    Records a single handle position change on any Element3d
//    (Polygon vertex, Rectangle corner, etc.) so that it can
//    be reverted (undo) or re-applied (redo) via the
//    setVertexPos() virtual method.
//---------------------------------------------------------

class HandleDragCommand : public UndoCommand
      {
      QPointer<Element3d> _element;
      int _handleIndex;
      QVector3D _oldPos;
      QVector3D _newPos;

    public:
      HandleDragCommand(Element3d* el, int idx, const QVector3D& oldPos, const QVector3D& newPos)
          : _element(el), _handleIndex(idx), _oldPos(oldPos), _newPos(newPos) {}
      void undo() override;
      void redo() override;
      QString description() const override { return QStringLiteral("Drag handle"); }
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
      ///< Associated LaserLayers that reference elements under the Layer
      ///< via the laserLayer property.  Stored so they can be removed/restored
      ///< together with the Layer.
      struct LinkedLaserLayer {
            QPointer<Fixture> parent;
            QPointer<Recipe> ll;
            int row {-1};
            };
      std::vector<LinkedLaserLayer> _linkedLaserLayers;
      /// If the removed element is a Fixture, this stores the pointer so
      /// that redo() can remove it from the project fixture list and
      /// undo() can re-add it.
      QPointer<Fixture> _removedFixture;
      RemoveElementCommand(ZCam* zcam, Element* parent, Element* child, int row)
          : _parent(parent), _child(child), _zcam(zcam), _row(row) {}
      void undo() override;
      void redo() override;
      QString description() const override;
      };

//---------------------------------------------------------
//   MoveElementCommand
//    Undoable command that moves an Element from one parent
//    to another (or to a different position within the same
//    parent).  undo() moves it back; redo() moves it again.
//    Both operations update the tree model and, when the parent
//    actually changes, notify the 3D scene.
//---------------------------------------------------------

class MoveElementCommand : public UndoCommand
      {
      QPointer<ZCam> _zcam;
      QPointer<Element> _element;
      QPointer<Element> _oldParent;
      QPointer<Element> _newParent;
      int _oldRow {-1}; ///< original position within old parent
      int _newRow {-1}; ///< target position within new parent

    public:
      MoveElementCommand(ZCam* zcam, Element* element, Element* oldParent, int oldRow, Element* newParent,
                         int newRow)
          : _zcam(zcam), _element(element), _oldParent(oldParent), _newParent(newParent), _oldRow(oldRow),
            _newRow(newRow) {}
      void undo() override;
      void redo() override;
      QString description() const override { return QStringLiteral("Move Element"); }
      };

//---------------------------------------------------------
//   InsertElementCommand
//    Undoable command that inserts a pre-created Element into
//    a parent at a given row (or at the end when row == -1).
//    undo() removes it; redo() re-inserts it.  Both operations
//    update the tree model and notify the 3D scene.
//---------------------------------------------------------

class InsertElementCommand : public UndoCommand
      {
      QPointer<ZCam> _zcam;
      QPointer<Element> _parent;
      QPointer<Element> _element;
      int _row {-1};

    public:
      InsertElementCommand(ZCam* zcam, Element* parent, Element* element, int row)
          : _zcam(zcam), _parent(parent), _element(element), _row(row) {}
      void undo() override;
      void redo() override;
      QString description() const override { return QStringLiteral("Insert Element"); }
      };