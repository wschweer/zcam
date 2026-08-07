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

#define NANOSVG_IMPLEMENTATION
#include <nanosvg/nanosvg.h>
#include "zcam.h"
#include "polygon.h"
#include "rectangle.h"
#include "ellipse.h"
#include "text.h"
#include "group.h"
#include "cad.h"
#include "project.h"
#include "undo.h"
#include "logger.h"
#include "dxfimport.h"
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QJSEngine>
#include <QFontMetrics>
#include <limits>
#include <cmath>
#include <functional>

//---------------------------------------------------------
//   importSvg
//    NanoSVG is called with units="px" so that the path coordinates
//    are returned in SVG user-space pixels (after viewBox scaling).
//    We then convert pixels to millimetres ourselves using the
//    standard 96-DPI factor (25.4 / 96 ≈ 0.2646).
//
//    This avoids a NanoSVG bug: when units="mm" is used and the SVG
//    has width/height in mm but NO viewBox, NanoSVG treats the user
//    units as pixels and applies an extra px→mm conversion, making
//    the coordinates ~3.78× too small.
//
//    With units="px" NanoSVG does no unit conversion at all; the
//    coordinates stay in pixel space.  We apply the single correct
//    px→mm factor ourselves, which works correctly for:
//      • SVGs with width in px + viewBox  (px → mm)
//      • SVGs with width in mm + viewBox (viewBox px → image px → mm)
//      • SVGs with width in px, no viewBox (px → mm)
//---------------------------------------------------------

void ZCam::importSvg(const QString& path) {
      NSVGimage* image = nsvgParseFromFile(path.toLocal8Bit(), "px", 96);
      if (!image) {
            Critical("importSvg: cannot parse SVG file: {}", path);
            return;
            }
      Debug("loaded SVG image {}x{}", image->width, image->height);

      // Pixel → millimetre conversion factor (96 DPI).
      constexpr double pxToMm = 25.4 / 96.0;

      // SVG uses a top-left origin with the Y-axis pointing downward.
      // CAM/CAD uses a bottom-left origin with Y pointing upward.
      // Mirror all Y coordinates about the SVG height so the image is
      // not rendered upside-down.
      const double svgHeight = image->height;

      // Build a PainterPath from all shapes and sub-paths in the SVG.
      // All coordinates are converted from pixels to millimetres.
      PainterPath pp;
      for (NSVGshape* shape = image->shapes; shape; shape = shape->next) {
            for (NSVGpath* svgPath = shape->paths; svgPath; svgPath = svgPath->next) {
                  // Each sub-path consists of cubic Bezier segments
                  // (4 points / 8 floats per segment)
                  Vec2d first;
                  Vec2d last;
                  for (int i = 0; i < svgPath->npts - 1; i += 3) {
                        float* p = &svgPath->pts[i * 2];
                        Vec2d p1(p[0] * pxToMm, (svgHeight - p[1]) * pxToMm);
                        if (i == 0) {
                              pp.moveTo(p1);
                              first = p1;
                              }
                        Vec2d p2(p[2] * pxToMm, (svgHeight - p[3]) * pxToMm);
                        Vec2d p3(p[4] * pxToMm, (svgHeight - p[5]) * pxToMm);
                        Vec2d p4(p[6] * pxToMm, (svgHeight - p[7]) * pxToMm);
                        pp.cubicTo(p2, p3, p4);
                        last = p4;
                        }
                  if (svgPath->closed) {
                        if (qFuzzyCompare(first.x(), last.x()) && qFuzzyCompare(first.y(), last.y()))
                              pp.back().pos = first;
                        else
                              pp.lineTo(first);
                        }
                  }
            }

      nsvgDelete(image);
      Debug("---ok");
      if (pp.empty()) {
            Warning("importSvg: no paths found in SVG file: {}", path);
            return;
            }

      if (!_project || !_project->cad()) {
            Critical("importSvg: no project or CAD element");
            return;
            }

      Group* layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Critical("importSvg: no visible layer");
            return;
            }

      auto* poly = new Polygon(this, layer);
      poly->setPainterPath(pp);
      poly->setName(QFileInfo(path).baseName());
      poly->set_pos(QVector3D(0, 0, 0));
      poly->set_lineWidth(0);
      poly->set_fill(false);
      poly->update();
      Debug("---ok2");

      auto cmd = new InsertElementCommand(this, layer, poly, -1);
      _project->undo()->beginMacro();
      _project->undo()->push(cmd);
      _project->undo()->endMacro();

      Debug("---ok3");
      }

