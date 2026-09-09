//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "polygon.h"
#include "zcam.h"
#include "tessgeometry.h"
#include "types.h"
#include <QMatrix4x4>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

//---------------------------------------------------------
//   SegmentInfo
//    Describes a single logical segment of the polygon path.
//    A segment is either a LineTo or a CurveTo (with two
//    control-point elements).  Each segment stores the painterPath
//    index of its start vertex and its end vertex.
//---------------------------------------------------------

struct SegmentInfo {
      int startIndex; ///< painterPath index of the start vertex (MoveTo or the previous endpoint)
      int endIndex;   ///< painterPath index of the segment element (LineTo endpoint, or CurveTo = c1)
      bool isCurve;   ///< true if this is a cubic bezier segment
      /// Returns the painterPath index of the actual endpoint vertex.
      /// For line segments this is endIndex (the LineTo element).
      /// For curve segments this is endIndex + 2 (CurveToData2),
      /// because CurveTo holds c1, CurveToData1 holds c2, and
      /// CurveToData2 holds the endpoint.
      int endPointIndex() const { return isCurve ? endIndex + 2 : endIndex; }
      /// Returns the painterPath index of the first control point (c1).
      /// Only valid for curve segments.
      int c1Index() const { return endIndex; }
      /// Returns the painterPath index of the second control point (c2).
      /// Only valid for curve segments.
      int c2Index() const { return endIndex + 1; }
      };

//   Build a list of logical segments from the painterPath.
//   A segment starts at the previous MoveTo/LineTo/CurveTo endpoint
//   and ends at the next LineTo or CurveTo endpoint.  CurveToData1
//   and CurveToData2 elements are part of the preceding CurveTo and
//   do not start a new segment.
//
//   PainterPath convention (matching cubicTo / SVG export / makeSpline):
//     CurveTo      = c1 (first control point)
//     CurveToData1 = c2 (second control point)
//     CurveToData2 = endPoint
//   So the segment "end index" is the CurveTo element, but the actual
//   endpoint position lives at CurveToData2 (index + 2).  The next
//   segment starts at CurveToData2.
static std::vector<SegmentInfo> buildSegmentList(const PainterPath& pp) {
      std::vector<SegmentInfo> segments;
      int currentStart = -1;
      for (int i = 0; i < pp.size(); ++i) {
            PPType t = pp[i].type;
            switch (t) {
                  case PPType::MoveTo: currentStart = i; break;
                  case PPType::LineTo:
                        if (currentStart >= 0) {
                              segments.push_back({currentStart, i, false});
                              currentStart = i;
                              }
                        break;
                  case PPType::CurveTo:
                        if (currentStart >= 0) {
                              segments.push_back({currentStart, i, true});
                              // Next segment starts at the endpoint,
                              // which is CurveToData2 at i + 2.
                              currentStart = i + 2;
                              }
                        break;
                  case PPType::CurveToData1:
                  case PPType::CurveToData2: break;
                  }
            }
      return segments;
      }

//---------------------------------------------------------
//   Line
//---------------------------------------------------------

Polygon::Polygon(ZCam* w, Element* parent) : Element3d(w, parent) {
      setName("");
      _geometry              = new TessGeometry(this);
      _controlHandleGeometry = new TessGeometry(this);
      QJSEngine::setObjectOwnership(_geometry, QJSEngine::CppOwnership);
      QJSEngine::setObjectOwnership(_controlHandleGeometry, QJSEngine::CppOwnership);
      }

Polygon::~Polygon() {
      delete _geometry;
      delete _controlHandleGeometry;
      }

//---------------------------------------------------------
//   makeSpline
//---------------------------------------------------------

int Polygon::makeSpline(int idx) {
      painterPath.makeSpline(idx);
      idx += 2;
      return idx;
      }

//---------------------------------------------------------
//   update
//    Rebuild the geometry from the painterPath.  The polygon
//    is stored as an open polyline; closing is done implicitly
//    in the path list when fill is enabled.
//---------------------------------------------------------

void Polygon::update(int flags) {
      _pathList = createPath();

      // Implicitly close the path list for filled polygons so
      // clipper2 and the tessellator see a closed contour.
      //      if (thinLine() && fill() && !_pathList.empty())
      //            closePath(_pathList);
      strokeAndFill();

      // Rebuild the dashed association lines for any bezier segments so
      // they stay in sync with the control points (drag, convert, split,
      // interactive drawing, undo/redo).
      updateControlHandles();
      }

//---------------------------------------------------------
//   addVertex
//---------------------------------------------------------

void Polygon::addVertex(const Vec2d& p) {
      currentVertex = painterPath.size();
      if (painterPath.empty())
            painterPath.moveTo(p);
      else
            painterPath.lineTo(p);
      }

//---------------------------------------------------------
//   canClose
//    check if p is close to the starting point of the polygon
//---------------------------------------------------------

bool Polygon::canClose(const Vec2d& ep) const {
      const Vec2d& sp = startPos();
      return (std::abs(sp.x() - ep.x()) < 0.1) && (std::abs(sp.y() - ep.y()) < 0.1);
      }

//---------------------------------------------------------
//   vertexPos
//    Return the position of the idx-th vertex as a QVector3D.
//    CurveToData elements are control points of a cubic bezier
//    and are also considered vertices.
//---------------------------------------------------------

QVector3D Polygon::vertexPos(int idx) const {
      if (idx < 0 || idx >= painterPath.size())
            return {};
      const Vec2d& p = painterPath[idx].pos;
      return QVector3D(p.x(), p.y(), 0.0);
      }

//---------------------------------------------------------
//   vertexWorldPos
//    Return the vertex position in root (world) coordinates.
//    Applies the element's full transformation chain
//    (parent matrices * local matrix) to the local vertex
//    position so the handle can be placed as a child of the
//    root node.
//---------------------------------------------------------

