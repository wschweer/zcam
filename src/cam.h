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
#include "tessgeometry.h"
#include "clipper2/clipper.h"

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
                  { "name": "scale",         "label": "Scale",    "type": "scale", "min": 0.001, "max": 1000, "default": [1.0, 1.0, 1.0] },
                  { "name": "lockScale",     "label": "Lock", "type": "lockScale", "default": 2 },
                  { "name": "line", "type": "line" },
                  { "row": { "panelRows":    { "label": "Rows",  "type": "int", "min": 1, "max": 100, "default": 1 },
                              "panelColumns": { "label": "Cols",  "type": "int", "min": 1, "max": 100, "default": 1 } },
                    "label": "Panels" },
                  { "row": { "panelHDistance": { "label": "H",  "type": "float", "unit": "mm", "min": 0.0, "max": 50.0, "default": 0.0 },
                              "panelVDistance": { "label": "V", "type": "float", "unit": "mm", "min": 0.0, "max": 50.0, "default": 0.0 } },
                    "label": "Dist." }
                  ]
                                                })"};

    signals:
      void panelChanged();

    public:
      Cam(ZCam*, Element* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("cam"); }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      /// Recalculate all cam data (panel layout, fixture, laser layers).
      /// Collects geometry from all LaserLayer children, arranges them
      /// in a panel grid (rows × columns with distance offsets), and
      /// stores the combined line geometry in this Cam's _geometry.
      Clipper2Lib::PathD convexHull() const;
      Clipper2Lib::RectD boundingBox() const;
      /// Recalculate all cam data (panel layout, fixture, laser layers).
      /// This is expensive and must be triggered explicitly — either by
      /// the manual refresh button (ZCam::refreshCam) or at startup.
      /// Property changes only set the camDirty flag so the user knows
      /// a refresh is pending.
      Q_INVOKABLE void updateCam();
      };