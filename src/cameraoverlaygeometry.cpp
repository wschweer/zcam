//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "cameraoverlaygeometry.h"

#include <QVector3D>
#include <cfloat>

//---------------------------------------------------------
//   CameraOverlayGeometry
//---------------------------------------------------------

CameraOverlayGeometry::CameraOverlayGeometry(QQuick3DObject* parent) : QQuick3DGeometry(parent) {
      rebuild();
      }

void CameraOverlayGeometry::set_trapezX(float v) {
      v = std::clamp(v, -1.0f, 1.0f);
      if (v == _trapezX)
            return;
      _trapezX = v;
      emit trapezXChanged();
      rebuild();
      }

void CameraOverlayGeometry::set_trapezY(float v) {
      v = std::clamp(v, -1.0f, 1.0f);
      if (v == _trapezY)
            return;
      _trapezY = v;
      emit trapezYChanged();
      rebuild();
      }

//---------------------------------------------------------
//   rebuild
//    Rebuild the unit quad with trapezoidal (keystone) correction.
//    vertex layout: (x, y, z, nx, ny, nz, u, v) — 8 floats
//
//    A true trapezoid (keystone) correction narrows one edge while
//    widening the opposite edge.  The transformation is:
//        x' = x * (1 + trapezX * y_normalized)
//        y' = y * (1 + trapezY * x_normalized)
//    where y_normalized and x_normalized range from -1 (bottom/left)
//    to +1 (top/right).  This produces a trapezoid: the top edge
//    becomes wider (or narrower) than the bottom edge, and similarly
//    for the left/right edges.
//---------------------------------------------------------

void CameraOverlayGeometry::rebuild() {
const float tx = _trapezX;
const float ty = _trapezY;
// corner order: bottom-left, bottom-right, top-right, top-left
// (x, y) in [-0.5, 0.5]; normalized coords in [-1, 1]
      struct Corner {
      float x, y, u, v;
      float nx() const { return x * 2.0f; }  // normalized -1..1
      float ny() const { return y * 2.0f; }
            };
const Corner corners[4] = {
         {-0.5f, -0.5f, 0.0f, 1.0f},
               { 0.5f, -0.5f, 1.0f, 1.0f},
         { 0.5f,  0.5f, 1.0f, 0.0f},
   {-0.5f,  0.5f, 0.0f, 0.0f},
};
const int indices[6] = {0, 1, 2, 0, 2, 3};

QByteArray vertexData;
vertexData.resize(6 * 8 * sizeof(float));
auto* vd = reinterpret_cast<float*>(vertexData.data());

QVector3D boundsMin(FLT_MAX, FLT_MAX, 0.0f);
QVector3D boundsMax(-FLT_MAX, -FLT_MAX, 0.0f);

for (int i = 0; i < 6; ++i) {
const Corner& c = corners[indices[i]];
// Trapezoid: scale x by a factor that depends on y position.
// When trapezX > 0: top edge widens, bottom edge narrows.
// When trapezX < 0: top edge narrows, bottom edge widens.
const float xScale = 1.0f + tx * c.ny();
            const float yScale = 1.0f + ty * c.nx();
      const float x   = c.x * xScale;
      const float y   = c.y * yScale;
      const float z   = 0.0f;
      vd[i * 8 + 0]   = x;
      vd[i * 8 + 1]   = y;
      vd[i * 8 + 2]   = z;
      vd[i * 8 + 3]   = 0.0f; // normal +z
      vd[i * 8 + 4]   = 0.0f;
      vd[i * 8 + 5]   = 1.0f;
      vd[i * 8 + 6]   = c.u;
            vd[i * 8 + 7]   = c.v;
            boundsMin.setX(std::min(boundsMin.x(), x));
            boundsMin.setY(std::min(boundsMin.y(), y));
            boundsMax.setX(std::max(boundsMax.x(), x));
            boundsMax.setY(std::max(boundsMax.y(), y));
            }

      clear();
      setStride(8 * sizeof(float));
      setPrimitiveType(PrimitiveType::Triangles);
      addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
      addAttribute(Attribute::NormalSemantic, 3 * sizeof(float), Attribute::F32Type);
      addAttribute(Attribute::TexCoordSemantic, 6 * sizeof(float), Attribute::F32Type);
      setVertexData(vertexData);
      setBounds(boundsMin, boundsMax);
      update();
      }