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
      inline static constexpr std::string_view _properties {R"({
            "class": "Project",
            "items": [
                  ]
                  })"};


    public:
      Project(ZCam*, Element* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("project"); }
      virtual const std::string_view properties() const override { return _properties; }

      /// Add a new Layer as child of the Cad element.
      /// The operation is undoable via the ProjectManager undo stack.
      Q_INVOKABLE void addLayer();

      /// Remove an element (e.g. a Layer) from the project tree.
      /// The operation is undoable via the ProjectManager undo stack.
      Q_INVOKABLE void removeElement(Element* el);
      };
