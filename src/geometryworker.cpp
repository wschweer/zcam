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

#include "geometryworker.h"

#include <QRunnable>
#include <QMetaObject>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <limits>

// Tesselator is in the tess2 subdirectory
#include "tesselator.h"

// Clipper2 for stroke (InflatePaths) and line operations
#include "clipper2/clipper.h"
//=========================================================
//   Singleton access
//=========================================================

GeometryWorker& GeometryWorker::instance() {
      static GeometryWorker instance;
      return instance;
      }

GeometryWorker::GeometryWorker() {
      // Use at most 3 threads to avoid oversubscription for small tasks
      m_pool.setMaxThreadCount(std::min(3, QThread::idealThreadCount()));
      m_pool.setExpiryTimeout(1000);
      }

GeometryWorker::~GeometryWorker() {
      m_shuttingDown.store(true, std::memory_order::relaxed);
      m_pool.clear();
      }

//---------------------------------------------------------
//   shutdown
//    Explicitly drain the thread pool at application exit.
//    Called from the aboutToQuit handler so that pending
//    geometry tasks finish before QML objects are destroyed.
//---------------------------------------------------------

void GeometryWorker::shutdown() {
      m_shuttingDown.store(true, std::memory_order::relaxed);
      m_pool.clear();
      }

//=========================================================
//   Helper: minMax for float vertex arrays
//=========================================================

static void minMaxFloat(const float* vert, int len, QVector3D& min, QVector3D& max) {
      if (len <= 0) {
            min = QVector3D();
            max = QVector3D();
            return;
            }
      min = QVector3D(vert[0], vert[1], vert[2]);
      max = min;
      for (int i = 1; i < len; ++i) {
            min.setX(std::min(min.x(), vert[i * 3 + 0]));
            min.setY(std::min(min.y(), vert[i * 3 + 1]));
            min.setZ(std::min(min.z(), vert[i * 3 + 2]));
            max.setX(std::max(max.x(), vert[i * 3 + 0]));
            max.setY(std::max(max.y(), vert[i * 3 + 1]));
            max.setZ(std::max(max.z(), vert[i * 3 + 2]));
            }
      }

static void minMaxByteArray(const QByteArray& vertices, QVector3D& min, QVector3D& max) {
      const float* vert = reinterpret_cast<const float*>(vertices.constData());
      int len           = vertices.size() / (sizeof(float) * 3);
      minMaxFloat(vert, len, min, max);
      }

//=========================================================
//   requestTesselation
//=========================================================

void GeometryWorker::requestTesselation(PathList pathList, TessCallback callback) {
      enqueue<TessResult>(
          [pl = std::move(pathList)]() -> TessResult {
                TessResult result;
                if (pl.empty())
                      return result;

                bool fill = pl.fill();
                if (!fill)
                      return result; // non-fill handled by requestLines

                Tesselator tess;

                // Add contours
                for (const auto& polygon : pl) {
                      std::vector<float> pg;
                      pg.reserve(polygon.size() * 3);
                      for (auto pp : polygon) {
                            pg.push_back(pp.x());
                            pg.push_back(pp.y());
                            pg.push_back(0.0f);
                            }
                      if (polygon.front() != polygon.back()) {
                            pg.push_back(polygon.front().x());
                            pg.push_back(polygon.front().y());
                            pg.push_back(0.0f);
                            }
                      tess.addContour(3, pg.data(), sizeof(float) * 3, static_cast<int>(pg.size() / 3));
                      }

                float normal[3] {0.0f, 0.0f, 1.0f};
                if (!tess.tesselate(TESS_WINDING_NONZERO, TESS_POLYGONS, 3, 3, normal))
                      return result;

                auto vertexCount  = tess.vertexCount();
                auto elementCount = tess.elementCount();
                auto vert         = tess.vertices();
                auto elements     = tess.elements();

                if (!vert)
                      return result;

                QByteArray vertices;
                QByteArray indices;
                vertices.resize(vertexCount * 3 * sizeof(float));
                indices.resize(elementCount * 6 * sizeof(quint32));
                memcpy(vertices.data(), vert, vertices.size());

                quint32* indexPtr = reinterpret_cast<quint32*>(indices.data());
                for (int i = 0; i < elementCount; ++i) {
                      quint32 i0 = static_cast<quint32>(elements[i * 3 + 0]);
                      quint32 i1 = static_cast<quint32>(elements[i * 3 + 1]);
                      quint32 i2 = static_cast<quint32>(elements[i * 3 + 2]);

                      indexPtr[i * 6 + 0] = i0;
                      indexPtr[i * 6 + 1] = i1;
                      indexPtr[i * 6 + 2] = i2;
                      indexPtr[i * 6 + 3] = i0;
                      indexPtr[i * 6 + 4] = i2;
                      indexPtr[i * 6 + 5] = i1;
                      }

                result.vertices = std::move(vertices);
                result.indices  = std::move(indices);
                minMaxByteArray(result.vertices, result.minBound, result.maxBound);
                result.valid = true;
                return result;
                },
          std::move(callback));
      }

