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

#include "dxfexport.h"
#include "dxfimport.h"

#include "zcam.h"
#include "project.h"
#include "cad.h"
#include "cam.h"
#include "fixture.h"
#include "group.h"
#include "recipe.h"
#include "polygon.h"
#include "rectangle.h"
#include "ellipse.h"
#include "text.h"
#include "logger.h"
#include "types.h"
#include "mop.h"
#include "laser_mop.h"

#include <QFileInfo>
#include <QString>
#include <QVector3D>
#include <QColor>
#include <QMatrix4x4>
#include <cmath>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

#include "libdxfrw.h"
#include "drw_interface.h"
#include "drw_base.h"
#include "drw_entities.h"
#include "drw_objects.h"
#include "drw_header.h"

namespace {
using namespace DRW;

//--------------------------------------------------------------------
//     DxfWriterInterface
//--------------------------------------------------------------------
//   Implements DRW_Interface to drive libdxfrw's DXF writer.
//   The CAD element tree is walked recursively; each element is
//   transformed to world (root) coordinates and emitted as one or
//   more DXF entities.  Layers are created per unique colour so
//   that DXF consumers can group geometry by colour.
//
//   Supported element types:
//     - Polygon → LWPOLYLINE (line segments) + SPLINE (cubic bezier
//       segments exported as degree-3 B-splines)
//     - Rectangle → LWPOLYLINE (4 vertices, closed)
//     - Ellipse → ELLIPSE entity (full ellipse) or LWPOLYLINE (arc)
//     - Text → TEXT entity
//     - Group / Cad → recurse into children
//--------------------------------------------------------------------

class DxfWriterInterface final : public DRW_Interface
   {
   ZCam* m_zcam;
   Cad* m_cad;
   dxfRW* m_dxf;
   std::unordered_map<int, std::string> m_colorToLayerName; ///< ACI colour → DXF layer name
   std::vector<int> m_layerColors;                           ///< all ACI colours seen, in insertion order
   int m_nextHandle {1};

   public:
      DxfWriterInterface(ZCam* zcam, Cad* cad, dxfRW* dxf)
         : m_zcam(zcam), m_cad(cad), m_dxf(dxf) {}

      void run();

   private:
      //--------------------------------------------------------------------
      //     resolveAciColor
      //--------------------------------------------------------------------
      //   Map a QColor to the nearest ACI (AutoCAD Color Index) value.
      //   This uses the libdxfrw dxfColors table (256 entries, index 0
      //   unused, 1..255).  Returns 7 (white/black) as fallback.
      //--------------------------------------------------------------------
      static int resolveAciColor(const QColor& color) {
         if (!color.isValid())
            return 7;
         int bestAci = 7;
         int bestDist = std::numeric_limits<int>::max();
         for (int i = 1; i <= 255; ++i) {
            int dr = color.red()   - dxfColors[i][0];
            int dg = color.green() - dxfColors[i][1];
            int db = color.blue()  - dxfColors[i][2];
            int dist = dr * dr + dg * dg + db * db;
            if (dist < bestDist) {
               bestDist = dist;
               bestAci  = i;
               }
            }
         return bestAci;
         }

      //--------------------------------------------------------------------
      //     layerNameForColor
      //--------------------------------------------------------------------
      //   Return a unique DXF layer name for the given ACI colour.
      //   Layers are named "ZCam_<aci>" and created on first use.
      //--------------------------------------------------------------------
      const std::string& layerNameForColor(int aci) {
         auto it = m_colorToLayerName.find(aci);
         if (it != m_colorToLayerName.end())
            return it->second;
         std::string name = "ZCam_" + std::to_string(aci);
         m_colorToLayerName[aci] = name;
         m_layerColors.push_back(aci);
         return m_colorToLayerName[aci];
         }

