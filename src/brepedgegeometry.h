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

#include "brepocct.h"   // must come before Qt headers

#include <QVector3D>
#include <QQuick3DGeometry>
#include <QtQml/qqmlregistration.h>
#include <nlohmann/json.hpp>

#include "brepgeometry.h"

class ZCam;
class Element;
class PathList;

//---------------------------------------------------------
//   BrepEdgeGeometry
//    Renders the visible feature edges of a BREP shape as
//    black line segments (LineStrip) over the solid model.
//    The edge data is computed by the owning BrepGeometry
//    after tessellating/meshing the shape (HLR); this
//    geometry class only uploads the resulting vertex /
//    index data to the GPU.
//---------------------------------------------------------

class BrepEdgeGeometry : public QQuick3DGeometry
      {
      Q_OBJECT
      QML_ELEMENT

      Q_PROPERTY(BrepGeometry* source READ source WRITE setSource NOTIFY sourceChanged)

      BrepGeometry* _source { nullptr };

      void rebuild();

      signals:
      void sourceChanged();

      public:
      explicit BrepEdgeGeometry(QQuick3DObject* parent = nullptr);
      BrepGeometry* source() const { return _source; }
      void setSource(BrepGeometry* src);
      };
