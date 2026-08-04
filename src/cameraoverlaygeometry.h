//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#pragma once

#include <QtQuick3D/qquick3dgeometry.h>
#include <QtQml/qqmlregistration.h>

//---------------------------------------------------------
//   CameraOverlayGeometry
//    A unit quad (1×1, centred at the origin, in the XY plane)
//    whose corners are transformed by trapezoid (keystone)
//    correction properties:
//        x' = x * (1 + trapezX * y_norm)
//        y' = y * (1 + trapezY * x_norm)
//    where y_norm / x_norm range from -1 (bottom/left) to +1 (top/right).
//    This produces a true trapezoid: the top edge becomes wider or
//    narrower than the bottom edge, and similarly for left/right.
//    trapezX / trapezY are clamped to -1 .. 1 by the owning
//    CameraElement.  The geometry is scaled to the physical
//    mm size through the model's scale — CameraElement::
//    overlaySize drives that scale from QML.
//
//    UVs span the full texture so the whole camera frame is
//    mapped onto the (possibly trapezoidal) quad.
//---------------------------------------------------------

class CameraOverlayGeometry : public QQuick3DGeometry
      {
      Q_OBJECT
      QML_ELEMENT

      Q_PROPERTY(float trapezX READ trapezX WRITE set_trapezX NOTIFY trapezXChanged)
      Q_PROPERTY(float trapezY READ trapezY WRITE set_trapezY NOTIFY trapezYChanged)

      float _trapezX {0.0f};
      float _trapezY {0.0f};

    public:
      explicit CameraOverlayGeometry(QQuick3DObject* parent = nullptr);
      float trapezX() const { return _trapezX; }
      float trapezY() const { return _trapezY; }
      void set_trapezX(float v);
      void set_trapezY(float v);

    signals:
      void trapezXChanged();
      void trapezYChanged();

    private:
      void rebuild();
      };
