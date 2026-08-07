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
#include <algorithm>

//---------------------------------------------------------
//   Grid
//---------------------------------------------------------

Grid::Grid(ZCam* zcam, Element* parent) : Element3d(zcam, parent) {
      setName("grid");
      // Push the grid slightly behind the XY plane (z < 0) to avoid
      // z-fighting with elements that lie at z == 0.  The value must
      // be large enough to survive the root-node scale (typically 5×)
      // and the depth-buffer precision at the camera distance (~1000).
      // A value of -1.0 mm gives -5.0 mm in world space at scale 5, which
      // is sufficient to separate the grid from all elements on the XY
      // plane without introducing visible parallax in isometric views.
      _pos           = QVector3D(0, 0, -1.0);
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
//---------------------------------------------------------
//   update
//    Build the grid line geometry.  Major lines every "raster"
//    mm; each raster cell is subdivided into "subraster" minor
//    intervals.  Major and minor lines are stored in separate
//    geometries so they can be rendered with different colours.
//
//    Screen-space expansion strategy ('grid' branch):  the
//    geometry is uploaded ONCE as flat degenerate quads that
//    carry no width at all.  A CustomMaterial vertex shader
//    (shaders/gridline.vert) projects each corner into clip
//    space and displaces it perpendicular to the projected line
//    direction by a CONSTANT PIXEL amount (uHalfWidthPx relative
//    to uViewportSize), scaled by clip.w so the perspective
//    division restores exact pixels.  Consequences:
//      · constant pixel width in EVERY view — zoom, pan and
//        rotation no longer trigger a geometry rebuild;
//      · no Z-fighting:  the quad stays flat in the XY plane
//        and the grid's 1 mm z offset wins the depth test;
//      · no CPU width / spacing / caps bookkeeping any more —
//        all of that lived in the old CPU billboard code.
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
      // (maxTravel.x, maxTravel.y).
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

      // Line construction uses INTEGER iteration indices instead of
      // accumulating a floating-point loop variable (x += major /
      // x += minor).  Floating-point accumulation causes drift so
      // minor lines which should coincide with a major line are no
      // longer at an exact multiple of major; a minor line would
      // then be rendered on top of a major line, producing a
      // doubled stroke.  With integer indices, i % sub == 0 exactly
      // identifies coinciding minor lines.
      constexpr double eps = 1e-6;

      Clipper2Lib::PathsD majorLines;
      Clipper2Lib::PathsD minorLines;

      // Major (raster) lines
      int startMx = static_cast<int>(std::floor(left / major));
      int endMx   = static_cast<int>(std::ceil((right + eps) / major));
      for (int i = startMx; i <= endMx; ++i) {
            double x = i * major;
            if (x < left - eps || x > right + eps)
                  continue;
            Clipper2Lib::PathD line;
            line.push_back({x, top});
            line.push_back({x, bottom});
            majorLines.push_back(line);
            }
      int startMy = static_cast<int>(std::floor(top / major));
      int endMy   = static_cast<int>(std::ceil((bottom + eps) / major));
      for (int i = startMy; i <= endMy; ++i) {
            double y = i * major;
            if (y < top - eps || y > bottom + eps)
                  continue;
            Clipper2Lib::PathD line;
            line.push_back({left, y});
            line.push_back({right, y});
            majorLines.push_back(line);
            }

      // Minor (subraster) lines — skip lines that coincide with
      // a major line (i % sub == 0).
      if (sub > 1) {
            int startmx = static_cast<int>(std::floor(left / minor));
            int endmx   = static_cast<int>(std::ceil((right + eps) / minor));
            for (int i = startmx; i <= endmx; ++i) {
                  if (i % sub == 0)
                        continue;
                  double x = i * minor;
                  if (x < left - eps || x > right + eps)
                        continue;
                  Clipper2Lib::PathD line;
                  line.push_back({x, top});
                  line.push_back({x, bottom});
                  minorLines.push_back(line);
                  }
            int startmy = static_cast<int>(std::floor(top / minor));
            int endmy   = static_cast<int>(std::ceil((bottom + eps) / minor));
            for (int i = startmy; i <= endmy; ++i) {
                  if (i % sub == 0)
                        continue;
                  double y = i * minor;
                  if (y < top - eps || y > bottom + eps)
                        continue;
                  Clipper2Lib::PathD line;
                  line.push_back({left, y});
                  line.push_back({right, y});
                  minorLines.push_back(line);
                  }
            }

      _geometry->setLinesForExpandedQuads(majorLines);
      _minorGeometry->setLinesForExpandedQuads(minorLines);
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
//    Screen-space expansion variant ('grid' branch):  the line
//    width is applied by a vertex shader in CLIP SPACE, so zoom,
//    pan and rotation no longer require a geometry rebuild at
//    all.  This slot only keeps the historical call sites alive
//    (View3DPanel still forwards viewport/viewDir/canvas size);
//    all state is stored for possible future use and for the
//    legacy overload, but update() is NOT re-triggered — the
//    shader derives everything from the current MVP matrix.
//---------------------------------------------------------

void Grid::setViewport(double left, double top, double right, double bottom,
                       const QVector3D& viewDir, double canvasW, double canvasH) {
      _viewWidth    = right - left;
      _viewHeight   = bottom - top;
      _canvasWidth  = canvasW > 0.0 ? canvasW : _canvasWidth;
      _canvasHeight = canvasH > 0.0 ? canvasH : _canvasHeight;
      _hasViewport  = true;
      _viewDir      = viewDir;
      // Intentionally NO update() here:  with the screen-space
      // expansion shader a camera change does not alter the
      // geometry — the whole point of the new approach.
      }

void Grid::setViewport(double left, double top, double right, double bottom, const QVector3D& viewDir) {
      // Legacy entry point without an explicit canvas size: keep
      // the previously stored (or default 1000 px) dimensions.
      setViewport(left, top, right, bottom, viewDir, _canvasWidth, _canvasHeight);
      }