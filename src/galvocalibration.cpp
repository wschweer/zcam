//=============================================================================
//  ZCam - manufactoring tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "galvocalibration.h"
#include "machine.h"
#include "laser.h"
#include "zcam.h"
#include "logger.h"
#include "machines.h"

#include <nlohmann/json.hpp>

#include <QFile>
#include <QTextStream>
#include <QVariantMap>
#include <cmath>

using json = nlohmann::json;
namespace {
// Correction table geometry used by LaserBJJCZ::writeCorrectionTable().
// The table spans grid coordinates [-32, 32] in both axes.
// One grid step corresponds to scale = 0x10000 / 64 = 1024 table units.
constexpr double CORRECTION_GRID_HALF = 32.0;
constexpr double CORRECTION_SCALE     = 65536.0 / 64.0; // = 1024 table units / grid unit
constexpr double MEASUREMENT_GRID     = 16.0;           // "9 point" cross is at +/- half field
// Convert a physical offset in mm to correction-table units.
// fieldHalf mm maps to CORRECTION_GRID_HALF grid units, i.e.
// CORRECTION_GRID_HALF * CORRECTION_SCALE table units.
double mmToTable(double mm, double fieldHalf) {
      return mm * (CORRECTION_GRID_HALF * CORRECTION_SCALE) / fieldHalf;
      }

// Convert correction-table units back to mm.
double tableToMm(double table, double fieldHalf) {
      return table * fieldHalf / (CORRECTION_GRID_HALF * CORRECTION_SCALE);
      }

// Position on the measured axis after applying the correction table.
// For an X measurement we return the corrected x coordinate (mm),
// for a Y measurement the corrected y coordinate.  The coordinate on
// the cross axis does not influence the measured length.
double correctedCoord(int gx, int gy, double fieldHalf, double bulge, bool isX) {
      const double r2         = double(gx * gx + gy * gy);
      const double nominal    = (isX ? gx : gy) * CORRECTION_SCALE;
      const double correction = bulge * r2 * (isX ? gx : gy);
      return tableToMm(nominal + correction, fieldHalf);
      }

// Simulated length of one measured line pair after applying the
// correction table model.  For an X measurement the line runs from
// (-16, gy) to (+16, gy); for a Y measurement from (gx, -16) to
// (gx, +16).
double simulatedLength(int crossGrid, bool isX, double fieldHalf, double bulgeX, double bulgeY) {
      if (isX) {
            return correctedCoord(16, crossGrid, fieldHalf, bulgeX, true) -
                   correctedCoord(-16, crossGrid, fieldHalf, bulgeX, true);
            }
      return correctedCoord(crossGrid, 16, fieldHalf, bulgeY, false) -
             correctedCoord(crossGrid, -16, fieldHalf, bulgeY, false);
      }

// One measurement sample used for fitting and for RMS evaluation.
struct Sample {
      double value;  // averaged measured length (mm)
      int crossGrid; // grid coordinate on the cross axis
      bool isX;      // true -> X-axis measurement
      };

// Fit a single bulge coefficient from the three samples of one axis.
// The measured length error equals
//     2 * bulge * r² * h          (h = 16, r² = h² + crossGrid²)
// because the correction moves both ends of the line by the same
// amount in opposite directions.  We use a least-squares fit over
// the three samples with their actual r² values.
double fitBulge(const Sample samples[3], double nominal, double fieldHalf) {
      double num = 0.0;
      double den = 0.0;
      for (int i = 0; i < 3; ++i) {
            const double r2 =
                MEASUREMENT_GRID * MEASUREMENT_GRID + double(samples[i].crossGrid * samples[i].crossGrid);
            const double errTable  = (samples[i].value - nominal) * mmToTable(1.0, fieldHalf);
            const double factor    = 2.0 * MEASUREMENT_GRID * r2;
            num                   += errTable * factor;
            den                   += factor * factor;
            }
      return den == 0.0 ? 0.0 : num / den;
      }

// RMS residual after applying the correction table model to all six
// averaged samples.
double rmsResidual(const Sample samples[6], double fieldHalf, double bulgeX, double bulgeY) {
      double sumSq = 0.0;
      for (int i = 0; i < 6; ++i) {
            const double simulated =
                simulatedLength(samples[i].crossGrid, samples[i].isX, fieldHalf, bulgeX, bulgeY);
            const double err  = samples[i].value - simulated;
            sumSq            += err * err;
            }
      return std::sqrt(sumSq / 6.0);
      }

      } //namespace

