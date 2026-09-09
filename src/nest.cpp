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

// libnest2d compile defines are provided by the CMake target
// (Libnest2D::libnest2d defines LIBNEST2D_THREADING_std,
// LIBNEST2D_GEOMETRIES_clipper, LIBNEST2D_OPTIMIZER_nlopt)

#include "nest.h"
#include "zcam.h"
#include "project.h"
#include "machine.h"
#include "undo.h"
#include "element3d.h"
#include "polygon.h"
#include "rectangle.h"
#include "ellipse.h"
#include "text.h"
#include "logger.h"
#include "clipper.h"

// Work around a libnest2d / C++23 ambiguity: the Double base class has
// implicit conversions to double, so the compiler-generated reversed
// operator!= candidates are ambiguous for Radians vs Radians.
// We define the missing operators before including the header.
#include <libnest2d/common.hpp>
inline bool operator!=(const libnest2d::Radians& a, const libnest2d::Radians& b) {
      return static_cast<double>(a) != static_cast<double>(b);
      }

inline bool operator!=(const libnest2d::Degrees& a, const libnest2d::Radians& b) {
      return !(a == b);
      }

inline bool operator!=(const libnest2d::Radians& a, const libnest2d::Degrees& b) {
      return !(b == a);
      }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wambiguous-reversed-operator"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <libnest2d/libnest2d.hpp>

#pragma GCC diagnostic pop

#include <vector>
#include <cmath>
#include <limits>
#include <QQuaternion>
#include <QMatrix4x4>

//--------------------------------------------------------------------
//     Nest
//--------------------------------------------------------------------

Nest::Nest(ZCam* zcam, Element* parent) : Group(zcam, parent) {
      setName("nest");
      initBinFromMachine();
      }

//--------------------------------------------------------------------
//   initBinFromMachine
//--------------------------------------------------------------------

void Nest::initBinFromMachine() {
      if (!zcam || !zcam->project())
            return;
      Machine* m = zcam->project()->machine();
      if (!m)
            return;
      QVector3D travel = m->maxTravel();
      set_binSize(QVector2D(travel.x(), travel.y()));
      }

//--------------------------------------------------------------------
//   set_binSize
//--------------------------------------------------------------------

void Nest::set_binSize(QVector2D v) {
      if (v == _binSize)
            return;
      _binSize = v;
      emit binSizeChanged();
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      // Keep the four corner handles in sync: every bin-size change
      // moves all corners.  The batch wrapper bumps the vertex revision
      // exactly once so the 3D viewport repositions the handle spheres.
      beginBatchUpdate();
      endBatchUpdate();
      }

//--------------------------------------------------------------------
//   updateSelectionGeometry
//    Draw the bin rectangle from (0,0) to (binSize.x, binSize.y).
//--------------------------------------------------------------------

void Nest::updateSelectionGeometry() {
      if (!_selectionGeometry)
            return;
      double w = _binSize.x();
      double h = _binSize.y();

      Clipper2Lib::PathD rect;
      rect.push_back({0.0, 0.0});
      rect.push_back({w, 0.0});
      rect.push_back({w, 0.0});
      rect.push_back({w, h});
      rect.push_back({w, h});
      rect.push_back({0.0, h});
      rect.push_back({0.0, h});
      rect.push_back({0.0, 0.0});
      Clipper2Lib::PathsD lines;
      lines.push_back(rect);
      _selectionGeometry->setLines(lines);
      }

//--------------------------------------------------------------------
//   isVertex
//    All four bin-corner handles are directly draggable.
//--------------------------------------------------------------------

bool Nest::isVertex(int idx) const {
      return idx >= 0 && idx < 4;
      }

//--------------------------------------------------------------------
//   vertexPos
//    Return the local position of the idx-th bin-corner handle.
//    The bin rectangle spans (0,0) .. (binSize.x, binSize.y),
//    matching the outline drawn by updateSelectionGeometry():
//       0 = bottom-left  (0, 0)
//       1 = bottom-right (w, 0)
//       2 = top-right    (w, h)
//       3 = top-left     (0, h)
//--------------------------------------------------------------------

