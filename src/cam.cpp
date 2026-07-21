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

#include "zcam.h"
#include "cam.h"
#include "project.h"
#include "fixture.h"
#include "framing.h"
#include "recipe.h"
#include "geometryworker.h"

#include <QPointer>
#include <future>
#include <limits>
#include <memory>
#include <vector>

using namespace Clipper2Lib;
/**
 * @brief Berechnet das Kreuzprodukt (Orientierungstest) von drei Punkten.
 *
 * @param O Der Ursprungspunkt (Anker).
 * @param A Der erste Punkt.
 * @param B Der zweite Punkt.
 * @return double
 * > 0: A-B ist eine "Linksabbiegung" von O-A (gegen den Uhrzeigersinn).
 * < 0: A-B ist eine "Rechtsabbiegung" von O-A (im Uhrzeigersinn).
 * = 0: Die Punkte sind kollinear.
 */
double cross_product(PointD O, PointD A, PointD B) {
      return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
      }

/**
 * @brief Berechnet die konvexe Hülle einer 2D-Punktwolke.
 *
 * Verwendet den Monotone Chain (Andrew's) Algorithmus.
 * Die zurückgegebene Hülle ist ein Polygon (Vektor von Punkten)
 * gegen den Uhrzeigersinn sortiert.
 *
 * @param points Ein std::vector von Point-Strukturen (die Punktwolke).
 * Dieser Vektor wird intern sortiert (sortiert übergeben).
 * @return std::vector<Point> Die Punkte, die die konvexe Hülle bilden.
 */
PathD calculateConvexHull(PathD& points) {
      int n = points.size();

      // the hull needs at least three points
      if (n < 3)
            return points;

      // sort point: x-Koordinate
      std::sort(points.begin(), points.end(), [](PointD a, PointD b) {
            if (a.x != b.x)
                  return a.x < b.x;
            return a.y < b.y;
            });

      PathD hull;

      // 2. Aufbau der unteren Hülle
      // Wir iterieren von links nach rechts
      for (int i = 0; i < n; ++i) {
            // Solange die letzten beiden Punkte auf der Hülle und der neue Punkt
            // eine Rechtskurve (oder eine Gerade) bilden, entfernen wir den
            // letzten Punkt von der Hülle.
            while (hull.size() >= 2 && cross_product(hull[hull.size() - 2], hull.back(), points[i]) <= 0)
                  hull.pop_back();
            hull.push_back(points[i]);
            }

      // 3. Aufbau der oberen Hülle
      // Wir merken uns die Größe der unteren Hülle.
      int lower_hull_size = hull.size();

      // Wir iterieren von rechts nach links (beginnend beim vorletzten Punkt,
      // da der letzte schon auf der Hülle ist).
      for (int i = n - 2; i >= 0; --i) {
            // Dieselbe Logik wie oben: Entferne Punkte, die "konkav" sind.
            while (hull.size() > lower_hull_size &&
                   cross_product(hull[hull.size() - 2], hull.back(), points[i]) <= 0)
                  hull.pop_back();
            hull.push_back(points[i]);
            }
      return hull;
      }

//---------------------------------------------------------
//   machineTime
//---------------------------------------------------------

double machineTime(Vec3d& p1, const CamPath& camPath) {
      double sec  = 0;
      double feed = 1000.0; // mm/min

      bool first = true;
      for (auto p2 : camPath) {
            double length = (p2 - p1).length();
            p1            = p2;
            if (first) {
                  sec   += (length * 60.0) / feed;
                  first  = false;
                  feed   = camPath.feed == 0.0 ? 1000.0 : camPath.feed;
                  }
            else {
                  sec += (length * 60.0) / feed;
                  }
            }
      return sec;
      }

double machineTime(const CamPathList& pl) {
      double sec = 0;

      Vec3d p1 {0.0, 0.0, 0.0};

      for (auto camPath : pl)
            sec += machineTime(p1, camPath);
      return sec;
      }

//---------------------------------------------------------
//   Cam
//---------------------------------------------------------

Cam::Cam(ZCam* zcam, Element* parent) : Element3d(zcam, parent) {
      setName("cam");
      zcam->project()->set_cam(this);

      // Cam owns the display geometry for all laser layers.
      _geometry = new TessGeometry(this);
      QJSEngine::setObjectOwnership(_geometry, QJSEngine::CppOwnership);
      set_model("CamShape.qml");

      // Cam is hidden by default — the user enables it via the
      // visibility toggle in the project tree when needed.
      set_show(false);
      };

//---------------------------------------------------------
//   updateCam
//    Collect geometry from all LaserLayer children (through the
//    Fixture), arrange them in a panel grid (rows × columns with
//    distanceX/distanceY offsets), and store the combined line
//    geometry in this Cam's _geometry.  Also update the Framing
//    contour around all collected data.
//
//    The per-layer tile lines are collected on the main thread (they
//    require QObject property access).  The expensive panel-grid
//    replication and line merging are offloaded to a GeometryWorker
//    background thread.  Results are applied on the main thread.
//---------------------------------------------------------