//=========================================================
//   requestLines
//=========================================================

void GeometryWorker::requestLines(Clipper2Lib::PathsD lines, LineCallback callback) {
      enqueue<LineResult>(
          [lines = std::move(lines)]() -> LineResult {
                LineResult result;
                if (lines.empty())
                      return result;

                int vertexCount = 0;
                for (const auto& polygon : lines)
                      vertexCount += static_cast<int>(polygon.size());
                if (vertexCount == 0)
                      return result;

                QByteArray vertices;
                vertices.resize(vertexCount * 3 * sizeof(float));
                float* data = reinterpret_cast<float*>(vertices.data());

                int offset = 0;
                for (const auto& polygon : lines) {
                      float* vert = data;
                      for (auto vertex : polygon) {
                            *data++ = static_cast<float>(vertex.x);
                            *data++ = static_cast<float>(vertex.y);
                            *data++ = 0.0f;
                            }
                      QVector3D min, max;
                      int len = static_cast<int>(polygon.size());
                      minMaxFloat(vert, len, min, max);
                      result.subsets.push_back({offset, len, min, max});
                      offset += len;
                      }

                result.vertices = std::move(vertices);

                // Compute overall bounding box from Clipper2
                Clipper2Lib::RectD bounds = Clipper2Lib::GetBounds(lines);
                result.minBound           = QVector3D(bounds.left, bounds.top, -1);
                result.maxBound           = QVector3D(bounds.right, bounds.bottom, 1);
                result.valid              = true;
                return result;
                },
          std::move(callback));
      }

//=========================================================
//   requestStroke
//=========================================================

void GeometryWorker::requestStroke(Clipper2Lib::PathsD paths, double halfLineWidth, int joinType, int endType,
                                   bool fill, StrokeCallback callback) {
      enqueue<StrokeResult>(
          [paths = std::move(paths), halfLineWidth, joinType, endType, fill]() -> StrokeResult {
                StrokeResult result;

                if (halfLineWidth > 0.0) {
                      double miterLim     = 2.0;
                      int precision       = 4;
                      double arcTolerance = 0.0;
                      auto et             = Clipper2Lib::EndType::Round;
                      auto jt             = Clipper2Lib::JoinType(joinType);
                      auto l = Clipper2Lib::InflatePaths(paths, halfLineWidth, jt, et, miterLim, precision,
                                                         arcTolerance);
                      if (!l.empty()) {
                            PathList pl(l);
                            // Close the path list
                            for (auto& p : pl)
                                  if (p.size() > 2 && p.front() != p.back())
                                        p.push_back(p.front());
                            pl.setFill(fill);
                            result.pathList = std::move(pl);
                            result.valid    = true;
                            }
                      }
                else {
                      // No stroke — just convert paths directly
                      PathList pl(paths);
                      pl.setFill(fill);
                      result.pathList = std::move(pl);
                      result.valid    = true;
                      }
                return result;
                },
          std::move(callback));
      }

//=========================================================
//   requestCamData
//=========================================================

