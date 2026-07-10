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
//   Framing
//    creates thes framing contour for fiber laser
//---------------------------------------------------------

class Framing : public Element3d {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("")

      inline static constexpr std::string_view _properties {R"({
            "class": "Framing",
            "items": [
                  { "name": "color",         "label": "Color",    "type": "color","default": "green" },
                  { "name": "pos",           "label": "Pos.",     "type": "vector3d", "unit": "mm",  "default": [0.0, 0.0, 0.0] },
                  { "name": "rot",           "label": "Rot.",     "type": "vector3d", "unit": "°", "min": 0.0, "max": 360, "default": [0.0, 0.0, 0.0] },
                  { "name": "scale",         "label": "Scale",    "type": "vector3d", "min": 0.001, "max": 1000, "default": [1.0, 1.0, 1.0] }
                  ]
            })"};


   public:
      virtual QString typeName() override { return QStringLiteral("framing"); }
      virtual const std::string_view properties() const override { return _properties; }
      Framing(ZCam* w, Element* parent = nullptr);
      virtual void update(int flags = ~0) override;
      };
