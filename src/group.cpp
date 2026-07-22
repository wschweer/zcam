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

#include "element3d.h"
#include "group.h"
#include "zcam.h"
#include "clipper.h"

//---------------------------------------------------------
//   Layer
//---------------------------------------------------------

Group::Group(ZCam* w, Element* parent) : Element3d(w, parent) {
      setName("layer");
      }

//---------------------------------------------------------
//   update
//    Rebuild the selection bounding box from the current children.
//    Also (re)connect to each child's change signals so the
//    bounding box stays in sync when children move, resize or
//    toggle visibility.
//---------------------------------------------------------

void Group::update(int flags) {
      // First update all children so their _pathList is populated.
      // This is critical because ProjectTree.qml calls element.update()
      // BEFORE recursing into children via addElement().  Without this,
      // childrenBoundingBox() would see empty path lists and produce
      // an empty bounding box.
      for (Element* child : children()) {
            auto* e3d = qobject_cast<Element3d*>(child);
            if (!e3d)
                  continue;
            e3d->update(flags);
            // Connect to each child's change signals (UniqueConnection
            // prevents duplicate connections on repeated update() calls).
            // Qt::UniqueConnection requires a member function pointer, so
            // we use the onChildChanged() slot instead of a lambda.
            connect(e3d, &Element3d::vertexRevisionChanged, this, &Group::onChildChanged, Qt::UniqueConnection);
            connect(e3d, &Element3d::selectionGeometryChanged, this, &Group::onChildChanged, Qt::UniqueConnection);
            connect(e3d, &Element3d::showChanged, this, &Group::onChildChanged, Qt::UniqueConnection);
            }
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      }

//---------------------------------------------------------
//   onChildChanged
//---------------------------------------------------------

void Group::onChildChanged() {
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      }

//---------------------------------------------------------
//   updateSelectionGeometry
//    Build a line rectangle from the bounding box of all child
//    elements so the QML layer can render it when this Group is
//    selected.  The bounding box is computed in this element's
//    local coordinate system.
//---------------------------------------------------------

void Group::updateSelectionGeometry() {
      if (!_selectionGeometry)
            return;
      QRectF bbox = childrenBoundingBox();
      if (bbox.isNull() || bbox.isEmpty()) {
            Clipper2Lib::PathsD empty;
            _selectionGeometry->setLines(empty);
            return;
            }
      // Lines primitive draws pairs of vertices as independent segments.
      // Emit the four edges of the bounding-box rectangle.
      Clipper2Lib::PathD rect;
      rect.push_back({bbox.left(), bbox.top()});     // P0
      rect.push_back({bbox.right(), bbox.top()});    // P1  (top edge)
      rect.push_back({bbox.right(), bbox.top()});    // P1
      rect.push_back({bbox.right(), bbox.bottom()}); // P2  (right edge)
      rect.push_back({bbox.right(), bbox.bottom()}); // P2
      rect.push_back({bbox.left(), bbox.bottom()});  // P3  (bottom edge)
      rect.push_back({bbox.left(), bbox.bottom()});  // P3
      rect.push_back({bbox.left(), bbox.top()});     // P0  (left edge)
      Clipper2Lib::PathsD lines;
      lines.push_back(rect);
      _selectionGeometry->setLines(lines);
      }

//---------------------------------------------------------
//   cycloid
//---------------------------------------------------------

void cycloid(Clipper2Lib::PointD pt, const Clipper2Lib::PointD& vector, PathD& path, double dist, double r) {
      double precision = 0.005;
      int s            = (std::numbers::pi / acos(1.0 - (precision / r))) / 4.0 + 0.5;
      if (s < 2) // minimum is 2 segments per quadrant
            s = 2;
      double step = (0.5 * std::numbers::pi) / double(s);

      double startAngle         = 0.0;
      double endAngle           = 2.0 * M_PI;
      int steps                 = 2.0 * M_PI / step;
      Clipper2Lib::PointD vstep = vector * (dist / steps);

      for (double angle = startAngle; angle < endAngle; angle += step) {
            path.push_back({cos(angle) * r + pt.x, sin(angle) * r + pt.y});
            pt.x += vstep.x;
            pt.y += vstep.y;
            }
      }

//---------------------------------------------------------
//   wobble
//    draw lines as a seqeuence of circles
//    with distance of wstep
//    - the circles are equally spaced
//    - a circle is drawn at p1 and p2
//---------------------------------------------------------

PathD wobble(const PathD& path, double step, double r) {
      PathD result;

      Clipper2Lib::PointD lp = path.front();
      int size               = path.size();
      double d               = 0;
      int steps              = 0;
      for (int i = 1; i < size; ++i) {
            Clipper2Lib::PointD v  = path[i] - lp;
            double l               = sqrt(v.x * v.x + v.y * v.y); // length of vector
            Clipper2Lib::PointD n  = Clipper2Lib::PointD(v.x / l, v.y / l) * step;
            d                     += l;
            while (d >= step) {
                  d -= step;
                  ++steps;
                  lp = lp + n;
                  }
            }
      step += d / (double)steps;

      lp = path.front();
      d  = 0;
      for (int i = 1; i < size; ++i) {
            Clipper2Lib::PointD v  = path[i] - lp;
            double l               = sqrt(v.x * v.x + v.y * v.y); // length of vector
            Clipper2Lib::PointD n  = Clipper2Lib::PointD(v.x / l, v.y / l) * step;
            d                     += l;
            while (d >= step) {
                  Clipper2Lib::PointD p   = lp + n;
                  d                      -= step;
                  Clipper2Lib::PointD cv  = p - lp;
                  double cl               = sqrt(cv.x * cv.x + cv.y * cv.y);
                  Clipper2Lib::PointD cn  = Clipper2Lib::PointD(cv.x / cl, cv.y / cl);
                  cycloid(lp, cn, result, step, r);
                  lp = p;
                  }
            }
      return result;
      }
