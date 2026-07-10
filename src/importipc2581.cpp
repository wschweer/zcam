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

#include <QUrl>
#include <QString>
#include <QFile>
#include <map>

#include "clipper.h"
#include "zcam.h"
#include "board.h"
#include "stock.h"
#include "cam.h"
#include "cad.h"
#include "layer.h"
#include "laserlayer.h"
#include "line.h"
#include "fixture.h"
#include "projecttree.h"
#include "undo.h"

enum FixtureType { FRONT = 1, BACK = 2, BOTH = 3, FRONT_MASK = 4, BACK_MASK = 8 };   // layer types

//---------------------------------------------------------
//   PcbPath
//---------------------------------------------------------

class PcbPath : public std::vector<Vec2d> {
   public:
      bool fill    { false };
      bool close   { false };
      bool stroke  { false };
      QString name;
      double lineWidth { 0.1 };
      QString lineEnd;

      bool operator==(const PcbPath& p) const {
            if (p.size() != size())
                  return false;
            for (int i = 0; i < size(); ++i) {
                  if (p[i] != at(i))
                        return false;
                  }
            return true;
            }
      };

static bool checkDoublette(const std::list<PcbPath>& paths, const PcbPath& path)
      {
      for (const auto& p : paths) {
            if (p == path)
                  return true;
            }
      return false;
      }

//---------------------------------------------------------
//   IPC2581LayerType
//---------------------------------------------------------

class IPC2581LayerType {
   public:
      QString name;
      QString function;
      QString side;
      };

//---------------------------------------------------------
//   DictEntry
//---------------------------------------------------------

enum class DictType { Line, RectCenter, Oval, Circle, RectRound, Path };

struct DictEntry {
      QString id;
      DictType type;

      double lineWidth;
      double width;
      double height;
      double radius;
      QString lineEnd;
      bool fill   { false };
      bool stroke { false };
      std::list<PcbPath> paths;
      };

//---------------------------------------------------------
//   RectRound
//---------------------------------------------------------

struct RectRound {
      double width;
      double height;
      double radius;
      bool upperRight;
      bool upperLeft;
      bool lowerRight;
      bool lowerLeft;
      void read(XmlReader& r) {
            width = r.doubleAttribute("width");
            height = r.doubleAttribute("height");
            radius = r.doubleAttribute("radius");
            upperRight = r.boolAttribute("upperRight");
            upperLeft = r.boolAttribute("upperLeft");
            lowerRight = r.boolAttribute("lowerRight");
            lowerLeft = r.boolAttribute("lowerLeft");
            r.readNext();
            }
      };

//---------------------------------------------------------
//   PcbLayer
//---------------------------------------------------------

class IPC2581;

class PcbLayer {
      IPC2581* pcb;

   public:
      PcbLayer(IPC2581* p) : pcb(p) {}
      Layer* layer { nullptr };
      FixtureType fixture;    // associated fixture ("front" or "back")

      bool fill;
      QString shortName;
      std::list<PcbPath> paths;

      void rectRound(double x, double y, double width, double height, double r, PcbPath& path);
      void createPrimitive(double x, double y, const DictEntry& de);
      PcbPath readPolygon(XmlReader& r);
      void readPolyline(XmlReader& r);
      void readLine(XmlReader& r);
      void readArc(XmlReader& r);
      void readPad(XmlReader& r);
      void readHole(XmlReader& r);
      };

//---------------------------------------------------------
//   IPC2581
//---------------------------------------------------------

class IPC2581 {

   public:
      IPC2581(Wcam* p) : zcam(p) {}

      Wcam* zcam;
      static const DictEntry emptyDict;
      std::map<QString,DictEntry> dictionary;
      std::map<QString,PcbLayer*> layers;
      std::map<QString,IPC2581LayerType> layerTypes;

      void readLayer(XmlReader& r);
      void addDict(const DictEntry& e);
      const DictEntry& dict(const QString& id) {
            if (!dictionary.contains(id)) {
                  Critical("{} not found", id);
                  return emptyDict;
                  }
            return dictionary[id];
            }

      void readPackage(XmlReader&);
      void readPadStack(XmlReader&);
      void readComponent(XmlReader&);
      void read(XmlReader& r);
      };

class PadStackPad {
   public:
      QString Layer;
      QString primitive;
      };

class PadStack : public vector<PadStackPad> {
      };

//---------------------------------------------------------
//   addDict
//---------------------------------------------------------

void IPC2581::addDict(const DictEntry& de)
      {
      if (dictionary.contains(de.id)) {
            Critical("{} already in dictionary", de.id);
            return;
            }
      dictionary[de.id] = de;
      }

//---------------------------------------------------------
//   readPolygon
//---------------------------------------------------------

PcbPath PcbLayer::readPolygon(XmlReader& r)
      {
      PcbPath path;

      while (r.readNextStartElement()) {
            if (r.name() == u"PolyBegin") {
                  double x = r.doubleAttribute("x");
                  double y = r.doubleAttribute("y");
                  path.push_back({x, y});
                  r.readNext();
                  }
            else if (r.name() == u"PolyStepSegment") {
                  double x = r.doubleAttribute("x");
                  double y = r.doubleAttribute("y");
                  path.push_back({x, y});
                  r.readNext();
                  }
            else if (r.name() == u"FillDesc") {
                  auto s = r.attribute("fillProperty");
                  if (s == u"HOLLOW")
                        path.fill = false;
                  r.readNext();
                  }
            else
                  r.unknown();
            }
      path.name = "Polygon";
      path.fill = true;
      path.close = true;
      return path;
      }

