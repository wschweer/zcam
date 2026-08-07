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

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTransform>
#include <cmath>
#include <map>
#include <numbers>
#include <vector>

#include "importipc2581.h"
#include "zcam.h"
#include "cad.h"
#include "cam.h"
#include "fixture.h"
#include "group.h"
#include "machine.h"
#include "polygon.h"
#include "painterpath.h"
#include "project.h"
#include "recipe.h"
#include "undo.h"
#include "xmlreader.h"

//---------------------------------------------------------
//   PcbPath
//    Intermediate representation of an IPC-2581 feature:
//    a point list with stroke/fill attributes.
//---------------------------------------------------------

class PcbPath : public std::vector<Vec2d>
      {
    public:
      bool fill {false};
      bool close {false};
      bool stroke {false};
      QString name;
      double lineWidth {0.1};
      QString lineEnd {QStringLiteral("ROUND")};
      bool operator==(const PcbPath& p) const {
            if (p.size() != size())
                  return false;
            for (size_t i = 0; i < size(); ++i)
                  if (p[i] != at(i))
                        return false;
            return true;
            }
      };

//---------------------------------------------------------
//   checkDoublette
//---------------------------------------------------------

static bool checkDoublette(const std::list<PcbPath>& paths, const PcbPath& path) {
      for (const auto& p : paths)
            if (p == path)
                  return true;
      return false;
      }

//---------------------------------------------------------
//   IPC2581LayerType
//---------------------------------------------------------

class IPC2581LayerType
      {
    public:
      QString name;
      QString function;
      QString side;
      };

//---------------------------------------------------------
//   DictEntry
//    Dictionary primitive (DictionaryStandard / DictionaryUser
//    / DictionaryLineDesc entries).
//---------------------------------------------------------

enum class DictType { None, Line, RectCenter, Oval, Circle, RectRound, Path };
struct DictEntry {
      QString id;
      DictType type {DictType::None};

      double lineWidth {0.0};
      double width {0.0};
      double height {0.0};
      double radius {0.0};
      QString lineEnd {QStringLiteral("ROUND")};
      bool fill {false};
      bool stroke {false};
      std::list<PcbPath> paths;
      };

//---------------------------------------------------------
//   RectRound
//---------------------------------------------------------

struct RectRound {
      double width {0.0};
      double height {0.0};
      double radius {0.0};
      bool upperRight {false};
      bool upperLeft {false};
      bool lowerRight {false};
      bool lowerLeft {false};
      void read(XmlReader& r) {
            width  = r.doubleAttribute("width");
            height = r.doubleAttribute("height");
            radius = r.doubleAttribute("radius");
            // IPC-2581C 3.5.9.14: at least one rounded corner required;
            // treat "no corner flag" as "all corners"
            upperRight = r.hasAttribute("upperRight") ? r.boolAttribute("upperRight") : true;
            upperLeft  = r.hasAttribute("upperLeft") ? r.boolAttribute("upperLeft") : true;
            lowerRight = r.hasAttribute("lowerRight") ? r.boolAttribute("lowerRight") : true;
            lowerLeft  = r.hasAttribute("lowerLeft") ? r.boolAttribute("lowerLeft") : true;
            if (!upperRight && !upperLeft && !lowerRight && !lowerLeft)
                  upperRight = upperLeft = lowerRight = lowerLeft = true;
            r.readNext();
            }
      };

class IPC2581;

//---------------------------------------------------------
//   PcbLayer
//    Collects the parsed features of one board layer.
//---------------------------------------------------------

class PcbLayer
      {
      IPC2581* pcb;

    public:
      PcbLayer(IPC2581* p) : pcb(p) {}
      QString name;               // layer name (Layer element name attribute)
      QString function;           // CONDUCTOR | SOLDERMASK | DRILL | ...
      QString side;               // TOP | BOTTOM | BOTH | INTERNAL | ALL | NONE
      std::list<PcbPath> paths;   // positive polarity features
      std::list<PcbPath> cutouts; // negative polarity features / Cutout shapes

      void addPath(const PcbPath& p, bool polarityNegative);
      PcbPath readPolygon(XmlReader& r);
      void readPolyline(XmlReader& r);
      void readLine(XmlReader& r);
      void readArc(XmlReader& r);
      void readPad(XmlReader& r, bool polarityNegative);
      void readHole(XmlReader& r);
      void readFeatures(XmlReader& r, bool polarityNegative);
      void createPrimitive(double x, double y, const DictEntry& de, const QTransform& xform = QTransform(),
                           bool polarityNegative = false);
      };

//---------------------------------------------------------
//   IPC2581
//    Parser for an IPC-2581 (revision C) file.
//---------------------------------------------------------

class IPC2581
      {
    public:
      ZCam* zcam;
      IPC2581(ZCam* p) : zcam(p) {}
      static const DictEntry emptyDict;
      std::map<QString, DictEntry> dictionary;
      std::vector<PcbLayer*> layers;         // ordered, ownership
      std::map<QString, PcbLayer*> layerMap; // layer name -> PcbLayer
      std::map<QString, IPC2581LayerType> layerTypes;
      QString units {QStringLiteral("MILLIMETER")}; // from CadHeader
      double unitScale {1.0};                       // to millimeter
      const DictEntry& dict(const QString& id) {
            auto it = dictionary.find(id);
            if (it != dictionary.end())
                  return it->second;
            Warning("IPC2581: dictionary entry <{}> not found", id.toUtf8().constData());
            return emptyDict;
            }
      PcbLayer* findLayer(const QString& name) {
            auto it = layerMap.find(name);
            return it != layerMap.end() ? it->second : nullptr;
            }
      PcbLayer* getLayer(const QString& name) { // find or create
            auto it = layerMap.find(name);
            if (it != layerMap.end())
                  return it->second;
            auto l  = new PcbLayer(this);
            l->name = name;
            layers.push_back(l);
            layerMap[name] = l;
            return l;
            }
      void readLayerFeature(XmlReader&);
      void readPackage(XmlReader&);
      void readPadStack(XmlReader&);
      void readComponent(XmlReader&);
      void readProfile(XmlReader&);
      void readStep(XmlReader&);
      void readCadData(XmlReader&);
      void readEcad(XmlReader&);
      void readContent(XmlReader&);
      void read(XmlReader&);
      };

const DictEntry IPC2581::emptyDict = DictEntry();

//---------------------------------------------------------
//   circle2Polygon
//    Append a circle approximation to path p.
//---------------------------------------------------------

static void circle2Polygon(Vec2d pos, double r, double startAngle, double endAngle, PcbPath& p,
                           double precision = 0.01) {
      int s = int(std::numbers::pi / std::acos(1.0 - (precision / r))) / 4 + 1;
      if (s < 2) // minimum is 2 segments per quadrant
            s = 2;
      double step = (0.5 * std::numbers::pi) / double(s);

      if (startAngle > endAngle)
            endAngle += 2.0 * std::numbers::pi;

      for (double angle = startAngle; angle < endAngle; angle += step)
            p.push_back({std::cos(angle) * r + pos.x(), std::sin(angle) * r + pos.y()});
      p.push_back({std::cos(endAngle) * r + pos.x(), std::sin(endAngle) * r + pos.y()});
      }

//---------------------------------------------------------
//   arcToPolygon
//    Flatten an arc (start/end/center per IPC-2581 Arc or
//    PolyStepCurve) and append the points (excluding the
//    start point, which is already in the path).
//---------------------------------------------------------

