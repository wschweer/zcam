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

#include "tessgeometry.h"
#include "tesselator.h"
#include "element3d.h"
#include "types.h"

//---------------------------------------------------------
//   minMax
//---------------------------------------------------------

static void minMax(const float* vert, int len, QVector3D& min, QVector3D& max) {
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
//   setPolygons
//    create Geometry for screen from _pathList
//---------------------------------------------------------

void TessGeometry::setPolygons(const PathList& _pathList) {
      clear();
      if (_pathList.empty())
            return;
      addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
      setStride(3 * sizeof(float));

      bool fill = _pathList.fill();
      if (fill) {
            setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
            addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);
            for (const auto& polygon : _pathList) {
                  std::vector<float> pg;
                  for (auto pp : polygon) {
                        pg.push_back(pp.x());
                        pg.push_back(pp.y());
                        pg.push_back(0.0);
                        }
                  if (fill && polygon.front() != polygon.back()) {
                        pg.push_back(polygon.front().x());
                        pg.push_back(polygon.front().y());
                        pg.push_back(0.0);
                        }
                  tess->addContour(3, pg.data(), sizeof(float) * 3, polygon.size());
                  }
            float normal[3]{0.0, 0.0, 1.0};
            if (!tess->tesselate(TESS_WINDING_NONZERO, TESS_POLYGONS, 3, 3, normal)) {
                  if (tess->status() != TESSstatus::TESS_STATUS_OK)
                        Debug("tesselate failed: {}", int(tess->status()));
                  }
            auto vertexCount  = tess->vertexCount();
            auto elementCount = tess->elementCount();
            auto vert         = tess->vertices();
            auto elements     = tess->elements();

            if (!vert) {
                  Debug("no vertices");
                  return;
                  }

            QByteArray vertices;
            QByteArray indices;

            vertices.resize(vertexCount * 3 * sizeof(float));
            indices.resize(elementCount * 3 * sizeof(quint32));

            memcpy(vertices.data(), vert, vertices.size());

            quint32* indexPtr = reinterpret_cast<quint32*>(indices.data());
            for (int i = 0; i < elementCount * 3; ++i)
                  indexPtr[i] = static_cast<quint32>(elements[i]);

            setVertexData(vertices);
            setIndexData(indices);

            QVector3D minBound;
            QVector3D maxBound;
            minMax(vertices, minBound, maxBound);
            setBounds(minBound, maxBound);
            }
      else {
            setPrimitiveType(QQuick3DGeometry::PrimitiveType::LineStrip);

            int vertexCount = 0;
            for (const auto& polygon : _pathList)
                  vertexCount += polygon.size();

            QByteArray vertices;
            vertices.resize(vertexCount * 3 * sizeof(float));
            float* data = reinterpret_cast<float*>(vertices.data());

            int offset = 0;
            //
            //  create a subset for every polygon
            //
            for (const auto& polygon : _pathList) {
                  float* vert = data;
                  for (auto vertex : polygon) {
                        *data++ = vertex.x();
                        *data++ = vertex.y();
                        *data++ = 0.0;
                        }
                  QVector3D min;
                  QVector3D max;
                  int len = polygon.size();
                  minMax(vert, len, min, max);
                  addSubset(offset, len, min, max);
                  offset += len;
                  }
            setVertexData(vertices);
            Clipper2Lib::RectD bounds = Clipper2Lib::GetBounds(_pathList.clipper());
            QVector3D minBound        = QVector3D(bounds.left, bounds.top, -1);
            QVector3D maxBound        = QVector3D(bounds.right, bounds.bottom, 1);
            setBounds(minBound, maxBound);
            }
      update();
      }

//---------------------------------------------------------
//   setLines
//---------------------------------------------------------

void TessGeometry::setLines(const Clipper2Lib::PathD& lines) {
      clear();
      if (lines.empty())
            return;
      addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
      setStride(3 * sizeof(float));

      setPrimitiveType(PrimitiveType::Lines);

      int vertexCount = lines.size();
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
//   setLines
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
            vertexCount += polygon.size();

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
            int len = polygon.size();
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
