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
#include <limits>
#include <cmath>
#include <algorithm>

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
      ++_geometryRevision;
      emit geometryRevisionChanged();
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
      ++_geometryRevision;
      emit geometryRevisionChanged();
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
            ++_geometryRevision;
            emit geometryRevisionChanged();
            update();
            return;
            }
      addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
      setStride(3 * sizeof(float));

      bool fill = _pathList.fill();
      int rev   = ++m_revision;

      if (fill) {
            setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
            addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);

            QPointer<TessGeometry> guard(this);
            GeometryWorker::instance().requestTesselation(
                _pathList, [this, guard, rev](const GeometryWorker::TessResult& r) {
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
//   setLinesAsQuads
//    Convert line segments into thin quads (triangle pairs) so they
//    render as filled triangles instead of GL_LINES primitives.
//    GPU line rasterization with lineWidth=1 is subject to sub-pixel
//    artefacts: depending on the exact pixel position of a line's
//    endpoints, the GPU may rasterize it 1 or 2 pixels wide, and
//    which lines are affected changes with camera rotation/zoom.
//    Rendering as triangles avoids this entirely because triangle
//    rasterization uses area-based coverage, not the diamond-exit
//    rule of GL_LINES.
//
//    The quads lie FLAT in the XY plane (all z == 0) and use the
//    in-plane perpendicular (-dy, dx, 0).  Keeping the stroke coplanar
//    with the z=0 scene geometry is essential:  the previous
//    cylinder-billboard variant lifted each quad out of the plane
//    toward the camera.  In oblique views those tilted ribbons fought
//    the z=0 geometry for depth (classic Z-fighting: the grid was
//    eaten in zig-zag stripes) and the dark major ribbons showed up
//    as fat vertical bars wherever the ribbon's edge faced the camera.
//
//    The correct on-screen width is recovered by SCALING the flat
//    stroke width instead of tilting the quad.  All grid lines are
//    axis-aligned (dir = ±e_x or ±e_y).  For a camera tilted by theta
//    from the plane normal the projected width of a flat X or Y line
//    foreshortens by |v.z| = cos(theta).  Pre-widening the quad by
//    1/|v.z| exactly cancels that, so the projected stroke keeps
//    2 * halfWidth * scale pixels at every elevation.  The factor is
//    capped at 60 (theta ≈ 89°):  beyond that the whole plane becomes
//    a hairline and a constant-width ribbon is more useful than an
//    infinitely wide one.
//---------------------------------------------------------

void TessGeometry::setLinesAsQuads(const Clipper2Lib::PathsD& lines, float halfWidth, QVector3D viewDir, float spacing) {
      clear();
      if (lines.empty()) {
            ++_geometryRevision;
            emit geometryRevisionChanged();
            update();
            return;
            }

      addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
      setStride(3 * sizeof(float));
      setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
      addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);

      // Normalise the view direction; a degenerate (zero) vector would
      // produce NaN vertices — fall back to the top-down direction.
      if (viewDir.length() < 1e-9f)
            viewDir = QVector3D(0, 0, 1);
      else
            viewDir.normalize();

      // Count segments and vertices
      int segmentCount = 0;
      for (const auto& path : lines)
            if (path.size() >= 2)
                  segmentCount += static_cast<int>(path.size()) - 1;
      if (segmentCount == 0) {
            ++_geometryRevision;
            emit geometryRevisionChanged();
            update();
            return;
            }

      int vertexCount = segmentCount * 4; // 4 verts per quad
      int indexCount  = segmentCount * 6; // 2 triangles per quad
      QByteArray vertices;
      vertices.resize(vertexCount * 3 * sizeof(float));
      QByteArray indices;
      indices.resize(indexCount * sizeof(uint32_t));

      float* vptr    = reinterpret_cast<float*>(vertices.data());
      uint32_t* iptr = reinterpret_cast<uint32_t*>(indices.data());
      uint32_t vidx  = 0;

      double minX = std::numeric_limits<double>::max();
      double minY = minX;
      double maxX = std::numeric_limits<double>::lowest();
      double maxY = maxX;
      double minZ = minX;
      double maxZ = maxX;

      for (const auto& path : lines) {
            for (size_t i = 0; i + 1 < path.size(); ++i) {
                  double x0 = path[i].x;
                  double y0 = path[i].y;
                  double x1 = path[i + 1].x;
                  double y1 = path[i + 1].y;

                  // Direction and length (in XY plane)
                  double dx  = x1 - x0;
                  double dy  = y1 - y0;
                  double len = std::sqrt(dx * dx + dy * dy);
                  if (len < 1e-12)
                        continue; // degenerate segment

                  // 3D line direction (normalized)
                  double dl = len; // segment length in XY
                  QVector3D dir(dx / dl, dy / dl, 0.0f);

                  // Flat in-plane perpendicular (rot90): the quad
                  // stays coplanar with the z=0 geometry so the depth
                  // offset of the grid model (1 mm behind the XY
                  // plane) always wins the depth test — no Z-fighting
                  // zig-zag with the scene, no fat bars when the
                  // camera grazes a tilted ribbon.
                  //
                  // Width compensation:  for the axis-aligned grid
                  // lines the projected width of a flat quad
                  // foreshortens by |v.z| = cos(tilt).  Scaling the
                  // half width by 1/|v.z| cancels that exactly, so
                  // the on-screen stroke stays at the requested
                  // pixel width for every camera elevation (the line
                  // LENGTH still foreshortens naturally).
                  // Two bounds keep the ribbon useful in extreme
                  // views:
                  //  · widen is capped at 60 (≈ 89° tilt) so an
                  //    edge-on view does not blow the quads into
                  //    infinite strips;
                  //  · the stroke is clamped slightly BELOW the line
                  //    SPACING (0.45 instead of 0.5) so neighbouring
                  //    ribbons always keep a thin gap — without this
                  //    the widening eats the gaps and the grid turns
                  //    into a woven hatched mess in oblique views
                  //    (see the zig-zag screenshot).
                  static constexpr double MAX_WIDEN = 60.0;
                  double zAbs    = std::abs(static_cast<double>(viewDir.z()));
                  double widen   = std::min(1.0 / std::max(zAbs, 1.0 / MAX_WIDEN), MAX_WIDEN);
                  double hwWorld = halfWidth * widen;
                  if (spacing > 1e-9f)
                        hwWorld = std::min(hwWorld, static_cast<double>(spacing) * 0.45);
                  QVector3D perp = QVector3D(static_cast<float>(-dir.y()), static_cast<float>(dir.x()), 0.0f)
                                   * static_cast<float>(hwWorld);

                  // Four corners of the flat quad (all z == 0)
                  // v0 = p0 + perp   v1 = p0 - perp
                  // v2 = p1 + perp   v3 = p1 - perp
                  float px = perp.x(), py = perp.y(), pz = 0.0f;
                  *vptr++ = static_cast<float>(x0 + px);
                  *vptr++ = static_cast<float>(y0 + py);
                  *vptr++ = pz;
                  *vptr++ = static_cast<float>(x0 - px);
                  *vptr++ = static_cast<float>(y0 - py);
                  *vptr++ = -pz;
                  *vptr++ = static_cast<float>(x1 + px);
                  *vptr++ = static_cast<float>(y1 + py);
                  *vptr++ = pz;
                  *vptr++ = static_cast<float>(x1 - px);
                  *vptr++ = static_cast<float>(y1 - py);
                  *vptr++ = -pz;

                  // Two triangles: (0,1,2) and (1,3,2)
                  *iptr++  = vidx + 0;
                  *iptr++  = vidx + 1;
                  *iptr++  = vidx + 2;
                  *iptr++  = vidx + 1;
                  *iptr++  = vidx + 3;
                  *iptr++  = vidx + 2;
                  vidx    += 4;

                  double c0x = x0 + px, c0y = y0 + py, c0z = pz;
                  double c1x = x0 - px, c1y = y0 - py, c1z = -pz;
                  double c2x = x1 + px, c2y = y1 + py, c2z = pz;
                  double c3x = x1 - px, c3y = y1 - py, c3z = -pz;
                  minX = std::min({minX, c0x, c1x, c2x, c3x});
                  minY = std::min({minY, c0y, c1y, c2y, c3y});
                  maxX = std::max({maxX, c0x, c1x, c2x, c3x});
                  maxY = std::max({maxY, c0y, c1y, c2y, c3y});
                  minZ = std::min({minZ, c0z, c1z, c2z, c3z});
                  maxZ = std::max({maxZ, c0z, c1z, c2z, c3z});
                  }
            }

      setVertexData(vertices);
      setIndexData(indices);
      setBounds(QVector3D(static_cast<float>(minX), static_cast<float>(minY), static_cast<float>(minZ)),
                QVector3D(static_cast<float>(maxX), static_cast<float>(maxY), static_cast<float>(maxZ)));
      ++_geometryRevision;
      emit geometryRevisionChanged();
      update();
      }

//---------------------------------------------------------
//   setLines (single path)
//---------------------------------------------------------

void TessGeometry::setLines(const Clipper2Lib::PathD& lines) {
      clear();
      if (lines.empty()) {
            ++_geometryRevision;
            emit geometryRevisionChanged();
            update();
            return;
            }
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
      ++_geometryRevision;
      emit geometryRevisionChanged();
      update();
      }

//---------------------------------------------------------
//   setLines (multiple paths with subsets)
//---------------------------------------------------------

void TessGeometry::setLines(const Clipper2Lib::PathsD& lines) {
      clear();
      if (lines.empty()) {
            ++_geometryRevision;
            emit geometryRevisionChanged();
            update();
            return;
            }
      addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
      setStride(3 * sizeof(float));

      setPrimitiveType(PrimitiveType::Lines);
      int vertexCount = 0;
      for (const auto& polygon : lines)
            vertexCount += static_cast<int>(polygon.size());
      if (vertexCount == 0) {
            ++_geometryRevision;
            emit geometryRevisionChanged();
            update();
            return;
            }

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
      ++_geometryRevision;
      emit geometryRevisionChanged();
      update();
      }

//---------------------------------------------------------
//   setEdges3D
//    Upload 3D line segments (independent pairs of vertices
//    connected as GL_LINES — e.g. the 12 edges of a selection
//    brick) for the element-selection overlay.
//---------------------------------------------------------

void TessGeometry::setEdges3D(const std::vector<QVector3D>& p0, const std::vector<QVector3D>& p1) {
      clear();
      if (p0.empty() || p0.size() != p1.size()) {
            ++_geometryRevision;
            emit geometryRevisionChanged();
            update();
            return;
            }
      addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
      setStride(3 * sizeof(float));
      setPrimitiveType(PrimitiveType::Lines);

      const int pairCount   = static_cast<int>(p0.size());
      const int vertexCount = pairCount * 2;
      QByteArray vertices;
      vertices.resize(vertexCount * 3 * sizeof(float));
      float* data = reinterpret_cast<float*>(vertices.data());

      QVector3D allMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max());
      QVector3D allMax(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest());
      for (int i = 0; i < pairCount; ++i) {
            const QVector3D& a = p0[i];
            const QVector3D& b = p1[i];
            *data++            = a.x();
            *data++            = a.y();
            *data++            = a.z();
            *data++            = b.x();
            *data++            = b.y();
            *data++            = b.z();
            for (const auto& v : { a, b }) {
                  allMin.setX(std::min(allMin.x(), v.x()));
                  allMin.setY(std::min(allMin.y(), v.y()));
                  allMin.setZ(std::min(allMin.z(), v.z()));
                  allMax.setX(std::max(allMax.x(), v.x()));
                  allMax.setY(std::max(allMax.y(), v.y()));
                  allMax.setZ(std::max(allMax.z(), v.z()));
                  }
            }
      setVertexData(vertices);
      addSubset(0, vertexCount, allMin, allMax);
      setBounds(allMin, allMax);
      ++_geometryRevision;
      emit geometryRevisionChanged();
      update();
      }
