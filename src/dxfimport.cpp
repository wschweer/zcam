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
#include "dxftess.h"

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
#include "scriptengine.h"
#include "treemodel.h"

#include <QFileInfo>
#include <QString>
#include <QVector3D>
#include <format>
#include <cmath>
#include <limits>
#include <numbers>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <functional>
#include "libdxfrw.h"
#include "drw_interface.h"
#include "drw_base.h"
#include "drw_entities.h"
#include "drw_header.h"
#include "config.h"
#include "laser_mop.h"

//---------------------------------------------------------
//   DxfReaderInterface
//    Implements DRW_Interface to receive callbacks from
//    libdxfrw while reading a DXF file.  The object
//    hierarchies found in the file are mapped onto the
//    project tree:
//      - every DXF layer becomes a Group (nested below the
//        import layer) that collects all entities drawn on
//        that layer,
//      - every referenced block becomes a Group; each INSERT
//        of the block adds a transformed child Group below the
//        block group.
//---------------------------------------------------------

class DxfReaderInterface final : public DRW_Interface
      {
      ZCam* m_zcam;
      Group* m_defaultLayer;          ///< the single root layer created for this import
      Group* m_parent {nullptr};      ///< current insertion parent inside the import layer
      Group* m_activeBlock {nullptr}; ///< block definition group of the insert being expanded
      int m_parentDepth {0};          ///< recursion guard for nested block expansion
      double m_unitScale;             ///< conversion factor to mm
      QString m_baseName;             ///< file base name for element naming
      std::unordered_map<std::string, Group*>
          m_dxfLayerMap; ///< dxf layer name -> Group inside the import layer
      std::unordered_map<Group*, std::unordered_map<int, Group*>>
          m_dxfColorMap; ///< (layer group, ACI color) -> color sub-group
      std::unordered_map<std::string, Group*>
          m_blockGroupMap;                              ///< block name -> Group inside the import layer
      std::unordered_set<const Group*> m_blockGroupSet; ///< fast O(1) lookup for isBlockGroup()
      // Block support: collect entities per block, then replicate on INSERT
      struct BlockEntity {
            enum class Type {
                  Line,
                  Arc,
                  Circle,
                  LWPolyline,
                  Ellipse,
                  Spline,
                  Point,
                  Text,
                  Solid,
                  Face3d,
                  Insert
                  };
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
            std::string layer; ///< dxf layer name of the source entity
            int color {256};   ///< dxf entity color (ACI code 62), 256=BYLAYER, 0=BYBLOCK
            // Insert (nested block reference)
            std::string block; ///< referenced block name
            double xscale {1.0};
            double yscale {1.0};
            };
      std::unordered_map<std::string, std::vector<BlockEntity>> m_blocks;
      std::string m_currentBlockName;
      bool m_inBlock {false};
      int m_insertCounter {0};
      // Line buffer: collect LINE entities and merge connected ones
      // into shared polylines instead of creating one Polygon per line.
      struct LineSeg {
            Vec2d p1, p2;
            std::string layer;
            int color {256}; ///< dxf entity color (ACI code 62), 256=BYLAYER, 0=BYBLOCK
            };
      std::vector<LineSeg> m_lineBuffer;
      /// DXF layer definitions collected from the LAYER table.
      /// Maps layer name → ACI color (code 62).  Used to resolve
      /// entities whose color is BYLAYER (256).
      std::unordered_map<std::string, int> m_dxfLayerColors;

      int m_entityCounts[12] {}; ///< per-type entity counts for logging
      ///
      /// Collect all (layer-group, color) sub-groups created during the
      /// import.  Each entry maps a Group pointer to the resolved ACI
      /// colour used for naming and LaserMop assignment.
      ///
      struct ColorGroupInfo {
            Group* group;
            int aci;
            };
      std::vector<ColorGroupInfo> m_colorGroups;

      static constexpr int kMaxInsertDepth            = 16; ///< recursion limit for nested INSERTs
      static constexpr std::string_view kBlockPrefix  = "\xe2\x96\xa0 "; ///< "■ " marks block groups
      static constexpr std::string_view kInsertPrefix = "\xe2\x86\xb3 "; ///< "↳ " marks block instances

    public:
      DxfReaderInterface(ZCam* zcam, Group* layer, const QString& baseName)
          : m_zcam(zcam), m_defaultLayer(layer), m_parent(layer), m_unitScale(1.0), m_baseName(baseName) {}
      double unitScale() const { return m_unitScale; }
      void setUnitScale(double s) { m_unitScale = s; }
      double mm(double v) const { return v * m_unitScale; }
      Vec2d mm2d(const DRW_Coord& c) const { return {mm(c.x), mm(c.y)}; }
      /// Return all colour sub-groups created during the import.
      const std::vector<ColorGroupInfo>& colorGroups() const { return m_colorGroups; }
      /// Resolved element color for the current entity:
      /// if the entity's color is 256 (BYLAYER), look up the
      /// layer color from m_dxfLayerColors; if 0 (BYBLOCK) and
      /// the layer color is unknown, use 7 (white) as default.
      /// BYBLOCK (0) is kept as a distinct colour so border/frame
      /// entities are not merged with white mark entities.
      int resolveColor(int entityColor, const std::string& layer) const {
            if (entityColor == 256) { // BYLAYER
                  auto it = m_dxfLayerColors.find(layer);
                  if (it != m_dxfLayerColors.end())
                        return it->second;
                  return 7; // default white
                  }
            // BYBLOCK (0) is kept as-is so border entities form
            // their own colour group, distinct from white (7).
            return entityColor;
            }
      /// ACI (AutoCAD Color Index) → QColor.
      /// Only the standard colours 1–8 and a few high-index ones
      /// are mapped explicitly; everything else falls back to
      /// white (7).
      static QColor aciToQColor(int aci) {
            // clang-format off
            switch (aci) {
                  case 0:  return QColor(255, 255, 255);  // BYBLOCK → white
                  case 1:  return QColor(255,   0,   0);  // red
                  case 2:  return QColor(255, 255,   0);  // yellow
                  case 3:  return QColor(  0, 255,   0);  // green
                  case 4:  return QColor(  0, 255, 255);  // cyan
                  case 5:  return QColor(  0,   0, 255);  // blue
                  case 6:  return QColor(255,   0, 255);  // magenta
                  case 7:  return QColor(255, 255, 255);  // white / black
                  case 8:  return QColor(128, 128, 128);  // grey
                  case 9:  return QColor(192, 192, 192);  // light grey
                  default: return QColor(255, 255, 255);  // fallback
                        }
            // clang-format on
            }
      /// Human-readable label for a resolved ACI colour, used as
      /// the sub-group name inside a DXF layer group.
      static QString aciLabel(int aci) {
            switch (aci) {
                  case 0: return QStringLiteral("BYBLOCK");
                  case 1: return QStringLiteral("Red");
                  case 2: return QStringLiteral("Yellow");
                  case 3: return QStringLiteral("Green");
                  case 4: return QStringLiteral("Cyan");
                  case 5: return QStringLiteral("Blue");
                  case 6: return QStringLiteral("Magenta");
                  case 7: return QStringLiteral("White");
                  case 8: return QStringLiteral("Grey");
                  case 9: return QStringLiteral("LightGrey");
                  default: return QStringLiteral("Color-%1").arg(aci);
                  }
            }
      int circleResolution() const {
            if (!m_zcam || !m_zcam->config())
                  return 360;
            return std::clamp(m_zcam->config()->dxfCircleResolution(), 8, 2048);
            }
      int curveResolution() const {
            if (!m_zcam || !m_zcam->config())
                  return 100;
            return std::clamp(m_zcam->config()->dxfCurveResolution(), 4, 1024);
            }
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
      void addLayer(const DRW_Layer& data) override { m_dxfLayerColors[data.name] = data.color; }
      void addDimStyle(const DRW_Dimstyle&) override {}
      void addVport(const DRW_Vport&) override {}
      void addTextStyle(const DRW_Textstyle&) override {}
      void addAppId(const DRW_AppId&) override {}
      void addBlock(const DRW_Block& data) override {
            Debug("DXF BLOCK begin: '{}'", data.name);
            m_currentBlockName = data.name;
            m_blocks[m_currentBlockName].clear();
            m_inBlock = true;
            }
      void setBlock(int /*handle*/) override {
            // DWG: switch to a previously defined block
            }
      void endBlock() override {
            Debug("DXF BLOCK end: '{}' ({} entities collected)", m_currentBlockName,
                m_blocks[m_currentBlockName].size());
            m_inBlock = false;
            m_currentBlockName.clear();
            }
      void addPoint(const DRW_Point& data) override {
            ++m_entityCounts[8];
            if (m_inBlock) {
                  BlockEntity e;
                  e.type  = BlockEntity::Type::Point;
                  e.p1    = data.basePoint;
                  e.layer = data.layer;
                  e.color = data.color;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            // A single point → small marker line (zero-length not useful)
            // Create a tiny polygon point marker
            auto* poly = new Polygon(m_zcam, m_parent);
            poly->setName(elementName("Point"));
            Vec2d p = mm2d(data.basePoint);
            poly->moveTo(p);
            poly->lineTo(p);
            poly->set_lineWidth(0.0);
            poly->set_fill(false);
            int rc = resolveColor(data.color, data.layer);
            poly->setColor(aciToQColor(rc));
            poly->update();
            insertElement(poly, entityParent(data.layer, rc));
            }
      void addLine(const DRW_Line& data) override {
            ++m_entityCounts[0];
            if (m_inBlock) {
                  BlockEntity e;
                  e.type  = BlockEntity::Type::Line;
                  e.p1    = data.basePoint;
                  e.p2    = data.secPoint;
                  e.layer = data.layer;
                  e.color = data.color;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            // Buffer the line for later merging into polylines
            m_lineBuffer.push_back({mm2d(data.basePoint), mm2d(data.secPoint), data.layer, data.color});
            }
      void addRay(const DRW_Ray&) override {}
      void addXline(const DRW_Xline&) override {}
      void addArc(const DRW_Arc& data) override {
            ++m_entityCounts[1];
            if (m_inBlock) {
                  BlockEntity e;
                  e.type     = BlockEntity::Type::Arc;
                  e.p1       = data.basePoint;
                  e.radius   = data.radious;
                  e.startAng = data.staangle;
                  e.endAng   = data.endangle;
                  e.isccw    = data.isccw;
                  e.layer    = data.layer;
                  e.color    = data.color;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            auto pp = arcToPainterPath(data);
            if (pp.empty())
                  return;
            Debug("DXF Arc: {} path elements (curves: {})", pp.size(),
                std::count_if(pp.begin(), pp.end(), [](const PPElement& e) {
                      return e.type == PPType::CurveTo || e.type == PPType::CurveToData1 ||
                             e.type == PPType::CurveToData2;
                      }));
            auto* poly = new Polygon(m_zcam, m_parent);
            poly->setName(elementName("Arc"));
            poly->setPainterPath(pp);
            poly->set_lineWidth(0.0);
            poly->set_fill(false);
            int rc = resolveColor(data.color, data.layer);
            poly->setColor(aciToQColor(rc));
            poly->update();
            insertElement(poly, entityParent(data.layer, rc));
            }
      void addCircle(const DRW_Circle& data) override {
            ++m_entityCounts[2];
            if (m_inBlock) {
                  BlockEntity e;
                  e.type   = BlockEntity::Type::Circle;
                  e.p1     = data.basePoint;
                  e.radius = data.radious;
                  e.layer  = data.layer;
                  e.color  = data.color;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            double r  = mm(data.radious);
            auto* ell = new Ellipse(m_zcam, m_parent);
            ell->setName(elementName("Circle"));
            ell->set_pos(QVector3D(mm(data.basePoint.x), mm(data.basePoint.y), 0.0));
            ell->set_size(QVector2D(r * 2.0, r * 2.0));
            int rc = resolveColor(data.color, data.layer);
            ell->setColor(aciToQColor(rc));
            ell->update();
            insertElement(ell, entityParent(data.layer, rc));
            }
      void addEllipse(const DRW_Ellipse& data) override {
            ++m_entityCounts[3];
            if (m_inBlock) {
                  BlockEntity e;
                  e.type     = BlockEntity::Type::Ellipse;
                  e.p1       = data.basePoint;
                  e.p2       = data.secPoint;
                  e.ratio    = data.ratio;
                  e.staparam = data.staparam;
                  e.endparam = data.endparam;
                  e.isccw    = data.isccw;
                  e.layer    = data.layer;
                  e.color    = data.color;
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
            int rc    = resolveColor(data.color, data.layer);

            if (full) {
                  auto* ell = new Ellipse(m_zcam, m_parent);
                  ell->setName(elementName("Ellipse"));
                  ell->set_pos(QVector3D(mm(data.basePoint.x), mm(data.basePoint.y), 0.0));
                  ell->set_size(QVector2D(majorR * 2.0, minorR * 2.0));
                  ell->set_rot(QVector3D(0.0, 0.0, rotDeg));
                  ell->setColor(aciToQColor(rc));
                  ell->update();
                  insertElement(ell, entityParent(data.layer, rc));
                  }
            else {
                  // Elliptical arc → tessellate into a polygon
                  auto pp = ellipseArcToPainterPath(data);
                  if (pp.empty())
                        return;
                  Debug("DXF EllipseArc: {} path elements (curves: {})", pp.size(),
                      std::count_if(pp.begin(), pp.end(), [](const PPElement& e) {
                            return e.type == PPType::CurveTo || e.type == PPType::CurveToData1 ||
                                   e.type == PPType::CurveToData2;
                            }));
                  auto* poly = new Polygon(m_zcam, m_parent);
                  poly->setName(elementName("EllipseArc"));
                  poly->setPainterPath(pp);
                  poly->set_lineWidth(0.0);
                  poly->set_fill(false);
                  poly->setColor(aciToQColor(rc));
                  poly->update();
                  insertElement(poly, entityParent(data.layer, rc));
                  }
            }
      void addLWPolyline(const DRW_LWPolyline& data) override {
            if (data.vertlist.empty())
                  return;
            ++m_entityCounts[4];
            if (m_inBlock) {
                  BlockEntity e;
                  e.type = BlockEntity::Type::LWPolyline;
                  for (const auto& v : data.vertlist)
                        e.vertices.push_back(*v);
                  e.flags = data.flags;
                  e.layer = data.layer;
                  e.color = data.color;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }

            auto* poly = new Polygon(m_zcam, m_parent);
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
            int rc = resolveColor(data.color, data.layer);
            poly->setColor(aciToQColor(rc));
            poly->update();
            insertElement(poly, entityParent(data.layer, rc));
            }
      void addPolyline(const DRW_Polyline& data) override {
            if (data.vertlist.empty())
                  return;
            ++m_entityCounts[5];
            if (m_inBlock) {
                  BlockEntity e;
                  e.type = BlockEntity::Type::LWPolyline;
                  for (const auto& v : data.vertlist)
                        e.vertices.push_back(DRW_Vertex2D(v->basePoint.x, v->basePoint.y, v->bulge));
                  e.flags = data.flags;
                  e.layer = data.layer;
                  e.color = data.color;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            auto* poly = new Polygon(m_zcam, m_parent);
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
            int rc = resolveColor(data.color, data.layer);
            poly->setColor(aciToQColor(rc));
            poly->update();
            insertElement(poly, entityParent(data.layer, rc));
            }
      void addSpline(const DRW_Spline* data) override {
            if (!data || data->controllist.empty())
                  return;
            ++m_entityCounts[6];
            if (m_inBlock) {
                  BlockEntity e;
                  e.type   = BlockEntity::Type::Spline;
                  e.degree = data->degree;
                  e.layer  = data->layer;
                  e.color  = data->color;
                  for (const auto& cp : data->controllist)
                        e.controlPoints.push_back(*cp);
                  for (const auto& fp : data->fitlist)
                        e.fitPoints.push_back(*fp);
                  e.knots = data->knotslist;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }

            std::vector<DRW_Coord> controls;
            controls.reserve(data->controllist.size());
            for (const auto& cp : data->controllist)
                  controls.push_back(*cp);
            std::vector<double> knots = data->knotslist;

            // Convert control points to Vec2d and apply unit scale
            std::vector<Vec2d> ctrlPts;
            ctrlPts.reserve(controls.size());
            for (const auto& c : controls)
                  ctrlPts.emplace_back(mm(c.x), mm(c.y));

            int deg = data->degree;
            // Try exact Bezier conversion for degree <= 3
            auto beziers = DxfTess::bsplineToCubicBeziers(deg, ctrlPts, knots);

            auto* poly = new Polygon(m_zcam, m_parent);
            poly->setName(elementName("Spline"));

            if (!beziers.empty()) {
                  // Use exact cubic Bezier segments
                  Debug("DXF Spline: degree {} → {} cubic Bezier segments", deg, beziers.size());
                  bool firstSeg = true;
                  for (const auto& bz : beziers) {
                        if (firstSeg) {
                              poly->moveTo(bz.p0);
                              firstSeg = false;
                              }
                        poly->cubicTo(bz.p1, bz.p2, bz.p3);
                        }
                  }
            else {
                  // Fallback: tessellate for degree > 3
                  auto pts = DxfTess::evaluateSpline(deg, controls, knots, curveResolution());
                  if (pts.empty()) {
                        delete poly;
                        return;
                        }
                  bool firstSeg = true;
                  for (const auto& pt : pts) {
                        Vec2d p(mm(pt.x()), mm(pt.y()));
                        if (firstSeg) {
                              poly->moveTo(p);
                              firstSeg = false;
                              }
                        else {
                              poly->lineTo(p);
                              }
                        }
                  }
            poly->set_lineWidth(0.0);
            poly->set_fill(false);
            int rc = resolveColor(data->color, data->layer);
            poly->setColor(aciToQColor(rc));
            poly->update();
            insertElement(poly, entityParent(data->layer, rc));
            }
      void addKnot(const DRW_Entity&) override {}
      void addInsert(const DRW_Insert& data) override {
            ++m_insertCounter;
            ++m_entityCounts[7];
            Debug("DXF INSERT: block='{}' pos=({},{}) rot={} scale=({},{}) layer='{}' {}", data.name,
                mm(data.basePoint.x), mm(data.basePoint.y), data.angle * 180.0 / std::numbers::pi,
                data.xscale, data.yscale, data.layer, m_inBlock ? "in-block" : "top-level");

            // A block may itself contain INSERTs of other blocks.  Collect
            // the reference now; it is expanded (with the full hierarchy)
            // when the containing block is inserted.
            if (m_inBlock) {
                  BlockEntity e;
                  e.type   = BlockEntity::Type::Insert;
                  e.p1     = data.basePoint;
                  e.angle  = data.angle;
                  e.xscale = data.xscale;
                  e.yscale = data.yscale;
                  e.block  = data.name;
                  e.layer  = data.layer;
                  e.color  = data.color;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            expandInsert(data.name, data.basePoint, data.angle, data.xscale, data.yscale);
            }
      void addTrace(const DRW_Trace& data) override {
            // A trace/solid is a filled quadrilateral
            auto* poly = new Polygon(m_zcam, m_parent);
            poly->setName(elementName("Trace"));
            poly->moveTo(mm2d(data.basePoint));
            poly->lineTo(mm2d(data.secPoint));
            poly->lineTo(mm2d(data.thirdPoint));
            poly->lineTo(mm2d(data.fourPoint));
            poly->lineTo(mm2d(data.basePoint)); // close
            poly->set_lineWidth(0.0);
            poly->set_fill(true);
            int rc = resolveColor(data.color, data.layer);
            poly->setColor(aciToQColor(rc));
            poly->update();
            insertElement(poly, entityParent(data.layer, rc));
            }
      void add3dFace(const DRW_3Dface& data) override {
            auto* poly = new Polygon(m_zcam, m_parent);
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
            int rc = resolveColor(data.color, data.layer);
            poly->setColor(aciToQColor(rc));
            poly->update();
            insertElement(poly, entityParent(data.layer, rc));
            }
      void addSolid(const DRW_Solid& data) override {
            auto* poly = new Polygon(m_zcam, m_parent);
            poly->setName(elementName("Solid"));
            poly->moveTo(mm2d(data.basePoint));
            poly->lineTo(mm2d(data.secPoint));
            poly->lineTo(mm2d(data.thirdPoint));
            poly->lineTo(mm2d(data.fourPoint));
            poly->lineTo(mm2d(data.basePoint)); // close
            poly->set_lineWidth(0.0);
            poly->set_fill(true);
            int rc = resolveColor(data.color, data.layer);
            poly->setColor(aciToQColor(rc));
            poly->update();
            insertElement(poly, entityParent(data.layer, rc));
            }
      void addMText(const DRW_MText& data) override {
            addTextEntity(data.basePoint, data.height, data.text, data.angle, data.layer, data.color);
            }
      void addText(const DRW_Text& data) override {
            addTextEntity(data.basePoint, data.height, data.text, data.angle, data.layer, data.color);
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
      //    Add a new element to its parent group.  The entire
      //    element tree is built without any undo/redo handling;
      //    a single InsertElementCommand for the top-level import
      //    layer is pushed at the end of import().
      //    Elements expanded into a block definition group are
      //    shared by all INSERTs of that block: identical copies
      //    created while expanding further instances are skipped.
      //---------------------------------------------------------
      void insertElement(Element3d* el, Group* parent = nullptr) {
            parent = parent ? parent : m_defaultLayer;
            if (isBlockGroup(parent) && hasIdenticalChild(parent, el)) {
                  Debug("DXF insert: <{}> → '{}' (SKIPPED — duplicate in block def)", el->name(),
                      parent->name());
                  delete el;
                  return;
                  }
            Debug("DXF insert: <{}> → '{}'", el->name(), parent->name());
            parent->addChild(el);
            }
      //---------------------------------------------------------
      //   isBlockGroup
      //---------------------------------------------------------
      bool isBlockGroup(const Group* g) const { return m_blockGroupSet.contains(g); }
      //---------------------------------------------------------
      //   hasIdenticalChild
      //    Cheap structural comparison used to detect that the
      //    same block entity was expanded into a block group more
      //    than once.
      //---------------------------------------------------------
      bool hasIdenticalChild(const Group* parent, const Element3d* el) const {
            const QString tn = const_cast<Element3d*>(el)->typeName();
            for (const Element* kid : parent->children()) {
                  auto* k = qobject_cast<Element3d*>(const_cast<Element*>(kid));
                  if (!k || k->typeName() != tn)
                        continue;
                  if (const auto* a = qobject_cast<const Polygon*>(el)) {
                        if (const auto* b = qobject_cast<const Polygon*>(k)) {
                              if (a->vertices() == b->vertices() &&
                                  std::ranges::equal(a->painterPathData(), b->painterPathData(),
                                      [](const PPElement& e1, const PPElement& e2) {
                                            return e1.type == e2.type && e1.pos == e2.pos;
                                            }))
                                    return true;
                              }
                        }
                  else if (const auto* a = qobject_cast<const Ellipse*>(el)) {
                        if (const auto* b = qobject_cast<const Ellipse*>(k)) {
                              if (a->pos() == b->pos() && a->size() == b->size() && a->rot() == b->rot())
                                    return true;
                              }
                        }
                  else if (const auto* a = qobject_cast<const Text*>(el)) {
                        if (const auto* b = qobject_cast<const Text*>(k)) {
                              if (a->pos() == b->pos() && a->name() == b->name())
                                    return true;
                              }
                        }
                  }
            return false;
            }
      //---------------------------------------------------------
      //   entityParent
      //    Resolve the parent group for an entity living on the
      //    given dxf layer with the given resolved color.
      //    Inside block expansion entities on
      //    layer "0" belong to the current insert instance (DXF
      //    resolves them against the layer of the INSERT); entities
      //    on any other layer belong to the block definition group
      //    so that all instances share that geometry.
      //    At top level, entities are grouped first by DXF layer
      //    and then by resolved color (sub-group named after the
      //    ACI colour label, e.g. "Cyan", "White").
      //---------------------------------------------------------
      Group* entityParent(const std::string& layer, int resolvedColor = -1) {
            if (m_parentDepth > 0) {
                  if (layer.empty() || layer == "0")
                        return m_parent;
                  return blockParent();
                  }
            // At top level: group by layer, then by color
            Group* layerGroup = dxfLayerGroup(QString::fromStdString(layer));
            if (resolvedColor < 0)
                  return layerGroup;
            return dxfColorGroup(layerGroup, resolvedColor);
            }
      //---------------------------------------------------------
      //   blockParent
      //    The block definition group whose entities are
      //    currently being expanded.  Entities on a named (non
      //    "0") dxf layer are added there once so every INSERT
      //    shares the same geometry.
      //---------------------------------------------------------
      Group* blockParent() const { return m_activeBlock ? m_activeBlock : m_defaultLayer; }
      //---------------------------------------------------------
      //   dxfLayerGroup
      //    Return (creating on first use) the Group representing
      //    the given dxf layer inside the root import layer.
      //---------------------------------------------------------
      Group* dxfLayerGroup(const QString& name) {
            auto it = m_dxfLayerMap.find(name.toStdString());
            if (it != m_dxfLayerMap.end())
                  return it->second;
            auto* g = new Group(m_zcam, m_defaultLayer);
            g->setName(name.isEmpty() ? elementName("Layer") : name);
            insertElement(g, m_defaultLayer);
            m_dxfLayerMap[name.toStdString()] = g;
            Debug("DXF created layer group: '{}'", g->name());
            return g;
            }
      //---------------------------------------------------------
      //   dxfColorGroup
      //    Return (creating on first use) a sub-Group inside the
      //    given layer group, named after the resolved ACI color
      //    label (e.g. "Cyan", "White").  Entities of different
      //    colors end up in separate sub-groups so they can be
      //    assigned to different LaserMops (cut vs mark).
      //---------------------------------------------------------
      Group* dxfColorGroup(Group* layerGroup, int aci) {
            auto& colorMap = m_dxfColorMap[layerGroup];
            auto it        = colorMap.find(aci);
            if (it != colorMap.end())
                  return it->second;
            auto* g = new Group(m_zcam, layerGroup);
            g->setName(aciLabel(aci));
            insertElement(g, layerGroup);
            colorMap[aci] = g;
            m_colorGroups.push_back({g, aci});
            Debug("DXF created color group: '{}' (aci={}) in layer '{}'", g->name(), aci, layerGroup->name());
            return g;
            }
      //---------------------------------------------------------
      //   blockGroup
      //    Return (creating on first use) the Group representing
      //    the named block definition inside the root import layer.
      //---------------------------------------------------------
      Group* blockGroup(const QString& name) {
            auto it = m_blockGroupMap.find(name.toStdString());
            if (it != m_blockGroupMap.end())
                  return it->second;
            auto* g = new Group(m_zcam, m_defaultLayer);
            g->setName(QString::fromUtf8(kBlockPrefix.data(), kBlockPrefix.size()) + name);
            insertElement(g, m_defaultLayer);
            m_blockGroupMap[name.toStdString()] = g;
            m_blockGroupSet.insert(g);
            Debug("DXF created block group: '{}'", g->name());
            return g;
            }
      //---------------------------------------------------------
      //   expandInsert
      //    Expand one block INSERT into the tree: the block
      //    definition gets a Group inside the root import layer
      //    (created lazily on first use) and the insert appears as
      //    a transformed child Group below it or below the current
      //    parent group.
      //---------------------------------------------------------
      void expandInsert(
          const std::string& blockName, const DRW_Coord& base, double rotation, double sx, double sy) {
            auto it = m_blocks.find(blockName);
            if (it == m_blocks.end() || it->second.empty())
                  return;

            Debug("DXF expandInsert: block='{}' depth={} entities={} pos=({},{}) rot={:.1f} scale=({},{})",
                blockName, m_parentDepth, it->second.size(), mm(base.x), mm(base.y),
                rotation * 180.0 / std::numbers::pi, sx, sy);

            // Guard against recursive block references (a block
            // inserting itself directly or transitively).
            if (m_parentDepth >= kMaxInsertDepth) {
                  Warning("DXF import: maximum block insert depth ({}) reached - skipping INSERT of '{}'",
                      int(kMaxInsertDepth), blockName);
                  return;
                  }

            double ang = rotation; // radians
            double cx  = mm(base.x);
            double cy  = mm(base.y);

            // Block definitions only contribute entities via INSERT, so the
            // block group is created lazily on first use.  Entities that the
            // block defines on a named (non "0") dxf layer are kept as direct
            // children of the block group and are therefore shared by all
            // instances; entities on layer "0" belong to each insert.  This
            // mirrors the DXF hierarchy where the block is the parent object
            // of its inserts.
            Group* blockParent  = blockGroup(QString::fromStdString(blockName));
            Group* insertParent = m_parent;
            if (m_parent == blockParent && m_parentDepth > 0) {
                  // Avoid adding a block instance as a child of itself when a
                  // block contains an INSERT of the same block.
                  insertParent = m_defaultLayer;
                  }
            auto* instGroup = new Group(m_zcam, insertParent);
            instGroup->setName(QStringLiteral("%1%2 #%3")
                    .arg(QString::fromUtf8(kInsertPrefix.data(), kInsertPrefix.size()),
                        QString::fromStdString(blockName))
                    .arg(m_insertCounter));
            instGroup->set_pos(QVector3D(cx, cy, 0.0));
            instGroup->set_rot(QVector3D(0.0, 0.0, ang * 180.0 / std::numbers::pi));
            instGroup->set_scale(QVector3D(sx, sy, 1.0));
            insertElement(instGroup, insertParent);

            // Expand the collected block entities into the instance group.
            // Their coordinates are relative to the block base point and are
            // positioned through the group transform set above.
            Group* savedParent  = m_parent;
            Group* savedBlock   = m_activeBlock;
            int savedDepth      = m_parentDepth;
            m_parent            = instGroup;
            m_activeBlock       = blockParent;
            m_parentDepth      += 1;
            for (const auto& e : it->second)
                  expandBlockEntity(e);

            m_parent      = savedParent;
            m_activeBlock = savedBlock;
            m_parentDepth = savedDepth;
            }
      //---------------------------------------------------------
      //   expandBlockEntity
      //    Create a ZCam element for one collected block entity.
      //    Coordinates are block-local (unit scale applied) so the
      //    parent group transform takes care of placement.
      //---------------------------------------------------------
      void expandBlockEntity(const BlockEntity& e) {
            int rc        = resolveColor(e.color, e.layer);
            Group* target = entityParent(e.layer, rc);
            Debug("DXF expandBlockEntity: type={} layer='{}' color={} → parent='{}' (depth={})",
                static_cast<int>(e.type), e.layer, rc, target ? target->name().toUtf8().data() : "null",
                m_parentDepth);
            QColor qc = aciToQColor(rc);
            switch (e.type) {
                  case BlockEntity::Type::Line: {
                        auto* poly = new Polygon(m_zcam, m_parent);
                        poly->setName(elementName("BlockLine"));
                        poly->moveTo(mm2d(e.p1));
                        poly->lineTo(mm2d(e.p2));
                        poly->set_lineWidth(0.0);
                        poly->set_fill(false);
                        poly->setColor(qc);
                        poly->update();
                        insertElement(poly, target);
                        break;
                        } // Note: block lines are typically few; no merging needed
                  case BlockEntity::Type::Circle: {
                        double r  = mm(e.radius);
                        auto* ell = new Ellipse(m_zcam, m_parent);
                        ell->setName(elementName("BlockCircle"));
                        ell->set_pos(QVector3D(mm(e.p1.x), mm(e.p1.y), 0.0));
                        ell->set_size(QVector2D(r * 2.0, r * 2.0));
                        ell->setColor(qc);
                        ell->update();
                        insertElement(ell, target);
                        break;
                        }
                  case BlockEntity::Type::Arc: {
                        auto* poly = new Polygon(m_zcam, m_parent);
                        poly->setName(elementName("BlockArc"));
                        poly->setPainterPath(arcToPainterPath(e.p1, e.radius, e.startAng, e.endAng, e.isccw));
                        poly->set_lineWidth(0.0);
                        poly->set_fill(false);
                        poly->setColor(qc);
                        poly->update();
                        insertElement(poly, target);
                        break;
                        }
                  case BlockEntity::Type::LWPolyline: {
                        if (e.vertices.empty())
                              break;
                        auto* poly = new Polygon(m_zcam, m_parent);
                        poly->setName(elementName("BlockPoly"));
                        bool firstV = true;
                        for (const auto& v : e.vertices) {
                              Vec2d pt(mm(v.x), mm(v.y));
                              if (firstV) {
                                    poly->moveTo(pt);
                                    firstV = false;
                                    }
                              else if (std::abs(v.bulge) > 1e-10) {
                                    Vec2d prev = poly->vertexPos(poly->vertices() - 1).toPointF();
                                    arcBulgeTo(*poly, prev, pt, v.bulge);
                                    }
                              else {
                                    poly->lineTo(pt);
                                    }
                              }
                        if (e.flags & 1)
                              poly->lineTo(poly->startPos());
                        poly->set_lineWidth(0.0);
                        poly->set_fill(e.flags & 1);
                        poly->setColor(qc);
                        poly->update();
                        insertElement(poly, target);
                        break;
                        }
                  case BlockEntity::Type::Point: {
                        auto* poly = new Polygon(m_zcam, m_parent);
                        poly->setName(elementName("BlockPoint"));
                        Vec2d p = mm2d(e.p1);
                        poly->moveTo(p);
                        poly->lineTo(p);
                        poly->set_lineWidth(0.0);
                        poly->set_fill(false);
                        poly->setColor(qc);
                        poly->update();
                        insertElement(poly, target);
                        break;
                        }
                  case BlockEntity::Type::Spline: {
                        // Convert control points to Vec2d and apply unit scale
                        std::vector<Vec2d> ctrlPts;
                        ctrlPts.reserve(e.controlPoints.size());
                        for (const auto& c : e.controlPoints)
                              ctrlPts.emplace_back(mm(c.x), mm(c.y));

                        auto beziers = DxfTess::bsplineToCubicBeziers(e.degree, ctrlPts, e.knots);

                        auto* poly = new Polygon(m_zcam, m_parent);
                        poly->setName(elementName("BlockSpline"));

                        if (!beziers.empty()) {
                              bool firstV = true;
                              for (const auto& bz : beziers) {
                                    if (firstV) {
                                          poly->moveTo(bz.p0);
                                          firstV = false;
                                          }
                                    poly->cubicTo(bz.p1, bz.p2, bz.p3);
                                    }
                              }
                        else {
                              // Fallback: tessellate for degree > 3
                              auto pts = DxfTess::evaluateSpline(
                                  e.degree, e.controlPoints, e.knots, curveResolution());
                              if (pts.empty()) {
                                    delete poly;
                                    break;
                                    }
                              bool firstV = true;
                              for (const auto& pt : pts) {
                                    Vec2d p(mm(pt.x()), mm(pt.y()));
                                    if (firstV) {
                                          poly->moveTo(p);
                                          firstV = false;
                                          }
                                    else {
                                          poly->lineTo(p);
                                          }
                                    }
                              }
                        poly->set_lineWidth(0.0);
                        poly->set_fill(false);
                        poly->setColor(qc);
                        poly->update();
                        insertElement(poly, target);
                        break;
                        }
                  case BlockEntity::Type::Ellipse: {
                        double majorR   = std::sqrt(e.p2.x * e.p2.x + e.p2.y * e.p2.y);
                        majorR          = mm(majorR);
                        double minorR   = majorR * e.ratio;
                        double rotation = std::atan2(e.p2.y, e.p2.x);
                        double rotDeg   = rotation * 180.0 / std::numbers::pi;
                        bool full =
                            (e.staparam == 0.0 && std::abs(e.endparam - 2.0 * std::numbers::pi) < 1e-6);
                        if (full) {
                              auto* ell = new Ellipse(m_zcam, m_parent);
                              ell->setName(elementName("BlockEllipse"));
                              ell->set_pos(QVector3D(mm(e.p1.x), mm(e.p1.y), 0.0));
                              ell->set_size(QVector2D(majorR * 2.0, minorR * 2.0));
                              ell->set_rot(QVector3D(0.0, 0.0, rotDeg));
                              ell->setColor(qc);
                              ell->update();
                              insertElement(ell, target);
                              }
                        else {
                              // Elliptical arc: build in local space with addArc,
                              // then rotate and translate control points.
                              double cx = mm(e.p1.x);
                              double cy = mm(e.p1.y);
                              double sa = e.staparam;
                              double ea = e.endparam;
                              if (sa > ea)
                                    ea += 2.0 * std::numbers::pi;
                              double sweep = ea - sa;

                              double startDeg = sa * 180.0 / std::numbers::pi;
                              double sweepDeg = sweep * 180.0 / std::numbers::pi;

                              QRectF arcRect(-majorR, -minorR, 2.0 * majorR, 2.0 * minorR);
                              PainterPath localPp;
                              localPp.addArc(arcRect, startDeg, -sweepDeg);

                              double cosR = std::cos(rotation);
                              double sinR = std::sin(rotation);
                              PainterPath pp;
                              pp.reserve(localPp.size());
                              for (const auto& elem : localPp) {
                                    double rx = elem.pos.x() * cosR - elem.pos.y() * sinR;
                                    double ry = elem.pos.x() * sinR + elem.pos.y() * cosR;
                                    pp.push_back({elem.type, Vec2d(rx + cx, ry + cy)});
                                    }
                              auto* poly = new Polygon(m_zcam, m_parent);
                              poly->setName(elementName("BlockEllipseArc"));
                              poly->setPainterPath(pp);
                              poly->set_lineWidth(0.0);
                              poly->set_fill(false);
                              poly->setColor(qc);
                              poly->update();
                              insertElement(poly, target);
                              }
                        break;
                        }
                  case BlockEntity::Type::Text: {
                        if (e.text.empty())
                              break;
                        auto* textEl = new Text(m_zcam, m_parent);
                        textEl->setName(elementName(e.text.c_str()));
                        textEl->set_pos(QVector3D(mm(e.p1.x), mm(e.p1.y), 0.0));
                        double h                    = mm(e.height);
                        constexpr double mmPerPoint = 0.352778;
                        textEl->set_pointSize(h / mmPerPoint);
                        textEl->setColor(qc);
                        textEl->update();
                        insertElement(textEl, target);
                        break;
                        }
                  case BlockEntity::Type::Insert: {
                        // Nested block reference: expand recursively so the
                        // tree mirrors the block hierarchy of the DXF file.
                        ++m_insertCounter;
                        expandInsert(e.block, e.p1, e.angle, e.xscale, e.yscale);
                        break;
                        }
                  default: break; // Other block entity types could be added
                  }
            }
      //---------------------------------------------------------
      //   addTextEntity
      //---------------------------------------------------------
      void addTextEntity(const DRW_Coord& pos, double height, const std::string& txt, double /*angle*/,
          const std::string& layer, int entityColor = 256) {
            if (txt.empty())
                  return;
            auto* textEl = new Text(m_zcam, m_parent);
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
            int rc = resolveColor(entityColor, layer);
            textEl->setColor(aciToQColor(rc));
            textEl->update();
            insertElement(textEl, entityParent(layer, rc));
            }
      //
      //---------------------------------------------------------
      //   arcToPainterPath
      //    Tessellate a DRW_Arc into a PainterPath.
      //---------------------------------------------------------
      //
      PainterPath arcToPainterPath(const DRW_Arc& data) {
            return arcToPainterPath(data.basePoint, data.radious, data.staangle, data.endangle, data.isccw);
            }
      //---------------------------------------------------------
      //   arcToPainterPath
      //    Convert an arc described by center, radius and the
      //    start/end angles (radians, drawing units) into a
      //    PainterPath using exact cubic Bézier segments.
      //    A circular arc is represented by at most 4 cubic
      //    Bézier segments (one per 90° quadrant), regardless
      //    of the configured circle resolution.
      //---------------------------------------------------------
      PainterPath arcToPainterPath(
          const DRW_Coord& center, double radius, double startAngle, double endAngle, int isccw) {
            PainterPath pp;
            double r = mm(radius);
            if (r <= 0.0)
                  return pp;

            double sa = startAngle; // radians
            double ea = endAngle;   // radians
            if (isccw == 0)
                  std::swap(sa, ea);

            double cx = mm(center.x);
            double cy = mm(center.y);

            if (sa > ea)
                  ea += 2.0 * std::numbers::pi;
            double sweep = ea - sa;

            // Convert to degrees for Qt's addArc which expects degrees.
            // Qt's coordinate system has Y pointing downward, so a CCW
            // arc in the standard math sense (Y up) is a CW arc in Qt's
            // sense (Y down).  We pass a negative sweep to draw CCW.
            double startDeg = sa * 180.0 / std::numbers::pi;
            double sweepDeg = sweep * 180.0 / std::numbers::pi;

            QRectF rect(cx - r, cy - r, 2.0 * r, 2.0 * r);
            pp.addArc(rect, startDeg, -sweepDeg);
            return pp;
            }
      //---------------------------------------------------------
      //   ellipseArcToPainterPath
      //    Convert an elliptical arc into a PainterPath using
      //    cubic Bézier segments.  The arc is first built in
      //    the ellipse's local (axis-aligned) coordinate system
      //    using addArc, then all control points are rotated by
      //    the ellipse's rotation angle and translated to the
      //    ellipse center.
      //---------------------------------------------------------
      PainterPath ellipseArcToPainterPath(const DRW_Ellipse& data) {
            PainterPath pp;
            double majorR = std::sqrt(data.secPoint.x * data.secPoint.x + data.secPoint.y * data.secPoint.y);
            majorR        = mm(majorR);
            double minorR = majorR * data.ratio;
            double rotation = std::atan2(data.secPoint.y, data.secPoint.x);
            double cx       = mm(data.basePoint.x);
            double cy       = mm(data.basePoint.y);

            if (majorR <= 0.0 || minorR <= 0.0)
                  return pp;

            double sa = data.staparam;
            double ea = data.endparam;
            if (sa > ea)
                  ea += 2.0 * std::numbers::pi;
            double sweep = ea - sa;

            // Build the arc in the local axis-aligned coordinate system.
            // The ellipse is centred at origin with rx=majorR, ry=minorR.
            double startDeg = sa * 180.0 / std::numbers::pi;
            double sweepDeg = sweep * 180.0 / std::numbers::pi;

            QRectF rect(-majorR, -minorR, 2.0 * majorR, 2.0 * minorR);
            PainterPath localPp;
            localPp.addArc(rect, startDeg, -sweepDeg);

            // Rotate and translate all control points
            double cosR = std::cos(rotation);
            double sinR = std::sin(rotation);
            pp.reserve(localPp.size());
            for (const auto& elem : localPp) {
                  double rx = elem.pos.x() * cosR - elem.pos.y() * sinR;
                  double ry = elem.pos.x() * sinR + elem.pos.y() * cosR;
                  pp.push_back({elem.type, Vec2d(rx + cx, ry + cy)});
                  }
            return pp;
            }
      //---------------------------------------------------------
      //   arcBulgeTo
      //    Given a bulge value between prev and current point,
      //    append a circular arc as exact cubic Bézier segment(s)
      //    to the polygon's painter path.
      //
      //    bulge = tan(theta/4) where theta is the included angle.
      //    The arc is a circular segment represented by cubic
      //    Bézier segments using the Kappa constant.
      //---------------------------------------------------------
      void arcBulgeTo(Polygon& poly, const Vec2d& prev, const Vec2d& curr, double bulge) {
            double dx    = curr.x() - prev.x();
            double dy    = curr.y() - prev.y();
            double chord = std::sqrt(dx * dx + dy * dy);
            if (chord < 1e-10)
                  return;

            double theta = 4.0 * std::atan(std::abs(bulge));
            double r     = chord / (2.0 * std::sin(theta / 2.0));

            double midx  = (prev.x() + curr.x()) / 2.0;
            double midy  = (prev.y() + curr.y()) / 2.0;
            double perpX = -dy / chord;
            double perpY = dx / chord;
            double dist  = r * std::cos(theta / 2.0);
            double sign  = (bulge > 0) ? 1.0 : -1.0;
            double cx    = midx + sign * perpX * dist;
            double cy    = midy + sign * perpY * dist;

            double startAng = std::atan2(prev.y() - cy, prev.x() - cx);
            double endAng   = std::atan2(curr.y() - cy, curr.x() - cx);

            if (bulge > 0) {
                  if (endAng < startAng)
                        endAng += 2.0 * std::numbers::pi;
                  }
            else {
                  if (endAng > startAng)
                        endAng -= 2.0 * std::numbers::pi;
                  }

            double startDeg = startAng * 180.0 / std::numbers::pi;
            double sweepDeg = (endAng - startAng) * 180.0 / std::numbers::pi;

            QRectF rect(cx - r, cy - r, 2.0 * r, 2.0 * r);
            PainterPath arcPp;
            arcPp.addArc(rect, startDeg, -sweepDeg);
            // Skip the MoveTo (first element) and append the rest
            poly.appendPainterPath(arcPp, true);
            }
      //---------------------------------------------------------
      //   flushLines
      //    Merge buffered LINE entities into connected polylines.
      //    Lines whose end point matches the start of another line
      //    (within tolerance) are chained into a single Polygon,
      //    drastically reducing the number of elements when a DXF
      //    file contains thousands of small line segments.
      //---------------------------------------------------------

    public:
      void flushLines() {
            if (m_lineBuffer.empty())
                  return;

            constexpr double tol = 0.01; // mm
            // Group lines by (layer, resolved-color) so lines of different
            // colors are never chained together.
            struct LayerColorKey {
                  std::string layer;
                  int color;
                  bool operator==(const LayerColorKey&) const = default;
                  };
            struct LayerColorKeyHash {
                  size_t operator()(const LayerColorKey& k) const {
                        size_t h  = std::hash<std::string>()(k.layer);
                        h        ^= std::hash<int>()(k.color) + 0x9e3779b9 + (h << 6) + (h >> 2);
                        return h;
                        }
                  };
            std::unordered_map<LayerColorKey, std::vector<size_t>, LayerColorKeyHash> byLayerColor;
            for (size_t i = 0; i < m_lineBuffer.size(); ++i) {
                  int rc = resolveColor(m_lineBuffer[i].color, m_lineBuffer[i].layer);
                  byLayerColor[{m_lineBuffer[i].layer, rc}].push_back(i);
                  }

            for (auto& [key, indices] : byLayerColor) {
                  if (indices.empty())
                        continue;

                  // Build chains: greedily connect end→start points
                  std::vector<bool> used(indices.size(), false);
                  std::vector<std::vector<size_t>> chains;

                  for (size_t seed = 0; seed < indices.size(); ++seed) {
                        if (used[seed])
                              continue;
                        used[seed]                = true;
                        std::vector<size_t> chain = {indices[seed]};
                        const LineSeg* tail       = &m_lineBuffer[indices[seed]];

                        // Forward chain: tail.p2 → next.p1
                        for (;;) {
                              bool found = false;
                              for (size_t j = 0; j < indices.size(); ++j) {
                                    if (used[j])
                                          continue;
                                    const LineSeg& ls = m_lineBuffer[indices[j]];
                                    if (std::abs(tail->p2.x() - ls.p1.x()) < tol &&
                                        std::abs(tail->p2.y() - ls.p1.y()) < tol) {
                                          used[j] = true;
                                          chain.push_back(indices[j]);
                                          tail  = &ls;
                                          found = true;
                                          break;
                                          }
                                    // Also try reversed: tail.p2 → ls.p2
                                    if (std::abs(tail->p2.x() - ls.p2.x()) < tol &&
                                        std::abs(tail->p2.y() - ls.p2.y()) < tol) {
                                          used[j] = true;
                                          // Reverse this segment
                                          m_lineBuffer[indices[j]] = {ls.p2, ls.p1, ls.layer, ls.color};
                                          chain.push_back(indices[j]);
                                          tail  = &m_lineBuffer[indices[j]];
                                          found = true;
                                          break;
                                          }
                                    }
                              if (!found)
                                    break;
                              }

                        // Backward chain: chain.front().p1 ← prev.p2
                        const LineSeg* head = &m_lineBuffer[chain.front()];
                        for (;;) {
                              bool found = false;
                              for (size_t j = 0; j < indices.size(); ++j) {
                                    if (used[j])
                                          continue;
                                    const LineSeg& ls = m_lineBuffer[indices[j]];
                                    if (std::abs(head->p1.x() - ls.p2.x()) < tol &&
                                        std::abs(head->p1.y() - ls.p2.y()) < tol) {
                                          used[j] = true;
                                          chain.insert(chain.begin(), indices[j]);
                                          head  = &m_lineBuffer[indices[j]];
                                          found = true;
                                          break;
                                          }
                                    // Also try reversed: head.p1 ← ls.p1
                                    if (std::abs(head->p1.x() - ls.p1.x()) < tol &&
                                        std::abs(head->p1.y() - ls.p1.y()) < tol) {
                                          used[j]                  = true;
                                          m_lineBuffer[indices[j]] = {ls.p2, ls.p1, ls.layer, ls.color};
                                          chain.insert(chain.begin(), indices[j]);
                                          head  = &m_lineBuffer[indices[j]];
                                          found = true;
                                          break;
                                          }
                                    }
                              if (!found)
                                    break;
                              }

                        chains.push_back(std::move(chain));
                        }

                  // Create one Polygon per chain
                  int polyCount = 0;
                  for (const auto& chain : chains) {
                        if (chain.empty())
                              continue;
                        auto* poly = new Polygon(m_zcam, m_parent);
                        poly->setName(elementName("Lines"));
                        bool first = true;
                        for (size_t idx : chain) {
                              const LineSeg& ls = m_lineBuffer[idx];
                              if (first) {
                                    poly->moveTo(ls.p1);
                                    poly->lineTo(ls.p2);
                                    first = false;
                                    }
                              else {
                                    poly->lineTo(ls.p2);
                                    }
                              }
                        // Check if closed
                        Vec2d start = m_lineBuffer[chain.front()].p1;
                        Vec2d end   = m_lineBuffer[chain.back()].p2;
                        bool closed =
                            std::abs(start.x() - end.x()) < tol && std::abs(start.y() - end.y()) < tol;
                        if (closed)
                              poly->set_fill(false); // don't fill by default
                        poly->set_lineWidth(0.0);
                        poly->set_fill(false);
                        poly->setColor(aciToQColor(key.color));
                        poly->update();
                        insertElement(poly, entityParent(key.layer, key.color));
                        ++polyCount;
                        }

                  Debug("DXF Lines on layer '{}' color {}: {} segments → {} polylines", key.layer, key.color,
                      indices.size(), polyCount);
                  }
            m_lineBuffer.clear();
            }
      //---------------------------------------------------------
      //   logImportStats
      //---------------------------------------------------------
      void logImportStats() const {
            Debug("DXF entity counts: Line={} Arc={} Circle={} Ellipse={} LWPoly={} Poly={} Spline={} "
                  "Insert={} Point={}",
                m_entityCounts[0], m_entityCounts[1], m_entityCounts[2], m_entityCounts[3], m_entityCounts[4],
                m_entityCounts[5], m_entityCounts[6], m_entityCounts[7], m_entityCounts[8]);
            }

    private:
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
//   optimizeAllPolygons
//    Recursively find every Polygon below `root` and call
//    optimize() on it.  optimize() performs two exact,
//    visually lossless reductions:
//      1) A cubic-bezier segment whose control points both lie
//         on the chord (start → end) collapses to a single LineTo.
//      2) Two consecutive straight segments A → B → C whose three
//         anchors are collinear merge into A → C, removing vertex B.
//    No-ops (no undo entry) for polygons that already have no
//    redundant vertices or curves.
//---------------------------------------------------------

static void optimizeAllPolygons(Element* root) {
      if (!root)
            return;
      if (auto* poly = qobject_cast<Polygon*>(root))
            poly->optimize();
      for (Element* child : root->children())
            optimizeAllPolygons(child);
      }

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

      // Create a new Layer for this DXF file.  The layer is not yet
      // added to the CAD tree; it will be inserted undoably at the
      // end after the entire element tree has been built.
      auto* layer = new Group(zcam, nullptr);
      layer->setName(fi.baseName());
      layer->setExpanded(true);

      // Create a LaserLayer linked to this Layer.  Like the layer
      // itself, the LaserMop is not yet added to the fixture; it will
      // be inserted undoably at the end.  If the DXF contains multiple
      // entity colours, one LaserMop per colour is created and linked
      // to the corresponding colour sub-group so that cut and mark
      // operations can use different recipes / power levels.
      Fixture* fixture = zcam->project()->fixture();
      if (!fixture) {
            if (!zcam->project()->fixtures().empty())
                  fixture = zcam->project()->fixtures().at(0);
            }
      LaserMop* laserMop {nullptr};
      std::vector<LaserMop*> extraMops; ///< per-colour LaserMops (owned until inserted)
      if (fixture) {
            laserMop = new LaserMop(zcam, nullptr);
            laserMop->setName(QStringLiteral("LL-%1").arg(fi.baseName()));
            laserMop->setExpanded(false);
            layer->set_mop(laserMop);
            laserMop->set_kerfOffset(-0.05);
            }

      // Read the DXF file: entities are added directly to the layer
      // tree without per-entity undo commands, TreeModel notifications,
      // or script-engine namespace updates.
      DxfReaderInterface reader(zcam, layer, fi.baseName());

      // Suppress per-entity script-engine overhead (applyDefaultScripts
      // and addElementToTree) during the read.
      ScriptEngine* se     = ScriptEngine::instance();
      bool savedRebuilding = se ? se->_rebuilding : false;
      if (se)
            se->_rebuilding = true;

      dxfRW dxf(path.toUtf8().constData());
      Debug("==========================");
      bool ok = dxf.read(&reader, false);
      Debug("==========================");

      // Restore script-engine state and rebuild the namespace once
      // for the entire subtree.
      if (se) {
            se->_rebuilding = savedRebuilding;
            if (ok)
                  se->registerSubtree(layer);
            }

      // Flush buffered LINE entities into merged polylines
      reader.logImportStats();
      reader.flushLines();

      // Create per-colour LaserMops: for each colour sub-group found
      // during the import, create a LaserMop linked to that sub-group.
      // The first (or only) colour's LaserMop is the one already set on
      // the top-level layer (laserMop); additional colours get their
      // own LaserMops linked to the corresponding sub-group.
      const auto& colorGroups = reader.colorGroups();
      if (fixture && colorGroups.size() > 1) {
            // Multiple colours: link the existing laserMop to the first
            // colour group, and create additional LaserMops for the rest.
            // Determine sensible defaults based on colour semantics:
            //   ACI 0 (BYBLOCK) → Default cut (kerfOffset, burn=true)
            //   ACI 4 (Cyan) → Cut (kerfOffset, burn=true)
            //   ACI 7 (White) → Mark (kerfOffset=0, burn=true)
            //   others → Default cut (burn=true)
            auto mopDefaults = [](int aci) {
                  struct Defaults {
                        double kerf;
                        bool burn;
                        };
                  switch (aci) {
                        case 0: return Defaults {-0.05, true};  // BYBLOCK → Default cut
                        case 4: return Defaults {-0.05, true};  // Cyan → Cut
                        case 7: return Defaults {0.0, true};    // White → Mark
                        default: return Defaults {-0.05, true}; // others → Default cut
                        }
                  };

            // Track colour indices used by the Mops we create so each
            // new Mop gets a unique colour from the configured Mop
            // palette.  The existing laserMop was already assigned a
            // colour by its constructor via nextFreeColorIndex(); we
            // pick up from there.
            int nextColorIdx = laserMop ? laserMop->colorIndex() : 1;

            for (size_t i = 0; i < colorGroups.size(); ++i) {
                  const auto& cg    = colorGroups[i];
                  auto [kerf, burn] = mopDefaults(cg.aci);
                  if (i == 0) {
                        // Use the existing laserMop for the first colour group
                        cg.group->set_mop(laserMop);
                        laserMop->set_kerfOffset(kerf);
                        laserMop->set_burn(burn);
                        Debug("DXF: linked LaserMop '{}' to colour group '{}' (aci={}, kerf={}, burn={})",
                            laserMop->name().toUtf8().data(), cg.group->name().toUtf8().data(), cg.aci, kerf,
                            burn);
                        }
                  else {
                        auto* mop = new LaserMop(zcam, nullptr);
                        mop->setName(QStringLiteral("LL-%1-%2")
                                .arg(fi.baseName(), DxfReaderInterface::aciLabel(cg.aci)));
                        mop->setExpanded(false);
                        mop->set_kerfOffset(kerf);
                        mop->set_burn(burn);
                        // Assign the next unique colour index.  The
                        // LaserMop constructor already called
                        // nextFreeColorIndex against the existing project
                        // tree, but since this Mop is not yet in the tree
                        // we must advance the index manually to avoid
                        // collisions with previously created Mops in
                        // this import.
                        nextColorIdx = (nextColorIdx % Mop::mopColorCount()) + 1;
                        if (nextColorIdx == 0)
                              nextColorIdx = 1;
                        mop->set_colorIndex(nextColorIdx);
                        cg.group->set_mop(mop);
                        extraMops.push_back(mop);
                        Debug("DXF: created LaserMop '{}' for colour group '{}' (aci={}, colorIdx={}, kerf={}, burn={})",
                            mop->name().toUtf8().data(), cg.group->name().toUtf8().data(), cg.aci,
                            nextColorIdx, kerf, burn);
                        }
                  }
            }
      else if (fixture && !colorGroups.empty()) {
            // Single colour: link the existing laserMop to the single colour group
            colorGroups[0].group->set_mop(laserMop);
            Debug("DXF: linked single LaserMop '{}' to colour group '{}'", laserMop->name().toUtf8().data(),
                colorGroups[0].group->name().toUtf8().data());
            }

      // Simplify every imported polygon: collapse degenerate bezier
      // segments to straight lines and merge consecutive collinear
      // vertices into a single segment.  optimize() is a no-op (no
      // undo entry, no signal) for polygons that are already clean.
      // Done before the insert-undo-macro so the optimisation commands
      // sit *behind* the import command on the undo stack — undoing the
      // import removes the layer and makes the stale optimisation
      // commands inert.
      if (ok)
            optimizeAllPolygons(layer);

      if (!ok) {
            // The read failed: the layer and all its children are not
            // in the project tree (no Qt parent owns them via addChild),
            // so delete the layer to avoid a leak.  The LaserMop was
            // created with nullptr parent and is not in any tree, so it
            // would be leaked if not deleted here.  The layer holds a
            // pointer to the LaserMop via set_mop() but does not take
            // Qt ownership, so delete it explicitly.
            for (auto* mop : extraMops)
                  delete mop;
            delete laserMop;
            delete layer;
            Critical("DXF import failed: {}", path.toUtf8().data());
            return false;
            }

      // Add the top-level layer and its linked LaserMop(s) to the project
      // with a single undoable command pair wrapped in a macro so the
      // entire import can be undone as one operation.
      zcam->project()->undo()->beginMacro();
      zcam->project()->undo()->push(new InsertElementCommand(zcam, cad, layer, -1));
      if (fixture && laserMop)
            zcam->project()->undo()->push(new InsertElementCommand(zcam, fixture, laserMop, -1));
      for (auto* mop : extraMops)
            zcam->project()->undo()->push(new InsertElementCommand(zcam, fixture, mop, -1));
      zcam->project()->undo()->endMacro();

      Debug("DXF import completed: {}", path.toUtf8().data());

      // Dump the resulting element tree
      std::function<void(Element*, int)> dumpTree = [&](Element* el, int depth) -> void {
            QString indent;
            indent.fill(' ', depth * 2);
            QString typeName = qobject_cast<Element3d*>(el)
                                   ? const_cast<Element3d*>(qobject_cast<Element3d*>(el))->typeName()
                                   : "element";
            Debug("{}{} '{}'", indent, typeName, el->name());
            for (const auto* child : el->children())
                  dumpTree(const_cast<Element*>(child), depth + 1);
            };
      Debug("=== DXF import tree ===");
      dumpTree(layer, 0);
      Debug("=== end tree ===");

      return ok;
      }

//=========================================================
//   DxfBBoxCollector
//    A minimal DRW_Interface implementation that only
//    collects the bounding box of all entities in a DXF
//    file.  It mirrors the unit-scale logic of
//    DxfReaderInterface so the bounding box is in mm.
//=========================================================
class DxfBBoxCollector final : public DRW_Interface
      {
      double m_unitScale {1.0};
      double m_dxfScale {72.0};
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
            if (x < m_minX)
                  m_minX = x;
            if (x > m_maxX)
                  m_maxX = x;
            if (y < m_minY)
                  m_minY = y;
            if (y > m_maxY)
                  m_maxY = y;
            }
      static double unitToMm(int unit) {
            switch (unit) {
                  case 0: return 1.0;
                  case 1: return 25.4;
                  case 2: return 25.4 * 12;
                  case 3: return 1609344.0;
                  case 4: return 1.0;
                  case 5: return 10.0;
                  case 6: return 1000.0;
                  case 7: return 1000000.0;
                  case 8: return 25.4 / 1000000.0;
                  case 9: return 25.4 / 1000.0;
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
            Debug("DXF BLOCK begin: '{}'", data.name);
            m_currentBlockName = data.name;
            m_blocks[m_currentBlockName].clear();
            m_inBlock = true;
            }
      void setBlock(int) override {}
      void endBlock() override {
            Debug("DXF BLOCK end: '{}' ({} entities collected)", m_currentBlockName,
                m_blocks[m_currentBlockName].size());
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
            expand(mm(data.basePoint.x), mm(data.basePoint.y));
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
            expand(mm(data.basePoint.x), mm(data.basePoint.y));
            expand(mm(data.secPoint.x), mm(data.secPoint.y));
            }
      void addRay(const DRW_Ray&) override {}
      void addXline(const DRW_Xline&) override {}
      void addArc(const DRW_Arc& data) override {
            double r = mm(data.radious);
            if (r <= 0.0)
                  return;
            double cx = mm(data.basePoint.x);
            double cy = mm(data.basePoint.y);
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
            if (r <= 0.0)
                  return;
            double cx = mm(data.basePoint.x);
            double cy = mm(data.basePoint.y);
            if (m_inBlock) {
                  BlockEntity e;
                  e.type   = BlockEntity::Type::Circle;
                  e.p1     = data.basePoint;
                  e.radius = data.radious;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            expand(cx - r, cy - r);
            expand(cx + r, cy + r);
            }
      void addEllipse(const DRW_Ellipse& data) override {
            double majorR = std::sqrt(data.secPoint.x * data.secPoint.x + data.secPoint.y * data.secPoint.y);
            majorR        = mm(majorR);
            double cx     = mm(data.basePoint.x);
            double cy     = mm(data.basePoint.y);
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
            expand(cx - majorR, cy - majorR);
            expand(cx + majorR, cy + majorR);
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
            for (const auto& v : data.vertlist)
                  expand(mm(v->x), mm(v->y));
            }
      void addPolyline(const DRW_Polyline& data) override {
            if (m_inBlock) {
                  BlockEntity e;
                  e.type = BlockEntity::Type::LWPolyline;
                  for (const auto& v : data.vertlist)
                        e.vertices.push_back(DRW_Vertex2D(v->basePoint.x, v->basePoint.y, v->bulge));
                  e.flags = data.flags;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            for (const auto& v : data.vertlist)
                  expand(mm(v->basePoint.x), mm(v->basePoint.y));
            }
      void addSpline(const DRW_Spline* data) override {
            if (!data)
                  return;
            if (m_inBlock) {
                  // Splines in blocks are not expanded; they would inflate
                  // the bbox for blocks that may never be inserted.
                  return;
                  }
            // Prefer fit points (which lie on the curve) over control points
            // (which form the convex hull and can be far outside the curve).
            if (!data->fitlist.empty())
                  for (const auto& fp : data->fitlist)
                        expand(mm(fp->x), mm(fp->y));
            else if (!data->controllist.empty())
                  for (const auto& cp : data->controllist)
                        expand(mm(cp->x), mm(cp->y));
            }
      void addKnot(const DRW_Entity&) override {}
      void addInsert(const DRW_Insert& data) override {
            auto it = m_blocks.find(data.name);
            if (it == m_blocks.end())
                  return;
            double ang  = data.angle;
            double sx   = data.xscale;
            double sy   = data.yscale;
            double cx   = mm(data.basePoint.x);
            double cy   = mm(data.basePoint.y);
            double cosA = std::cos(ang);
            double sinA = std::sin(ang);
            auto apply  = [&](const DRW_Coord& p) {
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
                              auto c   = apply(e.p1);
                              expand(c.first - r, c.second - r);
                              expand(c.first + r, c.second + r);
                              break;
                              }
                        case BlockEntity::Type::Arc: {
                              // Expand by actual arc extent, not full circle bbox.
                              double r  = mm(e.radius);
                              auto c    = apply(e.p1);
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
                              for (double a :
                                        {0.0, std::numbers::pi / 2, std::numbers::pi, 3.0 * std::numbers::pi / 2}) {
                                    double na = a;
                                    while (na < sa)
                                          na += 2.0 * std::numbers::pi;
                                    while (na > sa + 2.0 * std::numbers::pi + 1e-10)
                                          na -= 2.0 * std::numbers::pi;
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
                              majorR        = mm(majorR);
                              auto c        = apply(e.p1);
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
      Debug("=========================================");
      if (!dxf.read(&collector, false)) {
            Warning("DxfImport::boundingBox: failed to read DXF: {}", path.toUtf8().constData());
            return {};
            }
      Debug("=========================================");
      return collector.result();
      }

//---------------------------------------------------------
//   DxfImport::importAt
//    Import a DXF file and offset all created elements so
//    that the bounding box's bottom-left corner is at (x, y).
//---------------------------------------------------------

bool DxfImport::importAt(ZCam* zcam, const QString& path, double x, double y) {
      // Import normally, then shift the layer position using the
      // bounding box computed from the imported elements — this
      // avoids reading the DXF file a second time (boundingBox()
      // would parse it again via DxfBBoxCollector).
      bool ok = import(zcam, path);
      if (!ok)
            return false;

      // The import creates a new layer as the last child of CAD.
      Cad* cad = zcam->project()->cad();
      if (!cad || cad->children().isEmpty())
            return true;
      auto& kids         = cad->children();
      Element* lastChild = kids.last();
      if (!lastChild)
            return true;
      auto* lastGroup = qobject_cast<Group*>(lastChild);
      if (!lastGroup)
            return true;
      // Compute the bounding box from the imported elements.
      QRectF bbox = lastGroup->childrenBoundingBox();
      if (bbox.isNull() || bbox.isEmpty())
            return true;
      double offX = x - bbox.left();
      double offY = y - bbox.top();
      lastGroup->set_pos(QVector3D(offX, offY, 0));
      return true;
      }