static void arcToPolygon(const Vec2d& start, const Vec2d& end, const Vec2d& center, bool clockwise,
                         PcbPath& path, double precision) {
      double r = std::hypot(start.x() - center.x(), start.y() - center.y());
      if (r < 1e-6)
            r = std::hypot(end.x() - center.x(), end.y() - center.y());
      if (r < 1e-6) {
            path.push_back(end);
            return;
            }
      double a0    = std::atan2(start.y() - center.y(), start.x() - center.x());
      double a1    = std::atan2(end.y() - center.y(), end.x() - center.x());
      double sweep = clockwise ? (a0 - a1) : (a1 - a0);
      if (sweep <= 0.0)
            sweep += 2.0 * std::numbers::pi;

      // chord error based segmentation (same precision model as circle2Polygon)
      int segments = int(sweep * r / (4.0 * std::sqrt(2.0 * precision * r))) + 1;
      if (segments < 2)
            segments = 2;
      double step = sweep / segments;
      for (int i = 1; i <= segments; ++i) {
            double a = clockwise ? a0 - step * i : a0 + step * i;
            path.push_back({center.x() + r * std::cos(a), center.y() + r * std::sin(a)});
            }
      }

//---------------------------------------------------------
//   rectRoundToPolygon
//    Rounded rectangle with optional corner rounding.
//---------------------------------------------------------

static void rectRoundToPolygon(double x, double y, double w, double h, const RectRound& rr, PcbPath& path,
                               double precision) {
      double w2 = w * 0.5, h2 = h * 0.5, r = rr.radius;
      // corners: upper-left, upper-right, lower-right, lower-left
      bool ul = rr.upperLeft, ur = rr.upperRight, lr = rr.lowerRight, ll = rr.lowerLeft;

      path.push_back({x - w2 + (ul ? r : 0.0), y + h2}); // start at upper-left edge
      if (ul)
            circle2Polygon({x - w2 + r, y + h2 - r}, r, std::numbers::pi * 0.5, std::numbers::pi, path,
                           precision);
      if (ur)
            circle2Polygon({x + w2 - r, y + h2 - r}, r, 0.0, std::numbers::pi * 0.5, path, precision);
      path.push_back({x + w2, y + (ur ? (h2 - r) : h2)});
      if (lr)
            circle2Polygon({x + w2 - r, y - h2 + r}, r, std::numbers::pi * 1.5, 0.0, path, precision);
      path.push_back({x + (lr ? w2 - r : w2), y - h2});
      if (ll)
            circle2Polygon({x - w2 + r, y - h2 + r}, r, std::numbers::pi, std::numbers::pi * 1.5, path,
                           precision);
      path.push_back({x - w2 + (ul ? r : 0.0), y - h2 + (ll ? r : 0.0)});

      path.name  = QStringLiteral("RectRound");
      path.fill  = true;
      path.close = true;
      }

//---------------------------------------------------------
//   lineDescDefault
//---------------------------------------------------------

static void lineDescFromDict(const DictEntry& de, PcbPath& path) {
      path.lineWidth = de.lineWidth;
      path.lineEnd   = de.lineEnd;
      }

//---------------------------------------------------------
//   PcbLayer::addPath
//---------------------------------------------------------

void PcbLayer::addPath(const PcbPath& p, bool polarityNegative) {
      if (polarityNegative)
            cutouts.push_back(p);
      else if (checkDoublette(paths, p))
            Debug("IPC2581: duplicate path <{}> skipped", p.name.toUtf8().constData());
      else
            paths.push_back(p);
      }

//---------------------------------------------------------
//   PcbLayer::readPolygon
//---------------------------------------------------------

PcbPath PcbLayer::readPolygon(XmlReader& r) {
      PcbPath path;
      path.fill  = true; // FillDesc default is FILL
      path.close = true;
      path.name  = QStringLiteral("Polygon");
      Vec2d last;

      while (r.readNextStartElement())
            if (r.name() == u"PolyBegin") {
                  last = Vec2d(r.doubleAttribute("x"), r.doubleAttribute("y"));
                  path.push_back(last);
                  r.readNext();
                  }
            else if (r.name() == u"PolyStepSegment") {
                  last = Vec2d(r.doubleAttribute("x"), r.doubleAttribute("y"));
                  path.push_back(last);
                  r.readNext();
                  }
            else if (r.name() == u"PolyStepCurve") {
                  Vec2d end(r.doubleAttribute("x"), r.doubleAttribute("y"));
                  Vec2d center(r.doubleAttribute("centerX"), r.doubleAttribute("centerY"));
                  bool cw = r.boolAttribute("clockwise");
                  arcToPolygon(last, end, center, cw, path, 0.01);
                  last = end;
                  r.readNext();
                  }
            else if (r.name() == u"Xform") {
                  r.skipCurrentElement(); // per spec 3.4.4: not recommended
                  }
            else if (r.name() == u"LineDesc") {
                  path.lineWidth = r.doubleAttribute("lineWidth");
                  path.lineEnd   = r.attribute("lineEnd", u"ROUND").toString();
                  r.readNext();
                  }
            else if (r.name() == u"LineDescRef") {
                  lineDescFromDict(pcb->dict(r.attribute("id").toString()), path);
                  r.readNext();
                  }
            else if (r.name() == u"FillDesc") {
                  auto fp   = r.attribute("fillProperty");
                  path.fill = (fp != u"HOLLOW" && fp != u"VOID");
                  // HATCH / MESH are approximated as solid fill
                  r.skipCurrentElement(); // optional Color child
                  }
            else if (r.name() == u"FillDescRef") {
                  r.readNext(); // TODO: resolve DictionaryFillDesc
                  }
            else
                  r.unknown();
      // remove the duplicated closing point (last == first per spec)
      if (path.size() > 1 && path.front() == path.back())
            path.pop_back();
      return path;
      }

//---------------------------------------------------------
//   PcbLayer::readPolyline
//---------------------------------------------------------

void PcbLayer::readPolyline(XmlReader& r) {
      PcbPath path;
      path.name   = QStringLiteral("Polyline");
      path.stroke = true;
      path.fill   = false;
      Vec2d last;

      while (r.readNextStartElement())
            if (r.name() == u"PolyBegin") {
                  last = Vec2d(r.doubleAttribute("x"), r.doubleAttribute("y"));
                  path.push_back(last);
                  r.readNext();
                  }
            else if (r.name() == u"PolyStepSegment") {
                  last = Vec2d(r.doubleAttribute("x"), r.doubleAttribute("y"));
                  path.push_back(last);
                  r.readNext();
                  }
            else if (r.name() == u"PolyStepCurve") {
                  Vec2d end(r.doubleAttribute("x"), r.doubleAttribute("y"));
                  Vec2d center(r.doubleAttribute("centerX"), r.doubleAttribute("centerY"));
                  bool cw = r.boolAttribute("clockwise");
                  arcToPolygon(last, end, center, cw, path, 0.01);
                  last = end;
                  r.readNext();
                  }
            else if (r.name() == u"LineDesc") {
                  path.lineWidth = r.doubleAttribute("lineWidth");
                  path.lineEnd   = r.attribute("lineEnd", u"ROUND").toString();
                  r.readNext();
                  }
            else if (r.name() == u"LineDescRef") {
                  lineDescFromDict(pcb->dict(r.attribute("id").toString()), path);
                  r.readNext();
                  }
            else
                  r.unknown();
      paths.push_back(path);
      }

