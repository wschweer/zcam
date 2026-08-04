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

#include "grid.h"
#include "zcam.h"
#include "project.h"
#include "machine.h"
#include "clipper2/clipper.h"
#include <QColor>
#include <cmath>

//---------------------------------------------------------
//   Grid
//---------------------------------------------------------

Grid::Grid(ZCam* zcam, Element* parent) : Element3d(zcam, parent) {
      setName("grid");
      _pos           = QVector3D(0, 0, -0.1);
      _geometry      = new TessGeometry(this);
      _minorGeometry = new TessGeometry(this);
      setColor(QColor(150, 150, 150, 50)); // major lines: light grey
      set_selectable(false);
      set_pickLevel(-1); // never picked by pickModel
      set_show(true);
      set_model(QString("GridShape.qml"));

      connect(this, &Grid::rasterChanged, [this]() { update(); });
      connect(this, &Grid::subrasterChanged, [this]() { update(); });

      // Rebuild grid geometry when the machine changes (e.g. when
      // a project is loaded with a different machine, or when the
      // machine is reassigned) or when the machine's maxTravel
      // property changes (e.g. user edits it in the inspector).
      //
      // Qt::UniqueConnection requires pointer-to-member-function
      // slots, so we use dedicated slot methods instead of lambdas.
      if (zcam->project()) {
            connect(zcam->project(), &Project::machineChanged, this, &Grid::onMachineChanged);
            connectToMachine(zcam->project()->machine());
            }

      // Build initial geometry.
      update();
      }

//---------------------------------------------------------
//   connectToMachine
//    Connect to the machine's maxTravelChanged signal.  Tracks the
//    currently-connected machine so we don't connect twice to the
//    same machine.
//---------------------------------------------------------

void Grid::connectToMachine(Machine* m) {
      if (_connectedMachine == m)
            return;
      _connectedMachine = m;
      if (m)
            connect(m, &Machine::maxTravelChanged, this, &Grid::onMaxTravelChanged);
      }

//---------------------------------------------------------
//   onMachineChanged
//---------------------------------------------------------

void Grid::onMachineChanged() {
      Machine* m = (zcam->project() && zcam->project()->machine()) ? zcam->project()->machine() : nullptr;
      connectToMachine(m);
      update();
      }

//---------------------------------------------------------
//   onMaxTravelChanged
//---------------------------------------------------------

void Grid::onMaxTravelChanged() {
      update();
      }

//---------------------------------------------------------
//   update
//    Build line geometry for the grid.  Major lines every "raster"
//    mm.  Each raster cell is subdivided into "subraster" minor
//    intervals.  Major and minor lines are stored in separate
//    geometries so they can be rendered with different colours.
//
//    The grid covers exactly the machine work area: (0,0) to
//    (maxTravel.x, maxTravel.y) of the current machine.
//---------------------------------------------------------

void Grid::update(int) {
      if (!_geometry || !_minorGeometry)
            return;

      Machine* m   = (zcam->project() && zcam->project()->machine()) ? zcam->project()->machine() : nullptr;
      double mMaxX = 100.0;
      double mMaxY = 100.0;
      if (m) {
            mMaxX = m->maxTravel().x();
            mMaxY = m->maxTravel().y();
            }

      // The grid covers exactly the machine work area: (0,0) to
      // (maxTravel.x, maxTravel.y).  It no longer extends to fill
      // the visible viewport.
      double left   = 0.0;
      double top    = 0.0;
      double right  = mMaxX;
      double bottom = mMaxY;

      double major = _raster;
      int sub      = _subraster;
      if (major <= 0.0)
            major = 10.0;
      if (sub < 1)
            sub = 1;
      double minor = major / sub;

      Clipper2Lib::PathsD majorLines;
      Clipper2Lib::PathsD minorLines;

      constexpr double eps = 1e-6;

      // Align the start positions to the grid so that grid lines stay
      // at fixed world positions regardless of the viewport.
      double startX = std::floor(left / major) * major;
      double startY = std::floor(top / major) * major;

      // Major (raster) lines
      for (double x = startX; x <= right + eps; x += major) {
            Clipper2Lib::PathD line;
            line.push_back({x, top});
            line.push_back({x, bottom});
            majorLines.push_back(line);
            }
      for (double y = startY; y <= bottom + eps; y += major) {
            Clipper2Lib::PathD line;
            line.push_back({left, y});
            line.push_back({right, y});
            majorLines.push_back(line);
            }

      // Minor (subraster) lines — skip the lines that coincide with
      // a major line.
      if (sub > 1) {
            double startXm = std::floor(left / minor) * minor;
            double startYm = std::floor(top / minor) * minor;
            for (double x = startXm; x <= right + eps; x += minor) {
                  if (std::abs(std::fmod(x, major)) < minor / 2.0)
                        continue;
                  Clipper2Lib::PathD line;
                  line.push_back({x, top});
                  line.push_back({x, bottom});
                  minorLines.push_back(line);
                  }
            for (double y = startYm; y <= bottom + eps; y += minor) {
                  if (std::abs(std::fmod(y, major)) < minor / 2.0)
                        continue;
                  Clipper2Lib::PathD line;
                  line.push_back({left, y});
                  line.push_back({right, y});
                  minorLines.push_back(line);
                  }
            }

      _geometry->setLines(majorLines);
      _minorGeometry->setLines(minorLines);
      emit minorGeometryChanged();
      }

//---------------------------------------------------------
//   majorSpacing / minorSpacing
//    Convenience accessors used by the snap logic in ZCam::dragged().
//---------------------------------------------------------

double Grid::majorSpacing() const {
      return _raster > 0.0 ? _raster : 10.0;
      }

double Grid::minorSpacing() const {
      double major = majorSpacing();
      int sub      = _subraster >= 1 ? _subraster : 1;
      return major / sub;
      }

//---------------------------------------------------------
//   setViewport
//    No-op kept for QML compatibility.  The grid size is now
//    determined solely by the machine work area (maxTravel).
//---------------------------------------------------------

void Grid::setViewport(double, double, double, double) {
      // No longer needed — the grid size is determined solely by
      // the machine work area (maxTravel).  Kept as a no-op for
      // QML compatibility.
      }