QVector3D Polygon::vertexWorldPos(int idx) const {
      if (idx < 0 || idx >= painterPath.size())
            return {};
      const Vec2d& p = painterPath[idx].pos;
      QVector3D local(p.x(), p.y(), 0.0);
      return globalMatrix().map(local);
      }

//---------------------------------------------------------
//   setVertexPos
//    Set the position of the idx-th vertex.  Triggers a
//    geometry update and emits vertexRevisionChanged so the
//    QML handle positions stay in sync.
//---------------------------------------------------------

void Polygon::setVertexPos(int idx, const QVector3D& pos) {
      if (idx < 0 || idx >= painterPath.size())
            return;
      painterPath[idx].pos = Vec2d(pos.x(), pos.y());
      ++_vertexRevision;
      emit vertexRevisionChanged();
      update();
      emit geometryChanged();
      }

//---------------------------------------------------------
//   isVertex
//    Returns true if the element at idx is a vertex or a
//    bezier control point (CurveToData1 / CurveToData2).
//    All of these get a handle in the 3D view.  Control
//    points are distinguishable via isControlPoint().
//    While the polygon is being interactively drawn, no handles
//    are shown at all — the fixed anchors are not draggable yet
//    (clicks are consumed by the drawing tool), so showing them
//    would be misleading.
//
//    Once drawing is finished, the handles follow the segment
//    selection: when the polygon is selected as a whole (no
//    specific segment selected, _selectedSegment < 0) every anchor
//    and bezier control point gets a handle, so the whole outline
//    can be edited at once.
//    When a specific segment is selected, only that segment's
//    endpoints (and, for curve segments, its control points) show
//    handles, focusing the editing on that one edge.
//---------------------------------------------------------

bool Polygon::isVertex(int idx) const {
      if (idx < 0 || idx >= painterPath.size())
            return false;
      // No handles while the polygon is being drawn interactively.
      if (_drawing)
            return false;
      PPType t               = painterPath[idx].type;
      bool isVertexOrControl = t == PPType::MoveTo || t == PPType::LineTo || t == PPType::CurveTo ||
                               t == PPType::CurveToData1 || t == PPType::CurveToData2;
      if (!isVertexOrControl)
            return false;
      // No segment selection (focused or lasso): show handles for all vertices.
      if (!hasSelectedSegment())
            return true;
      // Segment mode: only show handles for the selected segment(s')
      // endpoints and control points.
      return isVertexInSelectedSegment(idx);
      }

//---------------------------------------------------------
//   isControlPoint
//    Returns true if the element at idx is a bezier control
//    point (CurveTo = c1 or CurveToData1 = c2).  Used by QML to
//    render these handles in a different colour.
//---------------------------------------------------------

bool Polygon::isControlPoint(int idx) const {
      if (idx < 0 || idx >= painterPath.size())
            return false;
      PPType t = painterPath[idx].type;
      // Convention: CurveTo = c1, CurveToData1 = c2, CurveToData2 = endPoint.
      // The control points (draggable handles shown in a different colour)
      // are CurveTo and CurveToData1.  CurveToData2 is the endpoint vertex.
      return t == PPType::CurveTo || t == PPType::CurveToData1;
      }

//---------------------------------------------------------
//   drag
//    Drag the idx-th vertex by delta.  This is a live update
//    during dragging and does NOT create an undo command.
//    The caller is responsible for calling ZCam::startVertexDrag()
//    before dragging and ZCam::endVertexDrag() after dragging to
//    create the undo command.
//---------------------------------------------------------

void Polygon::drag(int idx, const QVector3D& delta) {
      if (idx < 0 || idx >= painterPath.size())
            return;
      painterPath[idx].pos += Vec2d(delta.x(), delta.y());
      ++_vertexRevision;
      emit vertexRevisionChanged();
      update();
      emit geometryChanged();
      }

//---------------------------------------------------------
//   toJson
//    Serialize the painterPath (vector of PPElement) into JSON.
//    Each element is stored as {"type": <typeString>, "pos": [x, y]}.
//---------------------------------------------------------

static const char* ppTypeToString(PPType t) {
      switch (t) {
            case PPType::MoveTo: return "MoveTo";
            case PPType::LineTo: return "LineTo";
            case PPType::CurveTo: return "CurveTo";
            case PPType::CurveToData1: return "CurveToData1";
            case PPType::CurveToData2: return "CurveToData2";
            }
      return "Unknown";
      }

json Polygon::toJson() const {
      nlohmann::json data = Element3d::toJson();

      nlohmann::json elements = nlohmann::json::array();
      for (const auto& e : painterPath) {
            nlohmann::json elem;
            elem["type"] = ppTypeToString(e.type);
            elem["pos"]  = nlohmann::json::array({e.pos.x(), e.pos.y()});
            elements.push_back(elem);
            }
      data["painterPath"] = elements;
      return data;
      }

//---------------------------------------------------------
//   fromJson
//    Deserialize the painterPath from JSON.
//---------------------------------------------------------

static PPType stringToPPType(const std::string& s) {
      if (s == "MoveTo")
            return PPType::MoveTo;
      if (s == "LineTo")
            return PPType::LineTo;
      if (s == "CurveTo")
            return PPType::CurveTo;
      if (s == "CurveToData1")
            return PPType::CurveToData1;
      if (s == "CurveToData2")
            return PPType::CurveToData2;
      return PPType::MoveTo;
      }

