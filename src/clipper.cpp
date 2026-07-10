//=============================================================================
//  wcam
//  CAM path generator
//
//  Copyright (C) 2024 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "logger.h"
#include "clipper.h"
#include <QTransform>

//---------------------------------------------------------
//   invertPolygons
//---------------------------------------------------------

void Clipper::invertPolygons(PathsD& polygons, RectD bounds) {
      Clipper clipper;
      if (bounds.IsEmpty())
            bounds = Clipper2Lib::GetBounds(polygons);

      PathsD bbox;
      bbox.push_back(bounds.AsPath());

      clipper.AddSubject(bbox);
      clipper.AddClip(polygons);
      clipper.Execute(ClipType::Xor, FillRule::NonZero, polygons);
      }

//---------------------------------------------------------
//   createHatch
//---------------------------------------------------------

PathsD Clipper::createHatch(const RectD& bounds, double interval, double degrees) {
      double w = bounds.Width();
      double h = bounds.Height();

      double d = sqrt(w * w + h * h) * .5;

      QTransform t;
      auto pt = bounds.MidPoint();
      t.translate(pt.x, pt.y);

      PathsD ll;
      int n  = (d + d + interval) / interval;
      n     *= 4;
      ll.reserve(n);

      w = d;
      h = d;

      t.rotate(degrees);
      int row = 0;
      for (double y = -h; y <= h; y += interval) {
            qreal xo, yo;
            PathD& p = ll.emplace_back(PathD());
            t.map(-w, y, &xo, &yo);
            p.emplace_back(xo, yo, row);
            t.map(w, y, &xo, &yo);
            p.emplace_back(xo, yo, row);
            ++row;
            }

      return ll;
      }

static void myZCB(const PointD& p1, const PointD& p2, const PointD& p3, const PointD& p4, PointD& pt) {
      pt.z = p1.z;
      }

//---------------------------------------------------------
//   hatch
//---------------------------------------------------------

PathsD Clipper::hatch(const PathsD& input, const RectD& bounds, double angle, double interval) {
      PathsD lines = createHatch(bounds, interval, angle);

      PathsD dontCare;
      PathsD result;
      Clear();
      SetZCallback(myZCB);
      AddOpenSubject(lines);
      AddClip(input);
      //Debug("Intersect: input {} lines {}", input.size(), lines.size());
      Execute(ClipType::Intersection, FillRule::NonZero, dontCare, result);
      return result;
      }