//---------------------------------------------------------
//   PcbLayer::readLine
//---------------------------------------------------------

void PcbLayer::readLine(XmlReader& r) {
      PcbPath path;
      path.push_back({r.doubleAttribute("startX"), r.doubleAttribute("startY")});
      path.push_back({r.doubleAttribute("endX"), r.doubleAttribute("endY")});

      while (r.readNextStartElement())
            if (r.name() == u"LineDesc") {
                  path.lineWidth = r.doubleAttribute("lineWidth");
                  path.lineEnd   = r.attribute("lineEnd", u"ROUND").toString();
                  r.readNext();
                  }
            else if (r.name() == u"LineDescRef") {
                  lineDescFromDict(pcb->dict(r.attribute("id").toString()), path);
                  r.readNext();
                  }
            else
                  r.unknown();
      path.name   = QStringLiteral("Line");
      path.stroke = true;
      paths.push_back(path);
      }

//---------------------------------------------------------
//   PcbLayer::readArc
//---------------------------------------------------------

void PcbLayer::readArc(XmlReader& r) {
      PcbPath path;
      path.name   = QStringLiteral("Arc");
      path.stroke = true;

      Vec2d start(r.doubleAttribute("startX"), r.doubleAttribute("startY"));
      Vec2d end(r.doubleAttribute("endX"), r.doubleAttribute("endY"));
      Vec2d center(r.doubleAttribute("centerX"), r.doubleAttribute("centerY"));
      bool cw = r.boolAttribute("clockwise");

      path.push_back(start);
      arcToPolygon(start, end, center, cw, path, 0.01);

      while (r.readNextStartElement())
            if (r.name() == u"LineDesc") {
                  path.lineWidth = r.doubleAttribute("lineWidth");
                  path.lineEnd   = r.attribute("lineEnd", u"ROUND").toString();
                  r.readNext();
                  }
            else if (r.name() == u"LineDescRef") {
                  lineDescFromDict(pcb->dict(r.attribute("id").toString()), path);
                  r.readNext();
                  }
            else
                  r.unknown();
      paths.push_back(path);
      }

//---------------------------------------------------------
//   PcbLayer::createPrimitive
//    Instantiate a dictionary primitive at (x, y) with an
//    optional Xform transformation.
//---------------------------------------------------------

void PcbLayer::createPrimitive(double x, double y, const DictEntry& de, const QTransform& xform,
                               bool polarityNegative) {
      PcbPath p;
      double precision = 0.01;
      if (pcb->zcam->project() && pcb->zcam->project()->machine())
            precision = pcb->zcam->project()->machine()->circlePrecision();

      switch (de.type) {
            case DictType::Circle:
                  circle2Polygon({0, 0}, de.radius, 0.0, 2.0 * std::numbers::pi, p, precision);
                  if (p.size() > 1)
                        p.pop_back(); // circle2Polygon closes the loop
                  p.name  = QStringLiteral("Circle");
                  p.fill  = !de.stroke;
                  p.close = true;
                  break;
            case DictType::Oval: {
                  double w  = de.width;
                  double h  = de.height;
                  double h2 = h * 0.5;
                  circle2Polygon({-(w - h) * 0.5, 0.0}, h2, std::numbers::pi * 0.5, std::numbers::pi * 1.5, p,
                                 precision);
                  circle2Polygon({(w - h) * 0.5, 0.0}, h2, std::numbers::pi * 1.5, std::numbers::pi * 0.5, p,
                                 precision);
                  p.name  = QStringLiteral("Oval");
                  p.fill  = true;
                  p.close = true;
                  } break;
            case DictType::RectCenter:
                  p.push_back({-de.width * 0.5, de.height * 0.5});
                  p.push_back({de.width * 0.5, de.height * 0.5});
                  p.push_back({de.width * 0.5, -de.height * 0.5});
                  p.push_back({-de.width * 0.5, -de.height * 0.5});
                  p.name  = QStringLiteral("RectCenter");
                  p.fill  = true;
                  p.close = true;
                  break;
            case DictType::RectRound: {
                  RectRound rr;
                  rr.width  = de.width;
                  rr.height = de.height;
                  rr.radius = de.radius;
                  rectRoundToPolygon(0.0, 0.0, de.width, de.height, rr, p, precision);
                  p.fill  = true;
                  p.close = true;
                  } break;
            default: Warning("IPC2581: unhandled dictionary primitive type {}", int(de.type)); return;
            }

      p.lineWidth = de.lineWidth;
      if (de.stroke)
            p.stroke = true;

      // apply transform: Xform first (origin/rotation/mirror/scale), then location
      for (auto& pt : p) {
            QPointF q = xform.map(QPointF(pt.x(), pt.y())) + QPointF(x, y);
            pt.x()    = q.x();
            pt.y()    = q.y();
            }
      addPath(p, polarityNegative);
      }

//---------------------------------------------------------
//   PcbLayer::readPad
//---------------------------------------------------------

void PcbLayer::readPad(XmlReader& r, bool polarityNegative) {
      Q_UNUSED(r.attribute("padstackDefRef"));
      double x = 0.0, y = 0.0;
      QString shapeId;
      QTransform xform; // from Xform element

      while (r.readNextStartElement()) {
            auto n = r.name();
            if (n == u"Location") {
                  x = r.doubleAttribute("x");
                  y = r.doubleAttribute("y");
                  r.readNext();
                  }
            else if (n == u"Xform") {
                  double rot   = r.doubleAttribute("rotation", 0.0);
                  bool mirror  = r.boolAttribute("mirror");
                  double scale = r.doubleAttribute("scale", 1.0);
                  if (mirror) {
                        r.unknown();
                        Debug("IPC2581: pad Xform mirror not supported");
                        }
                  xform = QTransform().scale(scale, scale).rotate(rot);
                  r.readNext();
                  }
            else if (n == u"StandardPrimitiveRef") {
                  shapeId = r.attribute("id").toString();
                  r.readNext();
                  }
            else if (n == u"UserPrimitiveRef") {
                  shapeId = r.attribute("id").toString();
                  r.readNext();
                  }
            else if (n == u"PinRef") {
                  r.readNext(); // net/pin info: ignored
                  }
            else
                  r.unknown();
            }

      if (!shapeId.isEmpty())
            createPrimitive(x, y, pcb->dict(shapeId), xform, polarityNegative);
      }

//---------------------------------------------------------
//   PcbLayer::readHole
//    <Hole name="H1" diameter="1.0" platingStatus="PLATED"
//          plusTol="0.0" minusTol="0.0" x="89.94" y="-61.0"/>
//---------------------------------------------------------

void PcbLayer::readHole(XmlReader& r) {
      PcbPath path;
      path.name = QStringLiteral("Hole-%1").arg(r.attribute("name").toString());
      double d  = r.doubleAttribute("diameter");
      double x  = r.doubleAttribute("x");
      double y  = r.doubleAttribute("y");
      QString t = r.attribute("type", u"CIRCLE").toString();

      double precision = 0.01;
      if (pcb->zcam->project() && pcb->zcam->project()->machine())
            precision = pcb->zcam->project()->machine()->circlePrecision();

      if (t == u"SQUARE") {
            double h = d * 0.5;
            path.push_back({x - h, y + h});
            path.push_back({x + h, y + h});
            path.push_back({x + h, y - h});
            path.push_back({x - h, y - h});
            }
      else {
            circle2Polygon({x, y}, d * 0.5, 0.0, 2.0 * std::numbers::pi, path, precision);
            if (path.size() > 1)
                  path.pop_back();
            }
      path.close = true;
      path.fill  = true;
      paths.push_back(path);
      r.readNext();
      }