//---------------------------------------------------------
//   svgBoundingBox
//    Parse the SVG at the given path and compute the axis-aligned
//    bounding box of all path points, in millimetres, after the
//    Y-mirroring transformation applied during import.
//    Returns an empty QRectF if the SVG cannot be parsed or has
//    no path data.
//---------------------------------------------------------

QRectF ZCam::svgBoundingBox(const QString& path) {
      NSVGimage* image = nsvgParseFromFile(path.toLocal8Bit(), "px", 96);
      if (!image)
            return {};

      constexpr double pxToMm = 25.4 / 96.0;
      const double svgHeight  = image->height;

      double minX = std::numeric_limits<double>::max();
      double minY = std::numeric_limits<double>::max();
      double maxX = std::numeric_limits<double>::lowest();
      double maxY = std::numeric_limits<double>::lowest();

      for (NSVGshape* shape = image->shapes; shape; shape = shape->next) {
            for (NSVGpath* svgPath = shape->paths; svgPath; svgPath = svgPath->next) {
                  for (int i = 0; i < svgPath->npts; ++i) {
                        float* p = &svgPath->pts[i * 2];
                        double x = p[0] * pxToMm;
                        double y = (svgHeight - p[1]) * pxToMm;
                        minX     = std::min(minX, x);
                        minY     = std::min(minY, y);
                        maxX     = std::max(maxX, x);
                        maxY     = std::max(maxY, y);
                        }
                  }
            }

      nsvgDelete(image);

      if (minX > maxX || minY > maxY)
            return {};
      return QRectF(minX, minY, maxX - minX, maxY - minY);
      }

//---------------------------------------------------------
//   importSvgAt
//    Import an SVG file and position the resulting Polygon so
//    that the bounding box's bottom-left corner is at (x, y) in
//    the parent layer's local coordinate space.
//    The SVG path data has its origin at (0,0) in local coords.
//    After the Y-mirror, the path's bounding box starts at
//    (minX, minY).  We shift the polygon's pos by (-minX + x,
//    -minY + y) so that the bbox corner lands at (x, y).
//---------------------------------------------------------

void ZCam::importSvgAt(const QString& path, double x, double y) {
      QRectF bbox = svgBoundingBox(path);
      if (bbox.isNull() || bbox.isEmpty()) {
            importSvg(path);
            return;
            }

      // Parse the SVG and build the PainterPath (same as importSvg).
      NSVGimage* image = nsvgParseFromFile(path.toLocal8Bit(), "px", 96);
      if (!image) {
            Critical("importSvgAt: cannot parse SVG file: {}", path);
            return;
            }

      constexpr double pxToMm = 25.4 / 96.0;
      const double svgHeight  = image->height;

      PainterPath pp;
      for (NSVGshape* shape = image->shapes; shape; shape = shape->next) {
            for (NSVGpath* svgPath = shape->paths; svgPath; svgPath = svgPath->next) {
                  Vec2d first;
                  Vec2d last;
                  for (int i = 0; i < svgPath->npts - 1; i += 3) {
                        float* p = &svgPath->pts[i * 2];
                        Vec2d p1(p[0] * pxToMm, (svgHeight - p[1]) * pxToMm);
                        if (i == 0) {
                              pp.moveTo(p1);
                              first = p1;
                              }
                        Vec2d p2(p[2] * pxToMm, (svgHeight - p[3]) * pxToMm);
                        Vec2d p3(p[4] * pxToMm, (svgHeight - p[5]) * pxToMm);
                        Vec2d p4(p[6] * pxToMm, (svgHeight - p[7]) * pxToMm);
                        pp.cubicTo(p2, p3, p4);
                        last = p4;
                        }
                  if (svgPath->closed) {
                        if (qFuzzyCompare(first.x(), last.x()) && qFuzzyCompare(first.y(), last.y()))
                              pp.back().pos = first;
                        else
                              pp.lineTo(first);
                        }
                  }
            }

      nsvgDelete(image);

      if (pp.empty()) {
            Warning("importSvgAt: no paths found in SVG file: {}", path);
            return;
            }

      if (!_project || !_project->cad()) {
            Critical("importSvgAt: no project or CAD element");
            return;
            }

      Group* layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Critical("importSvgAt: no visible layer");
            return;
            }

      // Position so the bbox's bottom-left corner is at (x, y).
      double posX = x - bbox.left();
      double posY = y - bbox.top();

      auto* poly = new Polygon(this, layer);
      poly->setPainterPath(pp);
      poly->setName(QFileInfo(path).baseName());
      poly->set_pos(QVector3D(posX, posY, 0));
      poly->set_lineWidth(0);
      poly->set_fill(false);
      poly->update();

      auto cmd = new InsertElementCommand(this, layer, poly, -1);
      _project->undo()->beginMacro();
      _project->undo()->push(cmd);
      _project->undo()->endMacro();
      }

