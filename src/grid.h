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
//   Grid
//    Draws a grid covering the machine work area (maxX × maxY).
//    Major lines every "raster" mm (default 10).  Each raster cell
//    is subdivided into "subraster" minor intervals (default 5).
//    Major and minor lines are rendered in different colours.
//---------------------------------------------------------

class Grid : public Element3d
      {
      Q_OBJECT

      PROPV(double, raster, 20.0)
      PROPV(int, subraster, 5)
      PROPV(bool, snap, false)

      Q_PROPERTY(TessGeometry* minorGeometry READ minorGeometry NOTIFY minorGeometryChanged)

      inline static constexpr std::string_view _properties {
         R"json({
                  "class": "Grid",
                  "items": [
                    {
                      "row": {
                        "show": {
                          "label": "Show",
                          "type": "bool",
                          "default": true
                        },
                        "snap": {
                          "label": "Snap",
                          "type": "bool",
                          "default": false
                        }
                      },
                      "label": "Visibility"
                    },
                    {
                      "row": {
                        "raster": {
                          "label": " ",
                          "type": "float",
                          "unit": "mm",
                          "min": 0.1,
                          "max": 1000.0,
                          "default": 20.0
                        },
                        "subraster": {
                          "label": " ",
                          "type": "int",
                          "min": 1,
                          "max": 100,
                          "default": 5
                        }
                      },
                      "label": "Raster"
                    }
                  ]
                      })json"};

      TessGeometry* _minorGeometry {nullptr};

    signals:
      void minorGeometryChanged();

    public slots:
      void update(int flags = -1) override;

    public:
      explicit Grid(ZCam*, Element* parent = nullptr);
      // Returns the minor line spacing (major / subraster) in mm.
      double minorSpacing() const;
      // Returns the major line spacing (raster) in mm.
      double majorSpacing() const;
      virtual QString typeName() override { return QStringLiteral("grid"); }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      TessGeometry* minorGeometry() const { return _minorGeometry; }
      };