//---------------------------------------------------------
//   PcbLayer::readFeatures
//    Children of Set/Features: substitution group of
//    StandardShape / UserShape primitives.
//---------------------------------------------------------

void PcbLayer::readFeatures(XmlReader& r, bool polarityNegative) {
      double x = 0.0, y = 0.0; // modal location inside Features
      QTransform xform;
      bool haveXform = false;

      // helper lambdas
      auto readInlinePrimitive = [&](const QString& typeName) {
            PcbPath p;
            if (typeName == u"Circle") {
                  double d         = r.doubleAttribute("diameter");
                  double precision = 0.01;
                  circle2Polygon({x, y}, d * 0.5, 0.0, 2.0 * std::numbers::pi, p, precision);
                  if (p.size() > 1)
                        p.pop_back();
                  p.name  = QStringLiteral("Circle");
                  p.fill  = true;
                  p.close = true;
                  }
            else if (typeName == u"RectCenter") {
                  double w = r.doubleAttribute("width");
                  double h = r.doubleAttribute("height");
                  p.push_back({x - w * 0.5, y + h * 0.5});
                  p.push_back({x + w * 0.5, y + h * 0.5});
                  p.push_back({x + w * 0.5, y - h * 0.5});
                  p.push_back({x - w * 0.5, y - h * 0.5});
                  p.name  = QStringLiteral("RectCenter");
                  p.fill  = true;
                  p.close = true;
                  }
            else if (typeName == u"RectCorner") {
                  double lx = r.doubleAttribute("lowerLeftX");
                  double ly = r.doubleAttribute("lowerLeftY");
                  double ux = r.doubleAttribute("upperRightX");
                  double uy = r.doubleAttribute("upperRightY");
                  p.push_back({x + lx, y + uy});
                  p.push_back({x + ux, y + uy});
                  p.push_back({x + ux, y + ly});
                  p.push_back({x + lx, y + ly});
                  p.name  = QStringLiteral("RectCorner");
                  p.fill  = true;
                  p.close = true;
                  }
            else if (typeName == u"RectRound") {
                  RectRound rr;
                  rr.width      = r.doubleAttribute("width");
                  rr.height     = r.doubleAttribute("height");
                  rr.radius     = r.doubleAttribute("radius");
                  rr.upperRight = r.hasAttribute("upperRight") ? r.boolAttribute("upperRight") : true;
                  rr.upperLeft  = r.hasAttribute("upperLeft") ? r.boolAttribute("upperLeft") : true;
                  rr.lowerRight = r.hasAttribute("lowerRight") ? r.boolAttribute("lowerRight") : true;
                  rr.lowerLeft  = r.hasAttribute("lowerLeft") ? r.boolAttribute("lowerLeft") : true;
                  if (!rr.upperRight && !rr.upperLeft && !rr.lowerRight && !rr.lowerLeft)
                        rr.upperRight = rr.upperLeft = rr.lowerRight = rr.lowerLeft = true;
                  rectRoundToPolygon(x, y, rr.width, rr.height, rr, p, 0.01);
                  }
            else if (typeName == u"Oval") {
                  double w  = r.doubleAttribute("width");
                  double h  = r.doubleAttribute("height");
                  double h2 = h * 0.5;
                  circle2Polygon({x - (w - h) * 0.5, y}, h2, std::numbers::pi * 0.5, std::numbers::pi * 1.5,
                                 p, 0.01);
                  circle2Polygon({x + (w - h) * 0.5, y}, h2, std::numbers::pi * 1.5, std::numbers::pi * 0.5,
                                 p, 0.01);
                  p.name  = QStringLiteral("Oval");
                  p.fill  = true;
                  p.close = true;
                  }
            else if (typeName == u"Diamond") {
                  double w = r.doubleAttribute("width");
                  double h = r.doubleAttribute("height");
                  p.push_back({x, y + h * 0.5});
                  p.push_back({x + w * 0.5, y});
                  p.push_back({x, y - h * 0.5});
                  p.push_back({x - w * 0.5, y});
                  p.name  = QStringLiteral("Diamond");
                  p.fill  = true;
                  p.close = true;
                  }
            else if (typeName == u"Triangle") {
                  double b = r.doubleAttribute("base");
                  double h = r.doubleAttribute("height");
                  p.push_back({x - b * 0.5, y - h * 0.4});
                  p.push_back({x + b * 0.5, y - h * 0.4});
                  p.push_back({x, y + h * 0.6});
                  p.name  = QStringLiteral("Triangle");
                  p.fill  = true;
                  p.close = true;
                  }
            else {
                  return false;
                  }
            return true;
            };

      while (r.readNextStartElement()) {
            auto n = r.name();
            if (n == u"Location") {
                  x = r.doubleAttribute("x");
                  y = r.doubleAttribute("y");
                  r.readNext();
                  }
            else if (n == u"Xform") {
                  double rot   = r.doubleAttribute("rotation", 0.0);
                  double scale = r.doubleAttribute("scale", 1.0);
                  bool mirror  = r.boolAttribute("mirror");
                  double xOff  = r.doubleAttribute("xOffset", 0.0);
                  double yOff  = r.doubleAttribute("yOffset", 0.0);
                  // order per spec 3.3: 1) origin offset 2) rotation 3) mirror 4) scale
                  xform     = QTransform()
                                  .scale(scale, scale)
                                  .scale(mirror ? -1.0 : 1.0, 1.0)
                                  .rotate(rot)
                                  .translate(xOff, yOff);
                  haveXform = true;
                  r.readNext();
                  }
            else if (n == u"Line") {
                  readLine(r);
                  }
            else if (n == u"Arc") {
                  readArc(r);
                  }
            else if (n == u"Polyline") {
                  readPolyline(r);
                  }
            else if (n == u"Contour") {
                  while (r.readNextStartElement())
                        if (r.name() == u"Polygon") {
                              auto pp = readPolygon(r);
                              addPath(pp, polarityNegative);
                              }
                        else if (r.name() == u"Cutout") {
                              auto pp = readPolygon(r);
                              pp.name = QStringLiteral("Cutout");
                              cutouts.push_back(pp);
                              }
                        else
                              r.unknown();
                  }
            else if (n == u"UserSpecial") {
                  while (r.readNextStartElement()) {
                        if (r.name() == u"Contour") {
                              while (r.readNextStartElement())
                                    if (r.name() == u"Polygon") {
                                          auto pp = readPolygon(r);
                                          pp.name = QStringLiteral("UserContour");
                                          addPath(pp, polarityNegative);
                                          }
                                    else if (r.name() == u"Cutout") {
                                          auto pp = readPolygon(r);
                                          pp.name = QStringLiteral("Cutout");
                                          cutouts.push_back(pp);
                                          }
                                    else
                                          r.unknown();
                              }
                        else if (r.name() == u"Line")
                              readLine(r);
                        else if (r.name() == u"Polyline")
                              readPolyline(r);
                        else
                              r.unknown();
                        }
                  }
            else if (n == u"StandardPrimitiveRef" || n == u"UserPrimitiveRef") {
                  // per spec (3.5.2 / 3.5.4): children are Xform, Location,
                  // LineDescGroup, NonstandardAttribute
                  QString id = r.attribute("id").toString();
                  double px = x, py = y;
                  QTransform xf = haveXform ? xform : QTransform();
                  while (r.readNextStartElement()) {
                        if (r.name() == u"Location") {
                              px = x + r.doubleAttribute("x");
                              py = y + r.doubleAttribute("y");
                              r.readNext();
                              }
                        else if (r.name() == u"Xform") {
                              double rot    = r.doubleAttribute("rotation", 0.0);
                              double scale  = r.doubleAttribute("scale", 1.0);
                              bool mirror   = r.boolAttribute("mirror");
                              double xOff   = r.doubleAttribute("xOffset", 0.0);
                              double yOff   = r.doubleAttribute("yOffset", 0.0);
                              xf            = QTransform()
                                   .scale(scale, scale)
                                   .scale(mirror ? -1.0 : 1.0, 1.0)
                                   .rotate(rot)
                                   .translate(xOff, yOff);
                              r.readNext();
                              }
                        else if (r.name() == u"LineDesc" || r.name() == u"LineDescRef")
                              r.skipCurrentElement();
                        else if (r.name() == u"NonstandardAttribute")
                              r.skipCurrentElement();
                        else
                              r.unknown();
                        }
                  createPrimitive(px, py, pcb->dict(id), xf, polarityNegative);
                  }
            else if (n == u"Circle" || n == u"RectCenter" || n == u"RectCorner" || n == u"RectRound" ||
                     n == u"Oval" || n == u"Diamond" || n == u"Triangle") {
                  PcbPath p;
                  QString typeName = n.toString();
                  // read optional LineDesc/FillDesc children after attributes
                  if (readInlinePrimitive(typeName)) {
                        while (r.readNextStartElement())
                              if (r.name() == u"LineDesc") {
                                    p.lineWidth = r.doubleAttribute("lineWidth");
                                    p.lineEnd   = r.attribute("lineEnd", u"ROUND").toString();
                                    r.readNext();
                                    }
                              else if (r.name() == u"LineDescRef") {
                                    lineDescFromDict(pcb->dict(r.attribute("id").toString()), p);
                                    r.readNext();
                                    }
                              else if (r.name() == u"FillDesc") {
                                    auto fp = r.attribute("fillProperty");
                                    p.fill  = (fp != u"HOLLOW" && fp != u"VOID");
                                    r.skipCurrentElement();
                                    }
                              else if (r.name() == u"FillDescRef")
                                    r.readNext();
                              else
                                    r.unknown();
                        addPath(p, polarityNegative);
                        }
                  else
                        r.skipCurrentElement();
                  }
            else if (n == u"Butterfly" || n == u"Donut" || n == u"Ellipse" || n == u"Hexagon" ||
                     n == u"Moire" || n == u"Octagon" || n == u"Thermal") {
                  Debug("IPC2581: primitive <{}> not yet supported", n.toString());
                  r.skipCurrentElement();
                  }
            else if (n == u"Text") {
                  Debug("IPC2581: Text not yet supported");
                  r.skipCurrentElement();
                  }
            else
                  r.unknown();
            }
      Q_UNUSED(haveXform);
      }