void Polygon::fromJson(const json& data) {
      Element3d::fromJson(data);

      if (!data.contains("painterPath"))
            return;

      const nlohmann::json& elements = data.at("painterPath");
      painterPath.clear();
      for (const auto& elem : elements) {
            PPElement e;
            e.type          = stringToPPType(elem.at("type").get<std::string>());
            const auto& pos = elem.at("pos");
            e.pos           = Vec2d(pos[0].get<double>(), pos[1].get<double>());
            painterPath.push_back(e);
            }
      update();
      }

//---------------------------------------------------------
//   startDrawing
//    Begin a new polygon at point p.  Adds a MoveTo element
//    and a preview LineTo element that will follow the mouse.
//---------------------------------------------------------

void Polygon::startDrawing(const QVector2D& p) {
      Debug("==");
      Vec2d v(p.x(), p.y());
      painterPath.clear();
      painterPath.moveTo(v);
      // Add a preview vertex at the same position; it will be
      // updated as the mouse moves.
      painterPath.lineTo(v);
      _previewVertex = 1;
      _drawing       = true;
      ++_vertexRevision;
      emit vertexRevisionChanged();
      update();
      emit geometryChanged();
      }

//---------------------------------------------------------
//   updatePreview
//    Update the preview vertex position to follow the mouse.
//---------------------------------------------------------

void Polygon::updatePreview(const QVector2D& p) {
      if (!_drawing || _previewVertex < 0 || _previewVertex >= painterPath.size())
            return;
      painterPath[_previewVertex].pos = Vec2d(p.x(), p.y());
      ++_vertexRevision;
      emit vertexRevisionChanged();
      update();
      emit geometryChanged();
      }

//---------------------------------------------------------
//   continueDrawing
//    Fix the current preview vertex at position p and start a
//    new segment from p.  The preview vertex becomes a permanent
//    vertex and a new preview vertex is appended.
//---------------------------------------------------------

void Polygon::continueDrawing(const QVector2D& p) {
      Debug("==");
      if (!_drawing)
            return;
      Vec2d v(p.x(), p.y());
      // Fix the current preview vertex at p, then add a new preview vertex.
      if (_previewVertex >= 0 && _previewVertex < painterPath.size())
            painterPath[_previewVertex].pos = v;
      painterPath.lineTo(v); // new preview vertex
      _previewVertex = painterPath.size() - 1;
      ++_vertexRevision;
      emit vertexRevisionChanged();
      update();
      emit geometryChanged();
      }

//---------------------------------------------------------
//   continueDrawingBezier
//    Fix the current preview vertex at p and record the segment from
//    the previous fixed vertex to p as a cubic bezier instead of a
//    straight line.
//
//    The path is stored as a flat list of PPElements, so a bezier
//    segment occupies three entries: CurveTo (c1), CurveToData1 (c2)
//    and CurveToData2 (end point).  The control points are seeded on
//    the chord at 1/3 and 2/3 which reproduces a smooth S-curve
//    through the two anchor points.  They are stored as real
//    (draggable) vertices: Polygon::isVertex()/isControlPoint() and the
//    QML handle machinery already treat CurveToData1/2 as handles, and
//    toPathList()/toJson() consume all three entries.  This is the same
//    convention PainterPath::cubicTo() (SVG/text import) and makeSpline()
//    use.
//
//    Layout before (straight segment + preview):
//       [0..k-1] fixed    [k] preview
//    After:
//       [0..k-1] fixed    [k] CurveTo(c1)   [k+1] Data1(c2)   [k+2] Data2(end)   [k+3] preview
//---------------------------------------------------------

void Polygon::continueDrawingBezier(const QVector2D& p) {
      Debug("==");
      if (!_drawing)
            return;
      // Need at least one fixed anchor vertex (element 0) to connect to.
      if (painterPath.size() < 1)
            return;
      int last = static_cast<int>(painterPath.size()) - 1; // preview vertex
      // Find the last real endpoint strictly before the preview.
      // A plain LineTo segment sits right before it, but a bezier
      // segment leaves its control points in between: the endpoint is
      // CurveToData2, preceded by CurveToData1 (c2) and CurveTo (c1).
      // So we must skip CurveTo/CurveToData1 and stop at CurveToData2
      // (or MoveTo/LineTo for non-curve segments).
      int anchor = -1;
      for (int i = last - 1; i >= 0; --i) {
            PPType t = painterPath[i].type;
            if (t == PPType::MoveTo || t == PPType::LineTo || t == PPType::CurveToData2) {
                  anchor = i;
                  break;
                  }
            }
      if (anchor < 0)
            anchor = 0;
      const Vec2d a = painterPath[anchor].pos;
      Vec2d v(p.x(), p.y());
      if (v == a)
            v = a + Vec2d(1e-6, 0.0); // degenerate chord: nudge so controls differ
      Vec2d d1(a.x() + (v.x() - a.x()) / 3.0, a.y() + (v.y() - a.y()) / 3.0);
      Vec2d d2(a.x() + (v.x() - a.x()) * 2.0 / 3.0, a.y() + (v.y() - a.y()) * 2.0 / 3.0);
      // Rewrite the pending segment (a -> preview) as a bezier.
      // PainterPath convention (matching cubicTo / SVG export / makeSpline):
      //   CurveTo      = c1 (first control point)
      //   CurveToData1 = c2 (second control point)
      //   CurveToData2 = endPoint
      painterPath[last].type = PPType::CurveTo;
      painterPath[last].pos  = d1;
      PPElement c1 {PPType::CurveToData1, d2};
      PPElement c2 {PPType::CurveToData2, v};
      painterPath.insert(painterPath.begin() + last + 1, c1);
      painterPath.insert(painterPath.begin() + last + 2, c2);
      // Fresh preview vertex at the same point.
      painterPath.lineTo(v);
      _previewVertex = static_cast<int>(painterPath.size()) - 1;
      ++_vertexRevision;
      emit vertexRevisionChanged();
      update();
      emit geometryChanged();
      }

