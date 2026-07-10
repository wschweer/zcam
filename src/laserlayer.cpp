//=============================================================================
//  wcam
//  G-Code generator
//
//  Copyright (C) 2023 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "laserlayer.h"
#include "cam.h"
#include "clipper.h"
#include "element3d.h"
#include "fixture.h"
#include "layer.h"
#include "zcam.h"
#include "xmlreader.h"
#include "xmlwriter.h"

//---------------------------------------------------------
//   Point
//---------------------------------------------------------

using Point = Clipper2Lib::Point<double>;

static void optimize(LaserPath* lp, Clipper2Lib::PathsD lines);
static Clipper2Lib::PathsD optimizePath(Clipper2Lib::PathsD inputLines, Point& currentPos);

//---------------------------------------------------------
//   LaserLayer
//---------------------------------------------------------

LaserLayer::LaserLayer(ZCam* w, Element* parent) : Element3d(w, parent) {
      connect(this, &LaserLayer::kerfOffsetChanged, [this]() { update(); });
      _geometry = new TessGeometry(this);
      QJSEngine::setObjectOwnership(_geometry, QJSEngine::CppOwnership);

#if 0
      connect(burnProperty, &Property::valueChanged, [this] {
            if (wcam->topLevel() && wcam->topLevel()->fixture() && wcam->topLevel()->fixture()->framing())
                  wcam->topLevel()->fixture()->framing()->update();
            Debug("burn property changed");
                  });
      connect(invertProperty, &Property::valueChanged, [this] { update(-1); });
      Dictionary* d = new Dictionary;
      d->push_back({"None", int(ParameterType::None)});
      d->push_back({"Speed", int(ParameterType::Speed)});
      d->push_back({"Power", int(ParameterType::Power)});
      d->push_back({"Interval", int(ParameterType::Interval)});
      d->push_back({"Frequency", int(ParameterType::Frequency)});
      d->push_back({"Count", int(ParameterType::Count)});
      d->push_back({"Pulse", int(ParameterType::Pulse)});
      overrideType1Property->setDictionary(d);
      overrideType2Property->setDictionary(d); // will produce a memory leak
      setModel("Fixture.qml");
      connect(showMarksProperty, &Property::valueChanged, [this]() { update(); });
      connect(showMovesProperty, &Property::valueChanged, [this]() { update(); });
#endif
      }

LaserLayer::~LaserLayer() {
      delete _geometry;
      }

#if 0

//---------------------------------------------------------
//   LaserLayerTemplates::read
//---------------------------------------------------------

void LaserLayerTemplates::read(XmlReader& r) {
      auto l = new LaserLayer(wcam);
      l->read(r);
      //TODO      insertRow(-1, l);
            }

//---------------------------------------------------------
//   LaserLayerTemplates::write
//    do not write the protected default layer
//---------------------------------------------------------

void LaserLayerTemplates::write(XmlWriter& w) const {
      for (int row = 1; row < rowCount(); ++row)
            data(row)->write(w);
            }
#endif

//---------------------------------------------------------
//   collectLayerPath
//    return polygonlist for laser
//---------------------------------------------------------

PathsD LaserLayer::collectLayerPath() {
      spl.clear();
      Layer* layer = baseElement();
      if (!layer) {
            Critical("no base element");
            return spl;
            }
#if 0
      Cam* cam                 = wcam->topLevel()->cam();
      Fixture* fixture         = wcam->topLevel()->fixture();
      QMatrix4x4 fixtureMatrix = fixture->matrix();

      double panelHD = cam->panelHDistance();
      double panelVD = cam->panelVDistance();
      double w, h;
      fixture->size(w, h);

      for (int row = 0; row < cam->panelRows(); ++row) {
            for (int column = 0; column < cam->panelColumns(); ++column) {
                  double xo = (panelHD + w) * column;
                  double yo = (panelVD + h) * row;

                  for (const auto e : layer->children()) {
                        // collect into spl:
                        const auto* ce = toType<Element3d>(e);
                        const auto& pl = ce->pathList();

                        QMatrix4x4 matrix = ce->matrix() * fixtureMatrix;
                        if (ce->parent() && isType<Layer>(ce->parent()))
                              matrix = matrix * toType<Layer>(ce->parent())->matrix();

                        for (const auto& p : pl) {
                              Clipper2Lib::PathD cp;
                              for (const auto& pt : p) {
                                    auto r = matrix.map(QVector3D(pt.x(), pt.y(), 0.0));
                                    cp.push_back({r.x() + xo, r.y() + yo});
                                          }
                              spl.push_back(cp);
                                    }
                              }
                        }
                  }
#endif
      return spl;
      }

