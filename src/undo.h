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

#include <QList>
#include <QVariant>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

#include "logger.h"
#include "element.h"

class UndoCommand;
class ZCam;
class Cad;
class Cam;
class Group;
class Element3d;
class Fixture;
class Recipe;
class Rectangle;
class Polygon;
class Ellipse;
class Text;

//---------------------------------------------------------
//   UndoStack
//---------------------------------------------------------

class UndoStack : public QObject {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
      Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
      Q_PROPERTY(bool dirty READ dirty NOTIFY undoChanged)

      UndoCommand* curCmd { nullptr };
      QList<UndoCommand*> macroStack;  ///< stack of parent macros for nesting
      QList<UndoCommand*> list;
      int curIdx   { 0 };
      int cleanIdx { 0 };
      bool _active { true };   // don't record commands if not active
      bool inUndoRedo { false };

      ZCam* zcam;

   signals:
      void undoChanged();
      void dirtyChanged();

    public:
      UndoStack(ZCam* zc, QObject* parent = nullptr) : QObject(parent), zcam(zc) {};
      ~UndoStack();

      void reset();
      void beginMacro();
      void endMacro(bool rollback = false);
      void push(UndoCommand*); // push & execute
      void push1(UndoCommand*);
      void pop();
      void setClean()      { cleanIdx = curIdx; emit dirtyChanged(); }     // this is set by project->save()
      bool canUndo() const { return curIdx > 0; }
      bool canRedo() const { return curIdx < list.size(); }
      bool dirty() const   { return cleanIdx != curIdx; }
      bool isActive() const { return curCmd != nullptr; }

      Q_INVOKABLE void undo();
      Q_INVOKABLE void redo();
      };

//---------------------------------------------------------
//   UndoCommand
//---------------------------------------------------------

class UndoCommand {
      QList<UndoCommand*> childList;

    protected:
      ZCam* zcam;
      virtual void flip() {}

    public:
      UndoCommand(ZCam* w) : zcam(w) {}
      UndoCommand(const UndoCommand&) = delete;
      virtual ~UndoCommand();
      virtual void undo();
      virtual void redo();

      void appendChild(UndoCommand* cmd) { childList.append(cmd); }
      UndoCommand* removeChild()         { return childList.takeLast(); }
      int childCount() const             { return childList.size(); }
      void unwind();
      virtual void cleanup(bool undo);
      virtual std::string description() const { return "?"; }
      };

//---------------------------------------------------------
//   PropertyChangeCommand
//    Records a single property change on a QObject so that it
//    can be reverted (undo) or re-applied (redo).
//---------------------------------------------------------

class PropertyChangeCommand : public UndoCommand
      {
      Element* _element;
      std::string _propName;
      QVariant _oldValue;
      QVariant _newValue;

    public:
      PropertyChangeCommand(ZCam* zc, Element* el, const std::string& n, const QVariant& ov, const QVariant& nv)
          : UndoCommand(zc), _element(el), _propName(n), _oldValue(ov), _newValue(nv) {}
      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("Change {} of {}", _propName, _element->name()); }
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
      Element* _element;
      QString _oldName;
      QString _newName; // actual name after de-duplication

    public:
      RenameElementCommand(ZCam* zc, Element* el, const QString& oldName, const QString& newName)
          : UndoCommand(zc), _element(el), _oldName(oldName), _newName(newName) {}
      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("Rename {} to {}", _oldName, _newName); }
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
      Cad* _cad;
      Group* _layer;
      Fixture* _fixture;
      Recipe* _laserLayer;
      int _row {-1};   ///< position within cad's children
      int _llRow {-1}; ///< position within fixture's children

    public:
      AddLayerCommand(ZCam* zcam, Cad* cad, Fixture* fixture);

      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("AddLayer"); }
      };

//---------------------------------------------------------
//   AddFixtureCommand
//---------------------------------------------------------

class AddFixtureCommand : public UndoCommand
      {
      Cam* _cam;
      Fixture* _fixture;
      int _row {-1}; ///< position within cam's children

    public:
      AddFixtureCommand(ZCam* zcam, Cam* cam);

      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("AddFixture"); }
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
      Fixture* _fixture;
      Recipe* _laserLayer;
      int _row {-1}; ///< position within fixture's children

    public:
      AddLaserLayerCommand(ZCam* zcam, Fixture* fixture);

      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("AddRecipe"); }
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
      Group* _layer;
      Rectangle* _rect;
      int _row {-1}; ///< position within layer's children

    public:
      AddRectangleCommand(ZCam* zcam, Group* layer, double x, double y);

      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("AddRectangle"); }
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
      Group* _layer;
      Polygon* _poly;
      int _row {-1}; ///< position within layer's children

    public:
      AddPolygonCommand(ZCam* zcam, Group* layer, double x, double y);

      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("Add Polygon"); }
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
      Group* _layer;
      Ellipse* _ellipse;
      int _row {-1}; ///< position within layer's children

    public:
      AddEllipseCommand(ZCam* zcam, Group* layer, double x, double y);

      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("Add Ellipse"); }
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
      Group* _layer;
      Text* _text;
      int _row {-1}; ///< position within layer's children

    public:
      AddTextCommand(ZCam* zcam, Group* layer, double x, double y);

      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("Add Text"); }
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
      Element3d* _element;
      int _handleIndex;
      QVector3D _oldPos;
      QVector3D _newPos;

    public:
      HandleDragCommand(ZCam* zc, Element3d* el, int idx, const QVector3D& oldPos, const QVector3D& newPos)
          : UndoCommand(zc), _element(el), _handleIndex(idx), _oldPos(oldPos), _newPos(newPos) {}
      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("Drag handle"); }
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
      Element* _parent;
      Element* _child;
      int _row {-1}; ///< original position within parent's children

    public:
      /// If the removed element is a Fixture, this stores the pointer so
      /// that redo() can remove it from the project fixture list and
      /// undo() can re-add it.
      Fixture* _removedFixture;
      RemoveElementCommand(ZCam* zc, Element* parent, Element* child, int row)
          : UndoCommand(zc), _parent(parent), _child(child), _row(row) {}
      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("Remove Element"); }
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
      Element* _element;
      Element* _oldParent;
      Element* _newParent;
      int _oldRow {-1}; ///< original position within old parent
      int _newRow {-1}; ///< target position within new parent

    public:
      MoveElementCommand(ZCam* zc, Element* element, Element* oldParent, int oldRow, Element* newParent, int newRow)
          : UndoCommand(zc), _element(element), _oldParent(oldParent), _newParent(newParent), _oldRow(oldRow),
            _newRow(newRow) {}
      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("Move Element"); }
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
      Element* _parent;
      Element* _element;
      int _row {-1};

    public:
      InsertElementCommand(ZCam* zc, Element* parent, Element* element, int row)
          : UndoCommand(zc), _parent(parent), _element(element), _row(row) {}
      void undo() override;
      void redo() override;
      std::string description() const override { return std::format("Insert Element"); }
      };