//---------------------------------------------------------
//   readLine
//---------------------------------------------------------

void PcbLayer::readLine(XmlReader& r)
      {
      PcbPath path;

      double x = r.doubleAttribute("startX");
      double y = r.doubleAttribute("startY");
      path.push_back({x, y});
      x = r.doubleAttribute("endX");
      y = r.doubleAttribute("endY");
      path.push_back({x, y});

      while (r.readNextStartElement()) {
            if (r.name() == u"LineDesc") {
                  path.lineWidth = r.doubleAttribute("lineWidth");
                  path.lineEnd = r.attribute("lineEnd").toString();
                  r.readNext();
                  }
            else if (r.name() == u"LineDescRef") {
                  QString id = r.attribute("id").toString();
                  DictEntry de = pcb->dict(id);
                  path.lineWidth = de.lineWidth;
                  path.lineEnd   = de.lineEnd;
                  r.readNext();
                  }
            else
                  r.unknown();
            }
      r.readNext();
      path.name   = "Line";
      path.stroke = true;
      path.fill   = true;
      path.close  = false;
      if (checkDoublette(paths, path))
            Debug("====Doublette {}", path.name);
      else
            paths.push_back(path);
      }

//---------------------------------------------------------
//   readArc
//---------------------------------------------------------

void PcbLayer::readArc(XmlReader& r)
      {
      PcbPath path;

      double x = r.doubleAttribute("startX");
      double y = r.doubleAttribute("startY");
      path.push_back({x, y});
      double ex = r.doubleAttribute("endX");
      double ey = r.doubleAttribute("endY");
      double cx = r.doubleAttribute("centerX");
      double cy = r.doubleAttribute("centerY");
      path.push_back({ex, ey});

      Debug("Arc {} {} -- {} {}  center {} {}", x, y, ex, ey, cx, cy);

      while (r.readNextStartElement()) {
            if (r.name() == u"LineDesc") {
                  path.lineWidth = r.doubleAttribute("lineWidth");
                  path.lineEnd = r.attribute("lineEnd").toString();
                  r.readNext();
                  }
            else if (r.name() == u"LineDescRef") {
                  QString id = r.attribute("id").toString();
                  DictEntry de = pcb->dict(id);
                  path.lineWidth = de.lineWidth;
                  path.lineEnd   = de.lineEnd;
                  r.readNext();
                  }
            else
                  r.unknown();
            }
      r.readNext();
      path.name   = "Arc";
      path.stroke = true;
      path.fill   = true;
      path.close  = true;
      if (checkDoublette(paths, path))
            Debug("====Doublette {}", path.name);
      else
            paths.push_back(path);
      }

//---------------------------------------------------------
//   readPolyline
//---------------------------------------------------------

void PcbLayer::readPolyline(XmlReader& r)
      {
      PcbPath path;

      while (r.readNextStartElement()) {
            if (r.name() == u"PolyBegin") {
                  double x = r.doubleAttribute("x");
                  double y = r.doubleAttribute("y");
                  path.push_back({x, y});
                  r.readNext();
                  }
            else if (r.name() == u"PolyStepSegment") {
                  double x = r.doubleAttribute("x");
                  double y = r.doubleAttribute("y");
                  path.push_back({x, y});
                  r.readNext();
                  }
            else if (r.name() == u"LineDescRef") {
                  QString id = r.attribute("id").toString();
                  auto d = pcb->dict(id);
                  path.lineWidth = d.lineWidth;
                  r.readNext();
                  }
            else
                  r.unknown();
            }
      path.name = "Polyline";
      path.stroke = true;
      path.close  = false;
      path.fill   = true;
      if (checkDoublette(paths, path))
            Debug("Doublette {}", path.name);
      else
            paths.push_back(path);
      }

//---------------------------------------------------------
//   circle2Polygon
//    r - radius
//---------------------------------------------------------

static void circle2Polygon(Vec2d pos, double r, double startAngle, double endAngle, PcbPath& p,
   double precision = 0.002)
      {
      precision = 0.0001;
      int s = (std::numbers::pi / acos(1.0 - (precision / r))) / 4.0 + 0.5;
      if (s < 2) // minimum is 2 segments per quadrant
            s = 2;
      double step = (0.5 * std::numbers::pi) / double(s);

      if (startAngle > endAngle)
            endAngle += 2.0 * M_PI;

      for (double angle = startAngle; angle < endAngle; angle += step) {
            double xx = cos(angle) * r;
            double yy = sin(angle) * r;
            p.push_back({xx + pos.x(), yy + pos.y()});
            }
      p.push_back({cos(endAngle) * r + pos.x(), sin(endAngle) * r + pos.y()});
      }

//---------------------------------------------------------
//   rectRound
//---------------------------------------------------------

