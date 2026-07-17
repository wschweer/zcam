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
//   Stock
//---------------------------------------------------------

class Stock : public Element3d
      {
      Q_OBJECT

      PROP(QVector3D, size)
      inline static constexpr std::string_view _properties {R"({
            "class": "Stock",
            "items": [
                  { "row": { "show": { "label": "Show",     "type": "bool", "default": true },
                              "burn": { "label": "Burn",     "type": "bool", "default": true } },
                    "label": "Visibility" },
                  { "name": "color",         "label": "Color",    "type": "color","default": "green" },
                  { "name": "pos",           "label": "Pos.",     "type": "vector3d", "unit": "mm",  "default": [0.0, 0.0, 0.0] },
                  { "name": "rot",           "label": "Rot.",     "type": "vector3d", "unit": "°", "min": 0.0, "max": 360, "default": [0.0, 0.0, 0.0] },
                  { "name": "scale",         "label": "Scale",    "type": "vector3d", "min": 0.001, "max": 1000, "default": [1.0, 1.0, 1.0] },
                  { "name": "lockScale",     "label": "Lock", "type": "lockScale", "default": 2 }
                  ]
                        })"};

    public slots:
      void update(int flags = -1) override;

    public:
      explicit Stock(ZCam*, Element* parent = nullptr);
      ~Stock() {}
      virtual QString typeName() override { return QStringLiteral("stock"); }
      virtual const std::string_view properties() const override { return _properties; }
      };
