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

#include "geometryapi.h"

#include "logger.h"

#include <cmath>
#include <ranges>

//--------------------------------------------------------------------
//     GeometryApi — construction
//--------------------------------------------------------------------

GeometryApi::GeometryApi(QObject* parent) : QObject(parent) {
      }

//--------------------------------------------------------------------
//     Conversion helpers (shared with ScriptApi)
//--------------------------------------------------------------------
//   These functions bridge between the JavaScript representation
//   (QVariantList of [x,y] pairs) and the Clipper2 PathD / PathsD
//   types used internally for boolean and offset operations.
PathD GeometryApi::jsToPath(const QVariantList& jsPath) {
      PathD path;
      path.reserve(jsPath.size());
      for (const QVariant& v : jsPath) {
            QVariantList pair = v.toList();
            if (pair.size() < 2)
                  continue;
            path.push_back({pair[0].toDouble(), pair[1].toDouble()});
            }
      return path;
      }

QVariantList GeometryApi::pathToJs(const PathD& path) {
      QVariantList result;
      result.reserve(static_cast<int>(path.size()));
      for (const PointD& pt : path)
            result.append(QVariantList {pt.x, pt.y});
      return result;
      }

PathsD GeometryApi::jsToPaths(const QVariantList& jsPaths) {
      PathsD paths;
      paths.reserve(jsPaths.size());
      for (const QVariant& v : jsPaths)
            paths.push_back(jsToPath(v.toList()));
      return paths;
      }

QVariantList GeometryApi::pathsToJs(const PathsD& paths) {
      QVariantList result;
      result.reserve(static_cast<int>(paths.size()));
      for (const PathD& path : paths)
            result.append(pathToJs(path));
      return result;
      }

//--------------------------------------------------------------------
//     Vector helpers
//--------------------------------------------------------------------

QVariantList GeometryApi::vec2(double x, double y) {
      return {x, y};
      }

QVariantList GeometryApi::vec3(double x, double y, double z) {
      return {x, y, z};
      }

double GeometryApi::distance(double x1, double y1, double x2, double y2) {
      double dx = x2 - x1;
      double dy = y2 - y1;
      return std::sqrt(dx * dx + dy * dy);
      }

double GeometryApi::angle(double x1, double y1, double x2, double y2) {
      double a = std::atan2(y2 - y1, x2 - x1) * 180.0 / M_PI;
      if (a < 0.0)
            a += 360.0;
      return a;
      }

QVariantList GeometryApi::midpoint(double x1, double y1, double x2, double y2) {
      return {(x1 + x2) * 0.5, (y1 + y2) * 0.5};
      }

QVariantList GeometryApi::rotate(double x, double y, double angleDeg, double cx, double cy) {
      double rad = angleDeg * M_PI / 180.0;
      double c   = std::cos(rad);
      double s   = std::sin(rad);
      double dx  = x - cx;
      double dy  = y - cy;
      return {cx + dx * c - dy * s, cy + dx * s + dy * c};
      }

QVariantList GeometryApi::scale(double x, double y, double sx, double sy) {
      return {x * sx, y * sy};
      }

QVariantList GeometryApi::normalize(double x, double y) {
      double len = std::sqrt(x * x + y * y);
      if (len < 1e-12)
            return {0.0, 0.0};
      return {x / len, y / len};
      }

//--------------------------------------------------------------------
//     Path construction
//--------------------------------------------------------------------

QVariantList GeometryApi::regularPolygon(
    double cx, double cy, double radius, int sides, double startAngleDeg) {
      if (sides < 3)
            sides = 3;
      QVariantList path;
      double startRad = startAngleDeg * M_PI / 180.0;
      for (int i = 0; i < sides; ++i) {
            double a  = startRad + 2.0 * M_PI * i / sides;
            double px = cx + radius * std::cos(a);
            double py = cy + radius * std::sin(a);
            path.append(QVariantList {px, py});
            }
      // Close the path.
      if (!path.isEmpty())
            path.append(path.first().toList());
      return path;
      }

QVariantList GeometryApi::starPolygon(
    double cx, double cy, double outerR, double innerR, int points, double startAngleDeg) {
      if (points < 3)
            points = 3;
      QVariantList path;
      double startRad = startAngleDeg * M_PI / 180.0;
      int total       = points * 2;
      for (int i = 0; i < total; ++i) {
            double r  = (i % 2 == 0) ? outerR : innerR;
            double a  = startRad + M_PI * i / points;
            double px = cx + r * std::cos(a);
            double py = cy + r * std::sin(a);
            path.append(QVariantList {px, py});
            }
      // Close the path.
      if (!path.isEmpty())
            path.append(path.first().toList());
      return path;
      }

