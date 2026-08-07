//=============================================================================
//  wcam
//    Process CAD files for production on CNC and laser machines.
//
//  Copyright (C) 2025 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <functional>
#include <QTransform>
#include <QDir>
#include "zcam.h"
#include "polygon.h"
#include "rectangle.h"
#include "text.h"
#include "cad.h"
#include "cam.h"
#include "fixture.h"
#include "grid.h"
#include "project.h"
#include "types.h"
#include "undo.h"
#include "cal.h"
#include "ogr.h"

// resolution of scanned test grid image
// static const double DPI = 1200.0;
//static const double DPMM = 1200.0 / (25.4 * 0.73);
static const double DPMM = 1200.0 / 25.4;

//---------------------------------------------------------
//   createLine
//---------------------------------------------------------

static void createLine(ZCam* zcam, Group* layer, const Vec2d& p1, const Vec2d& p2) {
      auto diag1 = new Polygon(zcam, layer);
      diag1->setName("lines");
      diag1->moveTo(p1);
      diag1->lineTo(p2);
      diag1->setColor(QColor("black"));
      diag1->update();
      layer->addChild(diag1);
      }

//---------------------------------------------------------
//   galvotest
//    create a new project with a galvo testpattern
//---------------------------------------------------------

void ZCam::calibrationScan() {
      // Preserve the machine from the current project before
      // creating a new one.  The Machine* is owned by the
      // Machines asset (not by the Project), so the pointer
      // remains valid across project recreation.
      Machine* savedMachine = project() ? project()->machine() : nullptr;
      startNewProject();

      // Restore the machine to the new project so the material
      // test pattern uses the same machine as the current project.
      if (savedMachine)
            project()->set_machine(savedMachine);

      project()->setName("GalvoTest");
      project()->setExpanded(true);
      Cad* cad = project()->cad();
      cad->setExpanded(true);
      Cam* cam = project()->cam();
      cam->setExpanded(true);
      Fixture* fixture = project()->fixture();

      auto group = new Group(this, cad);
      group->setExpanded(true);

      double k = 150.0; // total size
      double b = 5;     // border
      double r = k * .5;
      double l = r + b;

      createLine(this, group, {-l, r}, {l, r});
      createLine(this, group, {-l, 0}, {l, 0});
      createLine(this, group, {-l, -r}, {l, -r});

      //      +---+---+
      //      |   |   |
      //      a---b---c
      //      |   |   |
      //      +---+---+

      createLine(this, group, {-r, +l}, {-r, -l});
      createLine(this, group, {0, +l}, {0, -l});
      createLine(this, group, {r, +l}, {r, -l});

      auto text = new Text(this, group);
      text->set_pos(QVector3D(10, 20, 0.0));

      text->set_pointSize(24.0);
      text->set_fill(false);
      text->set_text(QString("ZCam\n%1").arg(k));

      //      auto framing = new Framing>(this, fixture);
      //      fixture->setFraming(framing);

      auto ll = new Recipe(this, fixture);
      //      ll->setLaserLayer(wcam->laserLayerSettings("cbBlackTest"));

      //      _cadLayer->set(layers);

      endNewProject();
      }

//---------------------------------------------------------
//   galvotest65
//    Create a new project with a 65×65 grid test pattern that
//    fills the entire laser work area.  Two diagonal lines are
//    added for galvo alignment verification.
//---------------------------------------------------------