//---------------------------------------------------------
//   startSvgDrag
//    Parse the SVG, compute its bounding box, and create a
//    TessGeometry that renders the rectangle outline.  The QML
//    layer uses this geometry to show a preview box that follows
//    the mouse during drag-and-drop.
//---------------------------------------------------------

void ZCam::startSvgDrag(const QString& path) {
      _svgDragPath = path;
      _svgDragBBox = svgBoundingBox(path);

      if (_svgDragBBox.isNull() || _svgDragBBox.isEmpty()) {
            _dragPreviewGeometry = nullptr;
            emit dragPreviewGeometryChanged();
            return;
            }

      // Create a standalone TessGeometry (not tied to any element).
      if (_dragPreviewGeometry)
            delete _dragPreviewGeometry;
      _dragPreviewGeometry = new TessGeometry(nullptr);
      QJSEngine::setObjectOwnership(_dragPreviewGeometry, QJSEngine::CppOwnership);

      // Build a rectangle outline (4 edges as line segments).
      // Translate to origin so the bottom-left corner is at (0,0).
      // The QML layer positions the preview Model at the cursor
      // position, so a geometry starting at (0,0) aligns correctly.
      double rw = _svgDragBBox.width();
      double rh = _svgDragBBox.height();
      Clipper2Lib::PathD rect;
      rect.push_back({0.0, 0.0});
      rect.push_back({rw, 0.0});
      rect.push_back({rw, 0.0});
      rect.push_back({rw, rh});
      rect.push_back({rw, rh});
      rect.push_back({0.0, rh});
      rect.push_back({0.0, rh});
      rect.push_back({0.0, 0.0});
      Clipper2Lib::PathsD lines;
      lines.push_back(rect);
      _dragPreviewGeometry->setLines(lines);

      emit dragPreviewGeometryChanged();
      }

//---------------------------------------------------------
//   startDxfDrag
//    Compute the bounding box of a DXF file and create a
//    drag-preview geometry (rectangle outline) the same way
//    startSvgDrag does for SVG files.
//---------------------------------------------------------

void ZCam::startDxfDrag(const QString& path) {
      _svgDragPath = path;
      _svgDragBBox = DxfImport::boundingBox(this, path);

      if (_svgDragBBox.isNull() || _svgDragBBox.isEmpty()) {
            _dragPreviewGeometry = nullptr;
            emit dragPreviewGeometryChanged();
            return;
            }

      // Create a standalone TessGeometry (not tied to any element).
      if (_dragPreviewGeometry)
            delete _dragPreviewGeometry;
      _dragPreviewGeometry = new TessGeometry(nullptr);
      QJSEngine::setObjectOwnership(_dragPreviewGeometry, QJSEngine::CppOwnership);

      // Build a rectangle outline (4 edges as line segments).
      // Translate to origin so the bottom-left corner is at (0,0).
      double rw2 = _svgDragBBox.width();
      double rh2 = _svgDragBBox.height();
      Clipper2Lib::PathD rect2;
      rect2.push_back({0.0, 0.0});
      rect2.push_back({rw2, 0.0});
      rect2.push_back({rw2, 0.0});
      rect2.push_back({rw2, rh2});
      rect2.push_back({rw2, rh2});
      rect2.push_back({0.0, rh2});
      rect2.push_back({0.0, rh2});
      rect2.push_back({0.0, 0.0});
      Clipper2Lib::PathsD lines2;
      lines2.push_back(rect2);
      _dragPreviewGeometry->setLines(lines2);

      emit dragPreviewGeometryChanged();
      }

