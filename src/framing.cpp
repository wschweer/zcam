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

#include "clipper2/clipper.h"
#include "zcam.h"
#include "framing.h"
#include "cam.h"
#include "project.h"
#include "fixture.h"
#include <algorithm>
#include <cmath>

//---------------------------------------------------------
//   Framing
//---------------------------------------------------------

Framing::Framing(ZCam* w, Element* parent) : Element3d(w, parent) {
      setName("framing");
      _geometry = new TessGeometry(this);
      QJSEngine::setObjectOwnership(_geometry, QJSEngine::CppOwnership);
      // The framing contour is rebuilt when the CAM geometry changes ...
      connect(w->project(), &Project::updateFraming, [this] { update(); });
      // ... when the framing type changes ...
      connect(this, &Framing::framingTypeChanged, this, [this] { update(); });
      // ... and (for the Rectangle type) when the user edits the size.
      connect(this, &Framing::sizeChanged, this, [this] {
            if (!_suppressUpdate)
                  update();
            });
      update();
      }

//---------------------------------------------------------
//   set_size
//    Plain passthrough setter for the size property.  No lockSize
//    enforcement is applied: the user is free to resize the framing
//    rectangle with any aspect ratio.  A separate lockSize row is not
//    exposed for the Framing element.
//---------------------------------------------------------

void Framing::set_size(QVector2D v) {
      if (v == _size)
            return;
      _size = v;
      emit sizeChanged();
      }

//---------------------------------------------------------
//   isClosed
//---------------------------------------------------------

bool Framing::isClosed() const {
      return painterPath.isClosed();
      }

//---------------------------------------------------------
//   createPath
//    Build the centered framing rectangle in local coordinates.
//    The outline is a simple closed quadrilateral centred at the
//    element origin (like a normal Rectangle element).
//---------------------------------------------------------

void Framing::createPath() {
      double hw = size().x() * .5;
      double hh = size().y() * .5;
      painterPath.clear();
      painterPath.moveTo(QPointF(-hw, -hh));
      painterPath.lineTo(QPointF(+hw, -hh));
      painterPath.lineTo(QPointF(+hw, +hh));
      painterPath.lineTo(QPointF(-hw, +hh));
      painterPath.lineTo(QPointF(-hw, -hh));
      auto pl = painterPath.toPathList();
      pl.setFill(fill());
      _pathList = pl;
      }

//---------------------------------------------------------
//   update
//    Build the framing contour according to the selected type and
//    recompute the world-space laser contour.
//
//    - BoundingBox: axis-aligned rectangle around all burn geometry
//    - ConvexHull : convex hull polygon around all burn geometry
//    - Rectangle  : the user-defined, user-editable rectangle (this
//                   element's own pos/rot/scale/size)
//
//    The on-canvas geometry (local, centred) is always kept so the
//    3-D canvas applies the element transform via ProjectTree.qml.
//    The laser, however, reads a work-field-space contour: for the
//    auto types the element transform is the identity (the contour
//    is already stored in field space), for the Rectangle type the
//    element's own transform (and, if configured, the camera
//    projection) must be applied.  worldContour() is recomputed
//    here on the main thread so the laser background thread always
//    sees a consistent, up-to-date contour.
//---------------------------------------------------------

void Framing::update(int flags) {
      Cam* cam = zcam->project() ? zcam->project()->cam() : nullptr;

      switch (static_cast<FramingType>(framingType())) {
            case FramingType::BoundingBox: {
                  if (cam) {
                        Clipper2Lib::RectD bbox = cam->boundingBox();
                        if (!bbox.IsEmpty()) {
                              _pathList.clear();
                              _pathList.push_back(bbox.AsPath());
                              closePath(_pathList);
                              _pathList.setFill(fill());
                              }
                        }
                  else
                        _pathList.clear();
                  break;
                  }
            case FramingType::ConvexHull: {
                  if (cam) {
                        Clipper2Lib::PathD hull = cam->convexHull();
                        _pathList.clear();
                        if (hull.size() >= 3) {
                              _pathList.push_back(hull);
                              _pathList.setFill(fill());
                              }
                        }
                  else
                        _pathList.clear();
                  break;
                  }
            case FramingType::Rectangle:
                  createPath(); // sets _pathList (centred, local)
                  break;
            }

      // Stroke/fill inflation (lineWidth handling) and canvas geometry.
      strokeAndFill();

      // Recompute the world-space laser contour from the local geometry.
      // projectPathListToXY applies the element's globalMatrix() and, if
      // the Cam has perspective projection enabled, the central projection
      // as well — exactly the same pipeline used for the marking paths —
      // so the laser frames precisely the outline the user sees on canvas.
      bool persp = false;
      double h   = 0.0;
      QPointF vc;
      if (Cam* c = zcam->project() ? zcam->project()->cam() : nullptr) {
            persp = c->perspective();
            h     = c->projectionHeight();
            vc    = QPointF(c->viewCenter().x(), c->viewCenter().y());
            }
      Clipper2Lib::PathsD projected = projectPathListToXY(this, persp, h, vc);
      _worldContour.clear();
      if (!projected.empty())
            _worldContour = projected.front();
      (void)flags;
      }