void ZCam::createGalvoTest64() {
      // Preserve the machine from the current project before
      // creating a new one.  The Machine* is owned by the
      // Machines asset (not by the Project), so the pointer
      // remains valid across project recreation.
      Machine* savedMachine = project() ? project()->machine() : nullptr;
      startNewProject();

      // Restore the machine to the new project so the test
      // pattern uses the same machine as the current project.
      if (savedMachine)
            project()->set_machine(savedMachine);

      project()->setName("Galvo-Test 64");

      Cad* cad         = project()->cad();
      Cam* cam         = project()->cam();
      Fixture* fixture = project()->fixture();

      cad->setExpanded(true);
      cam->setExpanded(true);
      fixture->setExpanded(false);

      // Create a Grid covering the machine work area
      auto grid = new Grid(this, project());
      project()->addChild(grid);
      emit project() -> gridElementChanged();

      // Create a single layer for the galvo test pattern
      auto layer = new Group(this, cad);
      layer->setName("GalvoPattern64");
      layer->setExpanded(true);
      cad->addChild(layer);

      // Work area dimensions from the machine
      double w = 150.0;
      double h = 150.0;
      if (project()->machine()) {
            QVector3D mt = project()->machine()->maxTravel();
            w            = mt.x();
            h            = mt.y();
            }

      // 65×65 grid: 65 vertical and 65 horizontal lines evenly
      // spaced across the full work area.  The grid lines span
      // from (0,0) to (w,h) in machine coordinates.
      const int gridLines = 65;
      double stepX        = w / (gridLines - 1);
      double stepY        = h / (gridLines - 1);

      for (int i = 0; i < gridLines; ++i) {
            double x = i * stepX;
            createLine(this, layer, {x, 0.0}, {x, h});
            }
      for (int i = 0; i < gridLines; ++i) {
            double y = i * stepY;
            createLine(this, layer, {0.0, y}, {w, y});
            }

      // Two diagonal lines corner to corner
      createLine(this, layer, {0.0, 0.0}, {w, h});
      createLine(this, layer, {0.0, h}, {w, 0.0});

      // Border rectangle
      auto border = new Rectangle(this, layer);
      border->setName("border");
      border->set_size(QVector2D(w, h));
      border->set_pos(QVector3D(w * 0.5, h * 0.5, 0.0));
      border->set_fill(false);
      border->set_lineWidth(0.0);
      border->setColor(QColor("black"));
      border->update();
      layer->addChild(border);

      // Label text
      auto label = new Text(this, layer);
      label->setName("label");
      label->set_text("Galvo Test 64");
      label->set_pointSize(8.0);
      label->set_fontFamily("Noto Sans");
      label->set_pos(QVector3D(50.0, 25.0, 0.0));
      label->setColor(QColor("yellow"));
      label->update();
      layer->addChild(label);

      // Create a LaserLayer linked to the galvo pattern layer
      auto ll = new Recipe(this, fixture);
      ll->setName("LL-GalvoPattern64");
      layer->set_laserLayer(ll);
      auto recipes = this->recipes();
      if (recipes && recipes->recipeCount() > 0)
            ll->set_recipe(recipes->recipePtr(0));
      fixture->addChild(ll);

      endNewProject();
      }

//---------------------------------------------------------
//   SortedPoint
//---------------------------------------------------------

struct SortedPoint {
      cv::Point2f originalPt;
      cv::Point2f rotatedPt; // Temporär für die Sortierung
      int originalIndex;
      };

static int rows, cols;

//---------------------------------------------------------
//   analyzeImage
//---------------------------------------------------------