void PcbLayer::rectRound(double x, double y, double width, double height, double r, PcbPath& path)
      {
      double w2 = width * .5;
      double h2 = height * .5;

      Vec2d p1 {x - w2 + r, y + h2 - r};
      Vec2d p2 {x + w2 - r, y + h2 - r};
      Vec2d p3 {x + w2 - r, y - h2 + r};
      Vec2d p4 {x - w2 + r, y - h2 + r};

      double precision = pcb->zcam->machine()->circlePrecision();
      circle2Polygon(p1, r, M_PI*.5, M_PI, path, precision);
      circle2Polygon(p4, r, M_PI, M_PI*1.5, path, precision);
      circle2Polygon(p3, r, M_PI*1.5, 0.0, path, precision);
      circle2Polygon(p2, r, 0, M_PI*.5, path, precision);

      path.fill  = fill;
      path.close = fill;      //??
      path.name = "RectRound";
      }

//---------------------------------------------------------
//   readPad
//---------------------------------------------------------

void PcbLayer::readPad(XmlReader& r)
      {
//      auto s = r.attribute("padstackDefRef");
      double x {0.0}, y{0.0};
      double rotation {0.0 };

      QString id;
      QString name;
      while (r.readNextStartElement()) {
            if (r.name() == u"Location") {
                  x = r.doubleAttribute("x");
                  y = r.doubleAttribute("y");
                  r.readNext();
                  }
            else if (r.name() == u"StandardPrimitiveRef") {
                  id = r.attribute("id").toString();
                  r.readNext();
                  }
            else if (r.name() == u"PinRef") {
                  name = r.attribute("pin").toString();
                  r.readNext();
                  }
            else if (r.name() == u"Xform") {
                  rotation = r.doubleAttribute("rotation", 0.0);
                  r.readNext();
                  }
            else
                  r.unknown();
            }

      DictEntry de = pcb->dict(id);
      PcbPath path;
      double precision = pcb->zcam->machine()->circlePrecision();
      switch (de.type) {
            case DictType::Circle:
                  circle2Polygon({x, y}, de.radius, 0.0, 2* M_PI, path, precision);
                  path.name  = "Circle";
                  break;
            case DictType::Oval: {
                  double h = de.height;
                  double h2 = h * .5;
                  double w = de.width;

                  if (rotation == 90.0 || rotation == 270.0) {
                        std::swap(w, h);
                        h2 = h * .5;
                        }
                  else
                        Critical("unhandled oval rotation {} h {} w {}", rotation, h, w);

                  circle2Polygon({x+h2-w*.5,   y},   h2, M_PI*.5, M_PI*1.5, path, precision);
                  circle2Polygon({x+w-h2-w*.5, y}, h2, M_PI*1.5, M_PI*.5, path, precision);
                  path.name  = "Oval";
                  }
                  break;
            case DictType::RectCenter:
                  path.push_back({x - de.width * .5, y + de.height * .5 });
                  path.push_back({x + de.width * .5, y + de.height * .5 });
                  path.push_back({x + de.width * .5, y - de.height * .5 });
                  path.push_back({x - de.width * .5, y - de.height * .5 });
                  path.name = "RectCenter";
                  break;
            case DictType::RectRound: {
                  double w2 = de.width * .5;
                  double h2 = de.height * .5;
                  double r = de.radius;

                  Vec2d p1 {x - w2 + r, y + h2 - r};
                  Vec2d p2 {x + w2 - r, y + h2 - r};
                  Vec2d p3 {x + w2 - r, y - h2 + r};
                  Vec2d p4 {x - w2 + r, y - h2 + r};

                  circle2Polygon(p1, r, M_PI*.5, M_PI, path, precision);
                  circle2Polygon(p4, r, M_PI, M_PI*1.5, path, precision);
                  circle2Polygon(p3, r, M_PI*1.5, 0.0, path, precision);
                  circle2Polygon(p2, r, 0, M_PI*.5, path, precision);

                  path.name = "RectRound";
                  }
                  break;
            default:
                  Critical("{} not handled", int(de.type));
                  break;
            }
      path.close  = true;
      path.fill   = true;
      path.stroke = false;

      if (checkDoublette(paths, path))
            ; // Debug("=======Doublette {}", path.name);
      else
            paths.push_back(path);
      }

//---------------------------------------------------------
//   readHole
//---------------------------------------------------------

void PcbLayer::readHole(XmlReader& r)
      {
      // <Set geometry="PADSTACK_1" net="NET:Net-(J2-Pin_2)">
      // <Hole name="H1" diameter="1.0" platingStatus="PLATED" plusTol="0.0" minusTol="0.0" x="89.940" y="-61.0"/>
      PcbPath path;
      path.name = r.attribute("name").toString();
      double d = r.doubleAttribute("diameter");
      double x = r.doubleAttribute("x");
      double y = r.doubleAttribute("y");
      double precision = pcb->zcam->machine()->circlePrecision();
      circle2Polygon({x, y}, d * .5, 0.0, 2* M_PI, path, precision);
      path.close = true;
      path.fill = true;
      path.stroke = false;
      paths.push_back(path);
      r.readNext();
      }

//---------------------------------------------------------
//   createPrimitive
//---------------------------------------------------------