//---------------------------------------------------------
//   finishDrawing
//    Finish the polygon drawing.  The preview vertex (which
//    holds the current mouse position) is kept as the final
//    vertex.  The polygon is stored as an open polyline;
//    closing is done implicitly during rendering when fill
//    is enabled.
//---------------------------------------------------------

void Polygon::finishDrawing() {
      Debug("==");
      if (!_drawing)
            return;
      _previewVertex = -1;
      _drawing       = false;
      ++_vertexRevision;
      emit vertexRevisionChanged();
      update();
      emit geometryChanged();
      }

//---------------------------------------------------------
//   segmentCount
//---------------------------------------------------------

int Polygon::segmentCount() const {
      return static_cast<int>(buildSegmentList(painterPath).size());
      }

//---------------------------------------------------------
//   setSelectedSegment
//---------------------------------------------------------

void Polygon::setSelectedSegment(int idx) {
      if (idx == _selectedSegment && _lassoSelectedSegments.isEmpty())
            return;
      // Single-click focus: replace the focused segment and drop any
      // lasso-selected segments so the two modes do not mix.
      _selectedSegment       = idx;
      _lassoSelectedSegments.clear();
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      updateControlHandles();
      ++_vertexRevision;
      emit vertexRevisionChanged();
      }

//--------------------------------------------------------------------
//   clearSegmentSelection
//    Clear the focused segment and all lasso-selected segments.
//--------------------------------------------------------------------

void Polygon::clearSegmentSelection() {
      if (_selectedSegment < 0 && _lassoSelectedSegments.isEmpty())
            return;
      _selectedSegment       = -1;
      _lassoSelectedSegments.clear();
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      updateControlHandles();
      ++_vertexRevision;
      emit vertexRevisionChanged();
      }

//--------------------------------------------------------------------
//   setLassoSelectedSegments
//    Set the list of segments selected via the lasso tool.  The first
//    entry (if any) becomes the focused segment so keyboard-driven
//    editing ('b', 'p') operates on a deterministic segment.  Pass an
//    empty list to clear the lasso selection; the focused segment is
//    cleared too if it originated from the previous lasso, and kept if
//    it was set by an independent single click.
//--------------------------------------------------------------------

void Polygon::setLassoSelectedSegments(const QList<int>& idxs) {
      // Clamp and deduplicate, dropping out-of-range indices.
      int count = segmentCount();
      QList<int> clean;
      for (int i : idxs) {
            if (i < 0 || i >= count)
                  continue;
            if (!clean.contains(i))
                  clean.append(i);
            }
      // The focused segment is always the first lasso-selected segment
      // (or -1 when none), so keyboard-driven editing ('b', 'p') has a
      // deterministic target.  A lasso selection fully replaces the
      // previous focus for this polygon.
      int newFocus = clean.isEmpty() ? -1 : clean.first();
      if (clean == _lassoSelectedSegments && newFocus == _selectedSegment)
            return;
      _lassoSelectedSegments = clean;
      _selectedSegment       = newFocus;
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      updateControlHandles();
      ++_vertexRevision;
      emit vertexRevisionChanged();
      }

//--------------------------------------------------------------------
//   selectedSegmentIndices
//    Return the union of the focused segment and the lasso-selected
//    segments as a deduplicated list.  Used by keyboard-driven editing
//    (convert/split) and isVertexInSelectedSegment() to act on every
//    selected segment at once.
//--------------------------------------------------------------------

QList<int> Polygon::selectedSegmentIndices() const {
      QList<int> result = _lassoSelectedSegments;
      if (_selectedSegment >= 0 && !result.contains(_selectedSegment))
            result.append(_selectedSegment);
      return result;
      }

//---------------------------------------------------------
//   segmentMidpoint
//    Returns the world-space midpoint of the given segment.
//    For line segments this is the average of the two endpoints.
//    For curve segments this is the average of the start point and
//    the CurveTo endpoint (a reasonable approximation for picking).
//---------------------------------------------------------

QVector3D Polygon::segmentMidpoint(int segIdx) const {
      auto segments = buildSegmentList(painterPath);
      if (segIdx < 0 || segIdx >= static_cast<int>(segments.size()))
            return {};
      const auto& seg = segments[segIdx];
      const Vec2d& p1 = painterPath[seg.startIndex].pos;
      const Vec2d& p2 = painterPath[seg.endPointIndex()].pos;
      Vec2d mid((p1.x() + p2.x()) * 0.5, (p1.y() + p2.y()) * 0.5);
      QVector3D local(mid.x(), mid.y(), 0.0);
      return globalMatrix().map(local);
      }

//---------------------------------------------------------
//   findNearestSegment
//    Finds the segment closest to the given world position.
//    For line segments, computes the point-to-segment distance.
//    For curve segments, samples the cubic bezier (16 samples) and
//    computes the minimum point-to-segment distance across the
//    sampled polyline, so clicking on a curved edge picks it even
//    when the chord is far from the actual curve.
//    Returns the segment index, or -1 if the polygon has no segments.
//---------------------------------------------------------

