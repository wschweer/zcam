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
#include <algorithm>

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

inline Vec2d evaluateBSplinePoint(
    double t, int degree, const std::vector<Vec2d>& controls, const std::vector<double>& knots) {
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

inline std::vector<Vec2d> evaluateSpline(
    int degree, const std::vector<DRW_Coord>& controls, const std::vector<double>& knotsIn, int segments) {
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

//---------------------------------------------------------
//   insertKnot
//    Böhm's knot insertion: insert a knot u into the
//    B-spline (degree, controls, knots) by updating the
//    control points in-place.  Returns the new control
//    point vector.  Used by bsplineToCubicBeziers to
//    convert a B-spline into a sequence of Bézier segments.
//---------------------------------------------------------

inline std::vector<Vec2d> insertKnot(
    std::vector<Vec2d> controls, std::vector<double> knots, int degree, double u) {
      int n = static_cast<int>(controls.size()) - 1;
      int k = findKnotSpan(u, degree, knots);
      // controls[k-degree+1 .. k] are affected
      std::vector<Vec2d> newControls(n + 2);
      for (int i = 0; i <= k - degree; ++i)
            newControls[i] = controls[i];
      for (int i = k + 1; i <= n; ++i)
            newControls[i + 1] = controls[i];
      for (int i = k - degree + 1; i <= k; ++i) {
            double denom   = knots[i + degree] - knots[i];
            double alpha   = (denom < 1e-12) ? 0.0 : (u - knots[i]) / denom;
            newControls[i] = controls[i - 1] * (1.0 - alpha) + controls[i] * alpha;
            }
      return newControls;
      }

//---------------------------------------------------------
//   bsplineToCubicBeziers
//    Convert a clamped B-spline of degree ≤ 3 into a
//    sequence of cubic Bézier control-point quadruples.
//    Each quadruple is (p0, p1, p2, p3) defining one
//    cubic Bézier segment.
//
//    For degree 3 the conversion is exact: each knot span
//    becomes one cubic Bézier segment.  We insert each
//    interior knot until its multiplicity equals the
//    degree, splitting the spline into Bézier segments.
//    For degree 2 we first elevate to degree 3.
//    For degree 1 (polyline) the control polygon is
//    returned as line segments encoded as degenerate
//    cubics (collinear control points).
//---------------------------------------------------------

struct CubicBezier {
      Vec2d p0, p1, p2, p3;
      };

inline std::vector<CubicBezier> bsplineToCubicBeziers(
    int degree, const std::vector<Vec2d>& controlsIn, const std::vector<double>& knotsIn) {
      std::vector<CubicBezier> result;
      if (degree < 1 || controlsIn.size() <= static_cast<size_t>(degree))
            return result;

      // --- Degree 1: straight lines as degenerate cubics ---
      if (degree == 1) {
            for (size_t i = 0; i + 1 < controlsIn.size(); ++i) {
                  Vec2d p0 = controlsIn[i];
                  Vec2d p3 = controlsIn[i + 1];
                  Vec2d d  = (p3 - p0) * (1.0 / 3.0);
                  result.push_back({p0, p0 + d, p3 - d, p3});
                  }
            return result;
            }

      // --- Degree 2: elevate to degree 3 ---
      std::vector<Vec2d> controls = controlsIn;
      std::vector<double> knots   = knotsIn;

      if (knots.empty() || knots.size() < controls.size() + degree + 1)
            knots = makeUniformKnots(degree, static_cast<int>(controls.size()));

      if (degree == 2) {
            // Degree elevation 2 → 3
            int n = static_cast<int>(controls.size()) - 1;
            std::vector<Vec2d> elevated(n + 2);
            elevated[0]     = controls[0];
            elevated[n + 1] = controls[n];
            for (int i = 1; i <= n; ++i) {
                  double alpha = double(i) / double(n + 1);
                  elevated[i]  = controls[i - 1] * alpha + controls[i] * (1.0 - alpha);
                  }
            // Rebuild clamped uniform knot vector for degree 3
            knots    = makeUniformKnots(3, static_cast<int>(elevated.size()));
            controls = elevated;
            degree   = 3;
            }

      if (degree != 3)
            return result; // unsupported degree

      // --- Degree 3: extract Bézier segments via knot insertion ---
      // Insert each interior knot until its multiplicity = degree (3).
      // After full insertion, each span has exactly one segment,
      // and the control points form consecutive Bézier segments.

      // Work on copies so we can mutate
      std::vector<Vec2d> cps = controls;
      std::vector<double> ks = knots;

      // Collect unique interior knot values
      std::vector<double> interiorKnots;
      for (size_t i = degree + 1; i + degree < ks.size(); ++i) {
            if (ks[i] > ks[degree] + 1e-12 && ks[i] < ks[ks.size() - degree - 1] - 1e-12) {
                  if (interiorKnots.empty() || std::abs(interiorKnots.back() - ks[i]) > 1e-12)
                        interiorKnots.push_back(ks[i]);
                  }
            }

      // Insert each interior knot until its multiplicity equals degree
      for (double u : interiorKnots) {
            // Count current multiplicity
            auto countMult = [&]() {
                  int mult = 0;
                  for (double k : ks)
                        if (std::abs(k - u) < 1e-12)
                              ++mult;
                  return mult;
                  };
            while (countMult() < degree) {
                  cps = insertKnot(cps, ks, degree, u);
                  // Insert u into the knot vector at the correct position
                  auto it = std::upper_bound(ks.begin(), ks.end(), u);
                  ks.insert(it, u);
                  }
            }

      // After full knot insertion, adjacent Bézier segments share
      // their endpoint.  The control points are arranged as:
      //   [p0, p1, p2, p3, p3', p4, p5, p6, p6', p7, p8, p9, ...]
      // where p3==p3' is the shared endpoint between segment 0 and 1.
      // So segment i uses cps[3*i .. 3*i+3].
      int numSegs = (static_cast<int>(cps.size()) - 1) / 3;
      for (int i = 0; i < numSegs; ++i) {
            int base = i * 3;
            result.push_back({cps[base], cps[base + 1], cps[base + 2], cps[base + 3]});
            }
      return result;
      }

      } // namespace DxfTess
