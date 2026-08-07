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
#include "macros.h"

class Tesselator;
class Element3d;
class PathList;

#include "geometryworker.h" // for TessResult, LineResult definitions

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
      Q_PROPERTY(int geometryRevision READ geometryRevision NOTIFY geometryRevisionChanged)

      PROPV(Element3d*, element, nullptr)

      Tesselator* tess {nullptr};

      // Revision counter for async result validation.
      std::atomic<int> m_revision {0};

    signals:
      void geometryRevisionChanged();

    private:
      void applyTessResult(const GeometryWorker::TessResult& r);
      void applyLineResult(const GeometryWorker::LineResult& r);
      int _geometryRevision {0};

    public:
      explicit TessGeometry(Element3d* e, QQuick3DObject* parent = nullptr);
      ~TessGeometry();

      void setPolygons(const PathList& _pathList);
      void setLines(const Clipper2Lib::PathD&);
      void setLines(const Clipper2Lib::PathsD&);
      /// 3D wireframe edges (pairs of vertices connected as independent
      /// segments).  Used for the 3D selection box of volumetric
      /// elements (BREP).
      void setEdges3D(const std::vector<QVector3D>& p0,
                      const std::vector<QVector3D>& p1);
      /// Render line segments as thin quads (triangle pairs) instead of
      /// GPU line primitives.  GL_LINES with lineWidth=1 suffers from
      /// sub-pixel rasterization that makes some lines appear twice as
      /// thick depending on camera angle.  Quads rendered as triangles
      /// are not subject to this artefact.
      /// The quads lie flat in the XY plane (in-plane rot90 offset,
      /// all z == 0); the stroke width is pre-scaled by 1/|viewDir.z|
      /// to cancel the projection foreshortening at oblique angles.
      /// `spacing` is the distance between neighbouring lines and
      /// physically bounds the widening so the ribbons can never
      /// grow wider than their spacing — without this cap oblique
      /// views overlap the ribbons and the grid reads as a woven /
      /// hatched mess (see the zig-zag screenshot).
      void setLinesAsQuads(const Clipper2Lib::PathsD& lines, float halfWidth,
                           QVector3D viewDir = QVector3D(0, 0, 1), float spacing = 0.0f);

      /// Upload raw line segments as flat quad geometry for the
      /// SCREEN-SPACE EXPANSION shader (gridline.vert).  The quad is
      /// NOT widened in CPU space at all: width, axis and side are
      /// encoded per-vertex (stride = 5 floats):
      ///     xyz = end point coordinate (both corners of one end
      ///           share the same position)
      ///     uv.x = side (-1/+1)
      ///     uv.y = axis (0 = X line, 1 = Y line)
      /// The vertex shader displaces the corners in CLIP SPACE by a
      /// constant pixel amount perpendicular to the projected line
      /// direction, giving an exact, view-independent constant stroke.
      void setLinesForExpandedQuads(const Clipper2Lib::PathsD& lines);
      int geometryRevision() const { return _geometryRevision; }
      };