QVariantList GeometryApi::rectPath(double x, double y, double w, double h) {
      return {
         QVariantList {    x,     y},
          QVariantList {x + w,     y},
          QVariantList {x + w, y + h},
          QVariantList {    x, y + h},
         QVariantList {    x,     y}
            };
      }

QVariantList GeometryApi::circlePath(double cx, double cy, double radius, int segments) {
      if (segments < 3)
            segments = 64;
      QVariantList path;
      for (int i = 0; i < segments; ++i) {
            double a  = 2.0 * M_PI * i / segments;
            double px = cx + radius * std::cos(a);
            double py = cy + radius * std::sin(a);
            path.append(QVariantList {px, py});
            }
      // Close the path.
      if (!path.isEmpty())
            path.append(path.first().toList());
      return path;
      }

QVariantList GeometryApi::arcPath(
    double cx, double cy, double radius, double startAngle, double sweepAngle, int segments) {
      if (segments < 1)
            segments = 32;
      double startRad = startAngle * M_PI / 180.0;
      double sweepRad = sweepAngle * M_PI / 180.0;
      QVariantList path;
      for (int i = 0; i <= segments; ++i) {
            double a  = startRad + sweepRad * i / segments;
            double px = cx + radius * std::cos(a);
            double py = cy + radius * std::sin(a);
            path.append(QVariantList {px, py});
            }
      return path;
      }

//--------------------------------------------------------------------
//     Boolean operations on path data (Clipper2)
//--------------------------------------------------------------------

QVariantList GeometryApi::unionPaths(const QVariantList& pathsA, const QVariantList& pathsB) {
      PathsD a      = jsToPaths(pathsA);
      PathsD b      = jsToPaths(pathsB);
      PathsD result = Clipper2Lib::BooleanOp(ClipType::Union, FillRule::NonZero, a, b, 4);
      return pathsToJs(result);
      }

QVariantList GeometryApi::differencePaths(const QVariantList& pathsA, const QVariantList& pathsB) {
      PathsD a      = jsToPaths(pathsA);
      PathsD b      = jsToPaths(pathsB);
      PathsD result = Clipper2Lib::BooleanOp(ClipType::Difference, FillRule::NonZero, a, b, 4);
      return pathsToJs(result);
      }

QVariantList GeometryApi::intersectPaths(const QVariantList& pathsA, const QVariantList& pathsB) {
      PathsD a      = jsToPaths(pathsA);
      PathsD b      = jsToPaths(pathsB);
      PathsD result = Clipper2Lib::BooleanOp(ClipType::Intersection, FillRule::NonZero, a, b, 4);
      return pathsToJs(result);
      }

QVariantList GeometryApi::offsetPaths(const QVariantList& paths, double delta) {
      PathsD input = jsToPaths(paths);
      if (input.empty())
            return {};
      // InflatePaths with Round join/end for smooth offsets.
      PathsD result = Clipper2Lib::InflatePaths(
          input, delta, Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Polygon, 2.0, 4, 0.0);
      return pathsToJs(result);
      }

//--------------------------------------------------------------------
//     Geometric queries
//--------------------------------------------------------------------

double GeometryApi::pathArea(const QVariantList& path) {
      PathD p = jsToPath(path);
      return Clipper2Lib::Area(p);
      }

double GeometryApi::pathPerimeter(const QVariantList& path) {
      PathD p = jsToPath(path);
      if (p.size() < 2)
            return 0.0;
      double total = 0.0;
      for (size_t i = 0; i < p.size() - 1; ++i) {
            double dx  = p[i + 1].x - p[i].x;
            double dy  = p[i + 1].y - p[i].y;
            total     += std::sqrt(dx * dx + dy * dy);
            }
      // Closing edge (if the path looks closed).
      double dx0         = p[0].x - p.back().x;
      double dy0         = p[0].y - p.back().y;
      double closingDist = std::sqrt(dx0 * dx0 + dy0 * dy0);
      if (closingDist > 1e-9)
            total += closingDist;
      return total;
      }

bool GeometryApi::pointInPath(double px, double py, const QVariantList& path) {
      PathD p = jsToPath(path);
      if (p.size() < 3)
            return false;
      PointD pt {px, py};
      return Clipper2Lib::PointInPolygon(pt, p) == Clipper2Lib::PointInPolygonResult::IsInside;
      }

QVariantList GeometryApi::pathBoundingBox(const QVariantList& path) {
      PathD p = jsToPath(path);
      if (p.empty())
            return {0.0, 0.0, 0.0, 0.0};
      RectD bounds = Clipper2Lib::GetBounds(p);
      return {bounds.left, bounds.top, bounds.right, bounds.bottom};
      }

