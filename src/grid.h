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
//    Draws a grid covering the machine work area (maxTravel.x ×
//    maxTravel.y).  Major lines every "raster" mm (default 10).
//    Each raster cell is subdivided into "subraster" minor
//    intervals (default 5).  Major lines are WIDER (2 px) and
//    DARKER than minor lines (1 px).
//
//    Rendering ('grid' branch):  the geometry is uploaded ONCE
//    as flat degenerate quads that carry no width at all.  A
//    CustomMaterial vertex shader (shaders/gridline.vert)
//    expands each corner in CLIP SPACE perpendicular to the
//    projected line by a constant pixel amount, so the stroke
//    keeps its exact pixel width in EVERY view — zoom, pan and
//    rotation no longer rebuild the geometry, and there is no
//    Z-fighting because the quads stay flat in the XY plane.
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
                          "default": false,
                          "name": "snap",
                          "sublabel": "Snap"
                        }
                      ]
                    },
                    {
                      "label": "Raster",
                      "cells": [
                        {
                          "type": "float",
                          "unit": "mm",
                          "min": 0.1,
                          "max": 1000.0,
                          "default": 20.0,
                          "name": "raster",
                          "sublabel": " "
                        },
                        {
                          "type": "int",
                          "min": 1,
                          "max": 100,
                          "default": 5,
                          "name": "subraster",
                          "sublabel": " "
                        }
                      ]
                    }
                  ]
                      })json"};

      TessGeometry* _minorGeometry {nullptr};
      QPointer<class Machine> _connectedMachine; ///< machine we listen to for maxTravel changes

      // Visible viewport in local (mm) coordinates, set by setViewport().
      // Together with _canvasWidth/_canvasHeight (real panel pixels)
      // this yields the exact mm-per-pixel factor so the grid lines
      // keep their target on-screen pixel width regardless of zoom
      // level and window layout.  When the viewport is not set
      // (e.g. at startup), a fallback half-width is used.
      double _viewWidth {0.0};
      double _viewHeight {0.0};
      double _canvasWidth {1000.0};  ///< viewport width in real pixels
      double _canvasHeight {1000.0}; ///< viewport height in real pixels
      bool _hasViewport {false};

      // Camera view direction in local (pre-root-transform) coordinates.
      // Used to orient the line quads as cylinder billboards facing
      // the camera; the projected line width stays constant from
      // every angle because the quad's height offset is perpendicular
      // to the view ray by construction.
      QVector3D _viewDir {0, 0, 1};

    signals:
      void minorGeometryChanged();

    private slots:
      void onMachineChanged();
      void onMaxTravelChanged();

    public slots:
      void update(int flags = -1) override;
      /// Called from QML whenever the camera moves, rotates or
      /// zooms.  Stores the visible area and the canvas size in real
      /// pixels plus the camera view direction, then rebuilds the
      /// grid geometry at the correct on-screen line width.
      void setViewport(double left, double top, double right, double bottom,
                       const QVector3D& viewDir, double canvasW, double canvasH);
      /// Overload kept for legacy calls that do not pass the canvas
      /// size — falls back to the nominal 1000 px width.
      void setViewport(double left, double top, double right, double bottom, const QVector3D& viewDir);

    public:
      explicit Grid(ZCam*, Element* parent = nullptr);
      // Returns the minor line spacing (major / subraster) in mm.
      double minorSpacing() const;
      // Returns the major line spacing (raster) in mm.
      double majorSpacing() const;
      virtual QString typeName() override { return QStringLiteral("grid"); }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      Q_INVOKABLE bool deletable() const override { return true; }
      TessGeometry* minorGeometry() const { return _minorGeometry; }

    private:
      void connectToMachine(Machine* m);
      };