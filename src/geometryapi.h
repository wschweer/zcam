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

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "clipper.h"

//--------------------------------------------------------------------
//     GeometryApi
//--------------------------------------------------------------------
//   Pure math/geometry helper class exposed to JavaScript as the
//   global ``geom`` object.  All methods operate on plain data
//   (numbers and arrays of [x,y] pairs) — no Element pointers are
//   involved.  This makes the API safe, side-effect-free and easy
//   to compose in user scripts.
//
//   Path convention:  a "path" is a QVariantList of [x,y] pairs,
//   e.g. [[0,0],[10,0],[10,10],[0,10]].  A "paths" value is a
//   QVariantList of path arrays.
//
//   The conversion helpers jsToPath / pathToJs / jsToPaths /
//   pathsToJs are public static so that ScriptApi (the ``zcam``
//   object) can reuse them when bridging between Element geometry
//   and JavaScript arrays.
class GeometryApi : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("Access via ScriptEngine 'geom' global")

    public:
      GeometryApi(QObject* parent = nullptr);

      //------------------------------------------------------------------
      //  Conversion helpers — shared with ScriptApi
      //------------------------------------------------------------------

      /// JS array of [x,y] pairs → Clipper2 PathD.
      static PathD jsToPath(const QVariantList& jsPath);
      /// Clipper2 PathD → JS array of [x,y] pairs.
      static QVariantList pathToJs(const PathD& path);
      /// JS array of paths → Clipper2 PathsD.
      static PathsD jsToPaths(const QVariantList& jsPaths);
      /// Clipper2 PathsD → JS array of arrays of [x,y] pairs.
      static QVariantList pathsToJs(const PathsD& paths);

      //------------------------------------------------------------------
      //  Vector helpers
      //------------------------------------------------------------------

      /// Create a 2D vector [x, y].
      Q_INVOKABLE QVariantList vec2(double x, double y);
      /// Create a 3D vector [x, y, z].
      Q_INVOKABLE QVariantList vec3(double x, double y, double z);
      /// Euclidean distance between two 2D points.
      Q_INVOKABLE double distance(double x1, double y1, double x2, double y2);
      /// Angle from (x1,y1) to (x2,y2) in degrees [0, 360).
      Q_INVOKABLE double angle(double x1, double y1, double x2, double y2);
      /// Midpoint of two 2D points → [mx, my].
      Q_INVOKABLE QVariantList midpoint(double x1, double y1, double x2, double y2);
      /// Rotate (x,y) by angleDeg about (cx,cy) → [rx, ry].
      Q_INVOKABLE QVariantList rotate(double x, double y, double angleDeg, double cx = 0, double cy = 0);
      /// Scale (x,y) by (sx,sy) → [sx*x, sy*y].
      Q_INVOKABLE QVariantList scale(double x, double y, double sx, double sy);
      /// Normalize (x,y) to unit length → [nx, ny] (or [0,0] if zero).
      Q_INVOKABLE QVariantList normalize(double x, double y);

      //------------------------------------------------------------------
      //  Path construction (returns array of [x,y] pairs)
      //------------------------------------------------------------------

      /// Regular polygon with *sides* vertices on a circle of *radius*
      /// centred at (cx,cy), starting at *startAngleDeg*.
      Q_INVOKABLE QVariantList regularPolygon(
          double cx, double cy, double radius, int sides, double startAngleDeg = 0);
      /// Star polygon with *points* outer vertices and *points* inner
      /// vertices alternating between *outerR* and *innerR*.
      Q_INVOKABLE QVariantList starPolygon(
          double cx, double cy, double outerR, double innerR, int points, double startAngleDeg = 0);
      /// Rectangle outline as a closed path of 4 points.
      Q_INVOKABLE QVariantList rectPath(double x, double y, double w, double h);
      /// Circle approximated by *segments* line segments.
      Q_INVOKABLE QVariantList circlePath(double cx, double cy, double radius, int segments = 64);
      /// Circular arc from *startAngle* to *startAngle + sweepAngle*
      /// (both in degrees), approximated by *segments* line segments.
      Q_INVOKABLE QVariantList arcPath(
          double cx, double cy, double radius, double startAngle, double sweepAngle, int segments = 32);

      //------------------------------------------------------------------
      //  Boolean operations on path data (Clipper2)
      //------------------------------------------------------------------

      /// Union of two sets of paths → paths.
      Q_INVOKABLE QVariantList unionPaths(const QVariantList& pathsA, const QVariantList& pathsB);
      /// Difference (A minus B) of two sets of paths → paths.
      Q_INVOKABLE QVariantList differencePaths(const QVariantList& pathsA, const QVariantList& pathsB);
      /// Intersection of two sets of paths → paths.
      Q_INVOKABLE QVariantList intersectPaths(const QVariantList& pathsA, const QVariantList& pathsB);
      /// Offset (inflate/deflate) paths by *delta* → paths.
      Q_INVOKABLE QVariantList offsetPaths(const QVariantList& paths, double delta);

      //------------------------------------------------------------------
      //  Geometric queries
      //------------------------------------------------------------------

      /// Signed area of a single path (positive for CCW, negative for CW).
      Q_INVOKABLE double pathArea(const QVariantList& path);
      /// Perimeter (total edge length) of a path, including the closing edge.
      Q_INVOKABLE double pathPerimeter(const QVariantList& path);
      /// True if point (px,py) is inside the polygon *path*.
      Q_INVOKABLE bool pointInPath(double px, double py, const QVariantList& path);
      /// Bounding box of a path → [minX, minY, maxX, maxY].
      Q_INVOKABLE QVariantList pathBoundingBox(const QVariantList& path);
      /// Simplify a path by removing collinear vertices (tolerance in units).
      Q_INVOKABLE QVariantList simplifyPath(const QVariantList& path, double tolerance = 0.01);

      //------------------------------------------------------------------
      //  Path transforms
      //------------------------------------------------------------------

      /// Translate every point of *path* by (dx, dy).
      Q_INVOKABLE QVariantList translatePath(const QVariantList& path, double dx, double dy);
      /// Rotate every point of *path* by *angleDeg* about (cx, cy).
      Q_INVOKABLE QVariantList rotatePath(
          const QVariantList& path, double angleDeg, double cx = 0, double cy = 0);
      /// Scale every point of *path* by (sx, sy) about (cx, cy).
      Q_INVOKABLE QVariantList scalePath(
          const QVariantList& path, double sx, double sy, double cx = 0, double cy = 0);
      /// Mirror *path* along X and/or Y axis about the origin.
      Q_INVOKABLE QVariantList mirrorPath(const QVariantList& path, bool flipX, bool flipY);

      //------------------------------------------------------------------
      //  Array / grid helpers
      //------------------------------------------------------------------

      /// Rectangular grid of positions → array of [x, y].
      Q_INVOKABLE QVariantList gridPositions(
          double startX, double startY, double stepX, double stepY, int rows, int cols);
      /// Positions evenly distributed on a circle → array of [x, y].
      Q_INVOKABLE QVariantList circularPositions(
          double cx, double cy, double radius, int count, double startAngleDeg = 0);
      /// Positions evenly distributed along a line → array of [x, y].
      Q_INVOKABLE QVariantList linePositions(double x1, double y1, double x2, double y2, int count);
      };