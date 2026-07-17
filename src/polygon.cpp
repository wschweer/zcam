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
      int endIndex;   ///< painterPath index of the end vertex (LineTo or CurveTo)
      bool isCurve;   ///< true if this is a cubic bezier segment
      };

//   Build a list of logical segments from the painterPath.
//   A segment starts at the previous MoveTo/LineTo/CurveTo endpoint
//   and ends at the next LineTo or CurveTo endpoint.  CurveToData1
//   and CurveToData2 elements are part of the preceding CurveTo and
//   do not start a new segment.
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
                              currentStart = i + 2; // skip CurveToData1 and CurveToData2
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
      _geometry = new TessGeometry(this);
      QJSEngine::setObjectOwnership(_geometry, QJSEngine::CppOwnership);
      }

Polygon::~Polygon() {
      delete _geometry;
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
//    During interactive drawing, the preview vertex is excluded
//    so no handle is shown for it.
//---------------------------------------------------------

bool Polygon::isVertex(int idx) const {
      if (idx < 0 || idx >= painterPath.size())
            return false;
      if (_drawing && idx == _previewVertex)
            return false;
      // When no segment is selected (whole-polygon mode), show no
      // handles at all.  The user clicks a segment to get handles
      // for just that segment's endpoints.
      if (_selectedSegment < 0)
            return false;
      // When a segment is selected, only show handles for the vertices
      // belonging to that segment (its two endpoints and, for curve
      // segments, its control points).
      PPType t = painterPath[idx].type;
      bool isVertexOrControl = t == PPType::MoveTo || t == PPType::LineTo || t == PPType::CurveTo ||
                               t == PPType::CurveToData1 || t == PPType::CurveToData2;
      if (isVertexOrControl)
            return isVertexInSelectedSegment(idx);
      return false;
      }

//---------------------------------------------------------
//   isControlPoint
//    Returns true if the element at idx is a bezier control
//    point (CurveToData1 or CurveToData2).  Used by QML to
//    render these handles in a different colour.
//---------------------------------------------------------

bool Polygon::isControlPoint(int idx) const {
      if (idx < 0 || idx >= painterPath.size())
            return false;
      PPType t = painterPath[idx].type;
      return t == PPType::CurveToData1 || t == PPType::CurveToData2;
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
      if (idx == _selectedSegment)
            return;
      _selectedSegment = idx;
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      ++_vertexRevision;
      emit vertexRevisionChanged();
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
      const Vec2d& p2 = painterPath[seg.endIndex].pos;
      Vec2d mid((p1.x() + p2.x()) * 0.5, (p1.y() + p2.y()) * 0.5);
      QVector3D local(mid.x(), mid.y(), 0.0);
      return globalMatrix().map(local);
      }

//---------------------------------------------------------
//   findNearestSegment
//    Finds the segment whose world-space midpoint is closest to
//    the given world position.  Returns the segment index, or -1
//    if the polygon has no segments.
//---------------------------------------------------------

int Polygon::findNearestSegment(const QVector3D& worldPos) const {
      auto segments = buildSegmentList(painterPath);
      if (segments.empty())
            return -1;
      QMatrix4x4 gm   = globalMatrix();
      double bestDist = std::numeric_limits<double>::max();
      int bestIdx     = -1;
      for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
            const auto& seg = segments[i];
            const Vec2d& p1 = painterPath[seg.startIndex].pos;
            const Vec2d& p2 = painterPath[seg.endIndex].pos;
            Vec2d mid((p1.x() + p2.x()) * 0.5, (p1.y() + p2.y()) * 0.5);
            QVector3D worldMid = gm.map(QVector3D(mid.x(), mid.y(), 0.0));
            double dx          = worldMid.x() - worldPos.x();
            double dy          = worldMid.y() - worldPos.y();
            double dist        = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                  bestDist = dist;
                  bestIdx  = i;
                  }
            }
      return bestIdx;
      }

//---------------------------------------------------------
//   isVertexInSelectedSegment
//    Returns true if the given painterPath vertex index is one of
//    the two endpoint vertices of the currently selected segment.
//---------------------------------------------------------

bool Polygon::isVertexInSelectedSegment(int idx) const {
      if (_selectedSegment < 0)
            return false;
      auto segments = buildSegmentList(painterPath);
      if (_selectedSegment >= static_cast<int>(segments.size()))
            return false;
      const auto& seg = segments[_selectedSegment];
      if (idx == seg.startIndex || idx == seg.endIndex)
            return true;
      // For curve segments, the control points (CurveToData1 and
      // CurveToData2) also belong to this segment.
      if (seg.isCurve) {
            int data1 = seg.endIndex + 1;
            int data2 = seg.endIndex + 2;
            if (idx == data1 || idx == data2)
                  return true;
            }
      return false;
      }

//---------------------------------------------------------
//   updateSelectionGeometry
//    Override: when a segment is selected, highlight that single
//    segment as a line overlay instead of the bounding-box rectangle.
//    When no segment is selected, fall back to the base class
//    bounding-box behaviour.
//---------------------------------------------------------

void Polygon::updateSelectionGeometry() {
      if (_selectedSegment < 0) {
            Element3d::updateSelectionGeometry();
            return;
            }
      if (!_selectionGeometry)
            return;
      auto segments = buildSegmentList(painterPath);
      if (_selectedSegment >= static_cast<int>(segments.size())) {
            Clipper2Lib::PathsD empty;
            _selectionGeometry->setLines(empty);
            return;
            }
      const auto& seg = segments[_selectedSegment];
      const Vec2d& p1 = painterPath[seg.startIndex].pos;
      const Vec2d& p2 = painterPath[seg.endIndex].pos;
      Clipper2Lib::PathD line;
      line.push_back({p1.x(), p1.y()});
      line.push_back({p2.x(), p2.y()});
      Clipper2Lib::PathsD lines;
      lines.push_back(line);
      _selectionGeometry->setLines(lines);
      }