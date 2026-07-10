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

#include "truncatedcubegeometry.h"

#include <QByteArray>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <algorithm>
namespace {
// ── Vertex layout (32 bytes) ──────────────────────────────────
//   offset  0 :  position  (3 × float32)
//   offset 12 :  normal    (3 × float32)
//   offset 24 :  uv        (2 × float32)
//
struct Vertex {
      float pos[3];
      float normal[3];
      float uv[2];
      };

static_assert(sizeof(Vertex) == 32);
// ── Octagon vertices in 2-D (u, v) going CCW ──────────────────
//   Produced by cutting distance t from each corner of a
//   square spanning ±h.
//
struct OctCoord {
      float u, v;
      };

void buildOctagonVertices(float h, float t, OctCoord out[8]) {
      out[0] = {h - t, h};
      out[1] = {-h + t, h};
      out[2] = {-h, h - t};
      out[3] = {-h, -h + t};
      out[4] = {-h + t, -h};
      out[5] = {h - t, -h};
      out[6] = {h, -h + t};
      out[7] = {h, h - t};
      }

// ── Face definition: normal + in-plane axes + name ────────────
//   u × v must equal the outward normal (right-handed).
//   u maps to texture right, v maps to texture up.
//
struct FaceDef {
      QVector3D normal;
      QVector3D u;
      QVector3D v;
      const char* name;
      };

// The six axis-aligned main faces of the cube.
// After truncation each becomes a regular octagon.
const FaceDef kFaces[6] = {
   // top    n=+Z
         { {0, 0, 1},  {1, 0, 0}, {0, 1, 0},    "top"},
   // bottom n=-Z
         {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, "bottom"},
   // front  n=-Y
         { {0, -1, 0}, {1, 0, 0},  {0, 0, 1},  "front"},
   // rear   n=+Y
         {{0, 1, 0}, {-1, 0, 0},  {0, 0, 1},   "rear"},
   // right  n=+X
         { {1, 0, 0}, {0, 1, 0},  {0, 0, 1},  "right"},
   // left   n=-X
         {{-1, 0, 0},  {0, -1, 0},  {0, 0, 1},   "left"},
      };

// ── Corner definition: signs + name ───────────────────────────
// Each corner at (sx*h, sy*h, sz*h) is cut by a plane
// perpendicular to (sx, sy, sz).
//
struct CornerDef {
      float sx, sy, sz;
      const char* name;
      };

const CornerDef kCorners[8] = {
         { 1, -1,  1, "tfr"},
         {-1, -1,  1, "tfl"},
         { 1,  1,  1, "trr"},
         {-1,  1,  1, "trl"},
         { 1, -1, -1, "bfr"},
         {-1, -1, -1, "bfl"},
         { 1,  1, -1, "brr"},
         {-1,  1, -1, "brl"},
      };

      } // namespace

//-----------------------------------------------------------------------------
//  Construction
//-----------------------------------------------------------------------------
TruncatedCubeGeometry::TruncatedCubeGeometry(QQuick3DObject* parent) : QQuick3DGeometry(parent) {
      updateGeometry();
      }

//-----------------------------------------------------------------------------
void TruncatedCubeGeometry::setHalfSize(float h) {
      h = std::max(h, 1.0f);
      if (h == m_halfSize)
            return;
      m_halfSize = h;
      emit halfSizeChanged();
      updateGeometry();
      }

//-----------------------------------------------------------------------------
void TruncatedCubeGeometry::setTruncation(float t) {
      // Clamp to valid range (0, halfSize)
      t = std::clamp(t, 0.0f, m_halfSize * 0.49f);
      if (t == m_truncation)
            return;
      m_truncation = t;
      emit truncationChanged();
      updateGeometry();
      }

