//=============================================================================
//  ZCam - manufactoring tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#pragma once

#include "types.h"
#include "drw_base.h"

#include <vector>
#include <cmath>
#include <numbers>

class DRW_Coord;

//---------------------------------------------------------
//   DxfTess
//    Shared tessellation helpers for DXF import and preview.
//    The resolution values are taken from Config and clamped
//    to reasonable ranges before use.
//---------------------------------------------------------

namespace DxfTess {

//---------------------------------------------------------
//   circleSegments
//    Number of line segments for a circular or elliptical
//    arc with the given sweep angle (radians) and the
//    configured full-circle resolution.
//---------------------------------------------------------

inline int circleSegments(double sweepRad, int resolution) {
      if (resolution < 2)
            resolution = 2;
      double full  = 2.0 * std::numbers::pi;
      double sweep = std::abs(sweepRad);
      int segs     = static_cast<int>(std::ceil(resolution * sweep / full));
      return std::max(segs, 4);
      }

//---------------------------------------------------------
//   ellipseSegments
//    Alias for circleSegments — elliptical arcs use the same
//    angular subdivision.
//---------------------------------------------------------

inline int ellipseSegments(double sweepRad, int resolution) {
      return circleSegments(sweepRad, resolution);
      }

//---------------------------------------------------------
//   makeUniformKnots
//    Build a clamped uniform knot vector for a B-spline of the
//    given degree and control-point count.
//---------------------------------------------------------

inline std::vector<double> makeUniformKnots(int degree, int controlCount) {
      std::vector<double> knots;
      int n = controlCount - 1;
      int m = n + degree + 1;
      knots.reserve(m + 1);
      for (int i = 0; i <= m; ++i)
            if (i <= degree)
                  knots.push_back(0.0);
            else if (i >= n + 1)
                  knots.push_back(1.0);
            else
                  knots.push_back(double(i - degree) / double(n - degree));
      return knots;
      }

//---------------------------------------------------------
//   findKnotSpan
//    Find the knot span index k with knots[k] <= t < knots[k+1]
//    for a B-spline of the given degree.
//---------------------------------------------------------

inline int findKnotSpan(double t, int degree, const std::vector<double>& knots) {
      int n = static_cast<int>(knots.size()) - degree - 2;
      if (t >= knots[n + 1])
            return n;
      if (t <= knots[degree])
            return degree;
      int low  = degree;
      int high = n + 1;
      int mid  = (low + high) / 2;
      while (t < knots[mid] || t >= knots[mid + 1]) {
            if (t < knots[mid])
                  high = mid;
            else
                  low = mid;
            mid = (low + high) / 2;
            }
      return mid;
      }

//---------------------------------------------------------
//   evaluateBSplinePoint
//    Evaluate a non-rational B-spline at parameter t using
//    de Boor's algorithm.
//---------------------------------------------------------

inline Vec2d evaluateBSplinePoint(double t, int degree, const std::vector<Vec2d>& controls,
                                  const std::vector<double>& knots) {
      int k = findKnotSpan(t, degree, knots);
      std::vector<Vec2d> d(degree + 1);
      for (int j = 0; j <= degree; ++j)
            d[j] = controls[k - degree + j];
      for (int r = 1; r <= degree; ++r) {
            for (int j = degree; j >= r; --j) {
                  int left     = k - degree + j;
                  int right    = k + j + 1 - r;
                  double denom = knots[right] - knots[left];
                  double alpha = (denom < 1e-12) ? 0.0 : (t - knots[left]) / denom;
                  d[j]         = d[j - 1] * (1.0 - alpha) + d[j] * alpha;
                  }
            }
      return d[degree];
      }

//---------------------------------------------------------
//   evaluateSpline
//    Convert a DXF Spline entity into a polyline with the
//    configured number of segments.  Falls back to the control
//    points connected by straight lines when the spline data
//    is unusable.
//---------------------------------------------------------

inline std::vector<Vec2d> evaluateSpline(int degree, const std::vector<DRW_Coord>& controls,
                                         const std::vector<double>& knotsIn, int segments) {
      std::vector<Vec2d> result;
      if (degree < 1 || controls.size() <= static_cast<size_t>(degree) || segments < 2)
            return result;

      std::vector<Vec2d> pts;
      pts.reserve(controls.size());
      for (const auto& c : controls)
            pts.emplace_back(c.x, c.y);

      std::vector<double> knots = knotsIn;
      if (knots.empty() || knots.size() < pts.size() + degree + 1)
            knots = makeUniformKnots(degree, static_cast<int>(pts.size()));

      result.reserve(segments + 1);
      for (int i = 0; i <= segments; ++i) {
            double t = double(i) / double(segments);
            result.push_back(evaluateBSplinePoint(t, degree, pts, knots));
            }
      return result;
      }

      } // namespace DxfTess
