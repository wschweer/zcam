//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "tessgeometry.h"
#include "tesselator.h"
#include "element3d.h"
#include "types.h"
#include "geometryworker.h"

#include <QPointer>

//---------------------------------------------------------
//   minMax (kept for synchronous fallback in applyTessResult)
//---------------------------------------------------------

static void minMax(const float* vert, int len, QVector3D& min, QVector3D& max) {
      if (len <= 0) {
            min = QVector3D();
            max = QVector3D();
            return;
            }
      min = QVector3D(vert[0], vert[1], vert[2]);
      max = min;
      for (int i = 1; i < len; ++i) {
            min.setX(qMin(min.x(), vert[i * 3 + 0]));
            min.setY(qMin(min.y(), vert[i * 3 + 1]));
            min.setZ(qMin(min.z(), vert[i * 3 + 2]));

            max.setX(qMax(max.x(), vert[i * 3 + 0]));
            max.setY(qMax(max.y(), vert[i * 3 + 1]));
            max.setZ(qMax(max.z(), vert[i * 3 + 2]));
            }
      }

static void minMax(const QByteArray& vertices, QVector3D& min, QVector3D& max) {
      const float* vert = reinterpret_cast<const float*>(vertices.data());
      int len           = vertices.size() / (sizeof(float) * 3);
      minMax(vert, len, min, max);
      }

//---------------------------------------------------------
//   TessGeometry
//---------------------------------------------------------

TessGeometry::TessGeometry(Element3d* e, QQuick3DObject* parent) : QQuick3DGeometry(parent) {
      _element = e;
      tess     = new Tesselator();
      }

TessGeometry::~TessGeometry() {
      delete tess;
      }

//---------------------------------------------------------
//   applyTessResult
//    Apply tesselation result to QQuick3DGeometry on the main thread.
//---------------------------------------------------------

void TessGeometry::applyTessResult(const GeometryWorker::TessResult& r) {
      if (!r.valid) {
            Debug("tess result invalid");
            return;
            }
      setVertexData(r.vertices);
      setIndexData(r.indices);
      setBounds(r.minBound, r.maxBound);
      update();
      }

//---------------------------------------------------------
//   applyLineResult
//    Apply line-stripping result to QQuick3DGeometry on the main thread.
//---------------------------------------------------------

void TessGeometry::applyLineResult(const GeometryWorker::LineResult& r) {
      if (!r.valid)
            return;
      setVertexData(r.vertices);
      setBounds(r.minBound, r.maxBound);
      for (const auto& s : r.subsets)
            addSubset(s.offset, s.length, s.min, s.max);
      update();
      }

//---------------------------------------------------------
//   setPolygons
//    Create Geometry for screen from _pathList.
//    Filled polygons are tesselated in a background thread;
//    non-filled polygons have their vertex data built in a
//    background thread.  Results are applied on the main thread.
//---------------------------------------------------------

void TessGeometry::setPolygons(const PathList& _pathList) {
      clear();
      if (_pathList.empty()) {
            update();
            return;
            }
      addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
      setStride(3 * sizeof(float));

      bool fill = _pathList.fill();
      int rev = ++m_revision;

      if (fill) {
            setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
            addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);

            QPointer<TessGeometry> guard(this);
            GeometryWorker::instance().requestTesselation(_pathList,
                  [this, guard, rev](const GeometryWorker::TessResult& r) {
                        if (!guard || rev != m_revision.load())
                              return;
                        applyTessResult(r);
                        });
            }
      else {
            setPrimitiveType(QQuick3DGeometry::PrimitiveType::LineStrip);

            // Convert PathList to Clipper2 PathsD for background processing
            Clipper2Lib::PathsD paths = _pathList.clipper();

            QPointer<TessGeometry> guard(this);
            GeometryWorker::instance().requestLines(paths,
                  [this, guard, rev](const GeometryWorker::LineResult& r) {
                        if (!guard || rev != m_revision.load())
                              return;
                        applyLineResult(r);
                        });
            }
      }

//---------------------------------------------------------
//   setLines (single path)
//---------------------------------------------------------

void TessGeometry::setLines(const Clipper2Lib::PathD& lines) {
      clear();
      if (lines.empty())
            return;
      addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
      setStride(3 * sizeof(float));

      setPrimitiveType(PrimitiveType::Lines);

      int vertexCount = static_cast<int>(lines.size());
      QByteArray vertices;
      vertices.resize(vertexCount * 3 * sizeof(float));
      float* data = reinterpret_cast<float*>(vertices.data());

      float* p = data;
      for (const auto& vertex : lines) {
            *p++ = vertex.x;
            *p++ = vertex.y;
            *p++ = 0.0;
            }
      QVector3D min;
      QVector3D max;
      minMax(data, vertexCount, min, max);

      setVertexData(vertices);
      update();
      }

//---------------------------------------------------------
//   setLines (multiple paths with subsets)
//---------------------------------------------------------

void TessGeometry::setLines(const Clipper2Lib::PathsD& lines) {
      clear();
      if (lines.empty())
            return;
      addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
      setStride(3 * sizeof(float));

      setPrimitiveType(PrimitiveType::Lines);
      int vertexCount = 0;
      for (const auto& polygon : lines)
            vertexCount += static_cast<int>(polygon.size());
      if (vertexCount == 0)
            return;

      QByteArray vertices;
      vertices.resize(vertexCount * 3 * sizeof(float));
      float* data = reinterpret_cast<float*>(vertices.data());

      int offset = 0;
      //
      //  create a subset for every list
      //
      for (const auto& polygon : lines) {
            float* vert = data;
            for (auto vertex : polygon) {
                  *data++ = vertex.x;
                  *data++ = vertex.y;
                  *data++ = 0.0;
                  }
            QVector3D min;
            QVector3D max;
            int len = static_cast<int>(polygon.size());
            minMax(vert, len, min, max);
            addSubset(offset, len, min, max);
            offset += len;
            }
      setVertexData(vertices);
      Clipper2Lib::RectD bounds = Clipper2Lib::GetBounds(lines);
      QVector3D minBound        = QVector3D(bounds.left, bounds.top, -1);
      QVector3D maxBound        = QVector3D(bounds.right, bounds.bottom, 1);
      setBounds(minBound, maxBound);
      update();
      }