void Cam::updateCam() {
      Fixture* fixture = zcam->project()->fixture();
      if (!fixture) {
            Critical("no fixture");
            Clipper2Lib::PathsD empty;
            _geometry->setLines(empty);
            return;
            }

      double w, h;
      fixture->size(w, h);

      // Collect display lines from all LaserLayers.
      // Each LaserLayer produces two subsets:
      //   subset 0 — mark lines
      //   subset 1 — move lines
      GeometryWorker::CamInput input;
      input.fixtureW       = w;
      input.fixtureH       = h;
      input.panelRows      = panelRows();
      input.panelColumns   = panelColumns();
      input.panelHDistance = panelHDistance();
      input.panelVDistance = panelVDistance();

      for (auto e : fixture->children()) {
            if (!isType<Recipe>(e))
                  continue;
            auto* ll = toType<Recipe>(e);

            Clipper2Lib::PathsD tileLines = ll->collectDisplayLines();
            if (tileLines.size() < 2) {
                  Debug("nothing to display for {}", ll->name());
                  continue;
                  }

            GeometryWorker::CamInput::Layer layerData;
            layerData.tileLines = std::move(tileLines);
            layerData.burn      = ll->burn();
            input.layers.push_back(std::move(layerData));
            }

      // Offload panel-grid replication and line merging to a background thread.
      QPointer<Cam> guard(this);
      GeometryWorker::instance().requestCamData(std::move(input),
                                                [this, guard](const GeometryWorker::CamResult& r) {
                                                      if (!guard || !r.valid)
                                                            return;
                                                      _geometry->setLines(r.combinedLines);
                                                      });
      }

//---------------------------------------------------------
//   convexHull
//    Compute the convex hull of all burn LaserLayer geometry,
//    including panel-grid offsets.  The single-tile polygon data
//    is replicated across the grid to produce the full set of
//    points, then the convex hull is computed.
//---------------------------------------------------------

Clipper2Lib::PathD Cam::convexHull() const {
      Clipper2Lib::PathD points;
      Fixture* fixture = zcam->project()->fixture();
      if (!fixture)
            return points;

      double w, h;
      fixture->size(w, h);

      double panelHD = panelHDistance();
      double panelVD = panelVDistance();

      for (auto e : fixture->children()) {
            if (!isType<Recipe>(e))
                  continue;
            auto* layer = toType<Recipe>(e);
            if (!layer->burn())
                  continue;

            // Get single-tile polygon data
            Clipper2Lib::PathsD pl = layer->collectLayerPath();

            // Replicate across the panel grid
            for (int row = 0; row < panelRows(); ++row) {
                  for (int col = 0; col < panelColumns(); ++col) {
                        double xo = (panelHD + w) * col;
                        double yo = (panelVD + h) * row;
                        for (const auto& p : pl)
                              for (const auto& pt : p)
                                    points.push_back({pt.x + xo, pt.y + yo});
                        }
                  }
            }
      return calculateConvexHull(points);
      }

//---------------------------------------------------------
//   boundingBox
//    Compute the axis-aligned bounding box of all burn LaserLayer
//    geometry, including panel-grid offsets.  The single-tile
//    polygon data is replicated across the grid to produce the
//    full set of points, then the bounding box is computed.
//---------------------------------------------------------

Clipper2Lib::RectD Cam::boundingBox() const {
      Fixture* fixture = zcam->project()->fixture();
      if (!fixture)
            return {};

      double w, h;
      fixture->size(w, h);

      double panelHD = panelHDistance();
      double panelVD = panelVDistance();

      double minX = std::numeric_limits<double>::max();
      double minY = std::numeric_limits<double>::max();
      double maxX = std::numeric_limits<double>::lowest();
      double maxY = std::numeric_limits<double>::lowest();

      bool found = false;

      for (auto e : fixture->children()) {
            if (!isType<Recipe>(e))
                  continue;
            auto* layer = toType<Recipe>(e);
            if (!layer->burn())
                  continue;

            // Get single-tile polygon data
            Clipper2Lib::PathsD pl = layer->collectLayerPath();

            // Replicate across the panel grid
            for (int row = 0; row < panelRows(); ++row) {
                  for (int col = 0; col < panelColumns(); ++col) {
                        double xo = (panelHD + w) * col;
                        double yo = (panelVD + h) * row;
                        for (const auto& p : pl)
                              for (const auto& pt : p) {
                                    double x = pt.x + xo;
                                    double y = pt.y + yo;
                                    if (x < minX)
                                          minX = x;
                                    if (y < minY)
                                          minY = y;
                                    if (x > maxX)
                                          maxX = x;
                                    if (y > maxY)
                                          maxY = y;
                                    found = true;
                                    }
                        }
                  }
            }

      if (!found)
            return {};

      return {minX, minY, maxX, maxY};
      }
