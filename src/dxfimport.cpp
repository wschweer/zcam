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

#include <QFileInfo>
#include <QString>
#include <QVector3D>
#include <format>
#include <cmath>
#include <limits>
#include <numbers>
#include <algorithm>
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
      Group* m_defaultLayer;     ///< the single root layer created for this import
      Group* m_parent {nullptr}; ///< current insertion parent inside the import layer
      Group* m_activeBlock {nullptr}; ///< block definition group of the insert being expanded
      int m_parentDepth {0};     ///< recursion guard for nested block expansion
      double m_unitScale;        ///< conversion factor to mm
      QString m_baseName;        ///< file base name for element naming
      std::unordered_map<std::string, Group*>
          m_dxfLayerMap; ///< dxf layer name -> Group inside the import layer
      std::unordered_map<std::string, Group*>
          m_blockGroupMap; ///< block name -> Group inside the import layer
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
            // Insert (nested block reference)
            std::string block; ///< referenced block name
            double xscale {1.0};
            double yscale {1.0};
            };
      std::unordered_map<std::string, std::vector<BlockEntity>> m_blocks;
      std::string m_currentBlockName;
      bool m_inBlock {false};
      int m_insertCounter {0};

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
                  e.type  = BlockEntity::Type::Point;
                  e.p1    = data.basePoint;
                  e.layer = data.layer;
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
            poly->update();
            insertElement(poly, entityParent(data.layer));
            }
      void addLine(const DRW_Line& data) override {
            if (m_inBlock) {
                  BlockEntity e;
                  e.type  = BlockEntity::Type::Line;
                  e.p1    = data.basePoint;
                  e.p2    = data.secPoint;
                  e.layer = data.layer;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            auto* poly = new Polygon(m_zcam, m_parent);
            poly->setName(elementName("Line"));
            poly->moveTo(mm2d(data.basePoint));
            poly->lineTo(mm2d(data.secPoint));
            poly->set_lineWidth(0.0);
            poly->set_fill(false);
            poly->update();
            insertElement(poly, entityParent(data.layer));
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
                  e.layer    = data.layer;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            auto pp = arcToPainterPath(data);
            if (pp.empty())
                  return;
            auto* poly = new Polygon(m_zcam, m_parent);
            poly->setName(elementName("Arc"));
            poly->setPainterPath(pp);
            poly->set_lineWidth(0.0);
            poly->set_fill(false);
            poly->update();
            insertElement(poly, entityParent(data.layer));
            }
      void addCircle(const DRW_Circle& data) override {
            if (m_inBlock) {
                  BlockEntity e;
                  e.type   = BlockEntity::Type::Circle;
                  e.p1     = data.basePoint;
                  e.radius = data.radious;
                  e.layer  = data.layer;
                  m_blocks[m_currentBlockName].push_back(std::move(e));
                  return;
                  }
            double r  = mm(data.radious);
            auto* ell = new Ellipse(m_zcam, m_parent);
            ell->setName(elementName("Circle"));
            ell->set_pos(QVector3D(mm(data.basePoint.x), mm(data.basePoint.y), 0.0));
            ell->set_size(QVector2D(r * 2.0, r * 2.0));
            ell->update();
            insertElement(ell, entityParent(data.layer));
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
                  e.layer    = data.layer;
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
                  auto* ell = new Ellipse(m_zcam, m_parent);
                  ell->setName(elementName("Ellipse"));
                  ell->set_pos(QVector3D(mm(data.basePoint.x), mm(data.basePoint.y), 0.0));
                  ell->set_size(QVector2D(majorR * 2.0, minorR * 2.0));
                  ell->set_rot(QVector3D(0.0, 0.0, rotDeg));
                  ell->update();
                  insertElement(ell, entityParent(data.layer));
                  }
            else {
                  // Elliptical arc → tessellate into a polygon
                  auto pp = ellipseArcToPainterPath(data);
                  if (pp.empty())
                        return;
                  auto* poly = new Polygon(m_zcam, m_parent);
                  poly->setName(elementName("EllipseArc"));
                  poly->setPainterPath(pp);
                  poly->set_lineWidth(0.0);
                  poly->set_fill(false);
                  poly->update();
                  insertElement(poly, entityParent(data.layer));
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
                  e.layer = data.layer;
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
            poly->update();
            insertElement(poly, entityParent(data.layer));
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
                  e.layer = data.layer;
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
            poly->update();
            insertElement(poly, entityParent(data.layer));
            }
      void addSpline(const DRW_Spline* data) override {
            if (!data || data->controllist.empty())
                  return;

            if (m_inBlock) {
                  BlockEntity e;
                  e.type   = BlockEntity::Type::Spline;
                  e.degree = data->degree;
                  e.layer  = data->layer;
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

            auto pts = DxfTess::evaluateSpline(data->degree, controls, knots, curveResolution());
            if (pts.empty())
                  return;

            auto* poly = new Polygon(m_zcam, m_parent);
            poly->setName(elementName("Spline"));
            bool first = true;
            for (const auto& pt : pts) {
                  Vec2d p(mm(pt.x()), mm(pt.y()));
                  if (first) {
                        poly->moveTo(p);
                        first = false;
                        }
                  else {
                        poly->lineTo(p);
                        }
                  }
            poly->set_lineWidth(0.0);
            poly->set_fill(false);
            poly->update();
            insertElement(poly, entityParent(data->layer));
            }
      void addKnot(const DRW_Entity&) override {}
      void addInsert(const DRW_Insert& data) override {
            ++m_insertCounter;

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
            poly->update();
            insertElement(poly, entityParent(data.layer));
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
            poly->update();
            insertElement(poly, entityParent(data.layer));
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
            poly->update();
            insertElement(poly, entityParent(data.layer));
            }
      void addMText(const DRW_MText& data) override {
            addTextEntity(data.basePoint, data.height, data.text, data.angle, data.layer);
            }
      void addText(const DRW_Text& data) override {
            addTextEntity(data.basePoint, data.height, data.text, data.angle, data.layer);
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
      //    Elements expanded into a block definition group are
      //    shared by all INSERTs of that block: identical copies
      //    created while expanding further instances are skipped.
      //---------------------------------------------------------

      void insertElement(Element3d* el, Group* parent = nullptr) {
            parent = parent ? parent : m_defaultLayer;
            if (isBlockGroup(parent) && hasIdenticalChild(parent, el)) {
                  // Same block entity expanded again for another
                  // instance of the same block: one shared copy in
                  // the block definition group is enough.
                  delete el;
                  return;
                  }
            auto cmd = new InsertElementCommand(m_zcam, parent, el, -1);
            m_zcam->project()->undo()->push(cmd);
            }

      //---------------------------------------------------------
      //   isBlockGroup
      //---------------------------------------------------------

      bool isBlockGroup(const Group* g) const {
            for (const auto& [name, grp] : m_blockGroupMap) {
                  if (grp == g)
                        return true;
                  }
            return false;
            }

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
                              if (a->vertices() == b->vertices()
                                  && std::ranges::equal(a->painterPathData(), b->painterPathData(),
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
      //    given dxf layer.  Inside block expansion entities on
      //    layer "0" belong to the current insert instance (DXF
      //    resolves them against the layer of the INSERT); entities
      //    on any other layer belong to the block definition group
      //    so that all instances share that geometry.
      //---------------------------------------------------------

      Group* entityParent(const std::string& layer) {
            if (m_parentDepth > 0) {
                  if (layer.empty() || layer == "0")
                        return m_parent;
                  return blockParent();
                  }
            return dxfLayerGroup(QString::fromStdString(layer));
            }
      //---------------------------------------------------------
      //   blockParent
      //    The block definition group whose entities are
      //    currently being expanded.  Entities on a named (non
      //    "0") dxf layer are added there once so every INSERT
      //    shares the same geometry.
      //---------------------------------------------------------

      Group* blockParent() const {
            return m_activeBlock ? m_activeBlock : m_defaultLayer;
            }
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

      void expandInsert(const std::string& blockName, const DRW_Coord& base, double rotation, double sx,
                        double sy) {
            auto it = m_blocks.find(blockName);
            if (it == m_blocks.end() || it->second.empty())
                  return;

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
            Group* target = entityParent(e.layer);
            switch (e.type) {
                  case BlockEntity::Type::Line: {
                        auto* poly = new Polygon(m_zcam, m_parent);
                        poly->setName(elementName("BlockLine"));
                        poly->moveTo(mm2d(e.p1));
                        poly->lineTo(mm2d(e.p2));
                        poly->set_lineWidth(0.0);
                        poly->set_fill(false);
                        poly->update();
                        insertElement(poly, target);
                        break;
                        }
                  case BlockEntity::Type::Circle: {
                        double r  = mm(e.radius);
                        auto* ell = new Ellipse(m_zcam, m_parent);
                        ell->setName(elementName("BlockCircle"));
                        ell->set_pos(QVector3D(mm(e.p1.x), mm(e.p1.y), 0.0));
                        ell->set_size(QVector2D(r * 2.0, r * 2.0));
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
                        poly->update();
                        insertElement(poly, target);
                        break;
                        }
                  case BlockEntity::Type::Spline: {
                        auto pts =
                            DxfTess::evaluateSpline(e.degree, e.controlPoints, e.knots, curveResolution());
                        if (pts.empty())
                              break;
                        auto* poly = new Polygon(m_zcam, m_parent);
                        poly->setName(elementName("BlockSpline"));
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
                        poly->set_lineWidth(0.0);
                        poly->set_fill(false);
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
                              ell->update();
                              insertElement(ell, target);
                              }
                        else {
                              PainterPath pp;
                              double cx = mm(e.p1.x);
                              double cy = mm(e.p1.y);
                              double sa = e.staparam;
                              double ea = e.endparam;
                              if (sa > ea)
                                    ea += 2.0 * std::numbers::pi;
                              double sweep = ea - sa;
                              int segs     = DxfTess::ellipseSegments(sweep, circleResolution());
                              double step  = sweep / segs;
                              double cosR  = std::cos(rotation);
                              double sinR  = std::sin(rotation);
                              bool firstPt = true;
                              for (int i = 0; i <= segs; ++i) {
                                    double t  = sa + step * i;
                                    double ex = majorR * std::cos(t);
                                    double ey = minorR * std::sin(t);
                                    double rx = ex * cosR - ey * sinR;
                                    double ry = ex * sinR + ey * cosR;
                                    Vec2d pt(rx + cx, ry + cy);
                                    if (firstPt) {
                                          pp.moveTo(pt);
                                          firstPt = false;
                                          }
                                    else
                                          pp.lineTo(pt);
                                    }
                              auto* poly = new Polygon(m_zcam, m_parent);
                              poly->setName(elementName("BlockEllipseArc"));
                              poly->setPainterPath(pp);
                              poly->set_lineWidth(0.0);
                              poly->set_fill(false);
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
                         const std::string& layer) {
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
            textEl->update();
            insertElement(textEl, entityParent(layer));
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
      //    Tessellate an arc described by center, radius and the
      //    start/end angles (radians, drawing units) into a
      //    PainterPath.
      //---------------------------------------------------------
      PainterPath arcToPainterPath(const DRW_Coord& center, double radius, double startAngle, double endAngle,
                                   int isccw) {
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
            int segs     = DxfTess::circleSegments(sweep, circleResolution());
            double step  = sweep / segs;

            bool first = true;
            for (int i = 0; i <= segs; ++i) {
                  double a = sa + step * i;
                  Vec2d pt(std::cos(a) * r + cx, std::sin(a) * r + cy);
                  if (first) {
                        pp.moveTo(pt);
                        first = false;
                        }
                  else
                        pp.lineTo(pt);
                  }
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
            double sweep = ea - sa;

            int segs    = DxfTess::ellipseSegments(sweep, circleResolution());
            double step = sweep / segs;
            double cosR = std::cos(rotation);
            double sinR = std::sin(rotation);

            bool first = true;
            for (int i = 0; i <= segs; ++i) {
                  double t  = sa + step * i;
                  double ex = majorR * std::cos(t);
                  double ey = minorR * std::sin(t);
                  // Rotate
                  double rx = ex * cosR - ey * sinR;
                  double ry = ex * sinR + ey * cosR;
                  Vec2d pt(rx + cx, ry + cy);
                  if (first) {
                        pp.moveTo(pt);
                        first = false;
                        }
                  else
                        pp.lineTo(pt);
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

            int segs    = DxfTess::circleSegments(std::abs(endAng - startAng), circleResolution());
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

      // All undo commands generated during the DXF import (layer insertion,
      // laser layer insertion, and individual entity insertions) are wrapped
      // in a single macro so the entire import can be undone as one operation.
      zcam->project()->undo()->beginMacro();
            {
            auto cmd = new InsertElementCommand(zcam, cad, layer, -1);
            zcam->project()->undo()->push(cmd);
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
            auto cmd = new InsertElementCommand(zcam, fixture, ll, -1);
            zcam->project()->undo()->push(cmd);
            }

      // Read the DXF file using libdxfrw
      DxfReaderInterface reader(zcam, layer, fi.baseName());
      dxfRW dxf(path.toUtf8().constData());
      bool ok = dxf.read(&reader, false);

      zcam->project()->undo()->endMacro();

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
            m_currentBlockName = data.name;
            m_blocks[m_currentBlockName].clear();
            m_inBlock = true;
            }
      void setBlock(int) override {}
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
                              for (double a : {0.0, std::numbers::pi / 2, std::numbers::pi,
                                               3.0 * std::numbers::pi / 2}) {
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
      auto& kids         = cad->children();
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