//---------------------------------------------------------
//   endSvgDrag
//    Clean up the drag-preview geometry.
//---------------------------------------------------------

void ZCam::endSvgDrag() {
      _svgDragPath.clear();
      _svgDragBBox = {};
      if (_dragPreviewGeometry) {
            delete _dragPreviewGeometry;
            _dragPreviewGeometry = nullptr;
            emit dragPreviewGeometryChanged();
            }
      }

//---------------------------------------------------------
//   SVG export helpers
//    ZCam/CAD uses a bottom-left origin with the Y axis pointing
//    upward, whereas SVG uses a top-left origin with Y pointing
//    downward.  All exported coordinates are therefore wrapped in
//    a root group carrying the transform
//        translate(0, H) scale(1, -1)
//    where H is the height of the content bounding box.  Inside
//    the root group all coordinates can stay in CAD space.
//    Each nested group/element emits its local transform, so the
//    CAD hierarchy maps 1:1 onto the SVG group hierarchy.
//---------------------------------------------------------

namespace SvgExport {

//---------------------------------------------------------
//   fmt
//    Format a coordinate: three decimals, no trailing zeros.
//---------------------------------------------------------

static QString fmt(double v) {
      if (std::abs(v) < 0.0005)
            v = 0.0; // avoid "-0"
      QString s = QString::number(v, 'f', 3);
      if (s.contains('.')) {
            while (s.endsWith('0'))
                  s.chop(1);
            if (s.endsWith('.'))
                  s.chop(1);
            }
      return s;
      }

//---------------------------------------------------------
//   esc
//    Escape a string for XML text / attribute content.
//---------------------------------------------------------

static QString esc(const QString& s) {
      QString escaped;
      escaped.reserve(s.size());
      for (const QChar c : s) {
            switch (c.unicode()) {
                  case '<': escaped += "&lt;"; break;
                  case '>': escaped += "&gt;"; break;
                  case '&': escaped += "&amp;"; break;
                  case '"': escaped += "&quot;"; break;
                  default:
                        // strip characters that are invalid in XML 1.0
                        if (c.unicode() >= 0x20 || c == '\t' || c == '\n' || c == '\r')
                              escaped += c;
                        break;
                  }
            }
      return escaped;
      }

//---------------------------------------------------------
//   transformAttr
//    Convert an Element3d's local matrix into an SVG transform
//    attribute.  Uses the raw matrix coefficients
//    (a b c d e f) == (m11 m21 m12 m22 m31 m32), which
//    transparently includes mirroring (negative scales) and
//    uniform scales.  RotX/RotY are ignored (flat XY export),
//    so when they are zero the simple affine formula holds.
//    Returns an empty string for the identity transform.
//---------------------------------------------------------

static QString transformAttr(const Element3d* e) {
      QMatrix4x4 m = e->matrix();
      bool flipY   = false;
      // RotX / RotY make the flat-XY export ill-defined; fall back
      // to the general 3D mapping (with Y flip) in that case.
      if (!qFuzzyCompare(e->rot().x(), 0.0f) || !qFuzzyCompare(e->rot().y(), 0.0f))
            flipY = true;
      //QMatrix4x4 is column-major: (row, col) via operator()
      qreal a = m(0, 0), b = m(1, 0), c = m(0, 1), d = m(1, 1);
      qreal tx = m(0, 3), ty = m(1, 3);
      if (flipY) {
            b  = -b;
            d  = -d;
            ty = -ty;
            }
      bool identity = qFuzzyCompare(a, 1.0) && qFuzzyIsNull(b) && qFuzzyIsNull(c) && qFuzzyCompare(d, 1.0) &&
                      qFuzzyIsNull(tx) && qFuzzyIsNull(ty);
      if (identity)
            return {};
      return QStringLiteral(" transform=\"matrix(%1 %2 %3 %4 %5 %6)\"")
          .arg(fmt(a), fmt(b), fmt(c), fmt(d), fmt(tx), fmt(ty));
      }

//---------------------------------------------------------
//   styleAttr
//    fill / stroke attributes derived from the element color
//    and the fill / lineWidth flags.
//---------------------------------------------------------

static QString styleAttr(const Element3d* e) {
      QColor color    = e->color();
      if (!color.isValid())
            color = QColor("#333333");
      QString fillCol = e->fill() ? color.name() : QStringLiteral("none");
      QString s       = QStringLiteral("fill=\"%1\"").arg(fillCol);
      if (e->lineWidth() > 0.0) {
            s += QStringLiteral(" stroke=\"%1\" stroke-width=\"%2\"").arg(color.name(), fmt(e->lineWidth()));
            }
      else if (!e->fill()) {
            // No fill and no stroke: render as a thin outline so the
            // element is visible (matching the 3D viewport behavior where
            // the path outline is always shown even when lineWidth == 0
            // and fill is off).
            s += QStringLiteral(" stroke=\"%1\" stroke-width=\"0.1\"").arg(color.name());
            }
      return s;
      }

//---------------------------------------------------------
//   polygonToSvgD
//    Serialize the raw painter path of a Polygon into an SVG
//    path "d" string.  The PainterPath element types drive the
//    command choice so cubic bezier segments stay exact (C
//    command) instead of being flattened to polylines.
//---------------------------------------------------------

static QString polygonToSvgD(const Polygon* poly) {
      const PainterPath& pp = poly->painterPathData();
      QString d;
      const int n = int(pp.size());
      // A final vertex that coincides with the sub-path start is the
      // closing segment: it is emitted as a Z command.
      bool closed    = n >= 2 && pp.front().type == PPType::MoveTo && pp.back().type == PPType::LineTo &&
                       std::abs(pp.front().x() - pp.back().x()) < 1e-6 &&
                       std::abs(pp.front().y() - pp.back().y()) < 1e-6;
      const int last = closed ? n - 1 : n; // skip the duplicate closing point
      for (int i = 0; i < last; ++i) {
            const PPElement& e = pp[i];
            switch (e.type) {
                  case PPType::MoveTo: d += QStringLiteral(" M%1 %2").arg(fmt(e.x()), fmt(e.y())); break;
                  case PPType::LineTo: d += QStringLiteral(" L%1 %2").arg(fmt(e.x()), fmt(e.y())); break;
                  case PPType::CurveTo: {
                        const PPElement& p2 = pp[i + 1];
                        const PPElement& p3 = pp[i + 2];
                        d += QStringLiteral(" C%1 %2 %3 %4 %5 %6")
                                 .arg(fmt(e.x()), fmt(e.y()), fmt(p2.x()), fmt(p2.y()), fmt(p3.x()),
                                      fmt(p3.y()));
                        i += 2;
                        } break;
                  case PPType::CurveToData1:
                  case PPType::CurveToData2: break; // consumed by CurveTo
                  }
            }
      if (closed)
            d += " Z";
      return d.mid(1); // strip leading blank
      }

      } // namespace SvgExport

