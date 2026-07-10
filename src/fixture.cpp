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

#include <ranges>
#include <QMatrix4x4>

#include "zcam.h"
#include "fixture.h"
#include "fixture.h"
#include "layer.h"
#include "recipe.h"
#include "laserlayer.h"

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
double cross_product(PointD O, PointD A, PointD B)
      {
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
PathD calculateConvexHull(PathD& points)
      {
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
            while (hull.size() > lower_hull_size && cross_product(hull[hull.size() - 2], hull.back(), points[i]) <= 0)
                  hull.pop_back();
            hull.push_back(points[i]);
            }
      return hull;
      }

//---------------------------------------------------------
//   Fixture
//---------------------------------------------------------

Fixture::Fixture(ZCam* w, Element* parent) : Element3d(w, parent)
      {
      setName("fixture");
      w->topLevel()->set_fixture(this);
//      connect(posProperty, &Property::valueChanged, [this] { update(); });
//      connect(rotProperty, &Property::valueChanged, [this] { update(); });
//      connect(scaleProperty, &Property::valueChanged, [this] { update(); });
//      setModel("Fixture.qml");
      }

//---------------------------------------------------------
//   size
//---------------------------------------------------------

Clipper2Lib::RectD Fixture::size(double& width, double& height) const
      {
      Clipper2Lib::PathsD pl;
#if 0
      for (auto e : children()) {
            if (!isType<LaserLayer>(e))
                  continue;
            auto layer = toType<LaserLayer>(e);
            if (!layer->burn())
                  continue;
            if (!layer->baseElement()) {
                  Critical("no base element for <{}>", layer->name());
                  continue;
                  }
            for (auto e : layer->baseElement()->children()) {
                  auto* ce = toType<Element3d>(e);
                  QMatrix4x4 matrix;
                  if (ce->parent() && isType<Layer>(ce->parent()))
                        matrix = toType<Layer>(ce->parent())->matrix() * ce->matrix();
                  else
                        matrix = ce->matrix();

                  for (auto p : ce->pathList()) {
                        Clipper2Lib::PathD path;
                        for (auto pp : p) {
                              auto pt = matrix.map(QVector3D(pp.x(), pp.y(), 0.0));
                              path.emplace_back(pt.x(), pt.y());
                              }
                        pl.push_back(path);
                        }
                  }
            }
#endif
      Clipper2Lib::RectD r = GetBounds(pl);
      width                = std::abs(r.right - r.left);
      height               = std::abs(r.top - r.bottom);
      return r;
      }

//---------------------------------------------------------
//   convexHull
//---------------------------------------------------------

Clipper2Lib::PathD Fixture::convexHull() const
      {
      Clipper2Lib::PathD points;
      for (auto e : children()) {
            if (!isType<LaserLayer>(e))
                  continue;
            auto layer = toType<LaserLayer>(e);
            if (!layer->burn())
                  continue;

            PathsD pl = layer->collectLayerPath();
            for (const auto& p : pl) {
                  for (const auto& pt : p)
                        points.push_back(pt);
                  }
            }
      return calculateConvexHull(points);
      }

//---------------------------------------------------------
//   update
//---------------------------------------------------------

void Fixture::update(int flags)
      {
      if (!zcam->topLevel() || !zcam->topLevel()->cad()) {
            Debug("======incomplete");
            return;
            }
      auto allLaserLayer = views::filter(children(), [](QObject* e) { return isType<LaserLayer>(e); });
      for (auto ll : allLaserLayer)
            toType<LaserLayer>(ll)->update();
      }
