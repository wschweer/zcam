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

// Position on the measured axis after applying the physical lens
// distortion that the correction table compensates.  'bulge' and
// 'bulge4' are the values stored in the machine and sent to the
// controller; the correction table adds
//     (bulge*r² + bulge4*r⁴) * g
// to the nominal position, so the uncompensated physical error has
// the opposite sign.
double distortedCoord(int gx, int gy, double fieldHalf, double bulge, double bulge4, bool isX) {
      const double r2          = double(gx * gx + gy * gy);
      const double r4          = r2 * r2;
      const double nominal     = (isX ? gx : gy) * CORRECTION_SCALE;
      const double g           = isX ? gx : gy;
      const double distortion  = -(bulge * r2 + bulge4 * r4) * g;
      return tableToMm(nominal + distortion, fieldHalf);
      }

// Simulated length of one measured line pair.  The current bulge
// coefficients describe the correction written to the table; the
// simulated physical measurement uses the inverse distortion.
double simulatedLength(int crossGrid, bool isX, double fieldHalf, double bulgeX, double bulge4X,
                       double bulgeY, double bulge4Y) {
      if (isX) {
            return distortedCoord(16, crossGrid, fieldHalf, bulgeX, bulge4X, true) -
                   distortedCoord(-16, crossGrid, fieldHalf, bulgeX, bulge4X, true);
            }
      return distortedCoord(crossGrid, 16, fieldHalf, bulgeY, bulge4Y, false) -
             distortedCoord(crossGrid, -16, fieldHalf, bulgeY, bulge4Y, false);
      }

// One measurement sample used for fitting and for RMS evaluation.
struct Sample {
      double value;  // averaged measured length (mm)
      int crossGrid; // grid coordinate on the cross axis
      bool isX;      // true -> X-axis measurement
      };

// Solve a 2x2 linear system by Cramer's rule.
bool solve2x2(double a11, double a12, double a21, double a22, double b1, double b2, double& x1, double& x2) {
      const double det = a11 * a22 - a12 * a21;
      if (std::abs(det) < 1e-18)
            return false;
      x1 = (b1 * a22 - b2 * a12) / det;
      x2 = (a11 * b2 - a21 * b1) / det;
      return true;
      }

// Fit bulge (r²) and bulge4 (r⁴) correction-table coefficients from the
// three samples of one axis.  The measured length error in table units
// equals -2*h*(b2*r² + b4*r⁴) (h = 16).  We return the coefficients that
// must be stored in the machine and written to the controller.
bool fitBulgePair(const Sample samples[3], double nominal, double fieldHalf, double& bulge, double& bulge4) {
      bulge  = 0.0;
      bulge4 = 0.0;
      // Least-squares: for each sample i we have
      //    -err_i / (2*h) = b2 * r2_i + b4 * r4_i
      double s11 = 0.0, s12 = 0.0, s22 = 0.0, sy1 = 0.0, sy2 = 0.0;
      for (int i = 0; i < 3; ++i) {
            const double r2 =
                MEASUREMENT_GRID * MEASUREMENT_GRID + double(samples[i].crossGrid * samples[i].crossGrid);
            const double r4        = r2 * r2;
            const double errTable  = (samples[i].value - nominal) * mmToTable(1.0, fieldHalf);
            const double y         = -errTable / (2.0 * MEASUREMENT_GRID);
            s11                   += r2 * r2;
            s12                   += r2 * r4;
            s22                   += r4 * r4;
            sy1                   += r2 * y;
            sy2                   += r4 * y;
            }
      return solve2x2(s11, s12, s12, s22, sy1, sy2, bulge, bulge4);
      }

// RMS residual after applying the correction table model to all six
// averaged samples.
double rmsResidual(const Sample samples[6], double fieldHalf, double bulgeX, double bulge4X, double bulgeY,
                   double bulge4Y) {
      double sumSq = 0.0;
      for (int i = 0; i < 6; ++i) {
            const double simulated =
                simulatedLength(samples[i].crossGrid, samples[i].isX, fieldHalf, bulgeX, bulge4X, bulgeY, bulge4Y);
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
//        corr = (bulge * r² + bulge4 * r⁴) * g
//    where r² = gx² + gy² and r⁴ = r² * r².  The r⁴ term removes the
//    S-shaped (mustache) distortion visible along the diagonals of the
//    field.  The table entry is an offset that is added to the nominal
//    position, so the coefficients stored in the machine have the
//    opposite sign of the measured physical distortion.
//
//    scale: The centre measurement (xMiddle / yCenter) is least affected
//    by distortion and is used as the pure linear scale.
//    galvoScale is stored in percent: 100 = factor 1.0.
//
//    bulge/bulge4: The six measured pairs are averaged to cancel
//    translation.  For each axis we fit the two distortion coefficients
//    from the three averaged lengths using the actual r²/r⁴ of each
//    sample and the factor 2 that comes from moving both line ends.
//    The values stored in the machine and sent to the controller are the
//    negatives of the physical distortion coefficients.

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

      //--- bulge/bulge4: least-squares fit per axis ---
      double bulgeX, bulge4X, bulgeY, bulge4Y;
      if (!fitBulgePair(xSamples, nominal, fieldHalf, bulgeX, bulge4X) ||
          !fitBulgePair(ySamples, nominal, fieldHalf, bulgeY, bulge4Y)) {
            Critical("GalvoCalibration::compute: unable to fit bulge coefficients");
            return false;
            }

      _scale  = QVector2D(sx * 100.0, sy * 100.0);
      _bulge  = QVector2D(bulgeX, bulgeY);
      _bulge4 = QVector2D(bulge4X, bulge4Y);

      //--- RMS error after correction (simulated correction table) ---
      const Sample allSamples[6] = {xSamples[0], xSamples[1], xSamples[2],
                                    ySamples[0], ySamples[1], ySamples[2]};
      _rmsError                  = rmsResidual(allSamples, fieldHalf, bulgeX, bulge4X, bulgeY, bulge4Y);

      _valid = true;
      Info("GalvoCalibration: scale=({:.3f}%,{:.3f}%) bulge=({:.6e},{:.6e}) bulge4=({:.6e},{:.6e}) rms={:.4f} mm",
           _scale.x(), _scale.y(), bulgeX, bulgeY, bulge4X, bulge4Y, _rmsError);
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
      _bulge4   = QVector2D(0.0, 0.0);
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
      laser->set_galvoBulge4(_bulge4);

      // Persist all assets (config, machines, recipes) — also gives
      // user feedback in the status bar via the assetsSaved signal.
      if (zcam) {
            zcam->saveAssets();
            Info("GalvoCalibration: applied to '{}' and saved", laser->name());
            }
      return true;
      }