void CalData::analyzeImage(cv::Mat& img) {
      Ogr ogr;
      double angle = ogr.skew(img);

      Debug("========================SKEW {}", angle);

      clear();

      cv::Mat processed = img;

      // CLAHE für lokalen Kontrast (sehr wichtig bei schlechten Scans!)
      auto clahe = cv::createCLAHE(2.9, cv::Size(6, 6)); // 4654
      clahe->apply(img, img);

      // Rauschunterdrückung
      cv::GaussianBlur(processed, processed, cv::Size(5, 5), 0); // 4654

      //      cv::imwrite("debug_morph.png", processed);

      // 2. Shi-Tomasi Corner Detection
      int pointsToFind = 61 * 61;
      std::vector<cv::Point2f> cornersRaw;
      std::vector<cv::Point2f> corners;

      // Parameter tunen:
      // qualityLevel: 0.01

      //      cv::goodFeaturesToTrack(processed, corners, pointsToFind, 0.01, 130.0, cv::noArray(), 25, true, 0.06); // 4654
      //      cv::goodFeaturesToTrack(processed, corners, pointsToFind, 0.01, 130.0, cv::noArray(), 25, true, 0.06); // 4654
      cv::goodFeaturesToTrack(processed, cornersRaw, 0, 0.01, 130.0, cv::noArray(), 25, true, 0.06); // 4654

      Debug("Gefundene Punkte: {} gesucht {}", cornersRaw.size(), pointsToFind);

      if (cornersRaw.size() < pointsToFind) {
            Critical("not enough points found in image");
            return;
            }

      // Sub-Pixel Verfeinerung (Hohe Präzision)
      cv::TermCriteria criteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 40, 0.001);
      // Wir nehmen das unverzerrte Original-Graubild (oder das CLAHE Bild) für SubPix
      cv::cornerSubPix(processed, cornersRaw, cv::Size(60, 60), cv::Size(-1, -1), criteria);

      for (int i = 0; i < pointsToFind; ++i)
            corners.push_back(cornersRaw[i]);

      // Orientierung der Punktewolke finden
      // Wir legen ein "Rotated Rectangle" um alle Punkte. Der Winkel dieses Rechtecks
      // verrät uns die globale Rotation des Gitters.
      //      cv::RotatedRect box = cv::minAreaRect(corners);
      //      angle         = 0.0;    // DEBUG box.angle;

      //      Debug("==================box angle {} box {} {}", angle, box.size.width, box.size.height);

      // OpenCV Winkel-Normalisierung (Box-Winkel kann 90° versetzt sein je nach Version)
      /*      if (box.size.width < box.size.height) {
            Debug("====== rotate ======");
            angle += 90.0f;
                  }
  */
      // Umrechnung in Bogenmaß für die Rotationsmatrix
      double rad  = angle * CV_PI / 180.0;
      double cosA = std::cos(-rad); // Rückrotation zum Sortieren
      double sinA = std::sin(-rad);

      // Punkte temporär "gerade drehen" zum Sortieren
      std::vector<SortedPoint> sortList;
      sortList.reserve(corners.size());

      Debug("========corners {}", corners.size());

      for (int i = 0; i < corners.size(); i++) {
            double rx = corners[i].x * cosA - corners[i].y * sinA;
            double ry = corners[i].x * sinA + corners[i].y * cosA;
            sortList.push_back({corners[i], cv::Point2f(rx, ry), i});
            }

      // Sortieren: Zuerst nach Y (Zeilen), dann nach X (Spalten)
      // Da wir das Bild "gerade" gerechnet haben, liegen die Punkte einer Zeile
      // nun fast auf der gleichen Y-Höhe.

      // A. Grob nach Y sortieren
      std::sort(sortList.begin(), sortList.end(),
                [](const SortedPoint& a, const SortedPoint& b) { return a.rotatedPt.y < b.rotatedPt.y; });

      // B. Zeilenweise nach X sortieren
      cols = 61;
      rows = (int)sortList.size() / cols; // Sollte 61 sein, falls alle gefunden wurden

      // Wir gehen Zeile für Zeile durch und sortieren jeweils den 61er Block nach X
      for (int r = 0; r < rows; r++) {
            int startIdx = r * cols;
            int endIdx   = std::min(startIdx + cols, (int)sortList.size());

            std::sort(
                sortList.begin() + startIdx, sortList.begin() + endIdx,
                [](const SortedPoint& a, const SortedPoint& b) { return a.rotatedPt.x < b.rotatedPt.x; });
            }

      // Die Liste 'sortList' entspricht nun logisch dem Gitter von oben-links nach unten-rechts.

      int centerIdx = 30 * 61 + 30;
      // Sicherheitscheck, falls weniger Punkte gefunden wurden
      if (centerIdx >= sortList.size()) {
            Critical("Zu wenige Punkte für Bestimmung der Mitte! {} <= {}", centerIdx, sortList.size());
            return;
            }

      cv::Point2f origin = sortList[centerIdx].originalPt;

      // Für die Rotation nehmen wir den rechten Nachbarn (32, 64) falls vorhanden
      // Falls der Punkt fehlt, nehmen wir den linken und drehen das Vorzeichen um, etc.

      // Winkel berechnen (diesmal exakt anhand der mittleren Linie)
      double finalAngleRad = rad;
#if 1
      int rightIdx = centerIdx + 1; // Der Punkt rechts daneben (Index 32*65 + 33)
      if (rightIdx < sortList.size()) {
            cv::Point2f pRight = sortList[rightIdx].originalPt;
            // Achtung: Hier nehmen wir den Winkel zwischen Zentrum und rechtem Nachbarn
            finalAngleRad = std::atan2(pRight.y - origin.y, pRight.x - origin.x);
            }