void PcbLayer::createPrimitive(double x, double y, const DictEntry& de)
      {
      PcbPath p;
      double precision = pcb->zcam->machine()->circlePrecision();
      switch (de.type) {
            case DictType::RectRound:
                  rectRound(x, y, de.width, de.height, de.radius, p);
                  p.name = "RectRound";
                  p.close = true;
                  break;
            case DictType::Circle:
                  p.name = "Circle";
                  circle2Polygon({x, y}, de.radius, 0.0, 2* M_PI, p, precision);
                  break;
            default:
                  Critical("not implemented: {}", int(de.type));
                  break;
            }
      p.lineWidth = de.lineWidth;
      p.stroke = de.stroke;
      p.fill   = de.fill;
      paths.push_back(p);
      }

//---------------------------------------------------------
//   readPackage
//---------------------------------------------------------

void IPC2581::readPackage(XmlReader& r)
      {
      DictEntry d;
      d.type = DictType::Path;
      d.id   = r.attribute("name").toString();
      while (r.readNextStartElement()) {
            if (r.name() == u"Outline")
                  r.skipCurrentElement();
            else if (r.name() == u"PickupPoint")
                  r.skipCurrentElement();
            else if (r.name() == u"SilkScreen")
                  r.skipCurrentElement();
            else if (r.name() == u"AssemblyDrawing")
                  r.skipCurrentElement();
            else if (r.name() == u"Pin") {
                  double x = 0.0;
                  double y = 0.0;
                  QString id;
                  double rotation = 0.0;
                  while (r.readNextStartElement()) {
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
                              auto de = dict(id);
                              r.readNext();
                              }
                        else
                              r.unknown();
                        }

                  PcbPath path;
                  path.close  = true;
                  path.fill   = true;
                  path.stroke = false;
                  DictEntry de = dict(id);
                  double precision = zcam->machine()->circlePrecision();
                  switch (de.type) {
                        case DictType::Circle:
                              circle2Polygon({0.0, 0.0}, de.radius, 0.0, 2* M_PI, path, precision);
                              path.name  = "Circle";
                              break;
                        case DictType::Oval: {
                              double h = de.height;
                              double h2 = h * .5;
                              double w = de.width;

                              circle2Polygon({h2-w*.5,   0.0},   h2, M_PI*.5, M_PI*1.5, path, precision);
                              circle2Polygon({w-h2-w*.5, 0.0}, h2, M_PI*1.5, M_PI*.5, path, precision);
                              path.name  = "Oval";
                              }
                              break;
                        case DictType::RectCenter:
                              path.push_back({-de.width * .5, de.height * .5 });
                              path.push_back({de.width * .5,  de.height * .5 });
                              path.push_back({de.width * .5,  -de.height * .5 });
                              path.push_back({-de.width * .5, -de.height * .5 });
                              path.name = "RectCenter";
                              break;
                        case DictType::RectRound: {
                              double w2 = de.width * .5;
                              double h2 = de.height * .5;
                              double r = de.radius;

                              Vec2d p1 {-w2 + r, h2 - r};
                              Vec2d p2 {w2 - r, h2 - r};
                              Vec2d p3 {w2 - r, -h2 + r};
                              Vec2d p4 {w2 + r, -h2 + r};

                              circle2Polygon(p1, r, M_PI*.5, M_PI, path, precision);
                              circle2Polygon(p4, r, M_PI, M_PI*1.5, path, precision);
                              circle2Polygon(p3, r, M_PI*1.5, 0.0, path, precision);
                              circle2Polygon(p2, r, 0, M_PI*.5, path, precision);

                              path.name = "RectRound";
                              }
                              break;
                        default:
                              Critical("{} not handled", int(de.type));
                              break;
                        }
                  QTransform t;
                  t.translate(x, y);
                  t.rotate(rotation);
                  for (auto& pp : path) {
                        auto ppp = t.map(QPointF(pp.x(), pp.y()));
                        pp.x() = ppp.x();
                        pp.y() = ppp.y();
                        }
                  d.paths.push_back(path);
                  }
            else
                  r.unknown();
            }
      addDict(d);
      }

//---------------------------------------------------------
//   readPadStack
//---------------------------------------------------------

