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

#include "line.h"
#include "zcam.h"
#include "tessgeometry.h"
#include "types.h"

//---------------------------------------------------------
//   Line
//---------------------------------------------------------

Line::Line(ZCam* w, Element3d* parent) : Element3d(w, parent) {

      _geometry = new TessGeometry(this);
      QJSEngine::setObjectOwnership(_geometry, QJSEngine::CppOwnership);

      update();
      }

Line::~Line() {
      delete _geometry;
      }

//---------------------------------------------------------
//   makeSpline
//---------------------------------------------------------

int Line::makeSpline(int idx) {
      painterPath.makeSpline(idx);
      idx += 2;
      return idx;
      }

//---------------------------------------------------------
//   update
//---------------------------------------------------------

void Line::update(int flags) {
      _pathList = createPath();

      if (!_subElements.empty()) {
            int n = painterPath.size();
            if (painterPath.isClosed())
                  n -= 1;
            }
//      strokeAndFill();
      }

//---------------------------------------------------------
//   addVertex
//---------------------------------------------------------

void Line::addVertex(const Vec2d& p) {
      currentVertex = painterPath.size();
      if (painterPath.empty())
            painterPath.moveTo(p);
      else
            painterPath.lineTo(p);
      }

//---------------------------------------------------------
//   canClose
//    check if p is close to the starting point of the polygon
//---------------------------------------------------------

bool Line::canClose(const Vec2d& ep) const {
      const Vec2d& sp = startPos();
      return (std::abs(sp.x() - ep.x()) < 0.1) && (std::abs(sp.y() - ep.y()) < 0.1);
      }

//---------------------------------------------------------
//   closePath
//---------------------------------------------------------

void Line::closePath() {
      painterPath.closeSubpath();
      }