      //--------------------------------------------------------------------
      //     transformPoint
      //--------------------------------------------------------------------
      //   Transform a 2D local coordinate through an element's
      //   globalMatrix() to get the world (root CAD) coordinate.
      //   The Z component is dropped (flat 2D export).
      //--------------------------------------------------------------------
      static QPointF transformPoint(const QMatrix4x4& m, double x, double y) {
         QVector3D p = m.map(QVector3D(float(x), float(y), 0.0f));
         return QPointF(double(p.x()), double(p.y()));
         }

      //--------------------------------------------------------------------
      //     resolveElementColor
      //--------------------------------------------------------------------
      //   Get the effective colour for an element: prefer the effective
      //   LaserMop colour, fall back to the element's own _color, then
      //   to a default grey.
      //--------------------------------------------------------------------
      static QColor resolveElementColor(const Element3d* e) {
         Mop* mop = e->effectiveMop();
         if (mop) {
            QColor c = mop->mopColor();
            if (c.isValid())
               return c;
            }
         QColor c = e->color();
         if (c.isValid())
            return c;
         return QColor(128, 128, 128);
         }

      void exportElement(const Element3d* e);

      void writePolygon(const Polygon* poly, const QMatrix4x4& gm, int aci);
      void writeRectangle(const Rectangle* rect, const QMatrix4x4& gm, int aci);
      void writeEllipse(const Ellipse* ell, const QMatrix4x4& gm, int aci);
      void writeText(const Text* txt, const QMatrix4x4& gm, int aci);

      // --- DRW_Interface overrides (write side) ---
      // These are called by dxfRW::write() during the DXF file
      // construction.  We only need writeHeader, writeLayers,
      // writeEntities, and the stubs for the rest.
      void writeHeader(DRW_Header& data) override;
      void writeBlocks() override {}
      void writeBlockRecords() override {}
      void writeEntities() override;
      void writeLTypes() override;
      void writeLayers() override;
      void writeTextstyles() override;
      void writeVports() override;
      void writeDimstyles() override;
      void writeObjects() override {}
      void writeAppId() override;