//      else {
//            // Fallback: Nehmen wir den "Globalen" Winkel von minAreaRect
//            finalAngleRad = rad;
//            }
#endif

      double finalCos = std::cos(-finalAngleRad);
      double finalSin = std::sin(-finalAngleRad);

      Debug("=============================angle {}", finalAngleRad);
      // 8. Ergebnisliste aufbauen (Koordinatenursprung Mitte, Rotation entfernt)
      for (int idx = 0; idx < sortList.size(); ++idx) {
            const auto& sp = sortList[idx];
            int y          = (idx / 61) - 30;
            int x          = (idx % 61) - 30;

            double dx = sp.originalPt.x - origin.x;
            double dy = sp.originalPt.y - origin.y;

            double rx = dx * finalCos - dy * finalSin;
            double ry = dx * finalSin + dy * finalCos;

            auto rpos = rasterPos(x, y);
            int irx   = map(rx / DPMM - rpos.x());
            int iry   = map(ry / DPMM - rpos.y());

            setValue(x, y, {irx, iry});
            }
      // interpolating the missing border points
#if 0
      for (int y = -32; y <= 32; ++y) {
            setValue(-31, y, value(-30, y));
            setValue(-32, y, value(-30, y));
            setValue(31, y, value(30, y));
            setValue(32, y, value(30, y));
                  }
      for (int x = -32; x <= 32; ++x) {
            setValue(x, -31, value(x, -30));
            setValue(x, -32, value(x, -30));
            setValue(x, 31, value(x, 30));
            setValue(x, 32, value(x, 30));
                  }
#endif
      }

//---------------------------------------------------------
//   Trans
//---------------------------------------------------------

struct Trans {
      double ox  = 0; // offset
      double oy  = 0;
      double mx  = 1.0; // scale
      double my  = 1.0;
      double k1x = 0.0; // distortion
      double k1y = 0.0;
      double k2x = 0.0;
      double k2y = 0.0;
      double cx  = 0.0; // center offset
      double cy  = 0.0; //
      double rot = 0.0;
      Vec2d map(const Vec2d& v) const {
            //
            // scale + offset
            //
            double x = v.x() * mx + ox;
            double y = v.y() * my + oy;

#if 0
            // rotate
            double rad = rot * std::numbers::pi / 180.0;
            double cos = std::cos(-rad);
            double sin = std::sin(-rad);
            double xx  = x * cos - y * sin;
            double yy  = x * sin + y * cos;

            x = xx;
            y = yy;
#endif
#if 1
            double r2  = x * x + y * y;
            double r4  = r2 * r2;
            x         /= (1.0 + k1x * 0.00001 * r2 + k2x * 0.0000000001 * r4);
            y         /= (1.0 + k1y * 0.00001 * r2 + k2y * 0.0000000001 * r4);
#endif
            return Vec2d(x, y);
            }
      void dump() {
            Info("scale {:.6f} {:.6f} offset {:.2f} {:.2f} distortion {:.6f} {:.6f} {:.6f} {:.6f} rotation "
                 "{:.2f}",
                 mx, my, ox, oy, k1x, k1y, k2x, k2y, rot);
            }
      };

//---------------------------------------------------------
//   computeError
//---------------------------------------------------------

double CalData::error(const Trans& t) const {
      double e = 0;

      for (int y = -32; y <= 32; ++y)
            for (int x = -32; x <= 32; ++x)
                  e += error(t, x, y);
      return e;
      }

//---------------------------------------------------------
//   findMax
//    performs a binary search of variable v from v1 to v2
//    maximizing the return value of function.
//---------------------------------------------------------

static double findMax(double v1, double v2, double minStep, std::function<double(double v)> error) {
      // Sicherstellen, dass a immer der kleinere und b der größere Wert ist
      double a = std::min(v1, v2);
      double b = std::max(v1, v2);

      // Konstante für den Goldenen Schnitt (phi)
      // (sqrt(5) - 1) / 2
      const double phi = 0.618033988749895;

      // Initialisierung der zwei inneren Punkte c und d
      double c = b - phi * (b - a);
      double d = a + phi * (b - a);

      // Werte puffern, um unnötige Funktionsaufrufe zu vermeiden
      double errorC = error(c);
      double errorD = error(d);

      // Schleife läuft, solange das Intervall größer als die minimale Schrittweite ist
      while ((b - a) > minStep) {
            if (errorC < errorD) {
                  // Das Minimum liegt im Bereich [a, d]
                  b      = d;
                  d      = c;
                  errorD = errorC; // Wiederverwendung des berechneten Wertes
                  c      = b - phi * (b - a);
                  errorC = error(c); // Neuer Funktionsaufruf
                  }
            else {
                  // Das Minimum liegt im Bereich [c, b]
                  a      = c;
                  c      = d;
                  errorC = errorD; // Wiederverwendung des berechneten Wertes
                  d      = a + phi * (b - a);
                  errorD = error(d); // Neuer Funktionsaufruf
                  }
            }

      // Rückgabe des Mittelpunkts des verbleibenden Intervalls als beste Näherung
      return (a + b) / 2.0;
      }