//---------------------------------------------------------
//   IPC2581::readLayerFeature
//---------------------------------------------------------

void IPC2581::readLayerFeature(XmlReader& r) {
      QString layerRef = r.attribute("layerRef").toString(); // LAYER:, DRILL:, SLOT: ...

      Debug("IPC2581: LayerFeature <{}>", layerRef.toUtf8().constData());

      // normalize common prefixes
      QString name = layerRef;
      if (name.startsWith(QStringLiteral("LAYER:")))
            name = name.mid(6);
      else if (name.startsWith(QStringLiteral("DRILL:")))
            name = QStringLiteral("Drill");
      else if (name.startsWith(QStringLiteral("SLOT:")))
            name = name.mid(5);

      PcbLayer* l = getLayer(name);
      // copy layer characterization if available
      const auto& lt = layerTypes[layerRef];
      if (l->function.isEmpty()) {
            l->function = lt.function;
            l->side     = lt.side;
            }

      while (r.readNextStartElement()) {
            auto n = r.name();
            if (n == u"Set") {
                  QString polarity = r.attribute("polarity", u"POSITIVE").toString();
                  bool negative    = polarity == u"NEGATIVE";
                  QString net      = r.attribute("net").toString();
                  Q_UNUSED(net);
                  QString padUsage = r.attribute("padUsage", u"NONE").toString();
                  Q_UNUSED(padUsage);
                  QString geometry = r.attribute("geometry").toString(); // e.g. "outline", "boardoutline"
                  Q_UNUSED(geometry);

                  while (r.readNextStartElement()) {
                        auto sn = r.name();
                        if (sn == u"Features") {
                              l->readFeatures(r, negative);
                              }
                        else if (sn == u"Pad") {
                              l->readPad(r, negative);
                              }
                        else if (sn == u"Hole") {
                              l->readHole(r);
                              }
                        else if (sn == u"SlotCavity") {
                              while (r.readNextStartElement()) {
                                    if (r.name() == u"Location")
                                          r.readNext();
                                    else if (r.name() == u"Outline") {
                                          while (r.readNextStartElement())
                                                if (r.name() == u"Polygon") {
                                                      auto pp = l->readPolygon(r);
                                                      pp.name = QStringLiteral("SlotCavity");
                                                      l->addPath(pp, negative);
                                                      }
                                                else if (r.name() == u"LineDescRef")
                                                      r.readNext();
                                                else if (r.name() == u"LineDesc")
                                                      r.readNext();
                                                else
                                                      r.unknown();
                                          }
                                    else if (r.name() == u"Xform")
                                          r.readNext();
                                    else
                                          r.unknown();
                                    }
                              }
                        else if (sn == u"NonstandardAttribute") {
                              r.skipCurrentElement();
                              }
                        else if (sn == u"Fiducial") {
                              r.skipCurrentElement();
                              }
                        else if (sn == u"SpecRef") {
                              r.skipCurrentElement();
                              }
                        else if (sn == u"ColorGroup" || sn == u"Color" || sn == u"ColorRef") {
                              r.skipCurrentElement();
                              }
                        else if (sn == u"LineDescGroup" || sn == u"LineDesc" || sn == u"LineDescRef") {
                              r.skipCurrentElement(); // set-level line description, modal: TODO
                              }
                        else if (sn == u"NetShort") {
                              r.skipCurrentElement();
                              }
                        else
                              r.unknown();
                        }
                  }
            else
                  r.unknown();
            }
      }

//---------------------------------------------------------
//   IPC2581::readPackage
//    Store package land-pattern pins as a Path dictionary
//    entry (referenced by Component placement).
//---------------------------------------------------------

