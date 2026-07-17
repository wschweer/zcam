//=============================================================================
//  wcam
//  G-Code generator
//
//  Copyright (C) 2023 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "xmlreader.h"
#include "xmlwriter.h"
#include "zcam.h"
#include "tessgeometry.h"
#include "types.h"
#include "rectangle.h"
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
void Rectangle::set_size(QVector2D v) {
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
//   Rectangle
//---------------------------------------------------------

Rectangle::Rectangle(ZCam* w, Element* parent) : Element3d(w, parent) {
      setName("");
      _fill     = true;
      _geometry = new TessGeometry(this);
      QJSEngine::setObjectOwnership(_geometry, QJSEngine::CppOwnership);

      connect(this, &Rectangle::cornerChanged, [this] {
            if (!_suppressUpdate)
                  update();
            });
      connect(this, &Rectangle::sizeChanged, [this] {
            if (!_suppressUpdate)
                  update();
            });
      }

//---------------------------------------------------------
//   isClosed
//---------------------------------------------------------

bool Rectangle::isClosed() const {
      return painterPath.isClosed();
      }

//---------------------------------------------------------
//   createPath
//---------------------------------------------------------

PathList Rectangle::createPath() {
      auto rect = rectangle();

      double r = corner();
      painterPath.clear();
      if (r == 0.0) {
            painterPath.moveTo(rect.topLeft());
            painterPath.lineTo(rect.topRight());
            painterPath.lineTo(rect.bottomRight());
            painterPath.lineTo(rect.bottomLeft());
            painterPath.lineTo(rect.topLeft());
            }
      else {
            double d = 2 * r;
            QSizeF sz(d, d);
            painterPath.moveTo({rect.left() + r, rect.bottom()});

            QRectF re({rect.left(), rect.bottom() - d}, sz);
            painterPath.arcTo(re, -90, -90);

            re = QRectF({rect.left(), rect.top()}, sz);
            painterPath.arcTo(re, -180, -90);

            re = QRectF({rect.right() - d, rect.top()}, sz);
            painterPath.arcTo(re, -270, -90);

            re = QRectF({rect.right() - d, rect.bottom() - d}, sz);
            painterPath.arcTo(re, 0, -90);

            painterPath.lineTo({rect.left() + r, rect.bottom()});
            }
      auto pl = painterPath.toPathList();
      pl.setFill(fill());
      return pl;
      }

//---------------------------------------------------------
//   update
//---------------------------------------------------------

void Rectangle::update(int /* flags */) {
      _pathList = createPath();
      strokeAndFill();
      }

//---------------------------------------------------------
//   rectangle
//---------------------------------------------------------

QRectF Rectangle::rectangle() const {
      QRectF r(-size().x() * .5, -size().y() * .5, size().x(), size().y());
      return r;
      }

//---------------------------------------------------------
//   isVertex
//    All four corner handles are directly draggable.
//---------------------------------------------------------

bool Rectangle::isVertex(int idx) const {
      return idx >= 0 && idx < 4;
      }

//---------------------------------------------------------
//   vertexPos
//    Return the local position of the idx-th corner handle.
//    The rectangle is centered at the local origin, so the four
//    corners are at (±w/2, ±h/2).
//       0 = bottom-left  (-w/2, -h/2)
//       1 = bottom-right (+w/2, -h/2)
//       2 = top-right    (+w/2, +h/2)
//       3 = top-left     (-w/2, +h/2)
//---------------------------------------------------------

QVector3D Rectangle::vertexPos(int idx) const {
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

QVector3D Rectangle::vertexWorldPos(int idx) const {
      if (idx < 0 || idx >= 4)
            return {};
      QVector3D local = vertexPos(idx);
      return globalMatrix().map(local);
      }

//---------------------------------------------------------
//   setVertexPos
//    Move the idx-th corner to the given local position.
//    The opposite corner stays fixed; size and pos are adjusted
//    accordingly so the rectangle is resized rather than moved.
//
//    Corner pairs (opposite corners):
//       0 ↔ 2,  1 ↔ 3
//---------------------------------------------------------

void Rectangle::setVertexPos(int idx, const QVector3D& pos) {
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
      // Determined by the corner index, NOT by the mouse position.
      //   idx 0 (-w/2,-h/2) → dirX=-1, dirY=-1
      //   idx 1 (+w/2,-h/2) → dirX=+1, dirY=-1
      //   idx 2 (+w/2,+h/2) → dirX=+1, dirY=+1
      //   idx 3 (-w/2,+h/2) → dirX=-1, dirY=+1
      double dirX = (idx == 1 || idx == 2) ? 1.0 : -1.0;
      double dirY = (idx == 2 || idx == 3) ? 1.0 : -1.0;

      // Apply lockSize enforcement when dragging handles.
      auto mode = static_cast<LockScaleMode>(lockSize());
      if (mode == LockScaleMode::Square) {
            double s = std::max(newW, newH);
            newW = s;
            newH = s;
            }
      else if (mode == LockScaleMode::Lock) {
            if (qAbs(_size.x()) < 1e-12 || qAbs(_size.y()) < 1e-12) {
                  double s = std::max(newW, newH);
                  newW = s;
                  newH = s;
                  }
            else {
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

      // Rebuild geometry before QML learns about the new position.
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