//---------------------------------------------------------
//   GalvoCalibration
//---------------------------------------------------------
GalvoCalibration::GalvoCalibration(ZCam* zc, QObject* parent) : QObject(parent), zcam(zc) {
      }

//---------------------------------------------------------
//   compute
//    Compute galvoScale and galvoBulge from 12 measured
//    line lengths of the "Galvo Test 9" pattern.
//
//    The laser field is [-fieldHalf, fieldHalf] mm.
//    The "9 point" burn pattern places line pairs at +/- fieldHalf/2,
//    which corresponds to grid coordinates +/- 16 inside the correction
//    table used by LaserBJJCZ::writeCorrectionTable().
//
//    The correction table spans grid coordinates [-32, 32] and stores
//    offsets in units of CORRECTION_SCALE = 0x10000/64 = 1024.
//    For grid coordinate g the nominal table entry is g*1024 and the
//    lens-distortion correction is
//        corr = bulge * r² * g
//    where r² = gx² + gy².
//
//    scale: The centre measurement (xMiddle / yCenter) is least affected
//    by distortion and is used as the pure linear scale.
//    galvoScale is stored in percent: 100 = factor 1.0.
//
//    bulge: The six measured pairs are averaged to cancel translation.
//    For each axis we fit a single bulge coefficient to the three averaged
//    lengths using the actual r² of each sample and the factor 2 that
//    comes from moving both line ends.
//---------------------------------------------------------
bool GalvoCalibration::compute(Machine* machine, double xTopLeft, double xTopRight, double xMiddleLeft,
                               double xMiddleRight, double xBottomLeft, double xBottomRight, double yLeftTop,
                               double yLeftBottom, double yCenterTop, double yCenterBottom, double yRightTop,
                               double yRightBottom) {
      if (!machine) {
            Critical("GalvoCalibration::compute: no machine");
            return false;
            }

      const double fieldHalf = machine->maxTravel().x() * 0.5;
      if (fieldHalf <= 0.0) {
            Critical("GalvoCalibration::compute: invalid field size {}", fieldHalf);
            return false;
            }
      nominal = fieldHalf;

      //--- average each pair (cancels translation offset) ---
      const Sample xSamples[3] = {
               {      (xTopLeft + xTopRight) * 0.5,  16, true},
               {(xMiddleLeft + xMiddleRight) * 0.5,   0, true},
               {(xBottomLeft + xBottomRight) * 0.5, -16, true}
            };
      const Sample ySamples[3] = {
               {    (yLeftTop + yLeftBottom) * 0.5, -16, false},
               {(yCenterTop + yCenterBottom) * 0.5,   0, false},
               {  (yRightTop + yRightBottom) * 0.5,  16, false}
            };

      //--- scale: use center measurement only ---
      const double sx = nominal / xSamples[1].value;
      const double sy = nominal / ySamples[1].value;

      //--- bulge: least-squares fit per axis ---
      const double bulgeX = fitBulge(xSamples, nominal, fieldHalf);
      const double bulgeY = fitBulge(ySamples, nominal, fieldHalf);

      _scale = QVector2D(sx * 100.0, sy * 100.0);
      _bulge = QVector2D(bulgeX, bulgeY);

      //--- RMS error after correction (simulated correction table) ---
      const Sample allSamples[6] = {xSamples[0], xSamples[1], xSamples[2],
                                    ySamples[0], ySamples[1], ySamples[2]};
      _rmsError                  = rmsResidual(allSamples, fieldHalf, bulgeX, bulgeY);

      _valid = true;
      Info("GalvoCalibration: scale=({:.3f}%,{:.3f}%) bulge=({:.6e},{:.6e}) rms={:.4f} mm", _scale.x(),
           _scale.y(), bulgeX, bulgeY, _rmsError);
      emit resultsChanged();
      return true;
      }

