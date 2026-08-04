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

#include <QAbstractListModel>
#include "element3d.h"

class ZCam;

//---------------------------------------------------------
//   Group
//    Serves only as a display grouping of elements.  It has a
//    local coordinate system like all Element3d but has nothing
//    to display itself.  Every element (Polygon, Rectangle, etc.)
//    can also form a group by having children.
//---------------------------------------------------------

class Group : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("")

      PROPV(bool, invert, false)

      inline static constexpr std::string_view _properties {
         R"json({
    "class": "Group",
    "rows": [
        {
            "label": "State",
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
            "label": "Recipe",
            "cells": [
                {
                    "name": "laserLayer",
                    "type": "laserLayer",
                    "default": ""
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
                    "max": 1000.0,
                    "precision": 3,
                    "step": 0.1,
                    "bigStep": 1.0,
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
        },
        {
            "label": "Mirror",
            "cells": [
                {
                    "type": "bool",
                    "default": false,
                    "name": "mirrorX",
                    "sublabel": "X"
                },
                {
                    "type": "bool",
                    "default": false,
                    "name": "mirrorY",
                    "sublabel": "Y"
                }
            ]
        }
    ]
})json"};

    public:
      Group(ZCam*, Element* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("group"); }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool deletable() const override { return true; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      Q_INVOKABLE bool draggable() const override { return true; }

    public slots:
      void update(int flags = -1) override;

    private slots:
      // Called when any child's geometry or visibility changes so
      // the Group's selection bounding box stays in sync.
      void onChildChanged();

    protected:
      void updateSelectionGeometry() override;
      };

extern Clipper2Lib::PathD wobble(const Clipper2Lib::PathD& path, double step, double r);