//---------------------------------------------------------
//   TryData
//---------------------------------------------------------

struct TryData {
      enum TryParameter { SCALE_X, SCALE_Y, TRANSLATE_X, TRANSLATE_Y, ROTATE, K1_X, K1_Y, K2_X, K2_Y };
      TryParameter type;
      double min;
      double max;
      double minStep;
      } tryData[] = {
         {    TryData::SCALE_X,   0.1,   10,  0.00001}, //scale
         {    TryData::SCALE_Y,   0.1,   10,  0.00001}, //scale
         {TryData::TRANSLATE_X, -2000, 2000,    0.001},
         {TryData::TRANSLATE_Y, -2000, 2000,    0.001},
         {     TryData::ROTATE,   -45,   45,   0.0001},
         {       TryData::K1_X,    -1,    1,   0.0001},
         {       TryData::K1_Y,    -1,    1,   0.0001},
         {       TryData::K2_X,    -1,    1, 0.000001},
         {       TryData::K2_Y,    -1,    1, 0.000001},
      };

//---------------------------------------------------------
//   findMax
//---------------------------------------------------------

double findMax(TryData::TryParameter type, const CalData& points, Trans& t, bool useTable) {
      TryData td;
      if (useTable)
            td = tryData[int(type)];
      else {
            double value;

            switch (type) {
                  case TryData::SCALE_X: value = t.mx; break;
                  case TryData::SCALE_Y: value = t.my; break;
                  case TryData::TRANSLATE_X: value = t.ox; break;
                  case TryData::TRANSLATE_Y: value = t.oy; break;
                  case TryData::ROTATE: value = t.rot; break;
                  case TryData::K1_X: value = t.k1x; break;
                  case TryData::K1_Y: value = t.k1y; break;
                  case TryData::K2_X: value = t.k2x; break;
                  case TryData::K2_Y: value = t.k2y; break;
                  };
            double min     = value * 0.9;
            double max     = value * 1.1;
            double minStep = abs(value * 0.00001);
            td             = TryData(type, min, max, minStep);
            }
      return findMax(td.min, td.max, td.minStep, [points, t, td](double v) {
            Trans tt = t;
            switch (td.type) {
                  case TryData::SCALE_X: tt.mx = v; break;
                  case TryData::SCALE_Y: tt.my = v; break;
                  case TryData::TRANSLATE_X: tt.ox = v; break;
                  case TryData::TRANSLATE_Y: tt.oy = v; break;
                  case TryData::ROTATE: tt.rot = v; break;
                  case TryData::K1_X: tt.k1x = v; break;
                  case TryData::K1_Y: tt.k1y = v; break;
                  case TryData::K2_X: tt.k2x = v; break;
                  case TryData::K2_Y: tt.k2y = v; break;
                  };
            return points.error(tt);
            });
      }

//---------------------------------------------------------
//   findValues
//---------------------------------------------------------

static void findValues(const CalData& points, Trans& t, bool firstrun) {
      t.ox = findMax(TryData::TRANSLATE_X, points, t, firstrun);
      t.oy = findMax(TryData::TRANSLATE_Y, points, t, firstrun);
      t.mx = findMax(TryData::SCALE_X, points, t, firstrun);
      t.my = findMax(TryData::SCALE_Y, points, t, firstrun);

      //      t.rot = findMax(TryData::ROTATE, points, t, firstrun);
      t.k1x = findMax(TryData::K1_X, points, t, firstrun);
      t.k1y = findMax(TryData::K1_Y, points, t, firstrun);
      t.k2x = findMax(TryData::K2_X, points, t, firstrun);
      t.k2y = findMax(TryData::K2_Y, points, t, firstrun);
      }