QVector3D Nest::vertexPos(int idx) const {
      if (idx < 0 || idx >= 4)
            return {};
      switch (idx) {
            case 0: return QVector3D(0.0, 0.0, 0.0);
            case 1: return QVector3D(_binSize.x(), 0.0, 0.0);
            case 2: return QVector3D(_binSize.x(), _binSize.y(), 0.0);
            case 3: return QVector3D(0.0, _binSize.y(), 0.0);
            }
      return {};
      }

//--------------------------------------------------------------------
//   vertexWorldPos
//    Return the world (root) position of the idx-th bin-corner
//    handle so the 3D viewport can place the handle spheres.
//--------------------------------------------------------------------

QVector3D Nest::vertexWorldPos(int idx) const {
      if (idx < 0 || idx >= 4)
            return {};
      return globalMatrix().map(vertexPos(idx));
      }

//--------------------------------------------------------------------
//   setVertexPos
//    Move the idx-th bin corner to the given LOCAL position while
//    resizing the bin.  The opposite corner stays fixed in world
//    space; binSize and pos are adjusted accordingly so the bin is
//    resized rather than moved.  The pos parameter arrives in the
//    element's local (pre-scale) coordinate system because
//    ZCam::dragVertexTo() converts the world position via
//    globalMatrix().inverted().map(), which undoes the scale —
//    same convention as Rectangle::setVertexPos().
//
//    Corner pairs (opposite corners):
//       0 ↔ 2,  1 ↔ 3
//--------------------------------------------------------------------

void Nest::setVertexPos(int idx, const QVector3D& pos) {
      if (idx < 0 || idx >= 4)
            return;

      // Local position of the corner OPPOSITE the dragged one, using the
      // OLD bin size.  This corner must keep its world position.
      int opposite          = (idx + 2) % 4;
      QVector3D oldFixLocal = vertexPos(opposite);

      // Raw new dimensions from the drag position (per axis): the
      // distance between the dragged corner and the fixed corner.
      double newW = std::abs(pos.x() - oldFixLocal.x());
      double newH = std::abs(pos.y() - oldFixLocal.y());

      // Clamp to a minimum size so the bin can never collapse.
      newW = std::max(newW, 1.0);
      newH = std::max(newH, 1.0);

      // The opposite corner's local position with the NEW bin size.
      // Corner order (matches updateSelectionGeometry()):
      //   0 = (0,0)   1 = (w,0)   2 = (w,h)   3 = (0,h)
      auto cornerLocal = [](int i, double w, double h) -> QVector3D {
            switch (i) {
                  case 0: return QVector3D(0.0, 0.0, 0.0);
                  case 1: return QVector3D(float(w), 0.0, 0.0);
                  case 2: return QVector3D(float(w), float(h), 0.0);
                  case 3: return QVector3D(0.0, float(h), 0.0);
                  }
            return {};
            };
      QVector3D newFixLocal = cornerLocal(opposite, newW, newH);

      // Keep the fixed corner at its current WORLD position:
      //   newPos + C·M·newFixLocal == pos + C·M·oldFixLocal
      // (C·M = local matrix without translation).  For idx == 2 the
      // fixed corner is the local origin (0,0), so the position does
      // not move.  Note: the parameter *pos* shadows Element3d::pos(),
      // so the current position is read explicitly via this->pos().
      QMatrix4x4 m             = matrix();
      m(0, 3)                  = 0;
      m(1, 3)                  = 0;
      m(2, 3)                  = 0;
      QVector3D newWorldCenter = this->pos() + m.map(oldFixLocal - newFixLocal);

      // Set internal values directly, then emit the change signals in
      // one batch: begin/endBatchUpdate() suppresses the per-signal
      // vertexRevisionChanged and emits it exactly once at the end
      // (which drives the QML handle repositioning during the drag).
      _binSize = QVector2D(newW, newH);
      _pos     = newWorldCenter;

      // Rebuild the bin outline before QML learns about the change.
      updateSelectionGeometry();

      beginBatchUpdate();
      emit posChanged();
      emit binSizeChanged();
      emit selectionGeometryChanged();
      endBatchUpdate(); // sets _batching=false and emits vertexRevisionChanged
      }