//---------------------------------------------------------
//   saveParameters
//    Persist the 12 raw measurement values (mm) as JSON.
//---------------------------------------------------------
bool GalvoCalibration::saveParameters(const QString& filePath, double xTopLeft, double xTopRight,
                                      double xMiddleLeft, double xMiddleRight, double xBottomLeft,
                                      double xBottomRight, double yLeftTop, double yLeftBottom,
                                      double yCenterTop, double yCenterBottom, double yRightTop,
                                      double yRightBottom) {
      json j;
      j["version"]            = 1;
      j["type"]               = "GalvoCalibration9";
      j["xTopLeft"]           = xTopLeft;
      j["xTopRight"]          = xTopRight;
      j["xMiddleLeft"]        = xMiddleLeft;
      j["xMiddleRight"]       = xMiddleRight;
      j["xBottomLeft"]        = xBottomLeft;
      j["xBottomRight"]       = xBottomRight;
      j["yLeftTop"]           = yLeftTop;
      j["yLeftBottom"]        = yLeftBottom;
      j["yCenterTop"]         = yCenterTop;
      j["yCenterBottom"]      = yCenterBottom;
      j["yRightTop"]          = yRightTop;
      j["yRightBottom"]       = yRightBottom;

      QFile file(filePath);
      if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            Warning("GalvoCalibration::saveParameters: cannot open {} for writing", filePath.toStdString());
            return false;
            }
      QTextStream out(&file);
      out << QString::fromStdString(j.dump(2));
      Info("GalvoCalibration: saved parameters to '{}'", filePath.toStdString());
      return true;
      }

//---------------------------------------------------------
//   loadParameters
//    Read a JSON parameter set and return the 12 values.
//---------------------------------------------------------
QVariantMap GalvoCalibration::loadParameters(const QString& filePath) {
      QVariantMap rv;
      QFile file(filePath);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            Warning("GalvoCalibration::loadParameters: cannot open {}", filePath.toStdString());
            return rv;
            }
      const QByteArray data = file.readAll();
      try {
            json j = json::parse(data.constData(), data.constData() + data.size());
            if (!j.contains("type") || j["type"] != "GalvoCalibration9") {
                  Warning("GalvoCalibration::loadParameters: unknown file type in {}", filePath.toStdString());
                  return rv;
                  }
            auto get = [&](const char* name) {
                  return j.value(name, 0.0);
                  };
            rv["xTopLeft"]      = get("xTopLeft");
            rv["xTopRight"]     = get("xTopRight");
            rv["xMiddleLeft"]   = get("xMiddleLeft");
            rv["xMiddleRight"]  = get("xMiddleRight");
            rv["xBottomLeft"]   = get("xBottomLeft");
            rv["xBottomRight"]  = get("xBottomRight");
            rv["yLeftTop"]      = get("yLeftTop");
            rv["yLeftBottom"]   = get("yLeftBottom");
            rv["yCenterTop"]    = get("yCenterTop");
            rv["yCenterBottom"] = get("yCenterBottom");
            rv["yRightTop"]     = get("yRightTop");
            rv["yRightBottom"]  = get("yRightBottom");
            Info("GalvoCalibration: loaded parameters from '{}'", filePath.toStdString());
            }
      catch (const std::exception& e) {
            Warning("GalvoCalibration::loadParameters: parse error in {}: {}", filePath.toStdString(), e.what());
            rv.clear();
            }
      return rv;
      }

//---------------------------------------------------------
//   clear
//    Reset computed results to invalid state so the QML
//    dialog shows placeholders instead of stale values.
//---------------------------------------------------------
void GalvoCalibration::clear() {
      _valid    = false;
      _scale    = QVector2D(1.0, 1.0);
      _bulge    = QVector2D(0.0, 0.0);
      _rmsError = 0.0;
      nominal   = 0.0;
      emit resultsChanged();
      }

//---------------------------------------------------------
//   applyToMachine
//    Write the computed galvoScale and galvoBulge values
//    to the machine and persist the machine configuration.
//---------------------------------------------------------
bool GalvoCalibration::applyToMachine(Machine* machine) {
      if (!machine || !_valid) {
            Warning("GalvoCalibration::applyToMachine: no valid results");
            return false;
            }
      auto* laser = qobject_cast<Laser*>(machine);
      if (!laser) {
            Warning("GalvoCalibration::applyToMachine: machine is not a laser");
            return false;
            }

      laser->set_galvoScale(_scale);
      laser->set_galvoBulge(_bulge);

      // Persist all assets (config, machines, recipes) — also gives
      // user feedback in the status bar via the assetsSaved signal.
      if (zcam) {
            zcam->saveAssets();
            Info("GalvoCalibration: applied to '{}' and saved", laser->name());
            }
      return true;
      }