int Polygon::findNearestSegment(const QVector3D& worldPos) const {
      auto segments = buildSegmentList(painterPath);
      if (segments.empty())
            return -1;
      QMatrix4x4 gm   = globalMatrix();
      double bestDist = std::numeric_limits<double>::max();
      int bestIdx     = -1;
      // Helper: distance from a point to a line segment.
      auto pointToSeg = [](const QVector3D& pt, const QVector3D& a, const QVector3D& b) {
            QVector3D d  = b - a;
            double len2  = d.lengthSquared();
            double t     = 0.0;
            if (len2 > 1e-12) {
                  t = QVector3D::dotProduct(pt - a, d) / len2;
                  t = std::clamp(t, 0.0, 1.0);
                  }
            QVector3D proj = a + d * t;
            double dx = proj.x() - pt.x();
            double dy = proj.y() - pt.y();
            return std::sqrt(dx * dx + dy * dy);
            };
      for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
            const auto& seg = segments[i];
            const Vec2d& p1 = painterPath[seg.startIndex].pos;
            const Vec2d& p2 = painterPath[seg.endPointIndex()].pos;
            QVector3D wp1 = gm.map(QVector3D(p1.x(), p1.y(), 0.0));
            QVector3D wp2 = gm.map(QVector3D(p2.x(), p2.y(), 0.0));
            double dist;
            if (seg.isCurve) {
                  // Sample the cubic bezier and take the minimum
                  // distance across sampled segments.
                  const Vec2d& c1 = painterPath[seg.c1Index()].pos;
                  const Vec2d& c2 = painterPath[seg.c2Index()].pos;
                  const int N = 16;
                  QVector3D prev = wp1;
                  dist = std::numeric_limits<double>::max();
                  for (int s = 1; s <= N; ++s) {
                        double t = static_cast<double>(s) / N;
                        double u  = 1.0 - t;
                        double b0 = u * u * u;
                        double b1 = 3.0 * u * u * t;
                        double b2 = 3.0 * u * t * t;
                        double b3 = t * t * t;
                        Vec2d pt(b0 * p1.x() + b1 * c1.x() + b2 * c2.x() + b3 * p2.x(),
                                  b0 * p1.y() + b1 * c1.y() + b2 * c2.y() + b3 * p2.y());
                        QVector3D wpt = gm.map(QVector3D(pt.x(), pt.y(), 0.0));
                        double d2 = pointToSeg(worldPos, prev, wpt);
                        if (d2 < dist)
                              dist = d2;
                        prev = wpt;
                        }
                  }
            else {
                  dist = pointToSeg(worldPos, wp1, wp2);
                  }
            if (dist < bestDist) {
                  bestDist = dist;
                  bestIdx  = i;
                  }
            }
      return bestIdx;
      }

//---------------------------------------------------------
//   isVertexInSelectedSegment
//    Returns true if the given painterPath vertex index belongs to
//    any of the selected segments (focused or lasso-selected): i.e.
//    it is one of the endpoint vertices or a bezier control point.
//---------------------------------------------------------

