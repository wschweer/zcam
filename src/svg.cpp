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
#include "group.h"
#include "cad.h"
#include "project.h"
#include "undo.h"
#include "logger.h"
#include <QFileInfo>
#include <QJSEngine>
#include <limits>
#include <cmath>

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

      // SVG uses a top-left origin with the Y-axis pointing downward.
      // CAM/CAD uses a bottom-left origin with Y pointing upward.
      // Mirror all Y coordinates about the SVG height so the image is
      // not rendered upside-down.
      const double svgHeight = image->height;

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
                        Vec2d p1(p[0] * pxToMm, (svgHeight - p[1]) * pxToMm);
                        if (i == 0) {
                              pp.moveTo(p1);
                              first = p1;
                              }
                        Vec2d p2(p[2] * pxToMm, (svgHeight - p[3]) * pxToMm);
                        Vec2d p3(p[4] * pxToMm, (svgHeight - p[5]) * pxToMm);
                        Vec2d p4(p[6] * pxToMm, (svgHeight - p[7]) * pxToMm);
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

      Group* layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Critical("importSvg: no visible layer");
            return;
            }

      auto* poly = new Polygon(this, layer);
      poly->setPainterPath(pp);
      poly->setName(QFileInfo(path).baseName());
      poly->set_pos(QVector3D(0, 0, 0));
      poly->set_lineWidth(0);
      poly->set_fill(false);
      poly->update();
      Debug("---ok2");

      auto cmd = std::make_unique<InsertElementCommand>(this, layer, poly, -1);
      cmd->redo(); // apply immediately
      _project->pushCommand(std::move(cmd));

      _project->markDirty();
      setCamDirty(true);
      Debug("---ok3");
      }

//---------------------------------------------------------
//   svgBoundingBox
//    Parse the SVG at the given path and compute the axis-aligned
//    bounding box of all path points, in millimetres, after the
//    Y-mirroring transformation applied during import.
//    Returns an empty QRectF if the SVG cannot be parsed or has
//    no path data.
//---------------------------------------------------------

QRectF ZCam::svgBoundingBox(const QString& path) {
      NSVGimage* image = nsvgParseFromFile(path.toLocal8Bit(), "px", 96);
      if (!image)
            return {};

      constexpr double pxToMm = 25.4 / 96.0;
      const double svgHeight  = image->height;

      double minX = std::numeric_limits<double>::max();
      double minY = std::numeric_limits<double>::max();
      double maxX = std::numeric_limits<double>::lowest();
      double maxY = std::numeric_limits<double>::lowest();

      for (NSVGshape* shape = image->shapes; shape; shape = shape->next) {
            for (NSVGpath* svgPath = shape->paths; svgPath; svgPath = svgPath->next) {
                  for (int i = 0; i < svgPath->npts; ++i) {
                        float* p = &svgPath->pts[i * 2];
                        double x = p[0] * pxToMm;
                        double y = (svgHeight - p[1]) * pxToMm;
                        minX     = std::min(minX, x);
                        minY     = std::min(minY, y);
                        maxX     = std::max(maxX, x);
                        maxY     = std::max(maxY, y);
                        }
                  }
            }

      nsvgDelete(image);

      if (minX > maxX || minY > maxY)
            return {};
      return QRectF(minX, minY, maxX - minX, maxY - minY);
      }

//---------------------------------------------------------
//   importSvgAt
//    Import an SVG file and position the resulting Polygon so
//    that the bounding box's bottom-left corner is at (x, y) in
//    the parent layer's local coordinate space.
//    The SVG path data has its origin at (0,0) in local coords.
//    After the Y-mirror, the path's bounding box starts at
//    (minX, minY).  We shift the polygon's pos by (-minX + x,
//    -minY + y) so that the bbox corner lands at (x, y).
//---------------------------------------------------------