void IPC2581::readPackage(XmlReader& r) {
      DictEntry d;
      d.type = DictType::Path;
      d.id   = r.attribute("name").toString();

      while (r.readNextStartElement()) {
            auto n = r.name();
            if (n == u"Outline" || n == u"PickupPoint" || n == u"SilkScreen" || n == u"AssemblyDrawing" ||
                n == u"LandPattern" || n == u"Topside" || n == u"OtherSideView") {
                  r.skipCurrentElement();
                  }
            else if (n == u"Pin") {
                  double x = 0.0, y = 0.0;
                  double rotation = 0.0;
                  QString id;
                  while (r.readNextStartElement())
                        if (r.name() == u"Location") {
                              x = r.doubleAttribute("x");
                              y = r.doubleAttribute("y");
                              r.readNext();
                              }
                        else if (r.name() == u"Xform") {
                              rotation = r.doubleAttribute("rotation", 0.0);
                              r.readNext();
                              }
                        else if (r.name() == u"StandardPrimitiveRef") {
                              id = r.attribute("id").toString();
                              r.readNext();
                              }
                        else
                              r.unknown();
                  if (id.isEmpty())
                        continue;
                  const DictEntry& de = dict(id);
                  PcbPath path;
                  double precision = 0.01;
                  if (zcam->project() && zcam->project()->machine())
                        precision = zcam->project()->machine()->circlePrecision();

                  switch (de.type) {
                        case DictType::Circle:
                              circle2Polygon({0.0, 0.0}, de.radius, 0.0, 2.0 * std::numbers::pi, path,
                                             precision);
                              if (path.size() > 1)
                                    path.pop_back();
                              path.name = QStringLiteral("Circle");
                              break;
                        case DictType::Oval: {
                              double h2 = de.height * 0.5;
                              circle2Polygon({-(de.width - de.height) * 0.5, 0.0}, h2, std::numbers::pi * 0.5,
                                             std::numbers::pi * 1.5, path, precision);
                              circle2Polygon({(de.width - de.height) * 0.5, 0.0}, h2, std::numbers::pi * 1.5,
                                             std::numbers::pi * 0.5, path, precision);
                              path.name = QStringLiteral("Oval");
                              } break;
                        case DictType::RectCenter:
                              path.push_back({-de.width * 0.5, de.height * 0.5});
                              path.push_back({de.width * 0.5, de.height * 0.5});
                              path.push_back({de.width * 0.5, -de.height * 0.5});
                              path.push_back({-de.width * 0.5, -de.height * 0.5});
                              path.name = QStringLiteral("RectCenter");
                              break;
                        case DictType::RectRound: {
                              RectRound rr;
                              rr.width  = de.width;
                              rr.height = de.height;
                              rr.radius = de.radius;
                              rectRoundToPolygon(0.0, 0.0, de.width, de.height, rr, path, precision);
                              } break;
                        default:
                              Warning("IPC2581: package pin primitive type {} not handled", int(de.type));
                              break;
                        }
                  path.close = true;
                  path.fill  = true;

                  QTransform t;
                  t.rotate(rotation);
                  for (auto& pp : path) {
                        QPointF q = t.map(QPointF(pp.x(), pp.y())) + QPointF(x, y);
                        pp.x()    = q.x();
                        pp.y()    = q.y();
                        }
                  d.paths.push_back(path);
                  }
            else
                  r.unknown();
            }
      dictionary[d.id] = d;
      }

//---------------------------------------------------------
//   IPC2581::readPadStack
//---------------------------------------------------------

void IPC2581::readPadStack(XmlReader& r) {
      r.skipCurrentElement(); // padstack data is redundant once layer features are defined
      }

//---------------------------------------------------------
//   IPC2581::readComponent
//    Instantiate the package pin shapes at the component
//    placement (location + rotation), onto the mounting
//    side's component layer if such a layer exists.
//---------------------------------------------------------

void IPC2581::readComponent(XmlReader& r) {
      QString ref      = r.attribute("packageRef").toString();
      QString layerRef = r.attribute("layerRef").toString(); // mounting layer
      double x = 0.0, y = 0.0, rotation = 0.0;
      bool mirror = false;

      while (r.readNextStartElement()) {
            auto n = r.name();
            if (n == u"Xform") {
                  rotation = r.doubleAttribute("rotation", 0.0);
                  mirror   = r.boolAttribute("mirror");
                  r.readNext();
                  }
            else if (n == u"Location") {
                  x = r.doubleAttribute("x");
                  y = r.doubleAttribute("y");
                  r.readNext();
                  }
            else if (n == u"NonstandardAttribute")
                  r.skipCurrentElement();
            else
                  r.unknown();
            }
      if (ref.isEmpty() || dictionary.find(ref) == dictionary.end())
            return;

      QTransform t;
      if (mirror)
            t.scale(-1.0, 1.0);
      t.rotate(rotation);

      const DictEntry& d = dict(ref);

      // determine the layers to land the package pins on:
      //  - if the component's mounting layer maps to a parsed layer,
      //    use that layer only
      //  - otherwise put the pins on all CONDUCTOR layers of the
      //    mounting side (bottom when mirror=true)
      PcbLayer* mountLayer = nullptr;
      if (!layerRef.isEmpty()) {
            QString mapped = layerRef;
            if (mapped.startsWith(QStringLiteral("LAYER:")))
                  mapped = mapped.mid(6);
            mountLayer = findLayer(mapped);
            }

      for (auto l : layers) {
            if (l->function != QStringLiteral("CONDUCTOR"))
                  continue;
            bool accept;
            if (mountLayer)
                  accept = (l == mountLayer);
            else if (mirror)
                  accept = (l->side == QStringLiteral("BOTTOM"));
            else
                  accept = (l->side != QStringLiteral("BOTTOM"));
            if (!accept)
                  continue;
            for (auto p : d.paths) {
                  for (auto& pp : p) {
                        QPointF q = t.map(QPointF(pp.x(), pp.y())) + QPointF(x, y);
                        pp.x()    = q.x();
                        pp.y()    = q.y();
                        }
                  l->paths.push_back(p);
                  }
            }
      }

//---------------------------------------------------------
//   IPC2581::readProfile
//    Board profile: stored as a "Profile" layer (board outline).
//---------------------------------------------------------

void IPC2581::readProfile(XmlReader& r) {
      PcbLayer* l = getLayer(QStringLiteral("Profile"));
      l->function = QStringLiteral("BOARD_OUTLINE");
      l->side     = QStringLiteral("ALL");
      while (r.readNextStartElement())
            if (r.name() == u"Polygon") {
                  auto pp      = l->readPolygon(r);
                  pp.name      = QStringLiteral("Profile");
                  pp.lineWidth = 0.1;
                  l->addPath(pp, false);
                  }
            else
                  r.unknown();
      }

//---------------------------------------------------------
//   IPC2581::readStep
//---------------------------------------------------------

void IPC2581::readStep(XmlReader& r) {
      while (r.readNextStartElement()) {
            auto n = r.name();
            if (n == u"Datum")
                  r.skipCurrentElement();
            else if (n == u"NonstandardAttribute")
                  r.skipCurrentElement();
            else if (n == u"PadStackDef")
                  readPadStack(r);
            else if (n == u"Profile")
                  readProfile(r);
            else if (n == u"StepRepeat")
                  r.skipCurrentElement(); // panelization: TODO
            else if (n == u"Package")
                  readPackage(r);
            else if (n == u"Component")
                  readComponent(r);
            else if (n == u"LogicalNet")
                  r.skipCurrentElement();
            else if (n == u"PhyNetGroup")
                  r.skipCurrentElement();
            else if (n == u"LayerFeature")
                  readLayerFeature(r);
            else if (n == u"BendArea" || n == u"StackupZone" || n == u"Port" || n == u"Model" || n == u"Dfx")
                  r.skipCurrentElement();
            else
                  r.unknown();
            }
      }

