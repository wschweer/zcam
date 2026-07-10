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

#pragma once

#include <QtQuick3D/qquick3dgeometry.h>

//-----------------------------------------------------------------------------
//  TruncatedCubeGeometry
//    A QQuick3DGeometry that generates a truncated cube (cube with all 8
//    corners cut off by triangular faces).
//
//    The resulting mesh has:
//      • 6 octagonal main faces (the original cube faces with corners removed)
//      • 8 triangular corner faces (the cut surfaces)
//
//    Each face is a separate subset so that per-face materials can be
//    assigned in QML.  Subset names match the NavigationCube face names:
//      "top", "bottom", "front", "rear", "left", "right",
//      "tfr", "tfl", "trr", "trl", "bfr", "bfl", "brr", "brl"
//
//    Vertex layout (stride = 32 bytes):
//      offset  0:  position  (3 × float32)
//      offset 12:  normal    (3 × float32)
//      offset 24:  uv        (2 × float32)
//
//    Index type: uint32
//-----------------------------------------------------------------------------

class TruncatedCubeGeometry : public QQuick3DGeometry
      {
      Q_OBJECT
      QML_ELEMENT

      Q_PROPERTY(float halfSize READ halfSize WRITE setHalfSize NOTIFY halfSizeChanged)
      Q_PROPERTY(float truncation READ truncation WRITE setTruncation NOTIFY truncationChanged)

    public:
      explicit TruncatedCubeGeometry(QQuick3DObject* parent = nullptr);
      float halfSize() const { return m_halfSize; }
      void setHalfSize(float h);
      float truncation() const { return m_truncation; }
      void setTruncation(float t);

    signals:
      void halfSizeChanged();
      void truncationChanged();

    private:
      void updateGeometry();

      float m_halfSize {50.0f};
      float m_truncation {18.0f};
      };