//---------------------------------------------------------
//   svgExportElement
//    Recursive worker for exportSvg(): write one Element3d and
//    its children.  Group elements (layers / nested groups)
//    become SVG <g> elements; Polygon, Rectangle, Ellipse and
//    Text become <path>/<rect>/<ellipse>/<text>.  Everything
//    else is skipped (but its children are still visited).
//---------------------------------------------------------

static void svgExportElement(QTextStream& ts, const Element3d* e, int level) {
      if (!e->show())
            return;
      const QString indent(level * 2, ' ');
      using namespace SvgExport;

      if (isType<Group>(e)) {
            ts << indent << "<g id=\"" << esc(e->name()) << "\"" << transformAttr(e) << ">\n";
            for (const Element* c : e->children())
                  if (const auto* e3d = qobject_cast<const Element3d*>(c))
                        svgExportElement(ts, e3d, level + 1);
            ts << indent << "</g>\n";
            return;
            }

      if (isType<Polygon>(e)) {
            // The raw painter path (lines + cubic beziers, several
            // sub-paths possible) becomes one SVG <path>.
            const auto* poly = static_cast<const Polygon*>(e);
            QString d        = polygonToSvgD(poly);
            if (!d.isEmpty())
                  ts << indent << "<path id=\"" << esc(e->name()) << "\" d=\"" << d << "\" " << styleAttr(e)
                     << transformAttr(e) << "/>\n";
            }
      else if (isType<Rectangle>(e)) {
            const auto* rect = static_cast<const Rectangle*>(e);
            QRectF r         = rect->rectangle();
            QString tr       = transformAttr(e);
            if (qFuzzyCompare(rect->corner(), 0.0))
                  ts << indent << "<rect id=\"" << esc(e->name()) << "\" x=\"" << fmt(r.x()) << "\" y=\""
                     << fmt(r.y()) << "\" width=\"" << fmt(r.width()) << "\" height=\"" << fmt(r.height())
                     << "\" " << styleAttr(e) << tr << "/>\n";
            else
                  ts << indent << "<rect id=\"" << esc(e->name()) << "\" x=\"" << fmt(r.x()) << "\" y=\""
                     << fmt(r.y()) << "\" width=\"" << fmt(r.width()) << "\" height=\"" << fmt(r.height())
                     << "\" rx=\"" << fmt(rect->corner()) << "\" ry=\"" << fmt(rect->corner()) << "\" "
                     << styleAttr(e) << tr << "/>\n";
            }
      else if (isType<Ellipse>(e)) {
            const auto* el = static_cast<const Ellipse*>(e);
            QRectF r       = el->ellipseRect();
            ts << indent << "<ellipse id=\"" << esc(e->name()) << "\" cx=\"" << fmt(r.center().x())
               << "\" cy=\"" << fmt(r.center().y()) << "\" rx=\"" << fmt(r.width() * .5) << "\" ry=\""
               << fmt(r.height() * .5) << "\" " << styleAttr(e) << transformAttr(e) << "/>\n";
            }
      else if (isType<Text>(e)) {
            const auto* txt = static_cast<const Text*>(e);
            if (txt->text().isEmpty())
                  return;
            // The font used by the Text element renders at pointSize in
            // pt at Qt's text layout, scaled by FONT_SCALE
            // (== 0,352778/10 mm) and FONT_SCALE_UP (== 10) into
            // millimetres.  The product is exactly 0.352778 mm per
            // layout unit (pt == px @ 96 DPI), so the SVG font-size
            // in CSS px equals the point size.
            double pxSize = txt->pointSize();
            double weight = qBound(1, txt->weight(), 9) * 100; // QFont weight 1-9 -> CSS 100-900
            // Rebuild the same font that Text::update(FONT) constructs so
            // the metrics for textLength and the line height match the
            // on-screen layout.
            QFont f(txt->fontFamily());
            f.setPointSizeF(txt->pointSize() * FONT_SCALE_UP);
            f.setWeight(QFont::Weight(txt->weight()));
            f.setStretch(txt->stretch());
            f.setLetterSpacing(QFont::PercentageSpacing, txt->letterSpacing());
            f.setWordSpacing(txt->wordSpacing());
            f.setBold(txt->bold());
            f.setItalic(txt->italic());
            f.setUnderline(txt->underline());
            QFontMetrics fm(f);
            QStringList lines = txt->text().split('\n');
            // Widest line, used for the alignment anchor and for
            // textLength (forces the renderer to reproduce the Qt
            // metrics even if the viewer's font differs slightly).
            double maxW = 0.0;
            std::vector<double> widths;
            widths.reserve(lines.size());
            for (const QString& line : lines) {
                  double lw = fm.horizontalAdvance(line) * FONT_SCALE;
                  widths.push_back(lw);
                  maxW = std::max(maxW, lw);
                  }
            double lineH = fm.lineSpacing() * txt->lineSpacing() * 0.01 * FONT_SCALE; // mm
            for (int i = 0; i < lines.size(); ++i) {
                  if (lines[i].isEmpty())
                        continue;
                  double anchorX = 0.0;
                  QString anchor;
                  if (txt->align() == Qt::AlignHCenter) {
                        anchorX = maxW * .5;
                        anchor  = " text-anchor=\"middle\"";
                        }
                  else if (txt->align() == Qt::AlignRight) {
                        anchorX = maxW;
                        anchor  = " text-anchor=\"end\"";
                        }
                  ts << indent << "<text id=\"" << esc(e->name())
                     << (lines.size() > 1 ? QString::number(i + 1) : QString()) << "\" x=\"" << fmt(anchorX)
                     << "\" y=\"" << fmt(-i * lineH) << "\" font-family=\"" << esc(txt->fontFamily())
                     << "\" font-size=\"" << fmt(pxSize) << "\" font-weight=\"" << weight << "\"" << anchor;
                  if (!qFuzzyIsNull(txt->letterSpacing() - 100.0))
                        ts << " letter-spacing=\"" << fmt(pxSize * (txt->letterSpacing() - 100.0) * 0.01)
                           << "\"";
                  ts << " textLength=\"" << fmt(widths[i]) << "\" lengthAdjust=\"spacingAndGlyphs\"";
                  if (txt->italic())
                        ts << " font-style=\"italic\"";
                  if (txt->underline())
                        ts << " text-decoration=\"underline\"";
                  ts << " " << styleAttr(e) << transformAttr(e) << ">" << esc(lines[i]) << "</text>\n";
                  }
            }
      else if (!e->children().isEmpty()) {
            // Any other element type that carries children acts as a
            // group: export the children inside a transformed group.
            ts << indent << "<g id=\"" << esc(e->name()) << "\"" << transformAttr(e) << ">\n";
            for (const Element* c : e->children())
                  if (const auto* e3d = qobject_cast<const Element3d*>(c))
                        svgExportElement(ts, e3d, level + 1);
            ts << indent << "</g>\n";
            }
      }