      // --- DRW_Interface overrides (read side – unused, stubs) ---
      void addHeader(const DRW_Header*) override {}
      void addLType(const DRW_LType&) override {}
      void addLayer(const DRW_Layer&) override {}
      void addDimStyle(const DRW_Dimstyle&) override {}
      void addVport(const DRW_Vport&) override {}
      void addTextStyle(const DRW_Textstyle&) override {}
      void addAppId(const DRW_AppId&) override {}
      void addBlock(const DRW_Block&) override {}
      void setBlock(const int) override {}
      void endBlock() override {}
      void addPoint(const DRW_Point&) override {}
      void addLine(const DRW_Line&) override {}
      void addRay(const DRW_Ray&) override {}
      void addXline(const DRW_Xline&) override {}
      void addArc(const DRW_Arc&) override {}
      void addCircle(const DRW_Circle&) override {}
      void addEllipse(const DRW_Ellipse&) override {}
      void addLWPolyline(const DRW_LWPolyline&) override {}
      void addPolyline(const DRW_Polyline&) override {}
      void addSpline(const DRW_Spline*) override {}
      void addKnot(const DRW_Entity&) override {}
      void addInsert(const DRW_Insert&) override {}
      void addTrace(const DRW_Trace&) override {}
      void add3dFace(const DRW_3Dface&) override {}
      void addSolid(const DRW_Solid&) override {}
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
   };

//---------------------------------------------------------
//   DxfWriterInterface::run
//    Walk the CAD tree and collect all entities, then write
//    the DXF file via dxfRW::write().
//---------------------------------------------------------

void DxfWriterInterface::run() {
   m_dxf->write(this, DRW::AC1015, false);
   }

//---------------------------------------------------------
//   DxfWriterInterface::writeHeader
//---------------------------------------------------------

void DxfWriterInterface::writeHeader(DRW_Header& data) {
   data.addStr("$ACADVER", "AC1015", 1);
   data.addStr("$DWGCODEPAGE", "ANSI_1252", 3);
   data.addDouble("$INSBASE", 0.0, 10);
   data.addDouble("$INSBASE", 0.0, 20);
   data.addDouble("$INSBASE", 0.0, 30);
   data.addDouble("$EXTMIN", 0.0, 10);
   data.addDouble("$EXTMIN", 0.0, 20);
   data.addDouble("$EXTMAX", 0.0, 10);
   data.addDouble("$EXTMAX", 0.0, 20);
   data.addInt("$MEASUREMENT", 1, 70);  // metric
   // $INSUNITS = 4 (Millimeter).  This is critical: the DXF importer
   // uses $INSUNITS to determine the coordinate unit scale.  Without
   // it (or with value 0 = "unspecified"), the importer falls back to
   // pixel-unit scaling (1/dpmm), making the imported objects tiny.
   // Setting it to 4 (mm) ensures a 1:1 round-trip since ZCam stores
   // all CAD coordinates in millimetres.
   data.addInt("$INSUNITS", 4, 70);    // millimeter
   }

//---------------------------------------------------------
//   DxfWriterInterface::writeVports
//---------------------------------------------------------

void DxfWriterInterface::writeVports() {
   DRW_Vport vp;
   vp.name = "*Active";
   m_dxf->writeVport(&vp);
   }

//---------------------------------------------------------
//   DxfWriterInterface::writeLTypes
//---------------------------------------------------------

void DxfWriterInterface::writeLTypes() {
   DRW_LType lt;
   lt.name = "CONTINUOUS";
   lt.desc = "Solid line";
   lt.size  = 0;
   lt.length = 0.0;
   m_dxf->writeLineType(&lt);

   // Also write BYLAYER and BYBLOCK phantom linetypes
   lt.name = "BYLAYER";
   lt.desc = "";
   m_dxf->writeLineType(&lt);

   lt.name = "BYBLOCK";
   lt.desc = "";
   m_dxf->writeLineType(&lt);
   }

//---------------------------------------------------------
//   DxfWriterInterface::writeLayers
//---------------------------------------------------------

void DxfWriterInterface::writeLayers() {
   // Always write layer 0
   DRW_Layer layer0;
   layer0.name = "0";
   layer0.color = 7;
   layer0.lineType = "CONTINUOUS";
   m_dxf->writeLayer(&layer0);

   // Write one layer per colour seen during entity collection.
   // The layers must be created before entities are written, so we
   // need to pre-scan the CAD tree here.
   std::function<void(const Element3d*)> scan = [&](const Element3d* root) {
      for (const Element* c : root->children()) {
         const auto* e3d = qobject_cast<const Element3d*>(c);
         if (!e3d || !e3d->show())
            continue;
         QColor col = resolveElementColor(e3d);
         int aci = resolveAciColor(col);
         layerNameForColor(aci);
         scan(e3d);
         }
      };
   scan(m_cad);

   for (int aci : m_layerColors) {
      DRW_Layer layer;
      layer.name = layerNameForColor(aci);
      layer.color = aci;
      layer.lineType = "CONTINUOUS";
      m_dxf->writeLayer(&layer);
      }
   }

//---------------------------------------------------------
//   DxfWriterInterface::writeTextstyles
//---------------------------------------------------------

void DxfWriterInterface::writeTextstyles() {
   DRW_Textstyle ts;
   ts.name = "STANDARD";
   ts.font = "txt";
   ts.width = 1.0;
   ts.lastHeight = 1.0;
   m_dxf->writeTextstyle(&ts);
   }

//---------------------------------------------------------
//   DxfWriterInterface::writeDimstyles
//---------------------------------------------------------

void DxfWriterInterface::writeDimstyles() {
   DRW_Dimstyle ds;
   ds.name = "STANDARD";
   m_dxf->writeDimstyle(&ds);
   }

//---------------------------------------------------------
//   DxfWriterInterface::writeAppId
//---------------------------------------------------------

void DxfWriterInterface::writeAppId() {
   DRW_AppId ai;
   ai.name = "ZCAM";
   m_dxf->writeAppId(&ai);
   }

//---------------------------------------------------------
//   DxfWriterInterface::writeEntities
//    Recursively walk the CAD tree and write DXF entities for
//    every visible leaf element (Polygon, Rectangle, Ellipse,
//    Text).  Group/Cad elements are traversed but emit nothing
//    themselves.
//---------------------------------------------------------

void DxfWriterInterface::writeEntities() {
   std::function<void(const Element3d*)> walk = [&](const Element3d* root) {
      for (const Element* c : root->children()) {
         const auto* e3d = qobject_cast<const Element3d*>(c);
         if (!e3d || !e3d->show())
            continue;
         exportElement(e3d);
         walk(e3d);
         }
      };
   walk(m_cad);
   }

//---------------------------------------------------------
//   DxfWriterInterface::exportElement
//    Dispatch one Element3d to the right writer function.
//    The element's globalMatrix() transforms local 2D coordinates
//    to world (root CAD) coordinates.
//---------------------------------------------------------

void DxfWriterInterface::exportElement(const Element3d* e) {
   QMatrix4x4 gm = e->globalMatrix();
   QColor col = resolveElementColor(e);
   int aci = resolveAciColor(col);
   const std::string& layer = layerNameForColor(aci);

   if (isType<Polygon>(e)) {
      writePolygon(static_cast<const Polygon*>(e), gm, aci);
      }
   else if (isType<Rectangle>(e)) {
      writeRectangle(static_cast<const Rectangle*>(e), gm, aci);
      }
   else if (isType<Ellipse>(e)) {
      writeEllipse(static_cast<const Ellipse*>(e), gm, aci);
      }
   else if (isType<Text>(e)) {
      writeText(static_cast<const Text*>(e), gm, aci);
      }
   // Other element types (Groups, BREP, Image, Camera) are skipped;
   // their children are visited by the caller.
   }

//---------------------------------------------------------
//   DxfWriterInterface::writePolygon
//    Serialize a Polygon's painter path to DXF.  Line segments
//    (MoveTo + consecutive LineTo) are collected into one
//    LWPOLYLINE entity.  Cubic bezier segments (CurveTo +
//    CurveToData1 + CurveToData2) are emitted as individual SPLINE
//    entities (degree 3 B-spline with 4 control points).
//
//    Multiple sub-paths (separated by MoveTo) produce separate
//    LWPOLYLINE entities.
//
//    PainterPath convention:
//      CurveTo      = first control point (c1)
//      CurveToData1 = second control point (c2)
//      CurveToData2 = end point
//    The start point is the preceding element's position.
//
//    DXF SPLINE for a cubic bezier: degree=3, 4 control points,
//    knot vector = [0 0 0 0 1 1 1 1] (clamped, uniform).
//---------------------------------------------------------

void DxfWriterInterface::writePolygon(const Polygon* poly, const QMatrix4x4& gm, int aci) {
   const PainterPath& pp = poly->painterPathData();
   if (pp.empty())
      return;

   const std::string& layer = layerNameForColor(aci);
   const int n = int(pp.size());
   // Only mark the LWPOLYLINE as closed (flags & 1) when the element
   // is actually filled.  The DXF importer maps flags & 1 → set_fill(true),
   // so an unfilled closed contour must keep flags = 0 and close the path
   // with an explicit duplicate end vertex instead.
   const bool fill = poly->fill();

   // Collect line-segment vertices for LWPOLYLINE.
   // When we encounter a CurveTo, we flush the accumulated line
   // vertices as an LWPOLYLINE, then emit a SPLINE for the bezier.
   std::vector<QPointF> linePts;
   bool hasLinePts = false;

   auto flushLines = [&]() {
      if (linePts.size() < 2) {
         linePts.clear();
         hasLinePts = false;
         return;
         }
      // Check if the polyline is geometrically closed (first == last).
      bool geoClosed = linePts.size() >= 3 &&
                       std::abs(linePts.front().x() - linePts.back().x()) < 1e-6 &&
                       std::abs(linePts.front().y() - linePts.back().y()) < 1e-6;
      // Only set the DXF "closed" flag when the element is filled.
      // When unfilled but geometrically closed, keep flags = 0 and
      // write the duplicate closing vertex as a regular vertex so the
      // importer does not set fill = true.
      bool dxfClosed = fill && geoClosed;
      DRW_LWPolyline lw;
      lw.layer = layer;
      lw.color = aci;
      lw.lineType = "BYLAYER";
      lw.flags = dxfClosed ? 1 : 0;
      // When dxfClosed, drop the duplicate closing vertex (AutoCAD
      // implicitly closes the polyline).  When not dxfClosed, keep
      // all vertices including the closing duplicate.
      int count = dxfClosed ? int(linePts.size()) - 1 : int(linePts.size());
      for (int i = 0; i < count; ++i) {
         auto v = std::make_shared<DRW_Vertex2D>();
         v->x = linePts[i].x();
         v->y = linePts[i].y();
         v->bulge = 0.0;
         lw.vertlist.push_back(v);
         }
      lw.vertexnum = int(lw.vertlist.size());
      m_dxf->writeLWPolyline(&lw);
      linePts.clear();
      hasLinePts = false;
      };

   for (int i = 0; i < n; ++i) {
      const PPElement& e = pp[i];
      switch (e.type) {
         case PPType::MoveTo: {
            // Start a new sub-path.
            flushLines();
            QPointF p = transformPoint(gm, e.x(), e.y());
            linePts.push_back(p);
            hasLinePts = true;
            } break;
         case PPType::LineTo: {
            QPointF p = transformPoint(gm, e.x(), e.y());
            linePts.push_back(p);
            hasLinePts = true;
            } break;
         case PPType::CurveTo: {
            // Flush accumulated line vertices first.
            flushLines();
            // The start point is the previous element's position.
            QPointF start;
            if (i > 0)
               start = transformPoint(gm, pp[i - 1].x(), pp[i - 1].y());
            QPointF c1 = transformPoint(gm, e.x(), e.y());
            QPointF c2 = transformPoint(gm, pp[i + 1].x(), pp[i + 1].y());
            QPointF end = transformPoint(gm, pp[i + 2].x(), pp[i + 2].y());

            // Emit as a degree-3 B-spline (cubic bezier) SPLINE entity.
            DRW_Spline sp;
            sp.layer = layer;
            sp.color = aci;
            sp.lineType = "BYLAYER";
            sp.normalVec = DRW_Coord(0.0, 0.0, 1.0);
            sp.flags = 8;   // closed=0, planar=8
            sp.degree = 3;
            sp.nknots = 8;
            sp.ncontrol = 4;
            sp.nfit = 0;
            // Clamped uniform knot vector for a single cubic segment.
            sp.knotslist = {0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0};
            // Control points: start, c1, c2, end
            sp.controllist.push_back(std::make_shared<DRW_Coord>(start.x(), start.y(), 0.0));
            sp.controllist.push_back(std::make_shared<DRW_Coord>(c1.x(), c1.y(), 0.0));
            sp.controllist.push_back(std::make_shared<DRW_Coord>(c2.x(), c2.y(), 0.0));
            sp.controllist.push_back(std::make_shared<DRW_Coord>(end.x(), end.y(), 0.0));
            m_dxf->writeSpline(&sp);

            // After the bezier, the "current point" is end.
            // Continue collecting line vertices from end.
            linePts.push_back(end);
            hasLinePts = true;
            i += 2; // skip CurveToData1 and CurveToData2
            } break;
         case PPType::CurveToData1:
         case PPType::CurveToData2:
            // Consumed by CurveTo case; skip.
            break;
         }
      }
   flushLines();
   }

//---------------------------------------------------------
//   DxfWriterInterface::writeRectangle
//    Export a Rectangle as a closed LWPOLYLINE with 4 vertices.
//    The rectangle is centered at the local origin with extents
//    (-w/2, -h/2) to (+w/2, +h/2), transformed by globalMatrix().
//---------------------------------------------------------

void DxfWriterInterface::writeRectangle(const Rectangle* rect, const QMatrix4x4& gm, int aci) {
   const std::string& layer = layerNameForColor(aci);
   QRectF r = rect->rectangle();

   // Only set the DXF "closed" flag when the element is filled.
   // The DXF importer maps flags & 1 → set_fill(true).
   bool dxfClosed = rect->fill();

   DRW_LWPolyline lw;
   lw.layer = layer;
   lw.color = aci;
   lw.lineType = "BYLAYER";
   lw.flags = dxfClosed ? 1 : 0;

   double corners[4][2] = {
      {r.left(),  r.bottom()},
      {r.right(), r.bottom()},
      {r.right(), r.top()},
      {r.left(),  r.top()}
   };
   for (int i = 0; i < 4; ++i) {
      QPointF p = transformPoint(gm, corners[i][0], corners[i][1]);
      auto v = std::make_shared<DRW_Vertex2D>();
      v->x = p.x();
      v->y = p.y();
      v->bulge = 0.0;
      lw.vertlist.push_back(v);
      }
   // When not closed in DXF, add a closing vertex to keep the
   // geometric contour closed without triggering fill on reimport.
   if (!dxfClosed) {
      QPointF p = transformPoint(gm, corners[0][0], corners[0][1]);
      auto v = std::make_shared<DRW_Vertex2D>();
      v->x = p.x();
      v->y = p.y();
      v->bulge = 0.0;
      lw.vertlist.push_back(v);
      }
   lw.vertexnum = int(lw.vertlist.size());
   m_dxf->writeLWPolyline(&lw);
   }

//---------------------------------------------------------
//   DxfWriterInterface::writeEllipse
//    Export an Ellipse as a DXF ELLIPSE entity.
//    ZCam stores the ellipse as a 2D size (width × height)
//    centered at the local origin.  The DXF ELLIPSE entity
//    uses a center point, a major-axis endpoint (secPoint),
//    and a ratio (minor/major).  For a full circle/ellipse
//    the start parameter is 0 and end parameter is 2*PI.
//
//    For partial arcs (startAngle != 0 or endAngle != 360),
//    the parameters are in radians (0..2*PI).
//
//    The element's rotation (Z-axis) is baked into the
//    major-axis direction via globalMatrix().
//---------------------------------------------------------

void DxfWriterInterface::writeEllipse(const Ellipse* el, const QMatrix4x4& gm, int aci) {
   const std::string& layer = layerNameForColor(aci);
   QRectF r = el->ellipseRect();
   double rx = r.width() * 0.5;
   double ry = r.height() * 0.5;
   if (rx <= 0.0 || ry <= 0.0)
      return;

   // Center in world coords.
   QPointF center = transformPoint(gm, 0.0, 0.0);

   // Major axis endpoint: the major axis is along X (local).
   // We pick the larger axis as the major axis.
   double major, minor;
   if (rx >= ry) {
      major = rx;
      minor = ry;
      }
   else {
      major = ry;
      minor = rx;
      }
   double ratio = minor / major;

   // Major axis endpoint in local coords: along the major axis direction.
   double localMajX, localMajY;
   if (rx >= ry) {
      localMajX = major;
      localMajY = 0.0;
      }
   else {
      localMajX = 0.0;
      localMajY = major;
      }
   QPointF majEnd = transformPoint(gm, localMajX, localMajY);
   // The major axis vector is majEnd - center.
   double majVecX = majEnd.x() - center.x();
   double majVecY = majEnd.y() - center.y();

   DRW_Ellipse dxfEll;
   dxfEll.layer = layer;
   dxfEll.color = aci;
   dxfEll.lineType = "BYLAYER";
   dxfEll.basePoint = DRW_Coord(center.x(), center.y(), 0.0);
   dxfEll.secPoint  = DRW_Coord(majVecX, majVecY, 0.0);
   dxfEll.ratio = ratio;
   dxfEll.staparam = el->startAngle() * M_PI / 180.0;
   dxfEll.endparam = el->endAngle() * M_PI / 180.0;
   dxfEll.isccw = 1;
   m_dxf->writeEllipse(&dxfEll);
   }

//---------------------------------------------------------
//   DxfWriterInterface::writeText
//    Export a Text element as a DXF TEXT entity.
//    The text position is the element's local origin mapped
//    through globalMatrix().  The font height is converted
//    from ZCam's internal pointSize (in pt × FONT_SCALE_UP ×
//    FONT_SCALE) to mm.  The text content is taken from the
//    Text::text() property; multi-line text is split into
//    separate TEXT entities, one per line.
//---------------------------------------------------------

void DxfWriterInterface::writeText(const Text* txt, const QMatrix4x4& gm, int aci) {
   const std::string& layer = layerNameForColor(aci);
   if (txt->text().isEmpty())
      return;

   QPointF origin = transformPoint(gm, 0.0, 0.0);
   // ZCam text height in mm: pointSize * FONT_SCALE_UP * FONT_SCALE
   // where FONT_SCALE = 0.352778 * 0.1 and FONT_SCALE_UP = 10.0
   // → height_mm = pointSize * 0.352778
   double heightMm = txt->pointSize() * FONT_SCALE * FONT_SCALE_UP;

   QStringList lines = txt->text().split('\n');
   for (int i = 0; i < lines.size(); ++i) {
      if (lines[i].isEmpty())
         continue;
      DRW_Text dxfText;
      dxfText.layer = layer;
      dxfText.color = aci;
      dxfText.lineType = "BYLAYER";
      // Stack lines vertically (each line offset by one line height
      // in the negative Y direction, matching ZCam's text layout).
      double lineOffset = i * heightMm * 1.2;
      dxfText.basePoint = DRW_Coord(origin.x(), origin.y() - lineOffset, 0.0);
      dxfText.secPoint  = DRW_Coord(origin.x() + 1.0, origin.y() - lineOffset, 0.0);
      dxfText.height = heightMm;
      dxfText.text = lines[i].toUtf8().constData();
      dxfText.style = "STANDARD";
      dxfText.angle = 0.0;
      dxfText.alignH = DRW_Text::HLeft;
      dxfText.alignV = DRW_Text::VBaseLine;
      m_dxf->writeText(&dxfText);
      }
   }

} // anonymous namespace

