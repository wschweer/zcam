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

#include "element.h"
#include "cad.h"
#include "cam.h"
#include "fixture.h"
#include "machine.h"
#include <QPointer>

//---------------------------------------------------------
//   TopLevel
//---------------------------------------------------------

class Project : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no no")

      PROPV(Cad*, cad, nullptr)
      PROPV(Cam*, cam, nullptr)
      PROPV(Fixture*, fixture, nullptr)

      Q_PROPERTY(Machine* machine READ machine WRITE set_machine NOTIFY machineChanged)
      Q_PROPERTY(QString machineName READ machineName NOTIFY machineChanged)
      Q_PROPERTY(QList<Fixture*> fixtures READ fixtures NOTIFY fixturesChanged)
      Q_PROPERTY(Element3d* gridElement READ gridElement NOTIFY gridElementChanged)

      QList<Fixture*> _fixtures;
      QPointer<Machine> _machine;
      QString _machineName; ///< kept in sync with _machine for re-resolution after Machines changes

      inline static constexpr std::string_view _properties {R"({
            "class": "Project",
            "items": [
                  { "name": "machine", "label": "Machine", "type": "machine" }
                  ]
                                                      })"};

    signals:
      void machineChanged();
      void fixturesChanged();
      void updateFraming(int);
      void gridElementChanged();

    public:
      Project(ZCam*, Element* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("project"); }
      virtual const std::string_view properties() const override { return _properties; }
      Machine* machine() const { return _machine; }
      QString machineName() const { return _machine ? _machine->name() : QString(); }
      /// Returns the Grid child element, or nullptr if none exists.
      Element3d* gridElement() const;
      void set_machine(Machine* m);
      /// Re-resolve the Machine* from _machineName after the Machines asset changes.
      void resolveMachine();
      const QList<Fixture*>& fixtures() const { return _fixtures; }
      void addFixture(Fixture* f);
      void removeFixture(Fixture* f);

      /// Update the visibility (show property) of all CAD Layers based
      /// on whether they are referenced by at least one LaserLayer in
      /// the currently active fixture.  Layers not referenced by the
      /// active fixture are hidden so the 3D canvas only shows geometry
      /// belonging to the selected fixture.
      void updateCadLayerVisibility();

      /// Create a new Fixture as child of the Cam element.
      /// The operation is undoable via the ProjectManager undo stack.
      Q_INVOKABLE void addFixtureCmd();

      /// Add a new Layer as child of the Cad element.
      /// The operation is undoable via the ProjectManager undo stack.
      Q_INVOKABLE void addLayer();

      /// Add a new LaserLayer as child of the given Fixture element.
      /// The operation is undoable via the ProjectManager undo stack.
      Q_INVOKABLE void addLaserLayerCmd(Fixture* fixture);

      /// Remove an element (e.g. a Layer) from the project tree.
      /// The operation is undoable via the ProjectManager undo stack.
      Q_INVOKABLE void removeElement(Element* el);

      /// Move an element to a new parent (or a new position within the
      /// same parent).  The operation is undoable via the ProjectManager
      /// undo stack.
      /// \param element   the element to move
      /// \param newParent the destination parent (must be non-null)
      /// \param newRow    the desired row within newParent's children,
      ///                  or -1 to append at the end
      Q_INVOKABLE void moveElement(Element* element, Element* newParent, int newRow);
      };