QVariantList GeometryApi::simplifyPath(const QVariantList& path, double tolerance) {
      PathD p = jsToPath(path);
      if (p.size() < 3)
            return path;
      // Use TrimCollinear to remove redundant collinear vertices.
      // For tolerance > 0 we scale to int64, simplify, and scale back.
      if (tolerance <= 0.0) {
            PathD simplified = Clipper2Lib::TrimCollinear(p, 4, false);
            return pathToJs(simplified);
            }
      // Scale to int64 for precision-based simplification.
      double scale = 1.0 / tolerance;
      Clipper2Lib::Path64 p64;
      p64.reserve(p.size());
      for (const PointD& pt : p)
            p64.push_back(
                      {static_cast<int64_t>(std::round(pt.x * scale)),
                   static_cast<int64_t>(std::round(pt.y * scale))});
      Clipper2Lib::Path64 simplified64 = Clipper2Lib::TrimCollinear(p64, false);
      // Scale back.
      PathD simplified;
      simplified.reserve(simplified64.size());
      for (const Point64& pt : simplified64)
            simplified.push_back({static_cast<double>(pt.x) / scale, static_cast<double>(pt.y) / scale});
      return pathToJs(simplified);
      }

//--------------------------------------------------------------------
//     Path transforms
//--------------------------------------------------------------------

QVariantList GeometryApi::translatePath(const QVariantList& path, double dx, double dy) {
      QVariantList result;
      result.reserve(path.size());
      for (const QVariant& v : path) {
            QVariantList pair = v.toList();
            if (pair.size() < 2)
                  continue;
            result.append(QVariantList {pair[0].toDouble() + dx, pair[1].toDouble() + dy});
            }
      return result;
      }

QVariantList GeometryApi::rotatePath(const QVariantList& path, double angleDeg, double cx, double cy) {
      double rad = angleDeg * M_PI / 180.0;
      double c   = std::cos(rad);
      double s   = std::sin(rad);
      QVariantList result;
      result.reserve(path.size());
      for (const QVariant& v : path) {
            QVariantList pair = v.toList();
            if (pair.size() < 2)
                  continue;
            double x = pair[0].toDouble() - cx;
            double y = pair[1].toDouble() - cy;
            result.append(QVariantList {cx + x * c - y * s, cy + x * s + y * c});
            }
      return result;
      }

QVariantList GeometryApi::scalePath(const QVariantList& path, double sx, double sy, double cx, double cy) {
      QVariantList result;
      result.reserve(path.size());
      for (const QVariant& v : path) {
            QVariantList pair = v.toList();
            if (pair.size() < 2)
                  continue;
            double x = pair[0].toDouble();
            double y = pair[1].toDouble();
            result.append(QVariantList {cx + (x - cx) * sx, cy + (y - cy) * sy});
            }
      return result;
      }

QVariantList GeometryApi::mirrorPath(const QVariantList& path, bool flipX, bool flipY) {
      QVariantList result;
      result.reserve(path.size());
      for (const QVariant& v : path) {
            QVariantList pair = v.toList();
            if (pair.size() < 2)
                  continue;
            double x = flipX ? -pair[0].toDouble() : pair[0].toDouble();
            double y = flipY ? -pair[1].toDouble() : pair[1].toDouble();
            result.append(QVariantList {x, y});
            }
      return result;
      }

//--------------------------------------------------------------------
//     Array / grid helpers
//--------------------------------------------------------------------

QVariantList GeometryApi::gridPositions(
    double startX, double startY, double stepX, double stepY, int rows, int cols) {
      QVariantList result;
      for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                  result.append(QVariantList {startX + c * stepX, startY + r * stepY});
      return result;
      }

QVariantList GeometryApi::circularPositions(
    double cx, double cy, double radius, int count, double startAngleDeg) {
      if (count < 1)
            return {};
      double startRad = startAngleDeg * M_PI / 180.0;
      QVariantList result;
      for (int i = 0; i < count; ++i) {
            double a = startRad + 2.0 * M_PI * i / count;
            result.append(QVariantList {cx + radius * std::cos(a), cy + radius * std::sin(a)});
            }
      return result;
      }

QVariantList GeometryApi::linePositions(double x1, double y1, double x2, double y2, int count) {
      if (count < 1)
            return {};
      QVariantList result;
      for (int i = 0; i < count; ++i) {
            double t = (count == 1) ? 0.0 : static_cast<double>(i) / (count - 1);
            result.append(QVariantList {x1 + t * (x2 - x1), y1 + t * (y2 - y1)});
            }
      return result;
      }