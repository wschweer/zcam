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
    "rows": [
        {
            "label": "Visibility",
            "cells": [
                {
                    "type": "bool",
                    "default": true,
                    "name": "show",
                    "sublabel": "Show"
                },
                {
                    "type": "bool",
                    "default": true,
                    "name": "burn",
                    "sublabel": "Burn"
                }
            ]
        },
        {
            "label": "Color",
            "cells": [
                {
                    "name": "color",
                    "type": "color",
                    "default": "green"
                }
            ]
        },
        {
            "label": "Pos.",
            "cells": [
                {
                    "name": "pos",
                    "type": "vector3d",
                    "unit": "mm",
                    "default": [
                        0.0,
                        0.0,
                        0.0
                    ]
                }
            ]
        },
        {
            "label": "Rot.",
            "cells": [
                {
                    "name": "rot",
                    "type": "vector3d",
                    "unit": "°",
                    "min": 0.0,
                    "max": 360,
                    "default": [
                        0.0,
                        0.0,
                        0.0
                    ]
                }
            ]
        },
        {
            "label": "Scale",
            "cells": [
                {
                    "name": "scale",
                    "type": "scale",
                    "min": 0.001,
                    "max": 1000,
                    "default": [
                        1.0,
                        1.0,
                        1.0
                    ]
                }
            ]
        },
        {
            "label": "Lock",
            "cells": [
                {
                    "name": "lockScale",
                    "type": "lockScale",
                    "default": 2
                }
            ]
        }
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