//--------------------------------------------------------------------
//   nest
//    Run libnest2d to pack all child Element3d geometry into the
//    bin and apply the resulting transforms.
//
//    The child's fillPathList() is in the child's local coordinate
//    system (before pos/rot/scale).  We normalise each polygon so its
//    bounding box starts at (0,0), run libnest2d, then set the child's
//    rot.z and pos so its geometry lands at the nest position.
//--------------------------------------------------------------------

bool Nest::nest() {
      if (!zcam || !zcam->project())
            return false;
      // ── Collect child geometry ───────────────────────────────────
      struct ChildInfo {
            Element3d* element;
            Clipper2Lib::PathD localPath; ///< largest contour, child-local
            QRectF localBBox;             ///< bbox of localPath
            };
      std::vector<ChildInfo> children;

      for (Element* child : this->children()) {
            auto* e3d = qobject_cast<Element3d*>(child);
            if (!e3d || !e3d->show())
                  continue;
            const PathList& pl = e3d->fillPathList();
            if (pl.empty())
                  continue;

            // Find the largest-area contour (Path2d uses Vec2d with x()/y())
            const Path2d* bestPath = nullptr;
            double bestArea        = -1.0;
            for (const auto& path : pl) {
                  double area = 0.0;
                  int n       = path.size();
                  for (int i = 0; i < n; ++i) {
                        const auto& p1  = path[i];
                        const auto& p2  = path[(i + 1) % n];
                        area           += p1.x() * p2.y() - p2.x() * p1.y();
                        }
                  area = std::abs(area) * 0.5;
                  if (area > bestArea) {
                        bestArea = area;
                        bestPath = &path;
                        }
                  }
            if (!bestPath || bestPath->size() < 3)
                  continue;

            // Convert Path2d (Vec2d) to Clipper2Lib::PathD (PointD)
            Clipper2Lib::PathD clipperPath;
            clipperPath.reserve(bestPath->size());
            double minX = std::numeric_limits<double>::max();
            double minY = std::numeric_limits<double>::max();
            double maxX = std::numeric_limits<double>::lowest();
            double maxY = std::numeric_limits<double>::lowest();
            for (const auto& pt : *bestPath) {
                  double px = pt.x();
                  double py = pt.y();
                  clipperPath.push_back({px, py});
                  minX = std::min(minX, px);
                  minY = std::min(minY, py);
                  maxX = std::max(maxX, px);
                  maxY = std::max(maxY, py);
                  }

            children.push_back({e3d, std::move(clipperPath), QRectF(minX, minY, maxX - minX, maxY - minY)});
            }

      if (children.empty()) {
            Warning("Nest::nest: no child elements with geometry found");
            return false;
            }

      // ── Coordinate scale ──────────────────────────────────────────
      constexpr double SCALE = 1000000.0;

      // ── Build the bin ─────────────────────────────────────────────
      libnest2d::Box bin(libnest2d::PointImpl(0, 0),
          libnest2d::PointImpl(static_cast<ClipperLib::cInt>(_binSize.x() * SCALE),
              static_cast<ClipperLib::cInt>(_binSize.y() * SCALE)));

      // ── Build items ──────────────────────────────────────────────
      // Normalise each polygon so its bbox starts at (0,0).
      std::vector<libnest2d::Item> items;
      items.reserve(children.size());

      for (const auto& ci : children) {
            double offX = ci.localBBox.left();
            double offY = ci.localBBox.top();

            ClipperLib::Path clipperPath;
            for (const auto& pt : ci.localPath) {
                  double x = (pt.x - offX) * SCALE;
                  double y = (pt.y - offY) * SCALE;
                  clipperPath.push_back(ClipperLib::IntPoint(static_cast<ClipperLib::cInt>(std::round(x)),
                      static_cast<ClipperLib::cInt>(std::round(y))));
                  }

            items.emplace_back(libnest2d::PolygonImpl(clipperPath));
            }

      if (items.empty()) {
            Warning("Nest::nest: no valid items could be created");
            return false;
            }

      // ── Run nesting ──────────────────────────────────────────────
      libnest2d::NestConfig<libnest2d::BottomLeftPlacer, libnest2d::FirstFitSelection> cfg;
      cfg.placer_config.min_obj_distance = static_cast<ClipperLib::cInt>(spacing() * SCALE);
      cfg.placer_config.allow_rotations  = allowRotation();

      libnest2d::NestControl ctl;
      std::size_t packedCount = libnest2d::nest(
          items.begin(), items.end(), bin, static_cast<ClipperLib::cInt>(spacing() * SCALE), cfg, ctl);

      Debug("Nest::nest: packed {} bin(s), {} item(s)", packedCount, items.size());
      Info("Nest::nest: packing finished, {} item(s)", items.size());

      if (packedCount == 0) {
            Warning("Nest::nest: no items could be packed");
            return false;
            }

      // ── Apply results ─────────────────────────────────────────────
      // For each packed item, libnest2d returns:
      //   tx, ty  — bin-local position (mm) of the normalised (0,0) corner
      //   rotDeg  — rotation angle (0 or ±90°)
      //
      // We set the child's rot.z to rotDeg and compute pos so the
      // rotated geometry's bounding-box top-left lands at (tx, ty).
      // Scale and mirror are preserved.

      Project* proj = zcam->project();
      if (proj && proj->undo())
            proj->undo()->beginMacro();

      int applied = 0;
      for (size_t i = 0; i < children.size() && i < items.size(); ++i) {
            const auto& item = items[i];
            if (item.binId() < 0)
                  continue;
            ++applied;
            const auto& ci = children[i];

            double tx     = double(item.translation().X) / SCALE;
            double ty     = double(item.translation().Y) / SCALE;
            double rotDeg = double(item.rotation()) * 180.0 / M_PI;

            QVector3D oldRot = ci.element->rot();
            QVector3D newRot(oldRot.x(), oldRot.y(), rotDeg);

            // Build the rotation+scale matrix to compute the rotated bbox
            QMatrix4x4 rotMatrix;
            rotMatrix.rotate(QQuaternion::fromEulerAngles(newRot));
            QVector3D sc = ci.element->scale();
            rotMatrix.scale(sc);
            if (ci.element->mirrorX())
                  rotMatrix.scale(-1, 1, 1);
            if (ci.element->mirrorY())
                  rotMatrix.scale(1, -1, 1);

            double rMinX = std::numeric_limits<double>::max();
            double rMinY = std::numeric_limits<double>::max();
            for (const auto& pt : ci.localPath) {
                  auto v = rotMatrix.map(QVector3D(float(pt.x), float(pt.y), 0.0f));
                  rMinX  = std::min(rMinX, double(v.x()));
                  rMinY  = std::min(rMinY, double(v.y()));
                  }

            QVector3D oldPos = ci.element->pos();
            QVector3D newPos(tx - rMinX, ty - rMinY, oldPos.z());

            proj->changeProperty(ci.element, QStringLiteral("rot"), QVariant::fromValue(newRot));
            proj->changeProperty(ci.element, QStringLiteral("pos"), QVariant::fromValue(newPos));
            }

      if (proj && proj->undo())
            proj->undo()->endMacro();

      emit zcam->nestFinished(applied, static_cast<int>(items.size()));

      zcam->update();
      zcam->setCamDirty(true);
      return applied > 0;
      }