//---------------------------------------------------------
//   DxfExport::exportDxf
//    Public entry point: create a dxfRW writer, implement the
//    DRW_Interface and write the CAD tree to a DXF file.
//---------------------------------------------------------

bool DxfExport::exportDxf(ZCam* zcam, const QString& path) {
   if (!zcam || !zcam->project()) {
      Critical("exportDxf: no project");
      return false;
      }
   Cad* cad = zcam->project()->cad();
   if (!cad) {
      Critical("exportDxf: no CAD element");
      return false;
      }

   // Check if CAD has any visible content.
   bool hasContent = false;
   std::function<void(const Element3d*)> check = [&](const Element3d* root) {
      for (const Element* c : root->children()) {
         const auto* e3d = qobject_cast<const Element3d*>(c);
         if (!e3d || !e3d->show())
            continue;
         if (isType<Polygon>(e3d) || isType<Rectangle>(e3d) ||
             isType<Ellipse>(e3d) || isType<Text>(e3d)) {
            hasContent = true;
            return;
            }
         check(e3d);
         }
      };
   check(cad);
   if (!hasContent) {
      Warning("exportDxf: CAD is empty, nothing to export");
      return false;
      }

   std::string fn = path.toLocal8Bit().constData();
   dxfRW dxf(fn.c_str());
   DxfWriterInterface iface(zcam, cad, &dxf);
   iface.run();

   Info("exportDxf: wrote {}", path);
   return true;
   }