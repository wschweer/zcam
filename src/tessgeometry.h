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

#include <QQuick3DGeometry>
#include <QQuick3DObject>
#include <QVector3D>
#include <QVector2D>
#include <QtQml/qqmlregistration.h>
#include "clipper2/clipper.h"
#include "logger.h"

class Tesselator;
class Element3d;
class PathList;

//---------------------------------------------------------
//   TessGeometry
//---------------------------------------------------------

class TessGeometry : public QQuick3DGeometry
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("")

      Q_PROPERTY(Element3d* element READ element NOTIFY elementChanged)

      Tesselator* tess{nullptr};
      Element3d* _element{nullptr};

    signals:
      void elementChanged();

    public:
      explicit TessGeometry(Element3d* e, QQuick3DObject* parent = nullptr);
      ~TessGeometry();

      void setPolygons(const PathList& _pathList);
      void setLines(const Clipper2Lib::PathD&);
      void setLines(const Clipper2Lib::PathsD&);
      void setElement(Element3d* e) {
            if (e != _element) {
                  _element = e;
                  emit elementChanged();
                  }
            }
      Element3d* element() const { return _element; }
      };