//---------------------------------------------------------
//   update
//    collect data for one laser layer
//---------------------------------------------------------

void LaserLayer::update(int /* flags */) {
      LaserPath lp = collectLaserPath();
      Clipper2Lib::PathsD lineList;

      //
      // separate MarkTo and MoveTo segments
      //
      Clipper2Lib::PathD lines;
      LaserPathElement last;
      if (showMarks()) {
            for (auto& pt : lp) {
                  if (pt.type == LaserPathElementType::MarkTo) {
                        lines.push_back({last.x(), last.y()});
                        lines.push_back({pt.x(), pt.y()});
                        }
                  last = pt;
                  }
            }
      lineList.push_back(lines);
      lines.clear();
      if (!lp.empty()) {
            last = lp.front();
            if (showMoves()) {
                  for (auto& pt : lp) {
                        if (pt.type == LaserPathElementType::MoveTo) {
                              lines.push_back({last.x(), last.y()});
                              lines.push_back({pt.x(), pt.y()});
                              }
                        last = pt;
                        }
                  }
            }
      lineList.push_back(lines);
      _geometry->setLines(lineList);

#if 0
      //
      // process lines
      //
      //      Debug("{} {}  n {}", hasWobble, hasLine, lines.size());
      PathsD lineStrips;
      for (const auto& line : lines) {
            if (hasWobble)
                  lineStrips.push_back(wobble(line, wobbleStep, wobbleSize));
            if (hasLine) {
                  auto& path = lineStrips.emplace_back(Clipper2Lib::PathD());
                  for (const auto& point : line)
                        path.emplace_back(point.x, point.y);
                                                                                          }
      if (invert())
            Clipper::invertPolygons(poly);
#endif
      }

#if 0

//---------------------------------------------------------
//   readProperty
//    special handling for
//          - laserLayer -> sets
//---------------------------------------------------------

void LaserLayer::readProperty(XmlReader& r, bool blockSignals) {
      auto s = r.attribute("name").toString().toStdString();

      if (s == "laserLayer" || s == "baseName") {
            while (r.readNextStartElement()) {
                  if (r.name() == u"value") {
                        auto layerName = r.readElementText();
                        if (layerName.isEmpty())
                              Critical("no laserLayer name");
                        auto llt = wcam->laserLayerSettings(layerName);
                        //                        Debug("{}: setLaserLayer <{}>", name(), llt->name());
                        setLaserLayer(llt);
                              }
                  else {
                        Debug("unknown tag <{}>", r.name());
                        r.unknown();
                              }
                        }
                  }
      else if (s == "baseLayer") {
            //            Debug("baseLayer");
            while (r.readNextStartElement()) {
                  if (r.name() == u"value") {
                        auto name = r.readElementText();
                        //                        Debug("=========lookup <{}>", name);
                              }
                  else {
                        Debug("unknown tag <{}>", r.name());
                        r.unknown();
                              }
                        }
                  }
      else {
            Element::readProperty(r, blockSignals);
                  }
            }
#endif

//---------------------------------------------------------
//   collectLaserPath
//---------------------------------------------------------

LaserPath LaserLayer::collectLaserPath() const {
      if (!recipe()) {
            Critical("no laser layer settings");
            return LaserPath();
            }

      Layer* layer     = baseElement();
      Fixture* fixture = zcam->topLevel()->fixture();
      //      QMatrix4x4 fixtureMatrix = fixture->matrix();

      auto cam       = zcam->topLevel()->cam();
      double panelHD = cam->panelHDistance();
      double panelVD = cam->panelVDistance();
      double w, h;
      fixture->size(w, h);

#if 0
      Clipper2Lib::PathsD lineList;
      for (int row = 0; row < cam->panelRows(); ++row) {
            for (int column = 0; column < cam->panelColumns(); ++column) {
                  double xo = (panelHD + w) * column;
                  double yo = (panelVD + h) * row;

                  Debug("{} {} {} {}", row, column, xo, yo);

                  for (const auto e : layer->children()) {
                        const auto* ce = toType<Element3d>(e);
                        PathList pl    = ce->pathList();

                        QMatrix4x4 matrix = ce->matrix() * fixtureMatrix;
                        if (ce->parent() && isType<Layer>(ce->parent()))
                              matrix = matrix * toType<Layer>(ce->parent())->matrix();

                        //===========================================
                        //    convert to CAM coordinate system
                        //===========================================

                        Clipper2Lib::PathsD ll;
                        for (const auto& p : pl) {
                              Clipper2Lib::PathD cp;
                              for (const auto& pt : p) {
                                    auto r = matrix.map(QVector3D(pt.x(), pt.y(), 0.0));
                                    cp.push_back({r.x() + xo, r.y() + yo});
                                          }
                              ll.push_back(cp);
                                    }
                        auto ls         = laserLayer()->settings(0);
                        bool mustWobble = ls->wobble();
                        if (pl.fill())
                              lineList.append_range(createFill(ll));
                        else {
                              // process lines
                              if (mustWobble) {
                                    for (const auto& p : ll) {
                                          if (p.size() < 2)
                                                continue;
                                          auto wl = wobble(p, ls->wobbleStep(), ls->wobbleSize());
                                          lineList.push_back(wl);
                                                }
                                          }
                              else {
                                    for (const auto& l : ll) {
                                          if (l.size() < 2)
                                                continue;
                                          auto currentPoint = l[0];
                                          for (int i = 1; i < l.size(); ++i) {
                                                Clipper2Lib::PathD p;
                                                p.push_back(currentPoint);
                                                currentPoint = l[i];
                                                p.push_back(currentPoint);
                                                lineList.push_back(p);
                                                      }
                                                }
                                          }
                                    }
                              }
                        }
                  }
#endif
      LaserPath path;
      //      optimize(&path, lineList);
      return path;
      }

// Hilfsfunktion: Euklidische Distanz berechnen

//---------------------------------------------------------
//   getDistance
//---------------------------------------------------------

static double getDistance(const Point& p1, const Point& p2) {
      return std::hypot(p1.x - p2.x, p1.y - p2.y);
      }

//---------------------------------------------------------
//   optimizePath
//---------------------------------------------------------

static Clipper2Lib::PathsD optimizePath(Clipper2Lib::PathsD inputLines, Point& currentPos) {
      Clipper2Lib::PathsD optimizedLines;
      optimizedLines.reserve(inputLines.size());

      // Solange noch Linien in der Eingabeliste sind
      while (!inputLines.empty()) {
            double minDistance = std::numeric_limits<double>::max();
            size_t bestIndex   = 0;
            bool reverseLine   = false;

            // 1. Finde die nächste Linie (Greedy Nearest Neighbor)
            for (size_t i = 0; i < inputLines.size(); ++i) {
                  // Distanz zum Startpunkt der Linie
                  double distToStart = getDistance(currentPos, inputLines[i][0]);

                  // Distanz zum Endpunkt der Linie (falls wir sie rückwärts lasern wollen)
                  double distToEnd = getDistance(currentPos, inputLines[i][1]);

                  // Prüfe Startpunkt
                  if (distToStart < minDistance) {
                        minDistance = distToStart;
                        bestIndex   = i;
                        reverseLine = false;
                        }

                  // Prüfe Endpunkt (Bidirektionales Markieren erlauben)
                  // Falls es wichtig ist, Linien NICHT umzudrehen (z.B. wegen Schnittrichtung),
                  // diesen Block auskommentieren.
                  if (distToEnd < minDistance) {
                        minDistance = distToEnd;
                        bestIndex   = i;
                        reverseLine = true;
                        }
                  }

            // 2. Die beste Linie auswählen
            auto nextLine = inputLines[bestIndex];

            // 3. Falls der Endpunkt näher war, Start und Ende tauschen
            if (reverseLine)
                  std::swap(nextLine[0], nextLine[1]);

            // 4. Zur Ergebnisliste hinzufügen
            optimizedLines.push_back(nextLine);

            // 5. Neue aktuelle Position ist das Ende der gerade gefahrenen Linie
            currentPos = nextLine[1];

            // 6. Linie aus der Eingabeliste entfernen (Swap & Pop für Effizienz)
            // Wir tauschen das gefundene Element mit dem letzten und löschen das letzte.
            // Das ist O(1) statt O(N) beim Löschen.
            if (bestIndex != inputLines.size() - 1)
                  inputLines[bestIndex] = inputLines.back();
            inputLines.pop_back();
            }
      return optimizedLines;
      }

//---------------------------------------------------------
//   createFill
//    fill polygon spdi with hatch pattern
//---------------------------------------------------------

Clipper2Lib::PathsD LaserLayer::createFill(Clipper2Lib::PathsD& spdi) const {
      Clipper2Lib::PathsD lineList;
#if 0
      const LaserLayerSettings* fll = laserLayer();

      if (kerfOffset())
            spdi = InflatePaths(spdi, kerfOffset(), Clipper2Lib::JoinType::Miter, Clipper2Lib::EndType::Polygon, 2, 3);
      Clipper clipper;
      PathsD spd;
      clipper.AddSubject(spdi);
      clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, spd);

      //-------------------------------
      //    fill polygons
      //-------------------------------

      Point currentPos(0, 0);
      for (int i = 0; i < fll->rowCount(); ++i) {
            auto sl                   = fll->settings(i);
            Clipper2Lib::RectD bounds = Clipper2Lib::GetBounds(spd);
            qreal angle               = sl->startAngle();
            for (int i = 0; i < sl->numPasses(); ++i) {
                  double interval = sl->interval();
                  PathsD lines1;
                  auto hatchThread = std::thread([&lines1, spd, bounds, angle, interval] {
                        Clipper cl;
                        lines1 = cl.hatch(spd, bounds, angle, interval);
                              });
                  clipper.Clear();
                  PathsD lines2 = clipper.hatch(spd, bounds, angle + 90.0, sl->interval());
                  hatchThread.join();

                  lineList.append_range(lines1);
                  lineList.append_range(lines2);

                  angle += sl->angleIncrement();
                  while (angle >= 180.0)
                        angle -= 180.0;
                        }
                  }
#endif
      return lineList;
      }

