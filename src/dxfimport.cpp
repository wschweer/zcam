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

#include "dxfimport.h"

#include "zcam.h"
#include "project.h"
#include "cad.h"
#include "cam.h"
#include "fixture.h"
#include "group.h"
#include "recipe.h"
#include "polygon.h"
#include "ellipse.h"
#include "text.h"
#include "undo.h"
#include "logger.h"
#include "types.h"

#include <QFileInfo>
#include <QString>
#include <QVector3D>
#include <format>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

#include "libdxfrw.h"
#include "drw_interface.h"
#include "drw_base.h"
#include "drw_entities.h"
#include "drw_header.h"

//---------------------------------------------------------
//   DxfReaderInterface
//    Implements DRW_Interface to receive callbacks from
//    libdxfrw while reading a DXF file.  Each entity
//    callback creates a ZCam element (Polygon or Ellipse)
//    and inserts it into the appropriate Layer.
//---------------------------------------------------------

class DxfReaderInterface final : public DRW_Interface
      {
      ZCam* m_zcam;
      Group* m_defaultLayer;                                ///< the single layer created for this import
      double m_unitScale;                                   ///< conversion factor to mm
      QString m_baseName;                                   ///< file base name for element naming
      std::unordered_map<std::string, int> m_layerColorMap; ///< layer name -> color index
      // Block support: collect entities per block, then replicate on INSERT
      struct BlockEntity {
            enum class Type { Line, Arc, Circle, LWPolyline, Ellipse, Spline, Point, Text, Solid, Face3d };
            Type type;
            // Common data
            DRW_Coord p1, p2, p3, p4;
            double radius {0.0};
            double startAng {0.0};
            double endAng {0.0};
            double ratio {0.0};
            double staparam {0.0};
            double endparam {0.0};
            int isccw {1};
            std::vector<DRW_Vertex2D> vertices;
            int flags {0};
            // Spline
            int degree {0};
            std::vector<DRW_Coord> controlPoints;
            std::vector<DRW_Coord> fitPoints;
            std::vector<double> knots;
            // Text
            std::string text;
            double height {1.0};
            double angle {0.0};
            };
      std::unordered_map<std::string, std::vector<BlockEntity>> m_blocks;
      std::string m_currentBlockName;
      bool m_inBlock {false};

    public:
      DxfReaderInterface(ZCam* zcam, Group* layer, const QString& baseName)
          : m_zcam(zcam), m_defaultLayer(layer), m_unitScale(1.0), m_baseName(baseName) {}
      double unitScale() const { return m_unitScale; }
      void setUnitScale(double s) { m_unitScale = s; }
      double mm(double v) const { return v * m_unitScale; }
      Vec2d mm2d(const DRW_Coord& c) const { return {mm(c.x), mm(c.y)}; }
      /// Build an element name from the file base name and a type suffix.
      /// Element::setName() will de-duplicate automatically (e.g. "foo-Line", "foo-Line-1").
      QString elementName(const char* suffix) const {
            return QStringLiteral("%1-%2").arg(m_baseName, QString::fromUtf8(suffix));
            }
      //---- DRW_Interface overrides (read side) -------------------------
      void addHeader(const DRW_Header* data) override {
            // Determine unit scale from $INSUNITS.
            // $INSUNITS=0 means "unspecified". In that case the DXF values
            // are assumed to be in pixel units and are converted to mm
            // using the configured dxfScale (dots per millimeter).
            // Only when $INSUNITS is entirely absent from the header do we
            // fall back to $MEASUREMENT as a last resort.
            auto it = data->vars.find("$INSUNITS");
            if (it != data->vars.end() && it->second->type() == DRW_Variant::INTEGER) {
                  int unit = it->second->content.i;
                  if (unit == 0) {
                        // pixel units -> apply configured dpmm scale
                        double dpmm = m_zcam->config() ? m_zcam->config()->dxfScale() : 1.0;
                        if (dpmm <= 0.0)
                              dpmm = 1.0;
                        m_unitScale = 1.0 / dpmm;
                        }
                  else {
                        m_unitScale = unitToMm(unit);
                        }
                  }
            else {
                  // Fall back to $MEASUREMENT only when $INSUNITS was not present
                  auto mit = data->vars.find("$MEASUREMENT");
                  if (mit != data->vars.end() && mit->second->type() == DRW_Variant::INTEGER) {
                        if (mit->second->content.i == 0) // English
                              m_unitScale = 25.4;
                        }
                  }
            }
      void addLType(const DRW_LType&) override {}
      void addLayer(const DRW_Layer& data) override { m_layerColorMap[data.name] = data.color; }
      void addDimStyle(const DRW_Dimstyle&) override {}
      void addVport(const DRW_Vport&) override {}
      void addTextStyle(const DRW_Textstyle&) override {}
      void addAppId(const DRW_AppId&) override {}
      void addBlock(const DRW_Block& data) override {
            m_currentBlockName = data.name;
            m_blocks[m_currentBlockName].clear();
            m_inBlock = true;
            }
      void setBlock(int /*handle*/) override {
            // DWG: switch to a previously defined block
            }
      void endBlock() override {
            m_inBlock = false;
            m_currentBlockName.clear();
            }
      void addPoint(const DRW_Point& data) override {
            if (m_inBlock) {
                  BlockEntity e;
                  e.type = BlockEntity::Type::Point;
                  e.p1   = data.basePoint;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            // A single point → small marker line (zero-length not useful)
            // Create a tiny polygon point marker
            auto* poly = new Polygon(m_zcam, m_defaultLayer);
            poly->setName(elementName("Point"));
            Vec2d p = mm2d(data.basePoint);
            poly->moveTo(p);
            poly->lineTo(p);
            poly->set_lineWidth(0.0);
            poly->set_fill(false);
            poly->update();
            insertElement(poly);
            }
      void addLine(const DRW_Line& data) override {
            if (m_inBlock) {
                  BlockEntity e;
                  e.type = BlockEntity::Type::Line;
                  e.p1   = data.basePoint;
                  e.p2   = data.secPoint;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            auto* poly = new Polygon(m_zcam, m_defaultLayer);
            poly->setName(elementName("Line"));
            poly->moveTo(mm2d(data.basePoint));
            poly->lineTo(mm2d(data.secPoint));
            poly->set_lineWidth(0.0);
            poly->set_fill(false);
            poly->update();
            insertElement(poly);
            }
      void addRay(const DRW_Ray&) override {}
      void addXline(const DRW_Xline&) override {}
      void addArc(const DRW_Arc& data) override {
            if (m_inBlock) {
                  BlockEntity e;
                  e.type     = BlockEntity::Type::Arc;
                  e.p1       = data.basePoint;
                  e.radius   = data.radious;
                  e.startAng = data.staangle;
                  e.endAng   = data.endangle;
                  e.isccw    = data.isccw;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            auto pp = arcToPainterPath(data);
            if (pp.empty())
                  return;
            auto* poly = new Polygon(m_zcam, m_defaultLayer);
            poly->setName(elementName("Arc"));
            poly->setPainterPath(pp);
            poly->set_lineWidth(0.0);
            poly->set_fill(false);
            poly->update();
            insertElement(poly);
            }
      void addCircle(const DRW_Circle& data) override {
            if (m_inBlock) {
                  BlockEntity e;
                  e.type   = BlockEntity::Type::Circle;
                  e.p1     = data.basePoint;
                  e.radius = data.radious;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            double r  = mm(data.radious);
            auto* ell = new Ellipse(m_zcam, m_defaultLayer);
            ell->setName(elementName("Circle"));
            ell->set_pos(QVector3D(mm(data.basePoint.x), mm(data.basePoint.y), 0.0));
            ell->set_size(QVector2D(r * 2.0, r * 2.0));
            ell->update();
            insertElement(ell);
            }
      void addEllipse(const DRW_Ellipse& data) override {
            if (m_inBlock) {
                  BlockEntity e;
                  e.type     = BlockEntity::Type::Ellipse;
                  e.p1       = data.basePoint;
                  e.p2       = data.secPoint;
                  e.ratio    = data.ratio;
                  e.staparam = data.staparam;
                  e.endparam = data.endparam;
                  e.isccw    = data.isccw;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            double majorR = std::sqrt(data.secPoint.x * data.secPoint.x + data.secPoint.y * data.secPoint.y);
            majorR        = mm(majorR);
            double minorR = majorR * data.ratio;
            double rotation = std::atan2(data.secPoint.y, data.secPoint.x);
            double rotDeg   = rotation * 180.0 / std::numbers::pi;

            // Full ellipse: staparam=0, endparam=2π
            bool full = (data.staparam == 0.0 && std::abs(data.endparam - 2.0 * std::numbers::pi) < 1e-6);

            if (full) {
                  auto* ell = new Ellipse(m_zcam, m_defaultLayer);
                  ell->setName(elementName("Ellipse"));
                  ell->set_pos(QVector3D(mm(data.basePoint.x), mm(data.basePoint.y), 0.0));
                  ell->set_size(QVector2D(majorR * 2.0, minorR * 2.0));
                  ell->set_rot(QVector3D(0.0, 0.0, rotDeg));
                  ell->update();
                  insertElement(ell);
                  }
            else {
                  // Elliptical arc → tessellate into a polygon
                  auto pp = ellipseArcToPainterPath(data);
                  if (pp.empty())
                        return;
                  auto* poly = new Polygon(m_zcam, m_defaultLayer);
                  poly->setName(elementName("EllipseArc"));
                  poly->setPainterPath(pp);
                  poly->set_lineWidth(0.0);
                  poly->set_fill(false);
                  poly->update();
                  insertElement(poly);
                  }
            }
      void addLWPolyline(const DRW_LWPolyline& data) override {
            if (data.vertlist.empty())
                  return;

            if (m_inBlock) {
                  BlockEntity e;
                  e.type = BlockEntity::Type::LWPolyline;
                  for (const auto& v : data.vertlist)
                        e.vertices.push_back(*v);
                  e.flags = data.flags;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }

            auto* poly = new Polygon(m_zcam, m_defaultLayer);
            poly->setName(elementName("Polyline"));
            bool first = true;
            for (const auto& v : data.vertlist) {
                  Vec2d pt(mm(v->x), mm(v->y));
                  if (first) {
                        poly->moveTo(pt);
                        first = false;
                        }
                  else {
                        // Handle bulge (arc segment) by tessellating
                        if (std::abs(v->bulge) > 1e-10) {
                              // Get previous point
                              Vec2d prev = poly->vertexPos(poly->vertices() - 1).toPointF();
                              arcBulgeTo(*poly, prev, pt, v->bulge);
                              }
                        else {
                              poly->lineTo(pt);
                              }
                        }
                  }
            // Closed polyline
            if (data.flags & 1) {
                  Vec2d start = poly->startPos();
                  poly->lineTo(start);
                  }
            poly->set_lineWidth(0.0);
            poly->set_fill(data.flags & 1);
            poly->update();
            insertElement(poly);
            }
      void addPolyline(const DRW_Polyline& data) override {
            if (data.vertlist.empty())
                  return;
            if (m_inBlock) {
                  BlockEntity e;
                  e.type = BlockEntity::Type::LWPolyline;
                  for (const auto& v : data.vertlist)
                        e.vertices.push_back(DRW_Vertex2D(v->basePoint.x, v->basePoint.y, v->bulge));
                  e.flags = data.flags;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            auto* poly = new Polygon(m_zcam, m_defaultLayer);
            poly->setName(elementName("Polyline3d"));
            bool first = true;
            for (const auto& v : data.vertlist) {
                  Vec2d pt(mm(v->basePoint.x), mm(v->basePoint.y));
                  if (first) {
                        poly->moveTo(pt);
                        first = false;
                        }
                  else if (std::abs(v->bulge) > 1e-10) {
                        Vec2d prev = poly->vertexPos(poly->vertices() - 1).toPointF();
                        arcBulgeTo(*poly, prev, pt, v->bulge);
                        }
                  else {
                        poly->lineTo(pt);
                        }
                  }
            if (data.flags & 1) {
                  Vec2d start = poly->startPos();
                  poly->lineTo(start);
                  }
            poly->set_lineWidth(0.0);
            poly->set_fill(data.flags & 1);
            poly->update();
            insertElement(poly);
            }
      void addSpline(const DRW_Spline* data) override {
            if (!data || data->controllist.empty())
                  return;

            if (m_inBlock) {
                  BlockEntity e;
                  e.type   = BlockEntity::Type::Spline;
                  e.degree = data->degree;
                  for (const auto& cp : data->controllist)
                        e.controlPoints.push_back(*cp);
                  for (const auto& fp : data->fitlist)
                        e.fitPoints.push_back(*fp);
                  e.knots = data->knotslist;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }

            // For now, connect control points with straight line segments
            // (proper B-spline tessellation could be added later)
            auto* poly = new Polygon(m_zcam, m_defaultLayer);
            poly->setName(elementName("Spline"));
            bool first = true;
            for (const auto& cp : data->controllist) {
                  Vec2d pt(mm(cp->x), mm(cp->y));
                  if (first) {
                        poly->moveTo(pt);
                        first = false;
                        }
                  else {
                        poly->lineTo(pt);
                        }
                  }
            poly->set_lineWidth(0.0);
            poly->set_fill(false);
            poly->update();
            insertElement(poly);
            }
      void addKnot(const DRW_Entity&) override {}
      void addInsert(const DRW_Insert& data) override {
            auto it = m_blocks.find(data.name);
            if (it == m_blocks.end())
                  return;

            // Build a transform for the insert
            double ang = data.angle; // radians
            double sx  = data.xscale;
            double sy  = data.yscale;
            double cx  = mm(data.basePoint.x);
            double cy  = mm(data.basePoint.y);

            for (const auto& e : it->second) {
                  auto apply = [&](DRW_Coord p) -> Vec2d {
                        // Scale
                        p.x *= sx;
                        p.y *= sy;
                        // Rotate
                        double rx = p.x * std::cos(ang) - p.y * std::sin(ang);
                        double ry = p.x * std::sin(ang) + p.y * std::cos(ang);
                        // Translate (in mm)
                        return {mm(rx) + cx, mm(ry) + cy};
                        };

                  switch (e.type) {
                        case BlockEntity::Type::Line: {
                              auto* poly = new Polygon(m_zcam, m_defaultLayer);
                              poly->setName(elementName("BlockLine"));
                              poly->moveTo(apply(e.p1));
                              poly->lineTo(apply(e.p2));
                              poly->set_lineWidth(0.0);
                              poly->set_fill(false);
                              poly->update();
                              insertElement(poly);
                              break;
                              }
                        case BlockEntity::Type::Circle: {
                              double r  = mm(e.radius) * std::abs(sx); // approximate uniform scale
                              auto* ell = new Ellipse(m_zcam, m_defaultLayer);
                              ell->setName(elementName("BlockCircle"));
                              Vec2d c = apply(e.p1);
                              ell->set_pos(QVector3D(c.x(), c.y(), 0.0));
                              ell->set_size(QVector2D(r * 2.0, r * 2.0));
                              ell->update();
                              insertElement(ell);
                              break;
                              }
                        case BlockEntity::Type::Arc: {
                              // Approximate arc with tessellated polygon
                              PainterPath pp;
                              double r  = mm(e.radius);
                              double sa = e.startAng;
                              double ea = e.endAng;
                              if (e.isccw == 0)
                                    std::swap(sa, ea);
                              constexpr double precision = 0.01;
                              int segs =
                                  (std::numbers::pi / std::acos(1.0 - precision / std::max(r, 0.001))) / 4.0 +
                                  0.5;
                              if (segs < 2)
                                    segs = 2;
                              double step = (0.5 * std::numbers::pi) / segs;
                              if (sa > ea)
                                    ea += 2.0 * std::numbers::pi;
                              Vec2d center = apply(e.p1);
                              // Arc radius points need to be transformed too
                              // For simplicity, apply rotation+translation only (no scale on radius)
                              bool firstPt = true;
                              for (double a = sa; a < ea; a += step) {
                                    double px = std::cos(a) * r;
                                    double py = std::sin(a) * r;
                                    // Rotate by insert angle
                                    double rx = px * std::cos(ang) - py * std::sin(ang);
                                    double ry = px * std::sin(ang) + py * std::cos(ang);
                                    Vec2d pt(rx + center.x(), ry + center.y());
                                    if (firstPt) {
                                          pp.moveTo(pt);
                                          firstPt = false;
                                          }
                                    else
                                          pp.lineTo(pt);
                                    }
                                    {
                                    double px = std::cos(ea) * r;
                                    double py = std::sin(ea) * r;
                                    double rx = px * std::cos(ang) - py * std::sin(ang);
                                    double ry = px * std::sin(ang) + py * std::cos(ang);
                                    pp.lineTo(Vec2d(rx + center.x(), ry + center.y()));
                                    }
                              auto* poly = new Polygon(m_zcam, m_defaultLayer);
                              poly->setName(elementName("BlockArc"));
                              poly->setPainterPath(pp);
                              poly->set_lineWidth(0.0);
                              poly->set_fill(false);
                              poly->update();
                              insertElement(poly);
                              break;
                              }
                        case BlockEntity::Type::LWPolyline: {
                              auto* poly = new Polygon(m_zcam, m_defaultLayer);
                              poly->setName(elementName("BlockPoly"));
                              bool firstV = true;
                              for (const auto& v : e.vertices) {
                                    Vec2d pt = apply(DRW_Coord(v.x, v.y, 0));
                                    if (firstV) {
                                          poly->moveTo(pt);
                                          firstV = false;
                                          }
                                    else
                                          poly->lineTo(pt);
                                    }
                              if (e.flags & 1)
                                    poly->lineTo(poly->startPos());
                              poly->set_lineWidth(0.0);
                              poly->set_fill(e.flags & 1);
                              poly->update();
                              insertElement(poly);
                              break;
                              }
                        case BlockEntity::Type::Point: {
                              auto* poly = new Polygon(m_zcam, m_defaultLayer);
                              poly->setName(elementName("BlockPoint"));
                              Vec2d p = apply(e.p1);
                              poly->moveTo(p);
                              poly->lineTo(p);
                              poly->set_lineWidth(0.0);
                              poly->set_fill(false);
                              poly->update();
                              insertElement(poly);
                              break;
                              }
                        default: break; // Other block entity types could be added
                        }
                  }
            }
      void addTrace(const DRW_Trace& data) override {
            // A trace/solid is a filled quadrilateral
            auto* poly = new Polygon(m_zcam, m_defaultLayer);
            poly->setName(elementName("Trace"));
            poly->moveTo(mm2d(data.basePoint));
            poly->lineTo(mm2d(data.secPoint));
            poly->lineTo(mm2d(data.thirdPoint));
            poly->lineTo(mm2d(data.fourPoint));
            poly->lineTo(mm2d(data.basePoint)); // close
            poly->set_lineWidth(0.0);
            poly->set_fill(true);
            poly->update();
            insertElement(poly);
            }
      void add3dFace(const DRW_3Dface& data) override {
            auto* poly = new Polygon(m_zcam, m_defaultLayer);
            poly->setName(elementName("3dFace"));
            poly->moveTo(mm2d(data.basePoint));
            poly->lineTo(mm2d(data.secPoint));
            poly->lineTo(mm2d(data.thirdPoint));
            if (data.invisibleflag & DRW_3Dface::FourthEdge) {
                  // 3 edges only
                  }
            else {
                  poly->lineTo(mm2d(data.fourPoint));
                  }
            poly->lineTo(mm2d(data.basePoint)); // close
            poly->set_lineWidth(0.0);
            poly->set_fill(true);
            poly->update();
            insertElement(poly);
            }
      void addSolid(const DRW_Solid& data) override {
            auto* poly = new Polygon(m_zcam, m_defaultLayer);
            poly->setName(elementName("Solid"));
            poly->moveTo(mm2d(data.basePoint));
            poly->lineTo(mm2d(data.secPoint));
            poly->lineTo(mm2d(data.thirdPoint));
            poly->lineTo(mm2d(data.fourPoint));
            poly->lineTo(mm2d(data.basePoint)); // close
            poly->set_lineWidth(0.0);
            poly->set_fill(true);
            poly->update();
            insertElement(poly);
            }
      void addMText(const DRW_MText& data) override {
            addTextEntity(data.basePoint, data.height, data.text, data.angle);
            }
      void addText(const DRW_Text& data) override {
            addTextEntity(data.basePoint, data.height, data.text, data.angle);
            }
      void addDimAlign(const DRW_DimAligned*) override {}
      void addDimLinear(const DRW_DimLinear*) override {}
      void addDimRadial(const DRW_DimRadial*) override {}
      void addDimDiametric(const DRW_DimDiametric*) override {}
      void addDimAngular(const DRW_DimAngular*) override {}
      void addDimAngular3P(const DRW_DimAngular3p*) override {}
      void addDimOrdinate(const DRW_DimOrdinate*) override {}
      void addLeader(const DRW_Leader*) override {}
      void addHatch(const DRW_Hatch*) override {}
      void addViewport(const DRW_Viewport&) override {}
      void addImage(const DRW_Image*) override {}
      void linkImage(const DRW_ImageDef*) override {}
      void addComment(const char*) override {}
      void addPlotSettings(const DRW_PlotSettings*) override {}
      // Write-side stubs (not used for import)
      void writeHeader(DRW_Header&) override {}
      void writeBlocks() override {}
      void writeBlockRecords() override {}
      void writeEntities() override {}
      void writeLTypes() override {}
      void writeLayers() override {}
      void writeTextstyles() override {}
      void writeVports() override {}
      void writeDimstyles() override {}
      void writeObjects() override {}
      void writeAppId() override {}

    private:
      //---------------------------------------------------------
      //   insertElement
      //    Push a new element into the layer via the undo stack.
      //---------------------------------------------------------
      void insertElement(Element3d* el) {
            auto cmd = std::make_unique<InsertElementCommand>(m_zcam, m_defaultLayer, el, -1);
            cmd->redo();
            m_zcam->project()->pushCommand(std::move(cmd));
            }
      //---------------------------------------------------------
      //   addTextEntity
      //---------------------------------------------------------
      void addTextEntity(const DRW_Coord& pos, double height, const std::string& txt, double /*angle*/) {
            if (txt.empty())
                  return;
            auto* textEl = new Text(m_zcam, m_defaultLayer);
            textEl->setName(elementName(txt.c_str()));
            textEl->set_pos(QVector3D(mm(pos.x), mm(pos.y), 0.0));
            // Height is in drawing units; convert to mm
            double h = mm(height);
            // Convert mm height to logical point size. The Text element
            // internally renders at FONT_SCALE_UP * FONT_SCALE mm per point,
            // so the logical point size is mm / (FONT_SCALE_UP * FONT_SCALE).
            // 1 pt = 0.352778 mm.
            constexpr double mmPerPoint = 0.352778;
            textEl->set_pointSize(h / mmPerPoint);
            textEl->update();
            insertElement(textEl);
            }
      //---------------------------------------------------------
      //   arcToPainterPath
      //    Tessellate a DRW_Arc into a PainterPath.
      //---------------------------------------------------------
      PainterPath arcToPainterPath(const DRW_Arc& data) {
            PainterPath pp;
            double r = mm(data.radious);
            if (r <= 0.0)
                  return pp;

            double sa = data.staangle; // radians
            double ea = data.endangle; // radians

            constexpr double precision = 0.01;
            int segs                   = (std::numbers::pi / std::acos(1.0 - precision / r)) / 4.0 + 0.5;
            if (segs < 2)
                  segs = 2;
            double step = (0.5 * std::numbers::pi) / segs;

            double cx = mm(data.basePoint.x);
            double cy = mm(data.basePoint.y);

            if (sa > ea)
                  ea += 2.0 * std::numbers::pi;

            bool first = true;
            for (double a = sa; a < ea; a += step) {
                  Vec2d pt(std::cos(a) * r + cx, std::sin(a) * r + cy);
                  if (first) {
                        pp.moveTo(pt);
                        first = false;
                        }
                  else
                        pp.lineTo(pt);
                  }
            pp.lineTo(Vec2d(std::cos(ea) * r + cx, std::sin(ea) * r + cy));
            return pp;
            }
      //---------------------------------------------------------
      //   ellipseArcToPainterPath
      //    Tessellate an elliptical arc into a PainterPath.
      //---------------------------------------------------------
      PainterPath ellipseArcToPainterPath(const DRW_Ellipse& data) {
            PainterPath pp;
            double majorR = std::sqrt(data.secPoint.x * data.secPoint.x + data.secPoint.y * data.secPoint.y);
            majorR        = mm(majorR);
            double minorR = majorR * data.ratio;
            double rotation = std::atan2(data.secPoint.y, data.secPoint.x);
            double cx       = mm(data.basePoint.x);
            double cy       = mm(data.basePoint.y);

            double sa = data.staparam;
            double ea = data.endparam;
            if (sa > ea)
                  ea += 2.0 * std::numbers::pi;

            constexpr int segs = 64;
            double step        = (ea - sa) / segs;

            bool first = true;
            for (double t = sa; t < ea; t += step) {
                  double ex = majorR * std::cos(t);
                  double ey = minorR * std::sin(t);
                  // Rotate
                  double rx = ex * std::cos(rotation) - ey * std::sin(rotation);
                  double ry = ex * std::sin(rotation) + ey * std::cos(rotation);
                  Vec2d pt(rx + cx, ry + cy);
                  if (first) {
                        pp.moveTo(pt);
                        first = false;
                        }
                  else
                        pp.lineTo(pt);
                  }
            // Final point
                  {
                  double ex = majorR * std::cos(ea);
                  double ey = minorR * std::sin(ea);
                  double rx = ex * std::cos(rotation) - ey * std::sin(rotation);
                  double ry = ex * std::sin(rotation) + ey * std::cos(rotation);
                  pp.lineTo(Vec2d(rx + cx, ry + cy));
                  }
            return pp;
            }
      //---------------------------------------------------------
      //   arcBulgeTo
      //    Given a bulge value between prev and current point,
      //    tessellate the arc segment and append to the polygon.
      //---------------------------------------------------------
      void arcBulgeTo(Polygon& poly, const Vec2d& prev, const Vec2d& curr, double bulge) {
            // bulge = tan(theta/4) where theta is the included angle
            double dx    = curr.x() - prev.x();
            double dy    = curr.y() - prev.y();
            double chord = std::sqrt(dx * dx + dy * dy);
            if (chord < 1e-10)
                  return;

            double theta = 4.0 * std::atan(std::abs(bulge));
            double r     = chord / (2.0 * std::sin(theta / 2.0));

            // Center of arc
            double midx = (prev.x() + curr.x()) / 2.0;
            double midy = (prev.y() + curr.y()) / 2.0;
            // Direction perpendicular to chord
            double perpX = -dy / chord;
            double perpY = dx / chord;
            // Distance from midpoint to center
            double dist = r * std::cos(theta / 2.0);
            // Sign depends on bulge sign
            double sign = (bulge > 0) ? 1.0 : -1.0;
            double cx   = midx + sign * perpX * dist;
            double cy   = midy + sign * perpY * dist;

            double startAng = std::atan2(prev.y() - cy, prev.x() - cx);
            double endAng   = std::atan2(curr.y() - cy, curr.x() - cx);

            // Ensure correct direction
            if (bulge > 0) {
                  // CCW
                  if (endAng < startAng)
                        endAng += 2.0 * std::numbers::pi;
                  }
            else {
                  // CW
                  if (endAng > startAng)
                        endAng -= 2.0 * std::numbers::pi;
                  }

            constexpr double precision = 0.01;
            int segs =
                (std::abs(endAng - startAng) / (std::acos(1.0 - precision / std::max(r, 0.001)))) + 0.5;
            if (segs < 2)
                  segs = 2;
            double step = (endAng - startAng) / segs;

            for (int i = 1; i <= segs; ++i) {
                  double a = startAng + step * i;
                  poly.lineTo(Vec2d(std::cos(a) * r + cx, std::sin(a) * r + cy));
                  }
            }
      //---------------------------------------------------------
      //   unitToMm
      //    Convert DRW_Header::Units $INSUNITS value to mm scale.
      //---------------------------------------------------------
      static double unitToMm(int unit) {
            switch (unit) {
                  case 0: return 1.0;                     // Unspecified → assume mm
                  case 1: return 25.4;                    // Inch
                  case 2: return 25.4 * 12;               // Foot
                  case 3: return 1609344.0;               // Mile
                  case 4: return 1.0;                     // Millimeter
                  case 5: return 10.0;                    // Centimeter
                  case 6: return 1000.0;                  // Meter
                  case 7: return 1000000.0;               // Kilometer
                  case 8: return 25.4 / 1000000.0;        // Microinch
                  case 9: return 25.4 / 1000.0;           // Mil
                  case 10: return 25.4 * 36;              // Yard
                  case 11: return 0.0000001;              // Angstrom
                  case 12: return 0.000001;               // Nanometer
                  case 13: return 0.001;                  // Micron
                  case 14: return 100.0;                  // Decimeter
                  case 15: return 10000.0;                // Decameter
                  case 16: return 100000.0;               // Hectometer
                  case 17: return 1000000000.0;           // Gigameter
                  case 18: return 149597870690.0;         // Astronomical Unit
                  case 19: return 9454254955500000000.0;  // Light Year
                  case 20: return 30856774879000000000.0; // Parsec
                  default: return 1.0;
                  }
            }
      };

//---------------------------------------------------------
//   DxfImport::import
//    Public entry point. Creates a Layer for the DXF file,
//    reads all entities via libdxfrw, and links a LaserLayer.
//---------------------------------------------------------

bool DxfImport::import(ZCam* zcam, const QString& path) {
      Debug("import DXF: {}", path.toUtf8().data());

      if (!zcam->project() || !zcam->project()->cad()) {
            Critical("importDXF: no project or CAD element");
            return false;
            }

      QFileInfo fi(path);

      Cad* cad = zcam->project()->cad();

      // Create a new Layer for this DXF file
      auto* layer = new Group(zcam, cad);
      layer->setName(fi.baseName());
      layer->setExpanded(true);

            {
            auto cmd = std::make_unique<InsertElementCommand>(zcam, cad, layer, -1);
            cmd->redo();
            zcam->project()->pushCommand(std::move(cmd));
            }

      // Create a LaserLayer linked to this Layer
      Fixture* fixture = zcam->project()->fixture();
      if (!fixture) {
            if (!zcam->project()->fixtures().empty())
                  fixture = zcam->project()->fixtures().at(0);
            }
      if (fixture) {
            auto* ll = new Recipe(zcam, fixture);
            ll->setName(QStringLiteral("LL-%1").arg(fi.baseName()));
            ll->setExpanded(false);
            layer->set_laserLayer(ll);
            ll->set_kerfOffset(-0.05);
            auto cmd = std::make_unique<InsertElementCommand>(zcam, fixture, ll, -1);
            cmd->redo();
            zcam->project()->pushCommand(std::move(cmd));
            }

      // Read the DXF file using libdxfrw
      DxfReaderInterface reader(zcam, layer, fi.baseName());
      dxfRW dxf(path.toUtf8().constData());
      bool ok = dxf.read(&reader, false);

      zcam->project()->markDirty();
      zcam->setCamDirty(true);

      if (!ok)
            Critical("DXF import failed: {}", path.toUtf8().data());
      else
            Debug("DXF import completed: {}", path.toUtf8().data());

      return ok;
      }

//=========================================================
//   DxfBBoxCollector
//    A minimal DRW_Interface implementation that only
//    collects the bounding box of all entities in a DXF
//    file.  It mirrors the unit-scale logic of
//    DxfReaderInterface so the bounding box is in mm.
//=========================================================

class DxfBBoxCollector final : public DRW_Interface {
      double m_unitScale {1.0};
      double m_dxfScale  {72.0};
      double m_minX {std::numeric_limits<double>::max()};
      double m_minY {std::numeric_limits<double>::max()};
      double m_maxX {std::numeric_limits<double>::lowest()};
      double m_maxY {std::numeric_limits<double>::lowest()};
      // Block support
      struct BlockEntity {
            enum class Type { Line, Arc, Circle, LWPolyline, Ellipse, Point };
            Type type;
            DRW_Coord p1, p2;
            double radius {0.0};
            double startAng {0.0};
            double endAng {0.0};
            double ratio {0.0};
            double staparam {0.0};
            double endparam {0.0};
            int isccw {1};
            std::vector<DRW_Vertex2D> vertices;
            int flags {0};
            };
      std::unordered_map<std::string, std::vector<BlockEntity>> m_blocks;
      std::string m_currentBlockName;
      bool m_inBlock {false};

      double mm(double v) const { return v * m_unitScale; }

      void expand(double x, double y) {
            if (x < m_minX) m_minX = x;
            if (x > m_maxX) m_maxX = x;
            if (y < m_minY) m_minY = y;
            if (y > m_maxY) m_maxY = y;
            }

      static double unitToMm(int unit) {
            switch (unit) {
                  case 0:  return 1.0;
                  case 1:  return 25.4;
                  case 2:  return 25.4 * 12;
                  case 3:  return 1609344.0;
                  case 4:  return 1.0;
                  case 5:  return 10.0;
                  case 6:  return 1000.0;
                  case 7:  return 1000000.0;
                  case 8:  return 25.4 / 1000000.0;
                  case 9:  return 25.4 / 1000.0;
                  case 10: return 25.4 * 36;
                  default: return 1.0;
                  }
            }

public:
      explicit DxfBBoxCollector(double dxfScale) : m_dxfScale(dxfScale > 0.0 ? dxfScale : 72.0) {}

      QRectF result() const {
            if (m_minX > m_maxX || m_minY > m_maxY)
                  return {};
            return QRectF(m_minX, m_minY, m_maxX - m_minX, m_maxY - m_minY);
            }

      void addHeader(const DRW_Header* data) override {
            auto it = data->vars.find("$INSUNITS");
            if (it != data->vars.end() && it->second->type() == DRW_Variant::INTEGER) {
                  int unit = it->second->content.i;
                  if (unit == 0)
                        m_unitScale = 1.0 / m_dxfScale;
                  else
                        m_unitScale = unitToMm(unit);
                  }
            else {
                  auto mit = data->vars.find("$MEASUREMENT");
                  if (mit != data->vars.end() && mit->second->type() == DRW_Variant::INTEGER) {
                        if (mit->second->content.i == 0)
                              m_unitScale = 25.4;
                        }
                  }
            }
      void addLType(const DRW_LType&) override {}
      void addLayer(const DRW_Layer&) override {}
      void addDimStyle(const DRW_Dimstyle&) override {}
      void addVport(const DRW_Vport&) override {}
      void addTextStyle(const DRW_Textstyle&) override {}
      void addAppId(const DRW_AppId&) override {}
      void addBlock(const DRW_Block& data) override {
            m_currentBlockName = data.name;
            m_blocks[m_currentBlockName].clear();
            m_inBlock = true;
            }
      void setBlock(int) override {}
      void endBlock() override { m_inBlock = false; m_currentBlockName.clear(); }

      void addPoint(const DRW_Point& data) override {
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::Point; e.p1 = data.basePoint;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            expand(mm(data.basePoint.x), mm(data.basePoint.y));
            }
      void addLine(const DRW_Line& data) override {
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::Line; e.p1 = data.basePoint; e.p2 = data.secPoint;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            expand(mm(data.basePoint.x), mm(data.basePoint.y));
            expand(mm(data.secPoint.x), mm(data.secPoint.y));
            }
      void addRay(const DRW_Ray&) override {}
      void addXline(const DRW_Xline&) override {}
      void addArc(const DRW_Arc& data) override {
            double r = mm(data.radious);
            if (r <= 0.0) return;
            double cx = mm(data.basePoint.x);
            double cy = mm(data.basePoint.y);
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::Arc; e.p1 = data.basePoint;
                  e.radius = data.radious; e.startAng = data.staangle; e.endAng = data.endangle; e.isccw = data.isccw;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            // For arcs, expand by the actual arc extent, not the full
            // circle bbox.  Many DXF files have arcs with very large radii
            // but small sweep angles, where the full circle bbox would be
            // far larger than the actual drawing.
            double sa = data.staangle;
            double ea = data.endangle;
            if (data.isccw == 0)
                  std::swap(sa, ea);
            if (sa > ea)
                  ea += 2.0 * std::numbers::pi;
            // Start and end points of the arc
            expand(cx + r * std::cos(sa), cy + r * std::sin(sa));
            expand(cx + r * std::cos(ea), cy + r * std::sin(ea));
            // Also expand by any axis-crossing points within the arc sweep
            // (0, 90, 180, 270 degrees) where the arc reaches its extremes.
            for (double a : {0.0, std::numbers::pi / 2, std::numbers::pi, 3.0 * std::numbers::pi / 2}) {
                  double na = a;
                  while (na < sa)
                        na += 2.0 * std::numbers::pi;
                  while (na > sa + 2.0 * std::numbers::pi + 1e-10)
                        na -= 2.0 * std::numbers::pi;
                  if (na >= sa - 1e-10 && na <= ea + 1e-10)
                        expand(cx + r * std::cos(na), cy + r * std::sin(na));
                  }
            }
      void addCircle(const DRW_Circle& data) override {
            double r = mm(data.radious);
            if (r <= 0.0) return;
            double cx = mm(data.basePoint.x);
            double cy = mm(data.basePoint.y);
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::Circle; e.p1 = data.basePoint; e.radius = data.radious;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            expand(cx - r, cy - r);
            expand(cx + r, cy + r);
            }
      void addEllipse(const DRW_Ellipse& data) override {
            double majorR = std::sqrt(data.secPoint.x * data.secPoint.x + data.secPoint.y * data.secPoint.y);
            majorR = mm(majorR);
            double cx = mm(data.basePoint.x);
            double cy = mm(data.basePoint.y);
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::Ellipse; e.p1 = data.basePoint; e.p2 = data.secPoint;
                  e.ratio = data.ratio; e.staparam = data.staparam; e.endparam = data.endparam; e.isccw = data.isccw;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            expand(cx - majorR, cy - majorR);
            expand(cx + majorR, cy + majorR);
            }
      void addLWPolyline(const DRW_LWPolyline& data) override {
            if (data.vertlist.empty()) return;
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::LWPolyline;
                  for (const auto& v : data.vertlist) e.vertices.push_back(*v);
                  e.flags = data.flags;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            for (const auto& v : data.vertlist)
                  expand(mm(v->x), mm(v->y));
            }
      void addPolyline(const DRW_Polyline& data) override {
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::LWPolyline;
                  for (const auto& v : data.vertlist) e.vertices.push_back(DRW_Vertex2D(v->basePoint.x, v->basePoint.y, v->bulge));
                  e.flags = data.flags;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            for (const auto& v : data.vertlist)
                  expand(mm(v->basePoint.x), mm(v->basePoint.y));
            }
      void addSpline(const DRW_Spline* data) override {
            if (!data) return;
            if (m_inBlock) {
                  // Splines in blocks are not expanded; they would inflate
                  // the bbox for blocks that may never be inserted.
                  return;
                  }
            // Prefer fit points (which lie on the curve) over control points
            // (which form the convex hull and can be far outside the curve).
            if (!data->fitlist.empty()) {
                  for (const auto& fp : data->fitlist)
                        expand(mm(fp->x), mm(fp->y));
                  }
            else if (!data->controllist.empty()) {
                  for (const auto& cp : data->controllist)
                        expand(mm(cp->x), mm(cp->y));
                  }
            }
      void addKnot(const DRW_Entity&) override {}
      void addInsert(const DRW_Insert& data) override {
            auto it = m_blocks.find(data.name);
            if (it == m_blocks.end()) return;
            double ang = data.angle;
            double sx = data.xscale;
            double sy = data.yscale;
            double cx = mm(data.basePoint.x);
            double cy = mm(data.basePoint.y);
            double cosA = std::cos(ang);
            double sinA = std::sin(ang);
            auto apply = [&](const DRW_Coord& p) {
                  double px = p.x * sx;
                  double py = p.y * sy;
                  double rx = px * cosA - py * sinA;
                  double ry = px * sinA + py * cosA;
                  return std::make_pair(mm(rx) + cx, mm(ry) + cy);
                  };
            for (const auto& e : it->second) {
                  switch (e.type) {
                        case BlockEntity::Type::Line: {
                              auto p1 = apply(e.p1);
                              auto p2 = apply(e.p2);
                              expand(p1.first, p1.second);
                              expand(p2.first, p2.second);
                              break;
                              }
                        case BlockEntity::Type::Circle: {
                              double r = mm(e.radius) * std::abs(sx);
                              auto c = apply(e.p1);
                              expand(c.first - r, c.second - r);
                              expand(c.first + r, c.second + r);
                              break;
                              }
                        case BlockEntity::Type::Arc: {
                              // Expand by actual arc extent, not full circle bbox.
                              double r = mm(e.radius);
                              auto c = apply(e.p1);
                              double sa = e.startAng;
                              double ea = e.endAng;
                              if (e.isccw == 0)
                                    std::swap(sa, ea);
                              if (sa > ea)
                                    ea += 2.0 * std::numbers::pi;
                              // Endpoints
                              expand(c.first + r * std::cos(sa), c.second + r * std::sin(sa));
                              expand(c.first + r * std::cos(ea), c.second + r * std::sin(ea));
                              // Axis-crossing points within sweep
                              for (double a : {0.0, std::numbers::pi / 2, std::numbers::pi, 3.0 * std::numbers::pi / 2}) {
                                    double na = a;
                                    while (na < sa) na += 2.0 * std::numbers::pi;
                                    while (na > sa + 2.0 * std::numbers::pi + 1e-10) na -= 2.0 * std::numbers::pi;
                                    if (na >= sa - 1e-10 && na <= ea + 1e-10)
                                          expand(c.first + r * std::cos(na), c.second + r * std::sin(na));
                                    }
                              break;
                              }
                        case BlockEntity::Type::LWPolyline: {
                              for (const auto& v : e.vertices) {
                                    auto p = apply(DRW_Coord(v.x, v.y, 0));
                                    expand(p.first, p.second);
                                    }
                              break;
                              }
                        case BlockEntity::Type::Point: {
                              auto p = apply(e.p1);
                              expand(p.first, p.second);
                              break;
                              }
                        case BlockEntity::Type::Ellipse: {
                              double majorR = std::sqrt(e.p2.x * e.p2.x + e.p2.y * e.p2.y);
                              majorR = mm(majorR);
                              auto c = apply(e.p1);
                              expand(c.first - majorR, c.second - majorR);
                              expand(c.first + majorR, c.second + majorR);
                              break;
                              }
                        }
                  }
            }
      void addTrace(const DRW_Trace& data) override {
            expand(mm(data.basePoint.x), mm(data.basePoint.y));
            expand(mm(data.secPoint.x), mm(data.secPoint.y));
            expand(mm(data.thirdPoint.x), mm(data.thirdPoint.y));
            expand(mm(data.fourPoint.x), mm(data.fourPoint.y));
            }
      void add3dFace(const DRW_3Dface& data) override {
            expand(mm(data.basePoint.x), mm(data.basePoint.y));
            expand(mm(data.secPoint.x), mm(data.secPoint.y));
            expand(mm(data.thirdPoint.x), mm(data.thirdPoint.y));
            if (!(data.invisibleflag & DRW_3Dface::FourthEdge))
                  expand(mm(data.fourPoint.x), mm(data.fourPoint.y));
            }
      void addSolid(const DRW_Solid& data) override {
            expand(mm(data.basePoint.x), mm(data.basePoint.y));
            expand(mm(data.secPoint.x), mm(data.secPoint.y));
            expand(mm(data.thirdPoint.x), mm(data.thirdPoint.y));
            expand(mm(data.fourPoint.x), mm(data.fourPoint.y));
            }
      void addMText(const DRW_MText&) override {}
      void addText(const DRW_Text&) override {}
      void addDimAlign(const DRW_DimAligned*) override {}
      void addDimLinear(const DRW_DimLinear*) override {}
      void addDimRadial(const DRW_DimRadial*) override {}
      void addDimDiametric(const DRW_DimDiametric*) override {}
      void addDimAngular(const DRW_DimAngular*) override {}
      void addDimAngular3P(const DRW_DimAngular3p*) override {}
      void addDimOrdinate(const DRW_DimOrdinate*) override {}
      void addLeader(const DRW_Leader*) override {}
      void addHatch(const DRW_Hatch*) override {}
      void addViewport(const DRW_Viewport&) override {}
      void addImage(const DRW_Image*) override {}
      void linkImage(const DRW_ImageDef*) override {}
      void addComment(const char*) override {}
      void addPlotSettings(const DRW_PlotSettings*) override {}
      void writeHeader(DRW_Header&) override {}
      void writeBlocks() override {}
      void writeBlockRecords() override {}
      void writeEntities() override {}
      void writeLTypes() override {}
      void writeLayers() override {}
      void writeTextstyles() override {}
      void writeVports() override {}
      void writeDimstyles() override {}
      void writeObjects() override {}
      void writeAppId() override {}
      };

//---------------------------------------------------------
//   DxfImport::boundingBox
//    Compute the bounding box of all entities in a DXF file,
//    in millimetres.  Returns an empty QRectF on failure.
//---------------------------------------------------------

QRectF DxfImport::boundingBox(ZCam* zcam, const QString& path) {
      double dxfScale = zcam->config() ? zcam->config()->dxfScale() : 72.0;
      DxfBBoxCollector collector(dxfScale);
      dxfRW dxf(path.toUtf8().constData());
      if (!dxf.read(&collector, false)) {
            Warning("DxfImport::boundingBox: failed to read DXF: {}", path.toUtf8().constData());
            return {};
            }
      return collector.result();
      }

//---------------------------------------------------------
//   DxfImport::importAt
//    Import a DXF file and offset all created elements so
//    that the bounding box's bottom-left corner is at (x, y).
//---------------------------------------------------------

bool DxfImport::importAt(ZCam* zcam, const QString& path, double x, double y) {
      QRectF bbox = boundingBox(zcam, path);
      if (bbox.isNull() || bbox.isEmpty())
            return import(zcam, path);

      // Import normally, then shift the layer position.
      bool ok = import(zcam, path);
      if (!ok)
            return false;

      // The import creates a new layer as the last child of CAD.
      // Offset that layer so the DXF bbox bottom-left lands at (x, y).
      Cad* cad = zcam->project()->cad();
      if (!cad || cad->children().isEmpty())
            return true;
      // The import always inserts at the end (-1), so the last child
      // is the layer we just created.
      auto& kids = cad->children();
      Element* lastChild = kids.last();
      if (!lastChild)
            return true;
      double offX = x - bbox.left();
      double offY = y - bbox.top();
      // The DXF import creates a Group (which is an Element3d) so it has set_pos.
      auto* lastGroup = qobject_cast<Group*>(lastChild);
      if (lastGroup)
            lastGroup->set_pos(QVector3D(offX, offY, 0));
      return true;
      }