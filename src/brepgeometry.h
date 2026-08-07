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

#include <QString>
#include <QVector3D>
#include <QQuick3DGeometry>
#include <QtQml/qqmlregistration.h>
#include <nlohmann/json.hpp>

class ZCam;
class Element;
class PathList;

//---------------------------------------------------------
//   BrepGeometry
//    A QQuick3DGeometry that loads a native OpenCASCADE
//    ".brep" file, tessellates its faces into triangles
//    and uploads the mesh to the GPU.
//---------------------------------------------------------

class BrepGeometry : public QQuick3DGeometry
      {
      Q_OBJECT
      QML_ELEMENT

      Q_PROPERTY(QString filePath READ filePath WRITE setFilePath NOTIFY filePathChanged)
      Q_PROPERTY(bool hasEdgeData READ hasEdgeData NOTIFY hasEdgeDataChanged)

      QString _filePath;
      bool _loaded;
      bool _hasEdgeData { false };
      QByteArray _edgeVertexData;
      QByteArray _edgeIndexData;
      QVector3D _bMin;
      QVector3D _bMax;

      void extractVisibleEdges(const TopoDS_Shape& shape);

    signals:
      void filePathChanged();
      void hasEdgeDataChanged();

    public:
      explicit BrepGeometry(QQuick3DObject* parent = nullptr);
      QString filePath() const { return _filePath; }
      void setFilePath(const QString& path);
      /// Re-tessellate the shape and re-upload the GPU data even when
      /// the file path has not changed yet (needed after a project
      /// load, where the path is restored before the geometry exists).
      void updateGeometry();
      bool loaded() const { return _loaded; }
      bool hasEdgeData() const { return _hasEdgeData; }
      QByteArray edgeVertexData() const { return _edgeVertexData; }
      QByteArray edgeIndexData() const { return _edgeIndexData; }
      QVector3D boundsMin() const { return _bMin; }
      QVector3D boundsMax() const { return _bMax; }
      static int edgeVertexStride();
      };