void GeometryWorker::requestCamData(CamInput input, CamCallback callback) {
      enqueue<CamResult>(
          [input = std::move(input)]() -> CamResult {
                CamResult result;

                Clipper2Lib::PathsD allMarkLines;
                Clipper2Lib::PathsD allMoveLines;

                for (const auto& layer : input.layers) {
                      if (!layer.burn || layer.tileLines.size() < 2)
                            continue;

                      const auto& tileMarks = layer.tileLines[0];
                      const auto& tileMoves = layer.tileLines[1];

                      for (int row = 0; row < input.panelRows; ++row) {
                            for (int col = 0; col < input.panelColumns; ++col) {
                                  double xo = (input.panelHDistance + input.fixtureW) * col;
                                  double yo = (input.panelVDistance + input.fixtureH) * row;

                                  if (!tileMarks.empty()) {
                                        Clipper2Lib::PathD offsetMarks;
                                        offsetMarks.reserve(tileMarks.size());
                                        for (const auto& pt : tileMarks)
                                              offsetMarks.push_back({pt.x + xo, pt.y + yo});
                                        allMarkLines.push_back(std::move(offsetMarks));
                                        }
                                  if (!tileMoves.empty()) {
                                        Clipper2Lib::PathD offsetMoves;
                                        offsetMoves.reserve(tileMoves.size());
                                        for (const auto& pt : tileMoves)
                                              offsetMoves.push_back({pt.x + xo, pt.y + yo});
                                        allMoveLines.push_back(std::move(offsetMoves));
                                        }
                                  }
                            }
                      }

                Clipper2Lib::PathsD combined;
                // Subset 0: marks
                if (!allMarkLines.empty()) {
                      Clipper2Lib::PathD mergedMarks;
                      for (const auto& p : allMarkLines)
                            mergedMarks.append_range(p);
                      combined.push_back(std::move(mergedMarks));
                      }
                else {
                      combined.push_back({});
                      }
                // Subset 1: moves
                if (!allMoveLines.empty()) {
                      Clipper2Lib::PathD mergedMoves;
                      for (const auto& p : allMoveLines)
                            mergedMoves.append_range(p);
                      combined.push_back(std::move(mergedMoves));
                      }
                else {
                      combined.push_back({});
                      }

                result.combinedLines = std::move(combined);
                result.valid         = true;
                return result;
                },
          std::move(callback));
      }

//=========================================================
//   requestConvexHull
//=========================================================

void GeometryWorker::requestConvexHull(Clipper2Lib::PathD points, ConvexHullCallback callback) {
      enqueue<ConvexHullResult>(
          [points = std::move(points)]() mutable -> ConvexHullResult {
                ConvexHullResult result;
                int n = static_cast<int>(points.size());
                if (n < 3) {
                      result.hull  = points;
                      result.valid = true;
                      return result;
                      }

                // Sort by x, then y
                std::sort(points.begin(), points.end(),
                          [](const Clipper2Lib::PointD& a, const Clipper2Lib::PointD& b) {
                                if (a.x != b.x)
                                      return a.x < b.x;
                                return a.y < b.y;
                                });

                Clipper2Lib::PathD hull;

                // Lower hull
                for (int i = 0; i < n; ++i) {
                      while (hull.size() >= 2) {
                            auto& h = hull;
                            double cross =
                                (h[h.size() - 1].x - h[h.size() - 2].x) * (points[i].y - h[h.size() - 2].y) -
                                (h[h.size() - 1].y - h[h.size() - 2].y) * (points[i].x - h[h.size() - 2].x);
                            if (cross <= 0)
                                  hull.pop_back();
                            else
                                  break;
                            }
                      hull.push_back(points[i]);
                      }

                size_t lowerSize = hull.size();

                // Upper hull
                for (int i = n - 2; i >= 0; --i) {
                      while (hull.size() > lowerSize) {
                            auto& h = hull;
                            double cross =
                                (h[h.size() - 1].x - h[h.size() - 2].x) * (points[i].y - h[h.size() - 2].y) -
                                (h[h.size() - 1].y - h[h.size() - 2].y) * (points[i].x - h[h.size() - 2].x);
                            if (cross <= 0)
                                  hull.pop_back();
                            else
                                  break;
                            }
                      hull.push_back(points[i]);
                      }

                result.hull  = std::move(hull);
                result.valid = true;
                return result;
                },
          std::move(callback));
      }