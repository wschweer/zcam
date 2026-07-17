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

//---------------------------------------------------------
//   Grid
//---------------------------------------------------------

Grid::Grid(ZCam* zcam, Element* parent) : Element3d(zcam, parent) {
      setName("grid");
      _pos           = QVector3D(0, 0, -0.1);
      _geometry      = new TessGeometry(this);
      _minorGeometry = new TessGeometry(this);
      setColor(QColor(200, 200, 200, 140)); // major lines: light grey
      set_selectable(false);
      set_pickLevel(-1); // never picked by pickModel
      set_show(true);
      set_model(QString("GridShape.qml"));

      connect(this, &Grid::rasterChanged, [this]() { update(); });
      connect(this, &Grid::subrasterChanged, [this]() { update(); });

      // Rebuild grid geometry when the machine changes (e.g. when
      // a project is loaded with a different machine, or when the
      // machine is reassigned).  Previously this happened implicitly
      // because ProjectTree::addElement() called element.update()
      // on every scene rebuild; now that the grid is rendered in a
      // separate background View3D, we must update explicitly.
      if (zcam->project())
            connect(zcam->project(), &Project::machineChanged, [this]() { update(); });

      // Build initial geometry.
      update();
      }

//---------------------------------------------------------
//   update
//    Build line geometry for the grid.  Major lines every "raster"
//    mm.  Each raster cell is subdivided into "subraster" minor
//    intervals.  Major and minor lines are stored in separate
//    geometries so they can be rendered with different colours.
//---------------------------------------------------------

void Grid::update(int) {
      if (!_geometry || !_minorGeometry)
            return;

      Machine* m  = (zcam->project() && zcam->project()->machine()) ? zcam->project()->machine() : nullptr;
      double maxX = 100.0;
      double maxY = 100.0;
      if (m) {
            maxX = m->maxTravel().x();
            maxY = m->maxTravel().y();
            }

      double major = _raster;
      int sub      = _subraster;
      if (major <= 0.0)
            major = 10.0;
      if (sub < 1)
            sub = 1;
      double minor = major / sub;

      Clipper2Lib::PathsD majorLines;
      Clipper2Lib::PathsD minorLines;

      // Small epsilon for floating-point comparison so that a line
      // exactly at maxX/maxY is still included, but no line beyond
      // the machine work area is drawn.
      constexpr double eps = 1e-6;

      // Major (raster) lines — clamped to the machine work area
      for (double x = 0.0; x <= maxX + eps; x += major) {
            Clipper2Lib::PathD line;
            line.push_back({x, 0.0});
            line.push_back({x, maxY});
            majorLines.push_back(line);
            }
      for (double y = 0.0; y <= maxY + eps; y += major) {
            Clipper2Lib::PathD line;
            line.push_back({0.0, y});
            line.push_back({maxX, y});
            majorLines.push_back(line);
            }

      // Minor (subraster) lines — only inside major cells, skip the
      // lines that coincide with a major line (i.e. multiples of major).
      // Clamped to the machine work area so no minor line is drawn
      // outside maxTravel.
      if (sub > 1) {
            for (double x = minor; x <= maxX + eps; x += minor) {
                  if (std::abs(std::fmod(x, major)) < minor / 2.0)
                        continue; // skip, coincides with major line
                  Clipper2Lib::PathD line;
                  line.push_back({x, 0.0});
                  line.push_back({x, maxY});
                  minorLines.push_back(line);
                  }
            for (double y = minor; y <= maxY + eps; y += minor) {
                  if (std::abs(std::fmod(y, major)) < minor / 2.0)
                        continue; // skip, coincides with major line
                  Clipper2Lib::PathD line;
                  line.push_back({0.0, y});
                  line.push_back({maxX, y});
                  minorLines.push_back(line);
                  }
            }

      _geometry->setLines(majorLines);
      _minorGeometry->setLines(minorLines);
      emit minorGeometryChanged();
      }