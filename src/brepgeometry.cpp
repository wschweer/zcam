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

#include "brepgeometry.h"

#include <QtMath>

#include <cmath>
#include <QtMath>

#include <QVector>
#include <QVector3D>
#include <algorithm>

#include <Poly_Array1OfTriangle.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColgp_Array1OfDir.hxx>
#include <Geom_Curve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Poly_Polygon3D.hxx>
#include <TopoDS_Edge.hxx>
#include <map>
#include <set>

namespace {
//---------------------------------------------------------
//   BrepVertex
//---------------------------------------------------------

struct BrepVertex {
      float pos[3];
      float normal[3];
      float uv[2];
      };

//---------------------------------------------------------
//   toVector3D
//---------------------------------------------------------

QVector3D toVector3D(const gp_Pnt& p, const TopLoc_Location& loc) {
      gp_Pnt tp = p;
      if (!loc.IsIdentity())
            tp = tp.Transformed(loc.Transformation());
      return QVector3D(static_cast<float>(tp.X()), static_cast<float>(tp.Y()),
                       static_cast<float>(tp.Z()));
      }

//---------------------------------------------------------
//   toNormal
//---------------------------------------------------------

QVector3D toNormal(const gp_Dir& d, const TopLoc_Location& loc) {
      gp_Dir td = d;
      if (!loc.IsIdentity())
            td = td.Transformed(loc.Transformation());
      QVector3D n(static_cast<float>(td.X()), static_cast<float>(td.Y()),
                  static_cast<float>(td.Z()));
      n.normalize();
      return n;
      }

//---------------------------------------------------------
//   loadShapeFromFile
//---------------------------------------------------------

bool loadShapeFromFile(const QString& path, TopoDS_Shape& shape) {
      if (path.isEmpty())
            return false;
      OCC_CATCH_SIGNALS
      try {
            BRep_Builder builder;
            return BRepTools::Read(shape, path.toUtf8().constData(), builder) == Standard_True;
            }
      catch (const Standard_Failure& f) {
            return false;
            }
      }

//---------------------------------------------------------
//   tessellateShapeToMesh
//---------------------------------------------------------

bool tessellateShapeToMesh(const TopoDS_Shape& shape, QByteArray& vbuf, QByteArray& ibuf,
                           QVector3D& bMin, QVector3D& bMax) {
      OCC_CATCH_SIGNALS
      try {
            TopoDS_Shape s = shape;
            BRepMesh_IncrementalMesh mesher(s, 0.1, Standard_False, 0.5, Standard_True);
            QVector<BrepVertex> verts;
            QVector<quint32> idxs;

            for (TopExp_Explorer fExpl(s, TopAbs_FACE); fExpl.More(); fExpl.Next()) {
                  const TopoDS_Face& face = TopoDS::Face(fExpl.Current());
                  TopLoc_Location loc;
                  Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
                  if (tri.IsNull() || tri->NbTriangles() < 1)
                        continue;

                  const int nbNodes = tri->NbNodes();
                  const int nbTris  = tri->NbTriangles();

                  const int base = verts.size();
                  for (int i = 1; i <= nbNodes; ++i) {
                        BrepVertex v{};
                        const gp_Pnt& p = tri->Node(i);
                        QVector3D pos  = toVector3D(p, loc);
                        v.pos[0] = pos.x(); v.pos[1] = pos.y(); v.pos[2] = pos.z();
                        // Face-triangle normals are filled in below;
                        // leave zero here so missing normals still get
                        // a valid fallback (see the triangle loop).
                        if (tri->HasNormals()) {
                              const gp_Dir& d = tri->Normal(i);
                              QVector3D n = toNormal(d, loc);
                              v.normal[0] = n.x(); v.normal[1] = n.y(); v.normal[2] = n.z();
                              }
                        if (tri->HasUVNodes()) {
                              const gp_Pnt2d& uv = tri->UVNode(i);
                              v.uv[0] = float(uv.X()); v.uv[1] = float(uv.Y());
                              }
                        verts.append(v);
                        }
                  for (int i = 1; i <= nbTris; ++i) {
                        Standard_Integer a, b, c;
                        tri->Triangle(i).Get(a, b, c);
                        if (face.Orientation() == TopAbs_REVERSED)
                              std::swap(b, c);
                        idxs.append(quint32(base) + quint32(a - 1));
                        idxs.append(quint32(base) + quint32(b - 1));
                        idxs.append(quint32(base) + quint32(c - 1));

                        // If the mesh has no node normals we must
                        // compute a flat (face) normal so principled
                        // fragment lighting can actually shade the
                        // surface.  With no normals the lighting
                        // equations just return black.
                        if (!tri->HasNormals()) {
                              const QVector3D p0 = toVector3D(tri->Node(a), loc);
                              const QVector3D p1 = toVector3D(tri->Node(b), loc);
                              const QVector3D p2 = toVector3D(tri->Node(c), loc);
                              QVector3D nrm = QVector3D::crossProduct(p1 - p0,
                                                                             p2 - p0);
                              if (nrm.lengthSquared() > 1e-12f)
                                    nrm.normalize();
                              const int i0 = base + a - 1;
                              const int i1 = base + b - 1;
                              const int i2 = base + c - 1;
                              verts[i0].normal[0] = verts[i1].normal[0] =
                                       verts[i2].normal[0] = nrm.x();
                              verts[i0].normal[1] = verts[i1].normal[1] =
                                       verts[i2].normal[1] = nrm.y();
                              verts[i0].normal[2] = verts[i1].normal[2] =
                                       verts[i2].normal[2] = nrm.z();
                              }
                        }
                  }
            if (verts.isEmpty() || idxs.isEmpty())
                  return false;

            bMin = QVector3D(verts[0].pos[0], verts[0].pos[1], verts[0].pos[2]);
            bMax = bMin;
            for (const BrepVertex& v : verts) {
                  QVector3D p(v.pos[0], v.pos[1], v.pos[2]);
                  bMin.setX(std::min(bMin.x(), p.x()));
                  bMin.setY(std::min(bMin.y(), p.y()));
                  bMin.setZ(std::min(bMin.z(), p.z()));
                  bMax.setX(std::max(bMax.x(), p.x()));
                  bMax.setY(std::max(bMax.y(), p.y()));
                  bMax.setZ(std::max(bMax.z(), p.z()));
                  }
            vbuf = QByteArray(reinterpret_cast<const char*>(verts.constData()),
                              int(verts.size() * sizeof(BrepVertex)));
            ibuf = QByteArray(reinterpret_cast<const char*>(idxs.constData()),
                              int(idxs.size() * sizeof(quint32)));
            return true;
            }
      catch (const Standard_Failure&) {
            return false;
            }
      }
} // namespace

