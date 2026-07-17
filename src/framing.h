//=============================================================================
//  wcam
//    CAM tool for gcode and fiber laser machines.
//
//  Copyright (C) 2025 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#pragma once

#include "element3d.h"

//---------------------------------------------------------
//   FramingType
//    Controls how the framing contour is generated:
//      BoundingBox – axis-aligned rectangle around all geometry
//      ConvexHull   – convex hull polygon around all geometry
//---------------------------------------------------------
enum class FramingType : int {
      BoundingBox = 0,
      ConvexHull  = 1
      };

//---------------------------------------------------------
//   Framing
//    creates thes framing contour for fiber laser
//---------------------------------------------------------

class Framing : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("")

      PROPV(int, framingType, static_cast<int>(FramingType::ConvexHull))

      inline static constexpr std::string_view _properties {
         R"json({
                  "class": "Framing",
                  "items": [
                    {
                      "row": {
                        "show": {
                          "label": "Show",
                          "type": "bool",
                          "default": true
                        },
                        "burn": {
                          "label": "Show",
                          "type": "empty",
                          "default": true
                        }
                      },
                      "label": " "
                    },
                    {
                      "name": "framingType",
                      "label": "Type",
                      "type": "framingType",
                      "default": 1
                    }
                  ]
                      })json"};

    public:
      virtual QString typeName() override { return QStringLiteral("framing"); }
      virtual const std::string_view properties() const override { return _properties; }
      Framing(ZCam* w, Element* parent = nullptr);
      virtual void update(int flags = ~0) override;
      Q_INVOKABLE virtual bool visible() const override { return true; }
      };