//---------------------------------------------------------
//   scanImage
//---------------------------------------------------------

void ZCam::galvotest65img(const QString& filename) {
      // 2. Bild laden
      cv::Mat image = cv::imread(filename.toStdString(), cv::IMREAD_GRAYSCALE);

      if (image.empty()) {
            Debug("could not load file: <{}>", filename);
            return;
            }

      Debug("loaded picture: <{}> {} x {}", filename, image.cols, image.rows);

      CalData points(this);
      points.analyzeImage(image);

      auto tl      = project();
      auto cad     = tl->cad();
      auto fixture = tl->fixture();

      Assert(cad);
      Assert(fixture);

      Group* group = new Group(this, cad);
      auto ll      = new Recipe(this, fixture);
      ll->setName("LL-Calibration");
      fixture->addChild(ll);

      Trans t;

#if 1
      double lastError = -1;
      for (int i = 0; i < 32; ++i) {
            findValues(points, t, i == 0);
            double error = points.error(t);
            if (error == lastError) {
                  Debug("minimum error found with {} iterations: {}", i, error);
                  break;
                  }
            Info("{}: error {}", i, error);
            lastError = error;
            }
#endif
      t.dump();
      Polygon* r = new Polygon(this, group);
      r->setColor(QColor("red"));
      double d = .4;

      for (int y = -32; y <= 32; ++y) {
            for (int x = -32; x <= 32; ++x) {
                  auto p    = t.map(points.pos(x, y));
                  double fx = p.x();
                  double fy = p.y();
                  // draw a small square:
                  r->moveTo(Vec2d(fx - d, fy + d));
                  r->lineTo(Vec2d(fx + d, fy + d));
                  r->lineTo(Vec2d(fx + d, fy - d));
                  r->lineTo(Vec2d(fx - d, fy - d));
                  r->lineTo(Vec2d(fx - d, fy + d));
                  }
            }
      group->addChild(r);
      points.write("test.cor");
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

bool CalData::write(std::string fileName) const {
      QString dir = ZCam::defaultArtworkDirectory();
      QDir().mkpath(dir);
      string filePath = QDir(dir).filePath(QString::fromStdString(fileName)).toStdString();

      json j;

      j["version"] = "1.0";
      j["rows"]    = CalData::ROWS;
      j["columns"] = CalData::COLUMNS;
      j["data"]    = data;

      try {
            std::ofstream outFile(filePath);
            if (!outFile.is_open()) {
                  Critical("Could not open file {} for writing", filePath);
                  return false;
                  }
            outFile << j.dump(4);
            outFile.close();
            }
      catch (const std::exception& e) {
            Critical("Exception on write", e.what());
            return false;
            }
      return true;
      }

//---------------------------------------------------------
//   readCorFile
//---------------------------------------------------------

bool CalData::read(std::string fileName) {
#if 0
      string filePath = wcamPath.path().toStdString() + "/resources/" + fileName;
      std::ifstream inFile(filePath);

      if (!inFile.is_open()) {
            Critical("Cannot read {}", filePath);
            return false;
                  }

      try {
            json j;
            inFile >> j; // Liest den gesamten Stream und parst ihn
            if (j.value("rows", 0) != ROWS || j.value("columns", 0) != COLUMNS) {
                  Critical("bad dimensions in file");
                  return false;
                        }
            if (j.contains("data")) {
                  data = j["data"].get<std::array<CalOffset, SIZE>>();
                        }
            else {
                  Critical("no 'data' field in JSON");
                  return false;
                        }

            if (data.size() != ROWS * COLUMNS) {
                  Critical("Data has wrong size {}", data.size());
                  return false;
                        }
                  }
      catch (const json::parse_error& e) {
            Critical("JSON parsing error", e.what());
            return false;
                  }
      catch (const std::exception& e) {
            Critical("General error on read: ", e.what());
            return false;
                  }
#endif
      return true;
      }

//---------------------------------------------------------
//   error
//---------------------------------------------------------

double CalData::error(const Trans& t, int x, int y) const {
      auto diff = t.map(pos(x, y)) - rasterPos(x, y);
      return diff.x() * diff.x() + diff.y() * diff.y();
      }