//---------------------------------------------------------
//   exportSvg
//    Export the current project's CAD element tree to an SVG
//    file.  See SvgExport helper comments for the coordinate
//    mapping.
//---------------------------------------------------------

bool ZCam::exportSvg(const QString& path) {
      if (!_project || !_project->cad()) {
            Critical("exportSvg: no project or CAD element");
            return false;
            }

      const Cad* cad = _project->cad();

      // Bounding box in world (root CAD) coordinates — used for the
      // canvas size and the Y-flip offset.  Transform each child's
      // local bounding box through its global matrix and union.
      QRectF bbox;
      std::function<void(const Element3d*)> collect = [&](const Element3d* root) {
            for (const Element* c : root->children()) {
                  const auto* e3d = qobject_cast<const Element3d*>(c);
                  if (!e3d || !e3d->show())
                        continue;
                  QRectF bb = e3d->worldBoundingBox();
                  if (bb.isValid())
                        bbox |= bb;
                  collect(e3d);
                  }
            };
      collect(cad);

      if (bbox.isNull() || bbox.isEmpty()) {
            Warning("exportSvg: CAD is empty, nothing to export");
            return false;
            }

      QFile f(path);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            Critical("exportSvg: cannot open {} for writing", path);
            return false;
            }
      QTextStream ts(&f);
      ts.setEncoding(QStringConverter::Utf8);

      const double margin = 1.0; // 1 mm margin around the content
      const double x0     = bbox.x() - margin;
      const double y0     = bbox.y() - margin;
      const double w      = bbox.width() + 2 * margin;
      const double h      = bbox.height() + 2 * margin;

      ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
      ts << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << SvgExport::fmt(w) << "mm\" height=\""
         << SvgExport::fmt(h) << "mm\" viewBox=\"0 0 " << SvgExport::fmt(w) << " " << SvgExport::fmt(h)
         << "\"\n";
      ts << "     xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\">\n";

      // Root group: move the CAD origin to the top-left of the page
      // (plus the 1mm margin) and flip the Y axis downwards.
      ts << "  <g id=\"cad\" transform=\"translate(" << SvgExport::fmt(-x0) << " "
         << SvgExport::fmt(bbox.bottom() + margin) << ") scale(1 -1)\">\n";

      for (const Element* c : cad->children()) {
            const auto* e3d = qobject_cast<const Element3d*>(c);
            if (!e3d || !e3d->show())
                  continue;
            // Top-level children of Cad are the layers.  Mark them with
            // the Inkscape layer attributes so they are recognized as
            // layers by external tools.
            if (isType<Group>(e3d)) {
                  ts << "    <g id=\"" << SvgExport::esc(e3d->name())
                     << "\" inkscape:groupmode=\"layer\" inkscape:label=\"" << SvgExport::esc(e3d->name())
                     << "\"" << SvgExport::transformAttr(e3d) << ">\n";
                  for (const Element* gc : e3d->children())
                        if (const auto* e = qobject_cast<const Element3d*>(gc))
                              svgExportElement(ts, e, 3);
                  ts << "    </g>\n";
                  }
            else
                  svgExportElement(ts, e3d, 2);
            }

      ts << "  </g>\n";
      ts << "</svg>\n";
      f.close();

      Info("exportSvg: wrote {}", path);
      emit svgExported(path);
      return true;
      }
