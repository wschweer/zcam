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
#include <atomic>
#include "clipper2/clipper.h"
#include "logger.h"

class Tesselator;
class Element3d;
class PathList;

#include "geometryworker.h"  // for TessResult, LineResult definitions


//---------------------------------------------------------
//   TessGeometry
//    Renders polygon and line geometry for Qt Quick 3D.
//
//    Thread-safety:
//      CPU-intensive tesselation and vertex conversion are offloaded
//      to GeometryWorker background threads.  The QQuick3DGeometry
//      API (setVertexData, setIndexData, update, etc.) is only called
//      on the main thread from the async callback.
//
//      A revision counter prevents stale results from overwriting
//      newer geometry: each request increments m_revision, and the
//      callback checks whether it matches the current value before
//      applying results.
//---------------------------------------------------------

class TessGeometry : public QQuick3DGeometry
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("")

      Q_PROPERTY(Element3d* element READ element NOTIFY elementChanged)

      Tesselator* tess{nullptr};
      Element3d* _element{nullptr};

      // Revision counter for async result validation.
      std::atomic<int> m_revision{0};

    signals:
      void elementChanged();

    private:
      void applyTessResult(const GeometryWorker::TessResult& r);
      void applyLineResult(const GeometryWorker::LineResult& r);

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