bool Polygon::isVertexInSelectedSegment(int idx) const {
      auto segments = buildSegmentList(painterPath);
      if (segments.empty())
            return false;
      for (int sel : selectedSegmentIndices()) {
            if (sel < 0 || sel >= static_cast<int>(segments.size()))
                  continue;
            const auto& seg = segments[sel];
            if (idx == seg.startIndex || idx == seg.endPointIndex())
                  return true;
            // For curve segments, the control points (CurveTo = c1,
            // CurveToData1 = c2) also belong to this segment.
            if (seg.isCurve) {
                  if (idx == seg.c1Index() || idx == seg.c2Index())
                        return true;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   buildSegmentLine
//    Build the clipper2 line geometry for a single segment,
//    sampling cubic bezier curves for smooth rendering.
//---------------------------------------------------------

void Polygon::buildSegmentLine(Clipper2Lib::PathsD& lines, int segIdx) const {
      auto segments = buildSegmentList(painterPath);
      if (segIdx < 0 || segIdx >= static_cast<int>(segments.size()))
            return;
      const auto& seg = segments[segIdx];
      const Vec2d& p1 = painterPath[seg.startIndex].pos;
      const Vec2d& p2 = painterPath[seg.endPointIndex()].pos;
      if (seg.isCurve) {
            const Vec2d& c1 = painterPath[seg.c1Index()].pos;
            const Vec2d& c2 = painterPath[seg.c2Index()].pos;
            Clipper2Lib::PathD path;
            const int N = 32;
            for (int i = 0; i <= N; ++i) {
                  double t = static_cast<double>(i) / N;
                  double u  = 1.0 - t;
                  double b0 = u * u * u;
                  double b1 = 3.0 * u * u * t;
                  double b2 = 3.0 * u * t * t;
                  double b3 = t * t * t;
                  double x = b0 * p1.x() + b1 * c1.x() + b2 * c2.x() + b3 * p2.x();
                  double y = b0 * p1.y() + b1 * c1.y() + b2 * c2.y() + b3 * p2.y();
                  path.push_back({x, y});
                  }
            lines.push_back(path);
            }
      else {
            Clipper2Lib::PathD line;
            line.push_back({p1.x(), p1.y()});
            line.push_back({p2.x(), p2.y()});
            lines.push_back(line);
            }
      }

//--------------------------------------------------------------------
//   dashedSegments
//    Splits the straight line a→b into a sequence of short 2-point
//    segments (dashes) separated by gaps.  Every dash is emitted as its
//    own PathD with exactly two vertices so that setLines(PathsD) renders
//    each as one independent line segment (the gaps are simply absent
//    vertices).  `dashLen` and `gapLen` are expressed as fractions of the
//    total line length so the dash size scales with the segment and stays
//    readable at any zoom level.
//--------------------------------------------------------------------

static void dashedSegments(Clipper2Lib::PathsD& lines, const Vec2d& a, const Vec2d& b,
                           double dashLen = 0.10, double gapLen = 0.06) {
      const double dx = b.x() - a.x();
      const double dy = b.y() - a.y();
      const double len = std::hypot(dx, dy);
      if (len < 1e-9)
            return;
      const double dashPx = dashLen * len;
      const double gapPx  = gapLen * len;
      const double period = dashPx + gapPx;
      double t            = 0.0; // parameter along a→b
      while (t < 1.0) {
            const double tEnd = std::min(1.0, t + dashPx / len);
            Clipper2Lib::PathD dash;
            dash.push_back({a.x() + dx * t, a.y() + dy * t});
            dash.push_back({a.x() + dx * tEnd, a.y() + dy * tEnd});
            lines.push_back(dash);
            t += period / len;
            }
      }

//--------------------------------------------------------------------
//   updateControlHandles
//    Rebuilds the dashed association lines for the cubic-bezier segments
//    whose control-point handles are currently visible.  For a visible
//    segment with anchor points S (start) and E (end) and control points
//    C1 (CurveTo) and C2 (CurveToData1) two dashed lines are emitted:
//        S → C1   (which control point belongs to the start anchor)
//        E → C2   (which control point belongs to the end anchor)
//    This is the standard bezier-handle representation and makes it clear
//    which control points shape which segment.  The visibility mirrors
//    exactly the vertex-handle logic in isVertex(): while the polygon is
//    being drawn no helper lines are shown; once finished they appear for
//    every bezier segment when the polygon is selected as a whole (no
//    specific segment selected), or only for the selected segment(s)
//    (focused or lasso-selected) in segment mode.  Straight segments
//    produce nothing.  The lines live in the polygon's local coordinate
//    space (the same space as _pathList); Shape.qml renders them as a
//    sibling Model that inherits the polygon's transform, so no manual
//    transform is needed here.
//--------------------------------------------------------------------

void Polygon::updateControlHandles() {
      if (!_controlHandleGeometry)
            return;
      Clipper2Lib::PathsD lines;
      // No handles (and therefore no helper lines) while the polygon is
      // being drawn interactively — mirror isVertex().
      if (!_drawing) {
            const auto   segments = buildSegmentList(painterPath);
            const bool   noSel    = !hasSelectedSegment();
            const QList<int> selected = selectedSegmentIndices();
            for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
                  const auto& seg = segments[i];
                  if (!seg.isCurve)
                        continue;
                  // Mirror isVertex(): the bezier control points (and thus
                  // their dashed association lines) are shown for every
                  // bezier segment when the polygon is selected as a whole
                  // (no segment selected), or only for the selected
                  // segment(s) in segment mode.
                  if (!noSel && !selected.contains(i))
                        continue;
                  const Vec2d& start = painterPath[seg.startIndex].pos; // S
                  const Vec2d& end   = painterPath[seg.endPointIndex()].pos; // E
                  const Vec2d& c1    = painterPath[seg.c1Index()].pos; // C1 (start tangent)
                  const Vec2d& c2    = painterPath[seg.c2Index()].pos; // C2 (end tangent)
                  dashedSegments(lines, start, c1);
                  dashedSegments(lines, end, c2);
                  }
            }
      _controlHandleGeometry->setLines(lines);
      }

//---------------------------------------------------------
//   setHoveredSegment
//---------------------------------------------------------

void Polygon::setHoveredSegment(int idx) {
      if (idx == _hoveredSegment)
            return;
      _hoveredSegment = idx;
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      }

//---------------------------------------------------------
//   updateSelectionGeometry
//    Override: when a segment is selected or hovered, highlight
//    the corresponding segment(s) as a line overlay instead of
//    the bounding-box rectangle.  When neither is active, fall
//    back to the base class bounding-box behaviour.
//---------------------------------------------------------

void Polygon::updateSelectionGeometry() {
      auto selected = selectedSegmentIndices();
      if (selected.isEmpty() && _hoveredSegment < 0) {
            Element3d::updateSelectionGeometry();
            return;
            }
      if (!_selectionGeometry)
            return;
      Clipper2Lib::PathsD lines;
      for (int seg : selected)
            buildSegmentLine(lines, seg);
      if (_hoveredSegment >= 0 && !selected.contains(_hoveredSegment))
            buildSegmentLine(lines, _hoveredSegment);
      _selectionGeometry->setLines(lines);
      }

//---------------------------------------------------------
//   convertSelectedSegmentToBezier
//    Convert the currently selected line segment into a cubic
//    bezier segment.  The start point and end point of the
//    segment remain unchanged — only two control points are
//    added (at 1/3 and 2/3 of the chord), giving a curve that is
//    initially identical to the straight line.
//
//    PainterPath stores a cubic bezier as three consecutive
//    elements: CurveTo (c1), CurveToData1 (c2), CurveToData2
//    (endPoint).  The LineTo element at endIndex is replaced by
//    the CurveTo element (carrying c1); c2 and the endPoint are
//    inserted after it.
//
//    Layout before:
//       ... [startIndex] ... [endIndex=LineTo(p2)] ...
//    After:
//       ... [startIndex] ... [CurveTo(c1)] [Data1(c2)] [Data2(p2)] ...
//---------------------------------------------------------

void Polygon::convertSelectedSegmentToBezier() {
      if (_drawing)
            return;
      // Convert every selected straight segment (focused or lasso-selected).
      auto selected = selectedSegmentIndices();
      if (selected.isEmpty())
            return;

      // Recompute the segment list and convert, from highest index to
      // lowest so that inserts do not invalidate earlier indices.
      // Each conversion inserts two elements (c2, end) at a single index,
      // so indices above the one just edited shift by +2 — processing in
      // descending order keeps them valid.
      auto segments = buildSegmentList(painterPath);
      QList<int> toConvert;
      for (int sel : selected) {
            if (sel < 0 || sel >= static_cast<int>(segments.size()))
                  continue;
            if (!segments[sel].isCurve)
                  toConvert.append(sel);
            }
      if (toConvert.isEmpty())
            return; // nothing to convert

      std::sort(toConvert.begin(), toConvert.end(), std::greater<int>());

      // IMPORTANT: copy the endpoint values (do NOT take references).
      // The LineTo element at seg.endIndex is rewritten below, so any
      // reference into painterPath would be overwritten (or invalidated
      // by the following insert's reallocation) before it is used to
      // write the CurveToData2 endpoint.
      PainterPath oldPath = painterPath;
      for (int sel : toConvert) {
            auto segs = buildSegmentList(painterPath);
            const auto& seg = segs[sel];
            const Vec2d start = painterPath[seg.startIndex].pos;
            const Vec2d end   = painterPath[seg.endIndex].pos;
            // Control points at 1/3 and 2/3 of the chord: the curve
            // passes through start and end exactly as the line did.
            Vec2d c1(start.x() + (end.x() - start.x()) / 3.0,
                     start.y() + (end.y() - start.y()) / 3.0);
            Vec2d c2(start.x() + (end.x() - start.x()) * 2.0 / 3.0,
                     start.y() + (end.y() - start.y()) * 2.0 / 3.0);
            // Replace the LineTo element with the three-element bezier
            // representation (CurveTo=c1, Data1=c2, Data2=endPoint).
            painterPath[seg.endIndex].type = PPType::CurveTo;
            painterPath[seg.endIndex].pos  = c1;
            painterPath.insert(painterPath.begin() + seg.endIndex + 1,
                               PPElement {PPType::CurveToData1, c2});
            painterPath.insert(painterPath.begin() + seg.endIndex + 2,
                               PPElement {PPType::CurveToData2, end});
            }

      if (zcam && zcam->project()) {
            zcam->project()->undo()->beginMacro();
            zcam->project()->undo()->push(
                new PolygonPathCommand(zcam, this, oldPath, painterPath));
            zcam->project()->undo()->endMacro();
            }

      // All selected segments are now curves — clear the selection.
      _selectedSegment       = -1;
      _lassoSelectedSegments.clear();

      ++_vertexRevision;
      emit vertexRevisionChanged();
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      update();
      emit geometryChanged();
      }

//---------------------------------------------------------
//   splitSelectedSegment
//    Split the currently selected line segment at its geometric
//    midpoint by inserting a new LineTo vertex at the midpoint.
//    This subdivides the segment into two equal-length line
//    segments.
//
//    Layout before:
//       ... [startIndex] ... [endIndex] ...
//    After:
//       ... [startIndex] ... [newMidLineTo] [endIndex] ...
//
//    The new vertex is inserted right before the current endIndex
//    element, so the original endIndex is pushed one position later.
//    The segment selection is updated to remain on the first half
//    so the user sees the newly created vertex handle.
//---------------------------------------------------------

void Polygon::splitSelectedSegment() {
      if (_drawing)
            return;
      auto segments = buildSegmentList(painterPath);
      // Collect the selected straight segments (only lines can be split).
      QList<int> toSplit;
      for (int sel : selectedSegmentIndices()) {
            if (sel < 0 || sel >= static_cast<int>(segments.size()))
                  continue;
            if (!segments[sel].isCurve)
                  toSplit.append(sel);
            }
      if (toSplit.isEmpty())
            return;

      // Process in descending order so each midpoint insert does not
      // shift the indices of the segments we still have to split.
      std::sort(toSplit.begin(), toSplit.end(), std::greater<int>());

      // Record old painterPath for undo
      PainterPath oldPath = painterPath;
      for (int sel : toSplit) {
            auto segs  = buildSegmentList(painterPath);
            const auto& seg = segs[sel];
            const Vec2d& p1 = painterPath[seg.startIndex].pos;
            const Vec2d& p2 = painterPath[seg.endIndex].pos;
            Vec2d mid((p1.x() + p2.x()) * 0.5, (p1.y() + p2.y()) * 0.5);
            // Insert a new LineTo at the midpoint, before the current endIndex
            painterPath.insert(painterPath.begin() + seg.endIndex, PPElement {PPType::LineTo, mid});
            }

      // Push undo command
      if (zcam && zcam->project()) {
            zcam->project()->undo()->beginMacro();
            zcam->project()->undo()->push(
                new PolygonPathCommand(zcam, this, oldPath, painterPath));
            zcam->project()->undo()->endMacro();
            }

      // All selected segments have been split — clear the selection so
      // the new midpoint vertex handles are visible.
      _selectedSegment       = -1;
      _lassoSelectedSegments.clear();

      ++_vertexRevision;
      emit vertexRevisionChanged();
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      update();
      emit geometryChanged();
      }

//---------------------------------------------------------
//   pointOnSegment
//    Returns true when the point p lies on the segment a-b (inclusive of
//    the endpoints), within a tiny, scale-invariant tolerance.  Two
//    conditions must hold:
//      * p is collinear with a and b  (perpendicular deviation of p from
//        the line a-b is below one part in 100 000 of the chord length
//        a-b, i.e. invisible at any zoom level);
//      * p lies between a and b       (its coordinate-wise bounding box
//        falls inside the a-b bounding box).
//    A degenerate segment (a == b) only matches p == a.
//    This is the exact, safe test used by optimize(): it guarantees a
//    reduction never changes the rendered outline — only geometry that
//    already draws as the very same straight line is simplified.  In
//    particular it rejects the "spike" / overshoot cases where the middle
//    point is collinear but outside the a-b span.
//---------------------------------------------------------

static inline bool pointOnSegment(const Vec2d& a, const Vec2d& p, const Vec2d& b) {
      double dx  = b.x() - a.x();
      double dy  = b.y() - a.y();
      double len = std::hypot(dx, dy);
      if (len < 1e-9)
            return (p.x() - a.x()) * (p.x() - a.x()) + (p.y() - a.y()) * (p.y() - a.y()) <= 1e-12;
      // 1) collinear: perpendicular distance of p from the line a-b.
      double dist = std::abs(dx * (p.y() - a.y()) - dy * (p.x() - a.x())) / len;
      if (dist > 1e-5 * len)
            return false;
      // 2) between a and b: p must be inside the a-b bounding box.
      double tol = 1e-5 * len;
      if (p.x() < std::min(a.x(), b.x()) - tol || p.x() > std::max(a.x(), b.x()) + tol)
            return false;
      return !(p.y() < std::min(a.y(), b.y()) - tol || p.y() > std::max(a.y(), b.y()) + tol);
      }

//--------------------------------------------------------------------
//   optimize
//    Simplify the polygon geometry.  It works in a single right-to-left
//    pass over the logical segments and performs two kinds of reduction:
//
//    1) Bezier → line:  a cubic-bezier segment whose control points are
//       collinear with its chord (start → end) actually draws a straight
//       line.  It is replaced by a single straight line segment.
//
//    2) Line + line → line:  two consecutive straight segments A → B → C
//       whose three anchors are collinear (i.e. B does not change the
//       direction of travel) are merged into a single segment A → C,
//       removing the redundant intermediate vertex B.
//
//    The pass runs from the last segment to the first, so element
//    removals never invalidate the indices of the segments that still
//    have to be processed.  Because a segment is only combined with the
//    one to its right (which has already been reduced), a whole run of
//    collinear vertices is progressively folded into a single straight
//    segment (A → B → C → D becomes A → D).
//
//    The operation is exact: every reduction draws the very same outline
//    as before, it only removes redundant control points / vertices.
//    If nothing can be simplified the function is a no-op (the undo
//    stack is untouched and the project is not marked dirty).  When a
//    change is made it is recorded as a single undoable command.
//--------------------------------------------------------------------

void Polygon::optimize() {
      if (_drawing)
            return;

      auto segments = buildSegmentList(painterPath);
      if (segments.size() < 1)
            return; // nothing to optimize

      const int n = static_cast<int>(segments.size());

      PainterPath oldPath = painterPath;
      bool changed        = false;

      for (int i = n - 1; i >= 0; --i) {
            auto segs = buildSegmentList(painterPath);
            if (i >= static_cast<int>(segs.size()))
                  continue; // this segment was already absorbed
            const SegmentInfo seg = segs[i];
            const Vec2d start     = painterPath[seg.startIndex].pos;
            const Vec2d end       = painterPath[seg.endPointIndex()].pos;

            // (1) Degenerate bezier -> straight line.
            if (seg.isCurve) {
                  const Vec2d c1 = painterPath[seg.c1Index()].pos;
                  const Vec2d c2 = painterPath[seg.c2Index()].pos;
                  // Both control points on the chord start-end (between the
                  // endpoints) means the cubic bezier degenerates to that
                  // very straight chord — so it can be safely replaced by a
                  // single line segment.  pointOnSegment() also rejects the
                  // overshoot case where a control point is collinear but
                  // beyond an endpoint (which would change the outline).
                  if (pointOnSegment(start, c1, end) && pointOnSegment(start, c2, end)) {
                        // Replace the three-element bezier (CurveTo, CurveToData1,
                        // CurveToData2) with a single LineTo at the end point.
                        painterPath[seg.endIndex].type = PPType::LineTo;
                        painterPath[seg.endIndex].pos  = end;
                        painterPath.erase(painterPath.begin() + seg.endIndex + 1,
                                          painterPath.begin() + seg.endIndex + 3);
                        changed = true;
                        }
                  } // else: a genuine curve — keep it untouched.

            // (2) Merge this straight segment with the next one when the
            //     three anchors (start -> end -> nextEnd) are collinear.
            //     Recompute the segment list so every index used here is
            //     valid even if step (1) just changed the element layout.
            {
                  auto mSegs = buildSegmentList(painterPath);
                  if (i < static_cast<int>(mSegs.size()) && !mSegs[i].isCurve &&
                      i + 1 < static_cast<int>(mSegs.size())) {
                        const SegmentInfo& cur     = mSegs[i];
                        const SegmentInfo& nxt     = mSegs[i + 1];
                        if (!nxt.isCurve) {
                              const Vec2d curStart = painterPath[cur.startIndex].pos;
                              const Vec2d curEnd   = painterPath[cur.endPointIndex()].pos;
                              const Vec2d nextEnd  = painterPath[nxt.endPointIndex()].pos;
                              // Merge only when the middle vertex curEnd lies on
                              // the segment curStart -> nextEnd (i.e. it adds no
                              // bend and no spike).  pointOnSegment() rejects the
                              // collinear-but-outside (out-and-back) case so the
                              // outline is never changed.
                              if (pointOnSegment(curStart, curEnd, nextEnd)) {
                                    // The middle vertex `curEnd` sits at
                                    // cur.endIndex (a LineTo, i.e. the segment's
                                    // last element).  Removing it folds
                                    // A -> B -> C into A -> C.
                                    painterPath.erase(painterPath.begin() + cur.endIndex);
                                    changed = true;
                                    }
                              }
                        }
                  }
            }

      if (!changed)
            return; // nothing simplified — leave the undo stack untouched

      if (zcam && zcam->project()) {
            zcam->project()->undo()->beginMacro();
            zcam->project()->undo()->push(
                new PolygonPathCommand(zcam, this, oldPath, painterPath));
            zcam->project()->undo()->endMacro();
            }

      // The geometry changed — drop any stale segment selection (the
      // indices no longer refer to the same segments) so the new, cleaner
      // outline is shown with all of its handles.
      _selectedSegment       = -1;
      _lassoSelectedSegments.clear();

      ++_vertexRevision;
      emit vertexRevisionChanged();
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      update();
      emit geometryChanged();
      }