//-----------------------------------------------------------------------------
//  updateGeometry — rebuild the entire vertex / index / subset data
//-----------------------------------------------------------------------------
void TruncatedCubeGeometry::updateGeometry() {
      const float h = m_halfSize;
      const float t = m_truncation;

      QVector<Vertex> vertices;
      QVector<quint32> indices;
      vertices.reserve(72);
      indices.reserve(132);

      // Helper: append a vertex, return its index
      auto addVertex = [&](const QVector3D& pos, const QVector3D& nrm, const QVector2D& uv) -> quint32 {
            Vertex v {};
            v.pos[0]    = pos.x();
            v.pos[1]    = pos.y();
            v.pos[2]    = pos.z();
            v.normal[0] = nrm.x();
            v.normal[1] = nrm.y();
            v.normal[2] = nrm.z();
            v.uv[0]     = uv.x();
            v.uv[1]     = uv.y();
            vertices.append(v);
            return static_cast<quint32>(vertices.size() - 1);
            };

      // Helper: append a triangle
      auto addTri = [&](quint32 a, quint32 b, quint32 c) {
            indices.append(a);
            indices.append(b);
            indices.append(c);
            };
      // ── Subset bookkeeping ────────────────────────────────────────
      struct Subset {
            int indexOffset;
            int indexCount;
            QVector3D bMin;
            QVector3D bMax;
            QString name;
            };
      QVector<Subset> subsets;
      subsets.reserve(14);

      // ── 6 octagonal main faces ────────────────────────────────────
      OctCoord oct[8];
      buildOctagonVertices(h, t, oct);

      for (const FaceDef& f : kFaces) {
            int idxStart     = static_cast<int>(indices.size());
            QVector3D center = f.normal * h;

            // Generate 8 octagon vertices in 3-D
            quint32 vidx[8];
            for (int i = 0; i < 8; ++i) {
                  QVector3D pos = center + f.u * oct[i].u + f.v * oct[i].v;
                  QVector2D uv((oct[i].u + h) / (2.0f * h), (oct[i].v + h) / (2.0f * h));
                  vidx[i] = addVertex(pos, f.normal, uv);
                  }

            // Triangle fan from vertex 0 → 6 triangles
            for (int i = 1; i < 7; ++i)
                  addTri(vidx[0], vidx[i], vidx[i + 1]);

            // Bounds
            QVector3D bMin(1e9f, 1e9f, 1e9f);
            QVector3D bMax(-1e9f, -1e9f, -1e9f);
            for (int i = 0; i < 8; ++i) {
                  QVector3D p = center + f.u * oct[i].u + f.v * oct[i].v;
                  bMin.setX(std::min(bMin.x(), p.x()));
                  bMin.setY(std::min(bMin.y(), p.y()));
                  bMin.setZ(std::min(bMin.z(), p.z()));
                  bMax.setX(std::max(bMax.x(), p.x()));
                  bMax.setY(std::max(bMax.y(), p.y()));
                  bMax.setZ(std::max(bMax.z(), p.z()));
                  }

            subsets.append({idxStart, static_cast<int>(indices.size()) - idxStart, bMin, bMax,
                            QString::fromLatin1(f.name)});
            }

      // ── 8 triangular corner faces ─────────────────────────────────
      for (const CornerDef& c : kCorners) {
            int idxStart = static_cast<int>(indices.size());

            // Three cut points where the truncation plane intersects
            // the three cube edges meeting at this corner.
            QVector3D v0(c.sx * (h - t), c.sy * h, c.sz * h);
            QVector3D v1(c.sx * h, c.sy * (h - t), c.sz * h);
            QVector3D v2(c.sx * h, c.sy * h, c.sz * (h - t));

            QVector3D nrm(c.sx, c.sy, c.sz);
            nrm.normalize();

            // Ensure CCW winding when viewed from outside.
            // When sx*sy*sz < 0 the default winding is inverted.
            if (c.sx * c.sy * c.sz < 0)
                  std::swap(v1, v2);

            quint32 i0 = addVertex(v0, nrm, {0.5f, 0.5f});
            quint32 i1 = addVertex(v1, nrm, {0.5f, 0.5f});
            quint32 i2 = addVertex(v2, nrm, {0.5f, 0.5f});
            addTri(i0, i1, i2);

            QVector3D bMin(std::min({v0.x(), v1.x(), v2.x()}), std::min({v0.y(), v1.y(), v2.y()}),
                           std::min({v0.z(), v1.z(), v2.z()}));
            QVector3D bMax(std::max({v0.x(), v1.x(), v2.x()}), std::max({v0.y(), v1.y(), v2.y()}),
                           std::max({v0.z(), v1.z(), v2.z()}));

            subsets.append({idxStart, 3, bMin, bMax, QString::fromLatin1(c.name)});
            }

      // ── Upload to QQuick3DGeometry ────────────────────────────────
      clear();
      setStride(sizeof(Vertex));
      setPrimitiveType(PrimitiveType::Triangles);
      setBounds(QVector3D(-h, -h, -h), QVector3D(h, h, h));

      // Vertex buffer
      QByteArray vbuf(reinterpret_cast<const char*>(vertices.constData()),
                      static_cast<int>(vertices.size() * sizeof(Vertex)));
      setVertexData(vbuf);

      // Index buffer (uint32)
      QByteArray ibuf(reinterpret_cast<const char*>(indices.constData()),
                      static_cast<int>(indices.size() * sizeof(quint32)));
      setIndexData(ibuf);

      // Vertex attributes
      addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
      addAttribute(Attribute::NormalSemantic, 12, Attribute::F32Type);
      addAttribute(Attribute::TexCoordSemantic, 24, Attribute::F32Type);
      addAttribute(Attribute::IndexSemantic, 0, Attribute::U32Type);

      // Subsets (one per face → per-face materials in QML)
      for (const Subset& s : subsets)
            addSubset(s.indexOffset, s.indexCount, s.bMin, s.bMax, s.name);

      emit geometryNodeDirty();
      }