//---------------------------------------------------------
//   IPC2581::readCadData
//---------------------------------------------------------

void IPC2581::readCadData(XmlReader& r) {
      while (r.readNextStartElement()) {
            auto n = r.name();
            if (n == u"Layer") {
                  QString name     = r.attribute("name").toString();
                  QString function = r.attribute("layerFunction").toString();
                  QString side     = r.attribute("side").toString();
                  QString polarity = r.attribute("polarity", u"POSITIVE").toString();
                  layerTypes[name] = {name, function, side};
                  if (polarity == u"NEGATIVE")
                        Debug("IPC2581: layer <{}> is negative polarity", name.toUtf8().constData());
                  r.skipCurrentElement(); // optional Span / SpecRef children
                  }
            else if (n == u"Stackup")
                  r.skipCurrentElement();
            else if (n == u"Step")
                  readStep(r);
            else
                  r.unknown();
            }
      }

//---------------------------------------------------------
//   IPC2581::readEcad
//---------------------------------------------------------

void IPC2581::readEcad(XmlReader& r) {
      while (r.readNextStartElement()) {
            auto n = r.name();
            if (n == u"CadHeader") {
                  units = r.attribute("units").toString();
                  Debug("IPC2581: Ecad units = {}", units.toUtf8().constData());
                  if (units == u"MILLIMETER")
                        unitScale = 1.0;
                  else if (units == u"MICRON")
                        unitScale = 0.001;
                  else if (units == u"INCH")
                        unitScale = 25.4;
                  else
                        Warning("IPC2581: units <{}> not supported", units.toUtf8().constData());
                  r.skipCurrentElement();
                  }
            else if (n == u"CadData")
                  readCadData(r);
            else
                  r.unknown();
            }
      }

//---------------------------------------------------------
//   IPC2581::readContent
//---------------------------------------------------------

void IPC2581::readContent(XmlReader& r) {
      while (r.readNextStartElement()) {
            auto n = r.name();
            if (n == u"FunctionMode" || n == u"StepRef" || n == u"LayerRef" || n == u"BomRef" ||
                n == u"AvlRef" || n == u"DictionaryColor" || n == u"DictionaryFillDesc" ||
                n == u"DictionaryFont" || n == u"DictionaryFirmware") {
                  r.skipCurrentElement();
                  }
            else if (n == u"DictionaryLineDesc") {
                  while (r.readNextStartElement()) {
                        if (r.name() == u"EntryLineDesc") {
                              QString id = r.attribute("id").toString();
                              while (r.readNextStartElement())
                                    if (r.name() == u"LineDesc") {
                                          DictEntry e;
                                          e.id             = id;
                                          e.type           = DictType::Line;
                                          e.lineWidth      = r.doubleAttribute("lineWidth");
                                          e.lineEnd        = r.attribute("lineEnd", u"ROUND").toString();
                                          dictionary[e.id] = e;
                                          r.skipCurrentElement(); // Color child
                                          }
                                    else
                                          r.unknown();
                              }
                        else
                              r.unknown();
                        }
                  }
            else if (n == u"DictionaryStandard") {
                  while (r.readNextStartElement()) {
                        if (r.name() == u"EntryStandard") {
                              DictEntry e;
                              e.id = r.attribute("id").toString();
                              while (r.readNextStartElement()) {
                                    auto en = r.name();
                                    if (en == u"RectCenter") {
                                          e.type   = DictType::RectCenter;
                                          e.width  = r.doubleAttribute("width");
                                          e.height = r.doubleAttribute("height");
                                          r.skipCurrentElement();
                                          }
                                    else if (en == u"Circle") {
                                          e.type   = DictType::Circle;
                                          e.radius = r.doubleAttribute("diameter") * 0.5;
                                          r.skipCurrentElement();
                                          }
                                    else if (en == u"Oval") {
                                          e.type   = DictType::Oval;
                                          e.width  = r.doubleAttribute("width");
                                          e.height = r.doubleAttribute("height");
                                          r.skipCurrentElement();
                                          }
                                    else if (en == u"RectRound") {
                                          RectRound rr;
                                          rr.read(r);
                                          e.type   = DictType::RectRound;
                                          e.width  = rr.width;
                                          e.height = rr.height;
                                          e.radius = rr.radius;
                                          }
                                    else if (en == u"RectCham" || en == u"RectCorner" || en == u"Diamond" ||
                                             en == u"Donut" || en == u"Ellipse" || en == u"Hexagon" ||
                                             en == u"Moire" || en == u"Octagon" || en == u"Thermal" ||
                                             en == u"Triangle" || en == u"Butterfly" || en == u"Contour") {
                                          Debug("IPC2581: DictionaryStandard entry <{}> not yet supported",
                                                en.toString());
                                          r.skipCurrentElement();
                                          }
                                    else
                                          r.unknown();
                                    }
                              if (e.type != DictType::None)
                                    dictionary[e.id] = e;
                              }
                        else
                              r.unknown();
                        }
                  }
            else if (n == u"DictionaryUser") {
                  while (r.readNextStartElement()) {
                        if (r.name() == u"EntryUser") {
                              DictEntry e;
                              e.id = r.attribute("id").toString();
                              while (r.readNextStartElement()) {
                                    auto en = r.name();
                                    if (en == u"UserSpecial") {
                                          e.type = DictType::Path;
                                          r.skipCurrentElement(); // TODO: collect sub-features into e.paths
                                          }
                                    else if (en == u"Circle" || en == u"RectRound" || en == u"RectCenter" ||
                                             en == u"Oval") {
                                          // user primitives reduced to the standard shape model
                                          if (en == u"Circle") {
                                                e.type   = DictType::Circle;
                                                e.radius = r.doubleAttribute("diameter") * 0.5;
                                                }
                                          else if (en == u"RectRound") {
                                                e.type   = DictType::RectRound;
                                                e.width  = r.doubleAttribute("width");
                                                e.height = r.doubleAttribute("height");
                                                e.radius = r.doubleAttribute("radius");
                                                }
                                          else if (en == u"RectCenter") {
                                                e.type   = DictType::RectCenter;
                                                e.width  = r.doubleAttribute("width");
                                                e.height = r.doubleAttribute("height");
                                                }
                                          else {
                                                e.type   = DictType::Oval;
                                                e.width  = r.doubleAttribute("width");
                                                e.height = r.doubleAttribute("height");
                                                }
                                          while (r.readNextStartElement())
                                                if (r.name() == u"LineDesc") {
                                                      e.lineWidth = r.doubleAttribute("lineWidth");
                                                      e.lineEnd = r.attribute("lineEnd", u"ROUND").toString();
                                                      e.stroke  = true;
                                                      r.skipCurrentElement();
                                                      }
                                                else if (r.name() == u"FillDesc") {
                                                      auto fp = r.attribute("fillProperty");
                                                      e.fill  = (fp != u"HOLLOW" && fp != u"VOID");
                                                      r.skipCurrentElement();
                                                      }
                                                else
                                                      r.unknown();
                                          }
                                    else if (en == u"Arc" || en == u"Line" || en == u"Outline" ||
                                             en == u"Polyline" || en == u"Text") {
                                          Debug("IPC2581: DictionaryUser entry <{}> not yet supported",
                                                en.toString());
                                          r.skipCurrentElement();
                                          }
                                    else
                                          r.unknown();
                                    }
                              if (e.type != DictType::None)
                                    dictionary[e.id] = e;
                              }
                        else
                              r.unknown();
                        }
                  }
            else
                  r.unknown();
            }
      }

