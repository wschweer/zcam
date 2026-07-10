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
#include "stock.h"

//---------------------------------------------------------
//   Cam
//---------------------------------------------------------

class Cam : public Element3d
      {
      Q_OBJECT

      PROPV(double, raster, 1.0)
      PROPV(int, panelRows, 1)
      PROPV(int, panelColumns, 1)
      PROPV(double, panelHDistance, 0.0)
      PROPV(double, panelVDistance, 0.0)
      PROPV(Stock*, stock, nullptr)

      inline static constexpr std::string_view _properties {R"({
            "class": "Cam",
            "items": [
                  { "row": { "show": { "label": "Show",     "type": "bool", "default": true },
                              "burn": { "label": "Burn",     "type": "bool", "default": true } },
                    "label": "State" },
                  { "name": "pos",           "label": "Pos.",     "type": "vector3d", "unit": "mm",  "default": [0.0, 0.0, 0.0] },
                  { "name": "rot",           "label": "Rot.",     "type": "vector3d", "unit": "°", "min": 0.0, "max": 360, "default": [0.0, 0.0, 0.0] },
                  { "name": "scale",         "label": "Scale",    "type": "vector3d", "min": 0.001, "max": 1000, "default": [1.0, 1.0, 1.0] },
                  { "name": "line", "type": "line" },
                  { "row": { "panelRows":    { "label": "Rows",  "type": "int", "min": 1, "max": 100, "default": 1 },
                              "panelColumns": { "label": "Cols",  "type": "int", "min": 1, "max": 100, "default": 1 } },
                    "label": "Panels" },
                  { "row": { "panelHDistance": { "label": "H",  "type": "float", "unit": "mm", "min": 0.0, "max": 50.0, "default": 0.0 },
                              "panelVDistance": { "label": "V", "type": "float", "unit": "mm", "min": 0.0, "max": 50.0, "default": 0.0 } },
                    "label": "Dist." }
                  ]
                  })"};

      void updateCam(int flags);

    signals:
      void panelChanged();

    public:
      enum UpdateFlags { ROWS_COLUMNS = 0x1, DISTANCE = 0x2 };
      Cam(ZCam*, Element* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("cam"); }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      };