//---------------------------------------------------------
//---------------------------------------------------------
//   BrepGeometry
//---------------------------------------------------------
//---------------------------------------------------------

BrepGeometry::BrepGeometry(QQuick3DObject* parent)
    : QQuick3DGeometry(parent), _loaded(false) {
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   setFilePath
//---------------------------------------------------------
//---------------------------------------------------------

void BrepGeometry::setFilePath(const QString& path) {
      if (_filePath == path)
            return;
      _filePath = path;
      emit filePathChanged();
      updateGeometry();
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   updateGeometry
//---------------------------------------------------------
//---------------------------------------------------------

void BrepGeometry::updateGeometry() {
      _loaded = false;
      _hasEdgeData = false;
      clear();

      TopoDS_Shape shape;
      QByteArray vbuf, ibuf;
      if (loadShapeFromFile(_filePath, shape) &&
          tessellateShapeToMesh(shape, vbuf, ibuf, _bMin, _bMax)) {
            setStride(sizeof(BrepVertex));
            setPrimitiveType(PrimitiveType::Triangles);
            setVertexData(vbuf);
            setIndexData(ibuf);
            setBounds(_bMin, _bMax);
            addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
            addAttribute(Attribute::NormalSemantic, 12, Attribute::F32Type);
            addAttribute(Attribute::TexCoordSemantic, 24, Attribute::F32Type);
            addAttribute(Attribute::IndexSemantic, 0, Attribute::U32Type);
            _loaded = true;

            // Extract visible edges as black lines (HLR on the
            // tessellated mesh – fast and sufficient for display).
            extractVisibleEdges(shape);
            }
      else {
            // fallback: upload nothing so the shape is invisible
            setVertexData(QByteArray());
            setIndexData(QByteArray());
            _edgeVertexData.clear();
            _edgeIndexData.clear();
            }
      update();
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   extractVisibleEdges  (private helper)
//
//   Extracts feature edges from the tessellated mesh:
//   an edge is visible if the dihedral angle between the
//   normals of the two adjacent triangles exceeds a
//   threshold.  This is robust, fast, and exactly what
//   CAD viewers display as "hard edges" on imported BReps.
//---------------------------------------------------------
//---------------------------------------------------------

void BrepGeometry::extractVisibleEdges(const TopoDS_Shape& shape) {
      _edgeVertexData.clear();
      _edgeIndexData.clear();
      _hasEdgeData = false;
      if (shape.IsNull())
            return;

      OCC_CATCH_SIGNALS
      try {
            TopoDS_Shape s = shape;
            BRepMesh_IncrementalMesh mesher(s, 0.1, Standard_False, 0.5, Standard_True);

            // Collect all triangles of all faces as (v0, v1, v2)
            // world-space coordinates, keyed by a local face index.
            struct Triangle {
                  QVector3D p[3];
                  int face;
                  };
            QVector<Triangle> triangles;
            for (TopExp_Explorer fExpl(s, TopAbs_FACE); fExpl.More(); fExpl.Next()) {
                  const TopoDS_Face& face = TopoDS::Face(fExpl.Current());
                  TopLoc_Location loc;
                  Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
                  if (tri.IsNull() || tri->NbTriangles() < 1)
                        continue;
                  const int faceIdx = triangles.isEmpty() ? 0 : (triangles.last().face + 1);
                  for (int i = 1; i <= tri->NbTriangles(); ++i) {
                        Standard_Integer a, b, c;
                        tri->Triangle(i).Get(a, b, c);
                        if (face.Orientation() == TopAbs_REVERSED)
                              std::swap(b, c);
                        Triangle t;
                        t.p[0] = toVector3D(tri->Node(a), loc);
                        t.p[1] = toVector3D(tri->Node(b), loc);
                        t.p[2] = toVector3D(tri->Node(c), loc);
                        t.face = faceIdx;
                        triangles.append(t);
                        }
                  }

            const int nTri = triangles.size();
            if (nTri < 2)
                  return;

            // Build a map of "undirected edge" → list of incident
            // triangles so we can test the dihedral angle.
            struct EdgeKey {
                  QVector3D a;
                  QVector3D b;
                  bool operator<(const EdgeKey& o) const {
                        if (a.x() != o.a.x()) return a.x() < o.a.x();
                        if (a.y() != o.a.y()) return a.y() < o.a.y();
                        if (a.z() != o.a.z()) return a.z() < o.a.z();
                        if (b.x() != o.b.x()) return b.x() < o.b.x();
                        if (b.y() != o.b.y()) return b.y() < o.b.y();
                        return b.z() < o.b.z();
                        }
                  };
            auto makeEdge = [](const QVector3D& p, const QVector3D& q) {
                  return (p.x() < q.x() ||
                          (p.x() == q.x() && (p.y() < q.y() ||
                                              (p.y() == q.y() && p.z() < q.z()))))
                             ? EdgeKey{p, q} : EdgeKey{q, p};
                  };
            std::map<EdgeKey, QVector<int>> edgeMap;
            for (int i = 0; i < nTri; ++i) {
                  const Triangle& t = triangles[i];
                  edgeMap[makeEdge(t.p[0], t.p[1])].append(i);
                  edgeMap[makeEdge(t.p[1], t.p[2])].append(i);
                  edgeMap[makeEdge(t.p[2], t.p[0])].append(i);
                  }

            // Compute triangle normals once
            QVector<QVector3D> normals(nTri);
            for (int i = 0; i < nTri; ++i) {
                  const Triangle& t = triangles[i];
                  QVector3D e1 = t.p[1] - t.p[0];
                  QVector3D e2 = t.p[2] - t.p[0];
                  QVector3D n = QVector3D::crossProduct(e1, e2);
                  if (n.lengthSquared() > 1e-12f)
                        n.normalize();
                  normals[i] = n;
                  }

            // Feature-edge threshold: dihedral angle > ~30 degrees
            // (cos(30°) ≈ 0.866).  Boundary edges (single incident
            // triangle) are always visible.
            const float kAngleThreshold = 0.866f;
            QVector<QVector3D> points;
            for (const auto& [key, triList] : edgeMap) {
                  if (triList.size() == 1) {
                        // boundary edge
                        points.append(key.a);
                        points.append(key.b);
                        continue;
                        }
                  // only consider edge between two different faces
                  bool sameFace = true;
                  for (int i = 1; i < triList.size(); ++i)
                          if (triangles[triList[i]].face != triangles[triList[0]].face)
                                sameFace = false;
                  if (sameFace)
                        continue;

                  // visible if any pair of adjacent triangles on this
                  // edge have a sharp dihedral angle
                  bool sharp = false;
                  for (int i = 0; i < triList.size(); ++i) {
                        for (int j = i + 1; j < triList.size(); ++j) {
                              const float cosAng = QVector3D::dotProduct(normals[triList[i]],
                                                                         normals[triList[j]]);
                              if (cosAng < kAngleThreshold) {
                                    sharp = true;
                                    break;
                                    }
                              }
                        if (sharp)
                              break;
                        }
                  if (sharp) {
                        points.append(key.a);
                        points.append(key.b);
                        }
                  }

            const int nVertices = points.size();
            if (nVertices < 2)
                  return;

            // Build the vertex / index buffers (same layout as the
            // solid so the same PrincipledMaterial works; normals
            // and uvs are unused for lines).
            QVector<BrepVertex> verts(nVertices);
            for (int i = 0; i < nVertices; ++i) {
                  BrepVertex v{};
                  v.pos[0]    = points[i].x();
                  v.pos[1]    = points[i].y();
                  v.pos[2]    = points[i].z();
                  v.normal[0] = v.normal[1] = v.normal[2] = 0.0f;
                  v.uv[0]     = v.uv[1] = 0.0f;
                  verts[i] = v;
                  }

            QVector<quint32> idx(nVertices);
            for (int i = 0; i < nVertices; ++i)
                  idx[i] = quint32(i);

            _edgeVertexData   = QByteArray(reinterpret_cast<const char*>(verts.constData()),
                                          int(verts.size() * sizeof(BrepVertex)));
            _edgeIndexData    = QByteArray(reinterpret_cast<const char*>(idx.constData()),
                                          int(idx.size() * sizeof(quint32)));
            _hasEdgeData      = true;
            emit hasEdgeDataChanged();
            }
      catch (const Standard_Failure&) {
            _edgeVertexData.clear();
            _edgeIndexData.clear();
            }
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   edgeVertexStride
//---------------------------------------------------------
//---------------------------------------------------------

int BrepGeometry::edgeVertexStride() {
      return sizeof(BrepVertex);
      }
