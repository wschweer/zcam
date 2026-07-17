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

#include "xmlreader.h"
#include "xmlwriter.h"
#include "zcam.h"
#include "tessgeometry.h"
#include "types.h"
#include "ellipse.h"
#include <QMatrix4x4>

//---------------------------------------------------------
//   set_size
//    Custom setter for the size property that enforces the
//    lockSize mode (analogous to Element3d::set_scale):
//      Off    – accept the value as-is
//      Lock   – preserve the current aspect ratio; the axis
//               that changed the most drives the other proportionally
//      Square – force width == height using the most-changed axis
//---------------------------------------------------------
void Ellipse::set_size(QVector2D v) {
      if (v == _size)
            return;
      auto mode = static_cast<LockScaleMode>(lockSize());
      if (mode == LockScaleMode::Square) {
            double s = std::max(v.x(), v.y());
            v = QVector2D(s, s);
            }
      else if (mode == LockScaleMode::Lock) {
            if (qAbs(_size.x()) >= 1e-12 && qAbs(_size.y()) >= 1e-12) {
                  double factorW = v.x() / _size.x();
                  double factorH = v.y() / _size.y();
                  double factor  = std::max(factorW, factorH);
                  v = QVector2D(_size.x() * factor, _size.y() * factor);
                  }
            }
      if (v == _size)
            return;
      _size = v;
      emit sizeChanged();
      }

//---------------------------------------------------------
//   Ellipse
//---------------------------------------------------------

Ellipse::Ellipse(ZCam* w, Element* parent) : Element3d(w, parent) {
      setName("");
      _fill     = true;
      _geometry = new TessGeometry(this);
      QJSEngine::setObjectOwnership(_geometry, QJSEngine::CppOwnership);

      connect(this, &Ellipse::sizeChanged, [this] {
            if (!_suppressUpdate)
                  update();
            });
      connect(this, &Ellipse::startAngleChanged, [this] {
            if (!_suppressUpdate)
                  update();
            });
      connect(this, &Ellipse::endAngleChanged, [this] {
            if (!_suppressUpdate)
                  update();
            });
      }

//---------------------------------------------------------
//   isClosed
//---------------------------------------------------------

bool Ellipse::isClosed() const {
      return painterPath.isClosed();
      }

//---------------------------------------------------------
//   createPath
//    Build the ellipse path using cubic bezier segments.
//    The ellipse is centred at the local origin and inscribed
//    in the bounding rectangle (-w/2, -h/2, w, h).
//
//    When startAngle == 0 and endAngle == 360 (the defaults),
//    a full closed ellipse is drawn via addEllipse().
//    Otherwise a partial arc is drawn from startAngle to endAngle
//    using addArc().  The arc is drawn clockwise (negative sweep)
//    to match the Qt coordinate system (y-axis down).
//---------------------------------------------------------

PathList Ellipse::createPath() {
      QRectF rect = ellipseRect();

      painterPath.clear();

      double sa = startAngle();
      double ea = endAngle();

      // Normalise angles to [0, 360)
      sa = std::fmod(sa, 360.0);
      if (sa < 0.0)
            sa += 360.0;
      ea = std::fmod(ea, 360.0);
      if (ea < 0.0)
            ea += 360.0;

      // Full circle: use addEllipse for a closed path
      if (qFuzzyCompare(sa, 0.0) && qFuzzyCompare(ea, 360.0)) {
            painterPath.addEllipse(rect);
            }
      else {
            double sweep = ea - sa;
            if (sweep <= 0.0)
                  sweep += 360.0;
            // Qt arc uses negative sweep for clockwise (y-down)
            painterPath.addArc(rect, sa, -sweep);
            }

      auto pl = painterPath.toPathList();
      pl.setFill(fill());
      return pl;
      }

//---------------------------------------------------------
//   update
//---------------------------------------------------------

void Ellipse::update(int /* flags */) {
      _pathList = createPath();
      strokeAndFill();
      }

//---------------------------------------------------------
//   ellipseRect
//    Returns the bounding rectangle of the ellipse, centred
//    at the local origin.
//---------------------------------------------------------

QRectF Ellipse::ellipseRect() const {
      QRectF r(-size().x() * .5, -size().y() * .5, size().x(), size().y());
      return r;
      }

//---------------------------------------------------------
//   isVertex
//    All four corner handles are directly draggable.
//---------------------------------------------------------

bool Ellipse::isVertex(int idx) const {
      return idx >= 0 && idx < 4;
      }

//---------------------------------------------------------
//   vertexPos
//    Return the local position of the idx-th corner handle.
//    The ellipse is centred at the local origin, so the four
//    handles sit at the corners of the bounding rectangle:
//       0 = bottom-left  (-w/2, -h/2)
//       1 = bottom-right (+w/2, -h/2)
//       2 = top-right    (+w/2, +h/2)
//       3 = top-left     (-w/2, +h/2)
//---------------------------------------------------------

