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

#include "element3d.h"

//---------------------------------------------------------
//   Cad
//---------------------------------------------------------

class Cad : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no no")

      //      Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged);

      inline static constexpr std::string_view _properties {R"({
            "class": "Cad",
            "items": [
                  { "row": { "show": { "label": "Show",     "type": "bool", "default": true },
                              "burn": { "label": "Burn",     "type": "bool", "default": true } },
                    "label": "Visibility" },
                  { "name": "pos",           "label": "Pos.",     "type": "vector3d", "unit": "mm",  "default": [0.0, 0.0, 0.0] },
                  { "name": "rot",           "label": "Rot.",     "type": "vector3d", "unit": "°", "min": 0.0, "max": 360, "default": [0.0, 0.0, 0.0] },
                  { "name": "scale",         "label": "Scale",    "type": "vector3d", "min": 0.001, "max": 1000, "default": [1.0, 1.0, 1.0] }
                  ]
                        })"};

    protected:

    public:
      Cad(ZCam*, Element* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("cad"); }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      };