//---------------------------------------------------------
//   IPC2581::read
//    Top level: <IPC-2581> -> Content / LogisticHeader /
//    HistoryRecord / Bom / Ecad / Avl
//---------------------------------------------------------

void IPC2581::read(XmlReader& r) {
      while (r.readNextStartElement()) {
            if (r.name() == u"IPC-2581") {
                  while (r.readNextStartElement()) {
                        auto n = r.name();
                        if (n == u"Content")
                              readContent(r);
                        else if (n == u"LogisticHeader")
                              r.skipCurrentElement();
                        else if (n == u"HistoryRecord")
                              r.skipCurrentElement();
                        else if (n == u"Bom")
                              r.skipCurrentElement();
                        else if (n == u"Ecad")
                              readEcad(r);
                        else if (n == u"Avl")
                              r.skipCurrentElement();
                        else
                              r.unknown();
                        }
                  }
            else
                  r.unknown();
            }
      }

//---------------------------------------------------------
//   ImportIpc2581::import
//    Parse the file and create CAD elements for both the
//    CAD tree (Group layers with Polygon children) and a
//    Fixture with linked Recipes (laser layers).
//---------------------------------------------------------

namespace ImportIpc2581 {

//---------------------------------------------------------
//   side
//    Map layer function/side to a display side:
//    0=front, 1=back, 2=both
//---------------------------------------------------------

static int side(const PcbLayer* l) {
      if (l->side == u"TOP")
            return 0;
      if (l->side == u"BOTTOM")
            return 1;
      return 2;
      }

//---------------------------------------------------------
//   insertPolygon
//    Create a Polygon from path *p* and register it as a
//    child of *layer* via the undo stack (parent passed to
//    the Polygon constructor is NOT used for tree linking).
//---------------------------------------------------------

static void insertPolygon(ZCam* zcam, Group* layer, PcbPath p) {
      auto poly = new Polygon(zcam, layer);
      if (!p.stroke) {
            p.stroke = true; // default: show outline like dxf import
            p.fill   = false;
            }

      QColor strokeColor = QColor(Qt::green).lighter(50);

      poly->setName(p.name);
      poly->setColor(strokeColor);

      if (!p.empty()) {
            poly->moveTo(p.front());
            for (size_t i = 1; i < p.size(); ++i)
                  poly->lineTo(p[i]);
            if (p.close || p.fill)
                  const_cast<PainterPath&>(poly->painterPathData()).closeSubpath();
            }
      poly->set_fill(p.fill);
      if (p.stroke)
            poly->set_lineWidth(p.lineWidth);
      poly->update();
      zcam->project()->undo()->push(new InsertElementCommand(zcam, layer, poly, -1));
      }

//---------------------------------------------------------
//   import
//---------------------------------------------------------

bool import(ZCam* zcam, const QString& path) {
      Debug("ImportIpc2581: {}", path.toUtf8().constData());

      if (!zcam->project() || !zcam->project()->cad())
            return false;

      QFile file(path);
      if (!file.open(QIODeviceBase::ReadOnly)) {
            Warning("ImportIpc2581: cannot open <{}>: {}", path.toUtf8().constData(),
                    file.errorString().toUtf8().constData());
            return false;
            }

      XmlReader reader(&file);
      IPC2581 pcb(zcam);
      pcb.read(reader);

      if (reader.hasError()) {
            Warning("ImportIpc2581: XML error at line {}: {}", reader.lineNumber(),
                    reader.errorString().toUtf8().constData());
            return false;
            }

      // convert all coordinates to millimeter
      if (pcb.unitScale != 1.0) {
            for (auto l : pcb.layers) {
                  auto scalePaths = [&](std::list<PcbPath>& lst) {
                        for (auto& p : lst) {
                              for (auto& pt : p)
                                    pt = pt * pcb.unitScale;
                              p.lineWidth *= pcb.unitScale;
                              }
                        };
                  scalePaths(l->paths);
                  scalePaths(l->cutouts);
                  }
            }

      QFileInfo fi(path);
      Cad* cad         = zcam->project()->cad();
      UndoStack* us    = zcam->project()->undo();
      Fixture* fixture = zcam->project()->fixture();

      us->beginMacro();

      auto* root = new Group(zcam, cad);
      root->setName(fi.baseName());
      root->setExpanded(false);
      us->push(new InsertElementCommand(zcam, cad, root, -1));

      QRectF bbox;

      // create one CAD layer per IPC board layer
      for (auto l : pcb.layers) {
            if (l->paths.empty() && l->cutouts.empty())
                  continue;
            auto* gl = new Group(zcam, root);
            gl->setName(l->name);
            gl->setExpanded(false);
            us->push(new InsertElementCommand(zcam, root, gl, -1));
            if (l->function == QStringLiteral("CONDUCTOR"))
                  gl->set_invert(true);

            for (const auto& p : l->paths) {
                  insertPolygon(zcam, gl, p);
                  for (const auto& pt : p) {
                        QRectF pr(pt.x(), pt.y(), 0.1, 0.1);
                        bbox = bbox.isNull() ? pr : bbox.united(pr);
                        }
                  }
            // negative polarity features / Cutout: subtract the
            // material (approximated by a red filled outline)
            for (auto p : l->cutouts) {
                  p.name   = p.name + QStringLiteral("(cutout)");
                  p.fill   = true;
                  p.close  = true;
                  p.stroke = false;
                  insertPolygon(zcam, gl, p);
                  if (auto poly = qobject_cast<Polygon*>(gl->children().last())) {
                        poly->setColor(QColor("red"));
                        }
                  }
            }

      // link the CAD root layer into a fixture via a Recipe
      if (!fixture && !zcam->project()->fixtures().empty())
            fixture = zcam->project()->fixtures().at(0);
      if (fixture && root) {
            auto* ll = new Recipe(zcam, fixture);
            ll->setName(QStringLiteral("LL-%1").arg(fi.baseName()));
            ll->setExpanded(false);
            root->set_laserLayer(ll);
            us->push(new InsertElementCommand(zcam, fixture, ll, -1));
            }

      // center the imported geometry on the workspace
      if (bbox.isValid() && !bbox.isNull())
            root->set_pos(QVector3D(-bbox.center().x(), -bbox.center().y(), 0.0));

      us->endMacro();

      Debug("ImportIpc2581: done ({} layers)", pcb.layers.size());
      return true;
      }

      } // namespace ImportIpc2581

//---------------------------------------------------------
//   ImportIpc2581::isIpc2581File
//    Cheap sniff-test: an IPC-2581 file is an XML document
//    whose root element is <IPC-2581> (usual suffixes are
//    .xml or .cvg).
//---------------------------------------------------------

bool ImportIpc2581::isIpc2581File(const QString& path) {
      QFileInfo fi(path);
      QString suffix = fi.suffix().toLower();
      if (suffix != QStringLiteral("xml") && suffix != QStringLiteral("cvg"))
            return false;
      QFile f(path);
      if (!f.open(QIODeviceBase::ReadOnly))
            return false;
      const QByteArray head = f.read(4096);
      return head.contains("IPC-2581");
      }