//---------------------------------------------------------
//   setLinesForExpandedQuads
//    Upload raw line segments as flat quad geometry for the
//    screen-space expansion vertex shader (gridline.vert).
//    NO width is applied on the CPU: width, side and axis are
//    encoded per-vertex so the shader can displace the corners
//    in clip space by a CONSTANT PIXEL amount perpendicular to
//    the projected line direction.  Stride = 5 floats:
//        x y z  side(±1)  axis(0=X line / 1=Y line)
//---------------------------------------------------------

void TessGeometry::setLinesForExpandedQuads(const Clipper2Lib::PathsD& lines) {
      clear();
      if (lines.empty()) {
            ++_geometryRevision;
            emit geometryRevisionChanged();
            update();
            return;
            }

      addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
      addAttribute(QQuick3DGeometry::Attribute::TexCoordSemantic, 3 * sizeof(float),
                   QQuick3DGeometry::Attribute::F32Type);
      addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);
      setStride(5 * sizeof(float));
      setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

      int segmentCount = 0;
      for (const auto& path : lines)
            if (path.size() >= 2)
                  segmentCount += static_cast<int>(path.size()) - 1;
      if (segmentCount == 0) {
            ++_geometryRevision;
            emit geometryRevisionChanged();
            update();
            return;
            }

      QByteArray vertices;
      vertices.resize(segmentCount * 4 * 5 * static_cast<int>(sizeof(float)));
      QByteArray indices;
      indices.resize(segmentCount * 6 * static_cast<int>(sizeof(uint32_t)));

      float* vptr    = reinterpret_cast<float*>(vertices.data());
      uint32_t* iptr = reinterpret_cast<uint32_t*>(indices.data());
      uint32_t vidx  = 0;

      double minX = std::numeric_limits<double>::max();
      double minY = minX;
      double maxX = std::numeric_limits<double>::lowest();
      double maxY = maxX;
      double minZ = minX;
      double maxZ = maxX;

      for (const auto& path : lines) {
            for (size_t i = 0; i + 1 < path.size(); ++i) {
                  double x0 = path[i].x;
                  double y0 = path[i].y;
                  double x1 = path[i + 1].x;
                  double y1 = path[i + 1].y;
                  double dx = x1 - x0;
                  double dy = y1 - y0;
                  if (dx * dx + dy * dy < 1e-24)
                        continue;

                  // axis: 0 = line runs along X (end points differ
                  // in x), 1 = line runs along Y.
                  float axis = (std::abs(dx) >= std::abs(dy)) ? 0.0f : 1.0f;

                  // four corners: both corners of one end share the
                  // SAME world position; uv.x selects the side of
                  // the stroke, uv.y carries the axis flag.
                  //     end0: (x0,y0) side -1 / +1
                  //     end1: (x1,y1) side -1 / +1
                  *vptr++ = static_cast<float>(x0);
                  *vptr++ = static_cast<float>(y0);
                  *vptr++ = 0.0f;
                  *vptr++ = -1.0f;
                  *vptr++ = axis;
                  *vptr++ = static_cast<float>(x0);
                  *vptr++ = static_cast<float>(y0);
                  *vptr++ = 0.0f;
                  *vptr++ = 1.0f;
                  *vptr++ = axis;
                  *vptr++ = static_cast<float>(x1);
                  *vptr++ = static_cast<float>(y1);
                  *vptr++ = 0.0f;
                  *vptr++ = -1.0f;
                  *vptr++ = axis;
                  *vptr++ = static_cast<float>(x1);
                  *vptr++ = static_cast<float>(y1);
                  *vptr++ = 0.0f;
                  *vptr++ = 1.0f;
                  *vptr++ = axis;

                  // Two triangles: (0,1,2) and (1,3,2)
                  *iptr++  = vidx + 0;
                  *iptr++  = vidx + 1;
                  *iptr++  = vidx + 2;
                  *iptr++  = vidx + 1;
                  *iptr++  = vidx + 3;
                  *iptr++  = vidx + 2;
                  vidx    += 4;

                  minX = std::min({minX, x0, x1});
                  minY = std::min({minY, y0, y1});
                  maxX = std::max({maxX, x0, x1});
                  maxY = std::max({maxY, y0, y1});
                  minZ = 0.0;
                  maxZ = 0.0;
                  }
            }

      if (vidx == 0) {
            ++_geometryRevision;
            emit geometryRevisionChanged();
            update();
            return;
            }

      setVertexData(vertices);
      setIndexData(indices);
      // Expand the bounds by a small world margin so the pixel-wide
      // shader expansion never triggers view-frustum culling.
      float margin = 1.0f;
      setBounds(QVector3D(static_cast<float>(minX) - margin, static_cast<float>(minY) - margin, static_cast<float>(minZ) - margin),
                QVector3D(static_cast<float>(maxX) + margin, static_cast<float>(maxY) + margin, static_cast<float>(maxZ) + margin));
      ++_geometryRevision;
      emit geometryRevisionChanged();
      update();
      }