//---------------------------------------------------------
//   optimize
//---------------------------------------------------------

static void optimize(LaserPath* lp, Clipper2Lib::PathsD lines) {
      Point currentPosition = lp->empty() ? Point(0, 0) : Point(lp->back().p.x(), lp->back().p.y());
      auto p                = optimizePath(lines, currentPosition);
      currentPosition       = p.front()[0];
      lp->moveTo(currentPosition.x, currentPosition.y);
      for (const auto& l : p) {
            if (l.size() == 2) {
                  if (!(qFuzzyCompare(currentPosition.x, l[0].x) && qFuzzyCompare(currentPosition.y, l[0].y)))
                        lp->moveTo(l[0].x, l[0].y);
                  lp->markTo(l[1].x, l[1].y);
                  }
            else {
                  Critical("=========not a line======");
                  }
            }
      }

//---------------------------------------------------------
//   toLaserPath
//    Convert LineSegments to LaserPath by connecting
//    the mark segments with move segments if necessary.
//---------------------------------------------------------

LaserPath LineSegments::toLaserPath() {
      LaserPath lp;
      if (empty())
            return LaserPath();
      Vec2d currentPosition = front().p1;
      lp.moveTo(currentPosition.x(), currentPosition.y());
      for (const auto& l : *this) {
            if (!(qFuzzyCompare(currentPosition.x(), l.p1.x()) &&
                  qFuzzyCompare(currentPosition.y(), l.p1.y())))
                  lp.moveTo(l.p1.x(), l.p1.y());
            lp.markTo(l.p2.x(), l.p2.y());
            currentPosition = l.p2;
            }
      return lp;
      }

//---------------------------------------------------------
//   check
//---------------------------------------------------------

bool LaserPath::check() {
      return true;
      Vec2d currentPosition = front().p;
      for (auto p = begin(); p != end(); ++p) {
            const auto& l = *p;
            if (qFuzzyCompare(currentPosition.x(), l.p.x()) && qFuzzyCompare(currentPosition.y(), l.p.y())) {
                  Critical("zero segment in LaserPath");
                  erase(p);
                  }
            currentPosition = l.p;
            }
      return true;
      }