QVector3D Ellipse::vertexPos(int idx) const {
      if (idx < 0 || idx >= 4)
            return {};
      double hw = size().x() * .5;
      double hh = size().y() * .5;
      switch (idx) {
            case 0: return QVector3D(-hw, -hh, 0.0);
            case 1: return QVector3D(+hw, -hh, 0.0);
            case 2: return QVector3D(+hw, +hh, 0.0);
            case 3: return QVector3D(-hw, +hh, 0.0);
            }
      return {};
      }

//---------------------------------------------------------
//   vertexWorldPos
//    Return the world position of the idx-th corner handle.
//---------------------------------------------------------

QVector3D Ellipse::vertexWorldPos(int idx) const {
      if (idx < 0 || idx >= 4)
            return {};
      QVector3D local = vertexPos(idx);
      return globalMatrix().map(local);
      }

//---------------------------------------------------------
//   setVertexPos
//    Move the idx-th corner to the given local position.
//    The opposite corner stays fixed; size and pos are adjusted
//    accordingly so the ellipse is resized rather than moved.
//
//    Corner pairs (opposite corners):
//       0 ↔ 2,  1 ↔ 3
//---------------------------------------------------------

void Ellipse::setVertexPos(int idx, const QVector3D& pos) {
      if (idx < 0 || idx >= 4)
            return;

      // Local position of the opposite (fixed) corner.
      int opp              = (idx + 2) % 4;
      QVector3D fixedLocal = vertexPos(opp);

      // Raw new size from the drag position (per axis).
      // The pos parameter is already in the element's local (pre-scale)
      // coordinate system because dragVertexTo() converts the world
      // position via globalMatrix().inverted().map(), which undoes the
      // scale (globalMatrix includes the element's own scale).
      // Therefore the size difference is already the unscaled value —
      // no additional division by scale is needed.
      QVector3D newSizeLocal(pos.x() - fixedLocal.x(), pos.y() - fixedLocal.y(), 0.0);

      double newW = std::abs(newSizeLocal.x());
      double newH = std::abs(newSizeLocal.y());

      // Clamp to a minimum size.
      newW = std::max(newW, 1.0);
      newH = std::max(newH, 1.0);

      // Direction from the fixed corner toward the dragged corner.
      // Determined by the corner index, NOT by the mouse position:
      // when the size is (0,0) all corners are at the origin so the
      // mouse-position-based direction is undefined.
      //   idx 0 (-w/2,-h/2) → dirX=-1, dirY=-1
      //   idx 1 (+w/2,-h/2) → dirX=+1, dirY=-1
      //   idx 2 (+w/2,+h/2) → dirX=+1, dirY=+1
      //   idx 3 (-w/2,+h/2) → dirX=-1, dirY=+1
      double dirX = (idx == 1 || idx == 2) ? 1.0 : -1.0;
      double dirY = (idx == 2 || idx == 3) ? 1.0 : -1.0;

      // Apply lockSize enforcement when dragging handles.
      auto mode = static_cast<LockScaleMode>(lockSize());
      if (mode == LockScaleMode::Square) {
            // Force width == height using the larger dimension.
            double s = std::max(newW, newH);
            newW = s;
            newH = s;
            }
      else if (mode == LockScaleMode::Lock) {
            if (qAbs(_size.x()) < 1e-12 || qAbs(_size.y()) < 1e-12) {
                  // Initial creation with zero size: no aspect ratio
                  // to preserve yet — fall back to a square.
                  double s = std::max(newW, newH);
                  newW = s;
                  newH = s;
                  }
            else {
                  // Preserve the current aspect ratio using the
                  // larger scale factor.
                  double factorW = newW / _size.x();
                  double factorH = newH / _size.y();
                  double factor  = std::max(factorW, factorH);
                  newW = _size.x() * factor;
                  newH = _size.y() * factor;
                  }
            }

      // Recompute the center AFTER lockSize enforcement so the
      // opposite (fixed) corner truly stays fixed.  The center is
      // offset from the fixed corner by half the (possibly adjusted)
      // size in the drag direction.
      QVector3D newCenterLocal = fixedLocal + QVector3D(dirX * newW * .5, dirY * newH * .5, 0.0);

      // Convert local center offset to world (parent) space using the
      // element's local matrix with translation removed.
      QMatrix4x4 m             = matrix();
      m(0, 3)                  = 0;
      m(1, 3)                  = 0;
      m(2, 3)                  = 0;
      QVector3D worldOffset    = m.map(newCenterLocal);
      QVector3D newWorldCenter = this->pos() + worldOffset;

      // Set internal values directly, WITHOUT emitting signals.
      _pos         = newWorldCenter;
      _size        = QVector2D(newW, newH);
      _matrixDirty = true;
      update();

      // Now emit change signals so QML picks up the new position
      // and size simultaneously with the already-updated geometry.
      beginBatchUpdate();
      _suppressUpdate = true;
      emit posChanged();
      emit sizeChanged();
      _suppressUpdate = false;
      endBatchUpdate(); // sets _batching=false and emits vertexRevisionChanged
      emit geometryChanged();
      }