//---------------------------------------------------------
//   hasHandles
//    The Framing rectangle is editable on the 3-D canvas exactly
//    like a Rectangle: four corner handles, all directly draggable.
//---------------------------------------------------------

bool Framing::isVertex(int idx) const {
      return isRectMode() && idx >= 0 && idx < 4;
      }

//---------------------------------------------------------
//   vertexPos
//    Local position of the idx-th corner handle.  The rectangle is
//    centered at the local origin, so the four corners are at
//    (±w/2, ±h/2):
//       0 = bottom-left  (-w/2, -h/2)
//       1 = bottom-right (+w/2, -h/2)
//       2 = top-right    (+w/2, +h/2)
//       3 = top-left     (-w/2, +h/2)
//---------------------------------------------------------

QVector3D Framing::vertexPos(int idx) const {
      if (!isRectMode() || idx < 0 || idx >= 4)
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
//    World position of the idx-th corner handle.
//---------------------------------------------------------

QVector3D Framing::vertexWorldPos(int idx) const {
      if (!isRectMode() || idx < 0 || idx >= 4)
            return {};
      QVector3D local = vertexPos(idx);
      return globalMatrix().map(local);
      }

//---------------------------------------------------------
//   setVertexPos
//    Move the idx-th corner to the given LOCAL position.  The
//    opposite corner stays fixed; size and pos are adjusted so the
//    rectangle is resized (and re-centred) rather than moved.
//
//    Corner pairs (opposite corners):  0 ↔ 2,  1 ↔ 3
//
//    Mirrors Rectangle::setVertexPos (lockSize is not enforced here).
//---------------------------------------------------------

void Framing::setVertexPos(int idx, const QVector3D& pos) {
      if (!isRectMode() || idx < 0 || idx >= 4)
            return;

      // Local position of the opposite (fixed) corner.
      int opp              = (idx + 2) % 4;
      QVector3D fixedLocal = vertexPos(opp);

      // Raw new size from the drag position.  The pos parameter is
      // already in the element's local (pre-scale) coordinate system
      // because ZCam::dragVertexTo() converts the world position via
      // globalMatrix().inverted().map().
      QVector3D newSizeLocal(pos.x() - fixedLocal.x(), pos.y() - fixedLocal.y(), 0.0);

      double newW = std::max((double)std::abs(newSizeLocal.x()), 1.0);
      double newH = std::max((double)std::abs(newSizeLocal.y()), 1.0);

      // Direction from the fixed corner toward the dragged corner,
      // determined by the corner index (not the mouse position).
      double dirX = (idx == 1 || idx == 2) ? 1.0 : -1.0;
      double dirY = (idx == 2 || idx == 3) ? 1.0 : -1.0;

      // New center so the opposite corner stays fixed.
      QVector3D newCenterLocal = fixedLocal + QVector3D(dirX * newW * .5, dirY * newH * .5, 0.0);

      // Convert the local center offset to world (parent) space using
      // the element's local matrix with the translation removed.
      QMatrix4x4 m             = matrix();
      m(0, 3)                  = 0;
      m(1, 3)                  = 0;
      m(2, 3)                  = 0;
      QVector3D worldOffset    = m.map(newCenterLocal);
      QVector3D newWorldCenter = this->pos() + worldOffset;

      // Set internal values directly, without emitting signals.
      _pos         = newWorldCenter;
      _size        = QVector2D(newW, newH);
      _matrixDirty = true;

      // Rebuild geometry (canvas + world contour) before QML learns
      // about the new position and size.
      update();

      // Now emit the change signals so QML picks up position and size
      // simultaneously with the already-updated geometry.
      beginBatchUpdate();
      _suppressUpdate = true;
      emit posChanged();
      emit sizeChanged();
      _suppressUpdate = false;
      endBatchUpdate(); // sets _batching=false and emits vertexRevisionChanged
      }
