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

#define NANOSVG_IMPLEMENTATION
#include <nanosvg/nanosvg.h>
#include "zcam.h"
#include "polygon.h"
#include "layer.h"
#include "cad.h"
#include "project.h"
#include "projectmanager.h"
#include "undo.h"
#include "logger.h"

//---------------------------------------------------------
//   importSvg
//    NanoSVG is called with units="px" so that the path coordinates
//    are returned in SVG user-space pixels (after viewBox scaling).
//    We then convert pixels to millimetres ourselves using the
//    standard 96-DPI factor (25.4 / 96 ≈ 0.2646).
//
//    This avoids a NanoSVG bug: when units="mm" is used and the SVG
//    has width/height in mm but NO viewBox, NanoSVG treats the user
//    units as pixels and applies an extra px→mm conversion, making
//    the coordinates ~3.78× too small.
//
//    With units="px" NanoSVG does no unit conversion at all; the
//    coordinates stay in pixel space.  We apply the single correct
//    px→mm factor ourselves, which works correctly for:
//      • SVGs with width in px + viewBox  (px → mm)
//      • SVGs with width in mm + viewBox (viewBox px → image px → mm)
//      • SVGs with width in px, no viewBox (px → mm)
//---------------------------------------------------------

void ZCam::importSvg(const QString& path) {
      NSVGimage* image = nsvgParseFromFile(path.toLocal8Bit(), "px", 96);
      if (!image) {
            Critical("importSvg: cannot parse SVG file: {}", path);
            return;
            }
      Debug("loaded SVG image {}x{}", image->width, image->height);

      // Pixel → millimetre conversion factor (96 DPI).
      constexpr double pxToMm = 25.4 / 96.0;

      // Build a PainterPath from all shapes and sub-paths in the SVG.
      // All coordinates are converted from pixels to millimetres.
      PainterPath pp;
      for (NSVGshape* shape = image->shapes; shape; shape = shape->next) {
            for (NSVGpath* svgPath = shape->paths; svgPath; svgPath = svgPath->next) {
                  // Each sub-path consists of cubic Bezier segments
                  // (4 points / 8 floats per segment)
                  Vec2d first;
                  Vec2d last;
                  for (int i = 0; i < svgPath->npts - 1; i += 3) {
                        float* p = &svgPath->pts[i * 2];
                        Vec2d p1(p[0] * pxToMm, p[1] * pxToMm);
                        if (i == 0) {
                              pp.moveTo(p1);
                              first = p1;
                              }
                        Vec2d p2(p[2] * pxToMm, p[3] * pxToMm);
                        Vec2d p3(p[4] * pxToMm, p[5] * pxToMm);
                        Vec2d p4(p[6] * pxToMm, p[7] * pxToMm);
                        pp.cubicTo(p2, p3, p4);
                        last = p4;
                        }
                  if (svgPath->closed) {
                        if (qFuzzyCompare(first.x(), last.x()) && qFuzzyCompare(first.y(), last.y()))
                              pp.back().pos = first;
                        else
                              pp.lineTo(first);
                        }
                  }
            }

      nsvgDelete(image);
      Debug("---ok");
      if (pp.empty()) {
            Warning("importSvg: no paths found in SVG file: {}", path);
            return;
            }

      if (!_project || !_project->cad()) {
            Critical("importSvg: no project or CAD element");
            return;
            }

      Layer* layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Critical("importSvg: no visible layer");
            return;
            }

      auto* poly = new Polygon(this, layer);
      poly->setPainterPath(pp);
      poly->setName(QStringLiteral("Svg"));
      poly->set_pos(QVector3D(0, 0, 0));
      poly->set_lineWidth(0);
      poly->set_fill(false);
      poly->update();
      Debug("---ok2");

      auto cmd = std::make_unique<InsertElementCommand>(this, layer, poly, -1);
      cmd->redo(); // apply immediately
      _projectManager->pushCommand(std::move(cmd));

      _projectManager->markDirty();
      setCamDirty(true);
      Debug("---ok3");
      }