void IPC2581::readPadStack(XmlReader& r)
      {
//      auto name = r.attribute("name");
      while (r.readNextStartElement()) {
            if (r.name() == u"PadstackHoleDef")
                  r.skipCurrentElement();
            else if (r.name() == u"PadstackPadDef") {
  //                auto layer = r.attribute("layerRef");
                  //double x = 0.0;
                  //double y = 0.0;
                  QString id;
                  while (r.readNextStartElement()) {
                        if (r.name() == u"Location") {
                              //double x = r.doubleAttribute("x");
                              //double y = r.doubleAttribute("y");
                              r.readNext();
                              }
                        else if (r.name() == u"UserPrimitiveRef") {
                              id = r.attribute("id").toString();
                              r.readNext();
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
//   readComponent
//---------------------------------------------------------

void IPC2581::readComponent(XmlReader& r)
      {
      QTransform t;

      double x = 0.0;
      double y = 0.0;
      double rotation { 0.0 };
      QString ref = r.attribute("packageRef").toString();
      while (r.readNextStartElement()) {
            if (r.name() == u"Xform") {
                  rotation = r.doubleAttribute("rotation", 0.0);
                  r.readNext();
                  }
            else if (r.name() == u"Location") {
                  x = r.doubleAttribute("x");
                  y = r.doubleAttribute("y");
                  r.readNext();
                  }
            else
                  r.unknown();
            }
      if (!dictionary.contains(ref)) {
            Critical("{} not in dictionary", ref);
            return;
            }
      t.translate(x, y);
      t.rotate(rotation);

      auto d = dictionary[ref];
      if (layers.contains("F.Cu")) {
            for (auto p : d.paths) {
                  for (auto& pp : p) {
                        auto ppp = t.map(QPointF(pp.x(), pp.y()));
                        pp.x() = ppp.x();
                        pp.y() = ppp.y();
                        }
                  layers["F.Cu"]->paths.push_back(p);
                  }
            }
      if (layers.contains("B.Cu")) {
            for (auto p : d.paths) {
                  for (auto& pp : p) {
                        auto ppp = t.map(QPointF(pp.x(), pp.y()));
                        pp.x() = ppp.x();
                        pp.y() = ppp.y();
                        }
                  layers["B.Cu"]->paths.push_back(p);
                  }

            }
      }

//---------------------------------------------------------
//   readLayer
//---------------------------------------------------------

void IPC2581::readLayer(XmlReader& r)
      {
      QString s = r.attribute("layerRef").toString();  // LAYER:, DRILL: SLOT:

      Debug("=============LAYER {}", s);

      // check for layer to ignore
      for (auto ss : zcam->pcbIgnoreLayer().split(',')) {
            if (s == ss.simplified()) {
                  r.skipCurrentElement();
                  return;
                  }
            }
      QString ss = s;
      auto cl = zcam->pcbCombineLayer().split(',');
      for (auto sss : cl) {
            if (s == sss.simplified()) {
                  Debug("rename layer <{}> to <{}>", s, cl[0].simplified());
                  ss = cl[0].simplified();
                  break;
                  }
            }
      QString sss;
      if (ss.startsWith("LAYER"))
            sss = ss.mid(6);
      else if (ss.startsWith("DRILL:"))
            sss = "Drill";
      else if (ss.startsWith("SLOT:"))
            sss = ss.mid(5);
      else
            sss = ss;
      PcbLayer* l { nullptr };
      if (!layers.contains(sss)) {
            l = new PcbLayer(this);
            layers[sss] = l;
            }
      else
            l = layers[sss];

      auto& layerType = layerTypes[s];
      if (layerType.function == "CONDUCTOR" && layerType.side == "TOP")
            l->fixture = FixtureType::FRONT;
      else if (layerType.function == "CONDUCTOR" && layerType.side == "BOTTOM")
            l->fixture = FixtureType::BACK;
      else if (layerType.function == "SOLDERMASK" && layerType.side == "TOP")
            l->fixture = FixtureType::FRONT_MASK;
      else if (layerType.function == "SOLDERMASK" && layerType.side == "BOTTOM")
            l->fixture = FixtureType::BACK_MASK;
      else if (layerType.function == "DRILL" && layerType.side == "ALL")
            l->fixture = FixtureType::BOTH;
      else if (layerType.function == "BOARD_OUTLINE" && layerType.side == "ALL")
            l->fixture = FixtureType::BOTH;
      else if (layerType.function == "ROUT" && layerType.side == "ALL")
            l->fixture = FixtureType::BOTH;
      else if (layerType.function == "SILKSCREEN" && layerType.side == "TOP")
            l->fixture = FixtureType::FRONT;
      else if (layerType.function == "COURTYARD" && layerType.side == "TOP")
            l->fixture = FixtureType::FRONT;
      else
            Critical("unknown layer <{}> function: <{}> side: <{}>", s, layerType.function, layerType.side);

      l->shortName = sss;

      while (r.readNextStartElement()) {
            if (r.name() == u"Set") {
                  while (r.readNextStartElement()) {
                        if (r.name() == u"Features") {
                              double x = 0, y = 0;
                              while (r.readNextStartElement()) {
                                    if (r.name() == u"UserSpecial") {
                                          while (r.readNextStartElement()) {
                                                if (r.name() == u"Contour") {
                                                      while (r.readNextStartElement()) {
                                                            if (r.name() == u"Polygon") {
                                                                  auto pp = l->readPolygon(r);
                                                                  if (checkDoublette(l->paths, pp))
                                                                        Debug("Doublette {}", pp.name);
                                                                  else
                                                                        l->paths.push_back(pp);
                                                                  }
                                                            else
                                                                  r.unknown();
                                                            }
                                                      }
                                                else if (r.name() == u"Line")
                                                      l->readLine(r);
                                                else if (r.name() == u"Polyline")
                                                      l->readPolyline(r);
                                                else
                                                      r.unknown();
                                                }
                                          }
                                    else if (r.name() == u"Location") {
                                          x = r.doubleAttribute("x");
                                          y = r.doubleAttribute("y");
                                          r.readNext();
                                          }
                                    else if (r.name() == u"Line") {
                                          l->readLine(r);
                                          }
                                    else if (r.name() == u"Arc") {
                                          l->readArc(r);
                                          }
                                    else if (r.name() == u"UserPrimitiveRef") {
                                          QString id = r.attribute("id").toString();
                                          auto de = dict(id);
                                          l->createPrimitive(x, y, de);
                                          r.readNext();
                                          }
                                    else
                                          r.unknown();
                                    }
                              }
                        else if (r.name() == u"Pad")
                              l->readPad(r);
                        else if (r.name() == u"NonstandardAttribute") {
                              r.skipCurrentElement();
                              }
                        else if (r.name() == u"Hole")
                              l->readHole(r);
                        else if (r.name() == u"SlotCavity") {
                              while (r.readNextStartElement()) {
                                    if (r.name() == u"Location") {
                                          Debug("===skip Location");
                                          r.readNext();
                                          }
                                    else if (r.name() == u"Outline") {
                                          PcbPath pp;
                                          while (r.readNextStartElement()) {
                                                if (r.name() == u"Polygon") {
                                                      pp = l->readPolygon(r);
                                                      }
                                                else if (r.name() == u"LineDescRef") {
                                                      QString id = r.attribute("id").toString();
                                                      auto de = dict(id);
                                                      pp.lineWidth = de.lineWidth;
                                                      r.readNext();
                                                      }
                                                else
                                                      r.unknown();
                                                }
                                          l->paths.push_back(pp);
                                          }
                                    else
                                          r.unknown();
                                    }
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
//---------------------------------------------------------

void IPC2581::read(XmlReader& r)
      {
      while (r.readNextStartElement()) {
            if (r.name() == u"IPC-2581") {
                  while (r.readNextStartElement()) {
                        auto n = r.name();
                        if (n == u"Content") {
                              while (r.readNextStartElement()) {
                                    auto n = r.name();
                                    if (n == u"FunctionMode")
                                          r.readNext();
                                    else if (n == u"StepRef")
                                          r.readNext();
                                    else if (n == u"LayerRef")
                                          r.readNext();
                                    else if (n == u"DictionaryColor")
                                          r.readNext();
                                    else if (n == u"DictionaryLineDesc") {
                                          while (r.readNextStartElement()) {
                                                if (r.name() == u"EntryLineDesc") {
                                                      QString id = r.attribute("id").toString();
                                                      while (r.readNextStartElement()) {
                                                            if (r.name() == u"LineDesc") {
                                                                  DictEntry e;
                                                                  e.id = id;
                                                                  e.type = DictType::Line;
                                                                  e.lineWidth = r.doubleAttribute("lineWidth");
                                                                  e.lineEnd = r.attribute("lineEnd").toString();
                                                                  addDict(e);
                                                                  r.readNext();
                                                                  }
                                                            else
                                                                  r.unknown();
                                                            }
                                                      }
                                                else
                                                      r.unknown();
                                                }
                                          }
                                    else if (n == u"DictionaryFillDesc")
                                          r.readNext();
                                    else if (n == u"DictionaryStandard") {
                                          while (r.readNextStartElement()) {
                                                if (r.name() == u"EntryStandard") {
                                                      DictEntry e;
                                                      e.id = r.attribute("id").toString();
                                                      while (r.readNextStartElement()) {
                                                            if (r.name() == u"RectCenter") {
                                                                  e.type = DictType::RectCenter;
                                                                  e.width = r.doubleAttribute("width");
                                                                  e.height = r.doubleAttribute("height");
                                                                  r.readNext();
                                                                  }
                                                            else if (r.name() == u"Circle") {
                                                                  e.type = DictType::Circle;
                                                                  e.radius = r.doubleAttribute("diameter") * .5;
                                                                  r.readNext();
                                                                  }
                                                            else if (r.name() == u"Oval") {
                                                                  e.type = DictType::Oval;
                                                                  e.width = r.doubleAttribute("width");
                                                                  e.height = r.doubleAttribute("height");
                                                                  r.readNext();
                                                                  }
                                                            else if (r.name() == u"RectRound") {
                                                                  RectRound rr;
                                                                  rr.read(r);
                                                                  e.type = DictType::RectRound;
                                                                  e.width = rr.width;
                                                                  e.height = rr.height;
                                                                  e.radius = rr.radius;
                                                                  //missing:        <RectRound upperRight="true" upperLeft="true" lowerRight="true" lowerLeft="true"/>
                                                                  }
                                                            else
                                                                  r.unknown();
                                                            }
                                                      addDict(e);
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
                                                            if (r.name() == u"UserSpecial") {
                                                                  while (r.readNextStartElement()) {
                                                                        if (r.name() == u"Circle") {
                                                                              e.type = DictType::Circle;
                                                                              e.radius = r.doubleAttribute("diameter") * .5;
                                                                              while (r.readNextStartElement()) {
                                                                                    if (r.name() == u"LineDesc") {
                                                                                          e.lineWidth = r.doubleAttribute("lineWidth");
                                                                                          e.lineEnd = r.attribute("lineEnd").toString();
                                                                                          r.readNext();
                                                                                          }
                                                                                    else if (r.name() == u"FillDesc") {
                                                                                          auto fp = r.attribute("fillProperty");
                                                                                          e.fill = fp != u"HOLLOW";
                                                                                          r.readNext();
                                                                                          }
                                                                                    }
                                                                              }
                                                                        else if (r.name() == u"RectRound") {
                                                                              e.type = DictType::RectRound;
                                                                              RectRound rr;
                                                                              rr.read(r);
                                                                              e.type = DictType::RectRound;
                                                                              e.width = rr.width;
                                                                              e.height = rr.height;
                                                                              e.radius = rr.radius;
                                                                              e.fill   = true;
                                                                              e.stroke = true;

                                                                              while (r.readNextStartElement()) {
                                                                                    if (r.name() == u"LineDesc") {
                                                                                          e.lineWidth = r.doubleAttribute("lineWidth");
                                                                                          r.readNext();
                                                                                          }
                                                                                    else if (r.name() == u"FillDesc") {
                                                                                          auto fp = r.attribute("fillProperty");
                                                                                          e.fill = fp != u"HOLLOW";
                                                                                          r.readNext();
                                                                                          }
                                                                                    else
                                                                                          r.unknown();
                                                                                    }
                                                                              }
                                                                        else
                                                                              r.unknown();
                                                                        }
                                                                  }
                                                            else
                                                                  r.unknown();
                                                            }
                                                      addDict(e);
                                                      }
                                                else
                                                      r.unknown();
                                                }
                                          }
                                    else
                                          r.unknown();
                                    }
                              }
                        else if (n == u"LogisticHeader") {
                              r.skipCurrentElement();
                              }
                        else if (n == u"HistoryRecord") {
                              r.skipCurrentElement();
                              }
                        else if (n == u"Bom") {
                              r.skipCurrentElement();
                              }
                        else if (n == u"Ecad") {
                              while (r.readNextStartElement()) {
                                    auto n = r.name();
                                    if (n == u"CadHeader")
                                          r.skipCurrentElement();
                                    else if (n == u"CadData") {
                                          while (r.readNextStartElement()) {
                                                auto n = r.name();
                                                if (n == u"Layer") {
                                                      QString name = r.attribute("name").toString();
                                                      QString function = r.attribute("layerFunction").toString();
                                                      QString side = r.attribute("side").toString();
                                                      layerTypes[name] = { name, function, side };
                                                      while (r.readNextStartElement()) {
                                                            if (r.name() == u"Span")
                                                                  r.readNext();
                                                            else
                                                                  r.unknown();
                                                            }
                                                      }
                                                else if (n == u"Step") {
                                                      while (r.readNextStartElement()) {
                                                            auto n = r.name();
                                                            if (n == u"Span")
                                                                  r.skipCurrentElement();
                                                            else if (n == u"NonstandardAttribute")
                                                                  r.skipCurrentElement();
                                                            else if (n == u"PadStackDef")
                                                                  readPadStack(r);
                                                            else if (n == u"Datum")
                                                                  r.skipCurrentElement();
                                                            else if (n == u"Profile")
                                                                  r.skipCurrentElement();
                                                            else if (n == u"Package")
                                                                  readPackage(r);
                                                            else if (n == u"Component")
                                                                  readComponent(r);
                                                            else if (n == u"LayerFeature")
                                                                  readLayer(r);
                                                            else
                                                                  r.unknown();
                                                            }
                                                      }
                                                else
                                                      r.unknown();
                                                }
                                          }
                                    else
                                          r.unknown();
                                    }
                              }
                        else if (n == u"Avl") {
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
//   createLayer
//---------------------------------------------------------

static void createLayer(Wcam* zcam, Fixture* fixture, const PcbLayer* l)
      {
      LaserLayer* ll = createElement<LaserLayer>(zcam, "LL-" + l->shortName.toStdString(), fixture);
      ll->setExpanded(false);
      ll->setBaseElement(l->layer);
      if (l->shortName == "F.Cu" || l->shortName == "B.Cu") {
            ll->setLaserLayer(zcam->laserLayerSettings("PCB-Copper"));        // TODO: make configurable (template?)
//            ll->setKerfOffset(-0.05);
            }
      else if (l->shortName == "Drill") {
            ll->setLaserLayer(zcam->laserLayerSettings("PCB-Drill"));
//            ll->setKerfOffset(-0.05);
            }
      else if (l->shortName == "Edge.Cuts") {
//            ll->setKerfOffset(0.0);
            }
      }

//---------------------------------------------------------
//   importIPC2581
//---------------------------------------------------------

bool Wcam::importIPC2581(const QUrl& url)
      {
      setProjectType("laser");
      setName("NewProject");
      _description = "";
      setFilePath("unnamed");
      setIsNew(true);

      _projectTreeModel->beginReset();
      undo()->reset();
      for (auto f : _topLevelElements)
            delete f;
      _topLevelElements.clear();
      _hoverModel = nullptr;
      _editElement = nullptr;
      setCurrentElement(nullptr);

      QString path = url.path();
      Debug("open <{}>", path);

      QFile f(path);
      if (!f.open(QIODeviceBase::ReadOnly)) {
            Debug("open failed: {}", f.errorString());
            return false;
            }
      XmlReader r(&f);
      IPC2581 pcb(this);
      pcb.layers["F.Cu"] = new PcbLayer(&pcb);
      pcb.layers["B.Cu"] = new PcbLayer(&pcb);

      pcb.read(r);

      Board* board = ::createElement<Board>(this, "testboard", nullptr);
      _topLevelElements.push_back(board);
      setTopLevel(board);

      Cad* cad = ::createElement<Cad>(this, "Cad", board);
      Cam* cam = ::createElement<Cam>(this, "Cam", board);
      board->setCad(cad);
      board->setCam(cam);

      RectD bbox(false);
      for (auto& [name, l] : pcb.layers) {
            Layer* layer = createElement<Layer>(this, l->shortName.toStdString(), cad);
            l->layer     = layer;
            layer->setExpanded(false);
            if (name == "F.Cu" || name == "B.Cu")
                  layer->setInvert(true);

            for (const auto& p : l->paths) {
                  Line* line = createElement<Line>(this, p.name.toStdString(), layer);
                  for (const auto& pt : p) {
                        line->lineTo(pt);
                        bbox += RectD(pt.x(), pt.y(), pt.x(), pt.y());
                        }
                  if (p.fill)
                        line->setFill(p.fill);
                  line->setLineWidth(p.lineWidth);
                  if (p.close)
                        line->closePath();
                  }
            }
      std::map<QString,PcbLayer> layers;

      // center pcb:
      Clipper2Lib::PointD pt = bbox.MidPoint();
      cad->setPos(Vec3d(-pt.x, -pt.y, 0));

      //
      //    "stock" is a 100mmx160mm PCB board ("Eurocard"):
      //
      Stock* stock = createElement<Stock>(this, "Stock", cam);
      stock->setPos(Vec3d(-80.0, -50.0, 0));
      stock->setSize(Vec3d(160.0, 100.0, 0.0));
      board->setStock(stock);

      std::map<FixtureType, int> fixtureTypes;
      for (auto [name, l] : pcb.layers) {
            if (l->fixture & FRONT)
                  fixtureTypes[FRONT] = 1;
            if (l->fixture & BACK)
                  fixtureTypes[BACK] = 1;
            if (l->fixture & FRONT_MASK)
                  fixtureTypes[FRONT_MASK] = 1;
            if (l->fixture & BACK_MASK)
                  fixtureTypes[BACK_MASK] = 1;
            }

      // create fixture for every found type
      std::map<FixtureType, Fixture*> fixtures;
      for (auto ft : fixtureTypes) {
            switch(ft.first) {
                  case FRONT: {
                        auto front = createElement<Fixture>(this, "Front", cam);
                        board->addFixture(front);
                        fixtures[FRONT] = front;
                        front->setExpanded(true);
                        }
                        break;
                  case BACK: {
                        auto back  = createElement<Fixture>(this, "Back", cam);
                        board->addFixture(back);
                        fixtures[BACK] = back;
                        back->setExpanded(true);
                        }
                        break;
                  case FRONT_MASK: {
                        auto frontMask = createElement<Fixture>(this, "FrontMask", cam);
                        board->addFixture(frontMask);
                        fixtures[FRONT_MASK] = frontMask;
                        frontMask->setExpanded(true);
                        }
                        break;
                  case BACK_MASK: {
                        auto backMask  = createElement<Fixture>(this, "BackMask", cam);
                        board->addFixture(backMask);
                        fixtures[BACK_MASK] = backMask;
                        backMask->setExpanded(true);
                        }
                        break;
                  case BOTH:
                        break;
                  }
            }
      // fill fixtures with layers
      for (auto ft : fixtures) {
            auto fixture = ft.second;
            switch (ft.first) {
                  case FRONT:
                  case FRONT_MASK: {
#if 0
                        fixture->prop("posX")->setScript("Cad.posX");
                        fixture->prop("posX")->setBinding();
                        fixture->prop("posY")->setScript("Cad.posY");
                        fixture->prop("posY")->setBinding();
#endif
                        for (auto [name, l] : pcb.layers) {
                              if (l->fixture & ft.first)
                                    createLayer(this, fixture, l);
                              }
                        break;
                        }

                  case BACK:
                  case BACK_MASK: {
#if 0
                        fixture->prop("posX")->setScript("Front.posX");
                        fixture->prop("posX")->setBinding();
                        fixture->prop("posY")->setScript("-Front.posY");
                        fixture->prop("posY")->setBinding();
#endif
                        fixture->setRot({180.0, 0, 0 });
                        for (auto [name, l] : pcb.layers) {
                              if (l->fixture & ft.first)
                                    createLayer(this, fixture, l);
                              }
                        break;
                        }
                  case BOTH:
                        break;
                  }
            }

      if (fixtures.contains(BACK))
            board->setFixture(fixtures[BACK]);
      else if (fixtures.contains(FRONT))
            board->setFixture(fixtures[FRONT]);
      else
            Fatal("no  fixture {}", fixtures.size());

      traverseTree(_topLevelElements[0]); // set bindings
      _projectTreeModel->endReset();
      emit rootElementChanged(topLevel()->cad());
      emit projectChanged();
      emit projectTreeModelChanged();
      emit currentViewsChanged();   // list of fixtures changed
      return true;
      }

const DictEntry IPC2581::emptyDict = DictEntry();