void ZCam::importSvgAt(const QString& path, double x, double y) {
      QRectF bbox = svgBoundingBox(path);
      if (bbox.isNull() || bbox.isEmpty()) {
            importSvg(path);
            return;
            }

      // Parse the SVG and build the PainterPath (same as importSvg).
      NSVGimage* image = nsvgParseFromFile(path.toLocal8Bit(), "px", 96);
      if (!image) {
            Critical("importSvgAt: cannot parse SVG file: {}", path);
            return;
            }

      constexpr double pxToMm = 25.4 / 96.0;
      const double svgHeight  = image->height;

      PainterPath pp;
      for (NSVGshape* shape = image->shapes; shape; shape = shape->next) {
            for (NSVGpath* svgPath = shape->paths; svgPath; svgPath = svgPath->next) {
                  Vec2d first;
                  Vec2d last;
                  for (int i = 0; i < svgPath->npts - 1; i += 3) {
                        float* p = &svgPath->pts[i * 2];
                        Vec2d p1(p[0] * pxToMm, (svgHeight - p[1]) * pxToMm);
                        if (i == 0) {
                              pp.moveTo(p1);
                              first = p1;
                              }
                        Vec2d p2(p[2] * pxToMm, (svgHeight - p[3]) * pxToMm);
                        Vec2d p3(p[4] * pxToMm, (svgHeight - p[5]) * pxToMm);
                        Vec2d p4(p[6] * pxToMm, (svgHeight - p[7]) * pxToMm);
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

      if (pp.empty()) {
            Warning("importSvgAt: no paths found in SVG file: {}", path);
            return;
            }

      if (!_project || !_project->cad()) {
            Critical("importSvgAt: no project or CAD element");
            return;
            }

      Group* layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Critical("importSvgAt: no visible layer");
            return;
            }

      // Position so the bbox's bottom-left corner is at (x, y).
      double posX = x - bbox.left();
      double posY = y - bbox.top();

      auto* poly = new Polygon(this, layer);
      poly->setPainterPath(pp);
      poly->setName(QFileInfo(path).baseName());
      poly->set_pos(QVector3D(posX, posY, 0));
      poly->set_lineWidth(0);
      poly->set_fill(false);
      poly->update();

      auto cmd = std::make_unique<InsertElementCommand>(this, layer, poly, -1);
      cmd->redo(); // apply immediately
      _project->pushCommand(std::move(cmd));

      _project->markDirty();
      setCamDirty(true);
      }

//---------------------------------------------------------
//   startSvgDrag
//    Parse the SVG, compute its bounding box, and create a
//    TessGeometry that renders the rectangle outline.  The QML
//    layer uses this geometry to show a preview box that follows
//    the mouse during drag-and-drop.
//---------------------------------------------------------

void ZCam::startSvgDrag(const QString& path) {
      _svgDragPath = path;
      _svgDragBBox = svgBoundingBox(path);

      if (_svgDragBBox.isNull() || _svgDragBBox.isEmpty()) {
            _dragPreviewGeometry = nullptr;
            emit dragPreviewGeometryChanged();
            return;
            }

      // Create a standalone TessGeometry (not tied to any element).
      if (_dragPreviewGeometry)
            delete _dragPreviewGeometry;
      _dragPreviewGeometry = new TessGeometry(nullptr);
      QJSEngine::setObjectOwnership(_dragPreviewGeometry, QJSEngine::CppOwnership);

      // Build a rectangle outline (4 edges as line segments).
      Clipper2Lib::PathD rect;
      rect.push_back({_svgDragBBox.left(), _svgDragBBox.top()});
      rect.push_back({_svgDragBBox.right(), _svgDragBBox.top()});
      rect.push_back({_svgDragBBox.right(), _svgDragBBox.top()});
      rect.push_back({_svgDragBBox.right(), _svgDragBBox.bottom()});
      rect.push_back({_svgDragBBox.right(), _svgDragBBox.bottom()});
      rect.push_back({_svgDragBBox.left(), _svgDragBBox.bottom()});
      rect.push_back({_svgDragBBox.left(), _svgDragBBox.bottom()});
      rect.push_back({_svgDragBBox.left(), _svgDragBBox.top()});
      Clipper2Lib::PathsD lines;
      lines.push_back(rect);
      _dragPreviewGeometry->setLines(lines);

      emit dragPreviewGeometryChanged();
      }

//---------------------------------------------------------
//   endSvgDrag
//    Clean up the drag-preview geometry.
//---------------------------------------------------------

void ZCam::endSvgDrag() {
      _svgDragPath.clear();
      _svgDragBBox = {};
      if (_dragPreviewGeometry) {
            delete _dragPreviewGeometry;
            _dragPreviewGeometry = nullptr;
            emit dragPreviewGeometryChanged();
            }
      }
