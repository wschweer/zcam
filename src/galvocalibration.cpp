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
constexpr double MEASUREMENT_GRID     = 32.0;           // "9 point" cross spans the full field (grid ±32)

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

// Convert mm to grid units (used for offset conversion).
double mmToGrid(double mm, double fieldHalf) {
      return mm * CORRECTION_GRID_HALF / fieldHalf;
      }

// Position on the measured axis after applying the physical lens
// distortion that the correction table compensates.  'bulge' is the
// value stored in the machine and sent to the controller; the
// correction table adds
//     bulge * r² * g
// to the nominal position, so the uncompensated physical error has
// the opposite sign.
double distortedCoord(int gx, int gy, double fieldHalf, double bulge, bool isX) {
      const double r2         = double(gx * gx + gy * gy);
      const double nominal    = (isX ? gx : gy) * CORRECTION_SCALE;
      const double g          = isX ? gx : gy;
      const double distortion = -(bulge * r2) * g;
      return tableToMm(nominal + distortion, fieldHalf);
      }

// Simulated length of one measured line pair.  The current bulge
// coefficient describes the correction written to the table; the
// simulated physical measurement uses the inverse distortion.
// gxPos/gyPos are the grid coordinates of the line (positive endpoint
// along the measured axis);  isX selects which axis the length is
// measured along (horizontal pair -> bulgeX, vertical pair -> bulgeY).
double simulatedLength(int gxPos, int gyPos, double fieldHalf, double bulgeX, double bulgeY, bool isX) {
      if (isX) {
            return distortedCoord( gxPos, gyPos, fieldHalf, bulgeX, true) -
                   distortedCoord(-gxPos, gyPos, fieldHalf, bulgeX, true);
            }
      return distortedCoord(gxPos,  gyPos, fieldHalf, bulgeY, false) -
             distortedCoord(gxPos, -gyPos, fieldHalf, bulgeY, false);
      }

// One measurement sample used for fitting and for RMS evaluation.
struct Sample {
      double value; // averaged measured length (mm)
      int gx;       // grid x coordinate of the positive endpoint
      int gy;       // grid y coordinate of the positive endpoint
      bool isX;     // true -> X-axis measurement
      };

// One line-pair measurement: left/right (or top/bottom) half lengths.
struct PairSample {
      double leftX;    // measured length of the left/top line segment (mm)
      double rightX;   // measured length of the right/bottom line segment (mm)
      int g1;          // grid coordinate on the measured axis of the + endpoint
      int g2;          // grid coordinate on the cross axis
      };

//---------------------------------------------------------
//   estimateOffset
//    Estimate the beam offset (dx, dy) in mm from the
//    left/right asymmetry of the measured line pairs.
//
//    When the beam hits the galvo with an offset dx (in grid
//    units), the radial distortion centre shifts.  For an X-axis
//    pair at cross-coordinate G with half-length h:
//
//      Left  = nominal - bulge*(h²+G²)*h*mmPT
//                    - 3*bulge*h²*dx*mmPT + 2*bulge*h*G*dy*mmPT
//      Right = nominal - bulge*(h²+G²)*h*mmPT
//                    + 3*bulge*h²*dx*mmPT + 2*bulge*h*G*dy*mmPT
//
//    The asymmetry is:
//      (Right - Left) / 2 = 3 * bulge * h² * dx * mmPT
//
//    This is constant across all three X pairs (h = MEASUREMENT_GRID
//    is the same for all), so we average over all three.
//    Similarly for Y pairs:
//      (Bottom - Top) / 2 = 3 * bulge * h² * dy * mmPT
//
//    We can only determine the products bulge*dx and bulge*dy.
//    To get dx, dy in mm we need bulge.  We use the current
//    machine bulge as a first estimate.  If bulge is zero the
//    offset is undefined (and irrelevant).
//
//    Returns the offset in mm.
//---------------------------------------------------------
QVector2D estimateOffset(const PairSample xPairs[3], const PairSample yPairs[3],
                          double fieldHalf, double bulgeX, double bulgeY) {
      const double mmPT = 1.0 / mmToTable(1.0, fieldHalf); // mm per table unit
      const double h    = MEASUREMENT_GRID;

      // Average asymmetry over the three X pairs.
      double asymX = 0.0;
      for (int i = 0; i < 3; ++i)
            asymX += (xPairs[i].rightX - xPairs[i].leftX) * 0.5;
      asymX /= 3.0;

      // Average asymmetry over the three Y pairs.
      double asymY = 0.0;
      for (int i = 0; i < 3; ++i)
            asymY += (yPairs[i].rightX - yPairs[i].leftX) * 0.5;
      asymY /= 3.0;

      // asymX = 3 * bulgeX * h² * dx_grid * mmPT
      // => dx_grid = asymX / (3 * bulgeX * h² * mmPT)
      // => dx_mm  = dx_grid * fieldHalf / CORRECTION_GRID_HALF
      double dxMm = 0.0;
      double dyMm = 0.0;
      const double denom = 3.0 * h * h * mmPT;
      if (std::abs(bulgeX) > 1e-12)
            dxMm = (asymX / (denom * bulgeX)) * fieldHalf / CORRECTION_GRID_HALF;
      if (std::abs(bulgeY) > 1e-12)
            dyMm = (asymY / (denom * bulgeY)) * fieldHalf / CORRECTION_GRID_HALF;

      return QVector2D(dxMm, dyMm);
      }

//---------------------------------------------------------
//   centerMeasurements
//    Correct the measured pair values for the effect of the
//    beam offset so that the residual data is centred and the
//    bulge fit is not corrupted.
//
//    For X-axis pair at cross-coordinate G:
//      avg = (Left + Right) / 2
//          = nominal - bulge*(h²+G²)*h*mmPT - 2*bulge*h*G*dy*mmPT
//    The cross term -2*bulge*h*G*dy*mmPT depends on G and must
//    be subtracted from the average.
//
//    For Y-axis pair at cross-coordinate G:
//      avg = (Top + Bottom) / 2
//          = nominal - bulge*(h²+G²)*h*mmPT - 2*bulge*h*G*dx*mmPT
//    The cross term -2*bulge*h*G*dx*mmPT must be subtracted.
//
//    We also correct the individual left/right values for the
//    asymmetry so that left == right after centering:
//      left  += asym   (asym = (right-left)/2)
//      right -= asym
//    This makes the averaging in fitBulge exact.
//---------------------------------------------------------
void centerMeasurements(PairSample xPairs[3], PairSample yPairs[3],
                         double fieldHalf, double bulgeX, double bulgeY,
                         double dxMm, double dyMm) {
      const double mmPT = 1.0 / mmToTable(1.0, fieldHalf);
      const double h    = MEASUREMENT_GRID;
      const double dxG  = mmToGrid(dxMm, fieldHalf);
      const double dyG  = mmToGrid(dyMm, fieldHalf);

      // X pairs: remove cross term -2*bulgeX*h*G*dy*mmPT from the
      // average, and symmetrise left/right.
      for (int i = 0; i < 3; ++i) {
            const double G      = xPairs[i].g2;
            const double cross  = -2.0 * bulgeX * h * G * dyG * mmPT;
            const double asym   = (xPairs[i].rightX - xPairs[i].leftX) * 0.5;
            xPairs[i].leftX  += asym - cross * 0.5;
            xPairs[i].rightX -= asym - cross * 0.5;
            }

      // Y pairs: remove cross term -2*bulgeY*h*G*dx*mmPT from the
      // average, and symmetrise top/bottom.
      for (int i = 0; i < 3; ++i) {
            const double G      = yPairs[i].g2;
            const double cross  = -2.0 * bulgeY * h * G * dxG * mmPT;
            const double asym   = (yPairs[i].rightX - yPairs[i].leftX) * 0.5;
            yPairs[i].leftX  += asym - cross * 0.5;
            yPairs[i].rightX -= asym - cross * 0.5;
            }
      }

// Fit the r² distortion coefficients (axis-specific) from the six
// averaged line-pair measurements.
//
// Model: the correction table adds
//     corrX = k2x * r² * gx ,  corrY = k2y * r² * gy
// to the nominal position.  The galvo/lens distortion this compensates
// shifts the physical spot by the opposite amount, so a measured target
// error equals +corr.
//
// Per-axis pair measurements constrain k2x resp. k2y through the
// coordinate offsets at their radius.  Each line pair at +/-h measures
// 2*err(h), so the equation per pair is:
//     y = (nominal - avg) * tpm
// where avg = (left+right)/2.
//
// The three measurements per axis span two distinct radii
// (r² = 1024 and 2048), which is sufficient for a single-parameter fit.
bool fitBulge(const PairSample xPairs[3], const PairSample yPairs[3],
              double nominal, double fieldHalf, double& k2xOut, double& k2yOut) {
      k2xOut = 0.0;
      k2yOut = 0.0;

      const double tablePerMm = mmToTable(1.0, fieldHalf);

      // X axis: least-squares fit for k2x from three averaged pairs.
      double sumA = 0.0, sumB = 0.0;
      for (int i = 0; i < 3; ++i) {
            const double avg = (xPairs[i].leftX + xPairs[i].rightX) * 0.5;
            const double r2  = double(xPairs[i].g1 * xPairs[i].g1 + xPairs[i].g2 * xPairs[i].g2);
            const double y   = (nominal - avg) * tablePerMm;
            // equation: y = k2x * r² * MEASUREMENT_GRID
            const double a   = r2 * MEASUREMENT_GRID;
            sumA += a * a;
            sumB += a * y;
            }
      if (std::abs(sumA) < 1.0e-12)
            return false;
      k2xOut = sumB / sumA;

      // Y axis: least-squares fit for k2y from three averaged pairs.
      sumA = 0.0; sumB = 0.0;
      for (int i = 0; i < 3; ++i) {
            const double avg = (yPairs[i].leftX + yPairs[i].rightX) * 0.5;
            const double r2  = double(yPairs[i].g1 * yPairs[i].g1 + yPairs[i].g2 * yPairs[i].g2);
            const double y   = (nominal - avg) * tablePerMm;
            const double a   = r2 * MEASUREMENT_GRID;
            sumA += a * a;
            sumB += a * y;
            }
      if (std::abs(sumA) < 1.0e-12)
            return false;
      k2yOut = sumB / sumA;

      return true;
      }

// RMS residual after applying the correction table model to all six
// averaged pair samples.
double rmsResidual(const Sample pairSamples[6], double fieldHalf, double bulgeX, double bulgeY) {
      double sumSq = 0.0;
      for (int i = 0; i < 6; ++i) {
            const double simulated = simulatedLength(pairSamples[i].gx, pairSamples[i].gy, fieldHalf, bulgeX,
                                                     bulgeY, pairSamples[i].isX);
            const double err       = pairSamples[i].value - simulated;
            sumSq                 += err * err;
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
//    Compute galvo scale, offset and bulge from 12 measured
//    line lengths of the "Galvo Test 9" pattern.
//    galvoBulge4 is set to (0, 0).
//
//    Pipeline:
//      1. Offset: estimated from left/right asymmetry using the
//         current machine bulge as a first approximation.
//      2. Centering: the measured values are corrected for the
//         offset cross-terms so the bulge fit is not corrupted.
//      3. Bulge: least-squares fit per axis from the centred data.
//      4. Scale: from the center pair corrected for the fitted
//         bulge distortion at r² = g² (G=0).
//
//    The laser field is [-fieldHalf, fieldHalf] mm.
//    The "9 point" burn pattern places line pairs at the field
//    edges (grid ±32) and center (grid 0), spanning the full
//    correction table used by LaserBJJCZ::writeCorrectionTable().
//
//    The correction table spans grid coordinates [-32, 32] and stores
//    offsets in units of CORRECTION_SCALE = 0x10000/64 = 1024.
//    For grid coordinate g the nominal table entry is g*1024 and the
//    lens-distortion correction is
//        corr = bulge * r² * g
//    where r² = gx² + gy².  The table entry is an offset that is added
//    to the nominal position, so the coefficients stored in the machine
//    have the opposite sign of the measured physical distortion.
//
//    scale: The centre measurement (xMiddle / yCenter) is corrected
//    for the fitted bulge distortion and then used as the pure linear
//    scale.  galvoScale is stored as a factor: 1.0 = unity.
//
//    offset: The beam offset (dx, dy) in mm is estimated from the
//    left/right (or top/bottom) asymmetry of the line pairs.  This
//    requires knowing the bulge coefficient; we use the current
//    machine bulge as a first approximation.  The offset is applied
//    to centre the measurements before the bulge fit.
//
//    bulge: After centring, the six measured pairs are averaged to
//    cancel translation.  For each axis we fit the r² distortion
//    coefficient from the three averaged pair lengths using the
//    actual r² of each sample and the factor 2 that comes from
//    moving both line ends.
//
//    The values stored in the machine and sent to the controller are the
//    physical distortion coefficients.  The correction table adds
//        corr = +bulge * r² * g
//    to the nominal position, cancelling the physical distortion which
//    shifts the spot by -bulge * r² * g.  No sign inversion is needed.

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

      //--- raw pair measurements (kept separate; averaging happens per equation) ---
      PairSample xPairs[3] = {
            {xTopLeft, xTopRight,       32, 32},
            {xMiddleLeft, xMiddleRight, 32,  0},
            {xBottomLeft, xBottomRight, 32, -32}
            };
      PairSample yPairs[3] = {
            {yLeftTop, yLeftBottom,     -32, 32},
            {yCenterTop, yCenterBottom,   0, 32},
            {yRightTop, yRightBottom,    32, 32}
            };

      //--- offset: estimate from asymmetry using current machine bulge ---
      auto* laser = qobject_cast<Laser*>(machine);
      const double initialBulgeX = laser ? laser->galvoBulge().x() : 0.0;
      const double initialBulgeY = laser ? laser->galvoBulge().y() : 0.0;
      _offset = estimateOffset(xPairs, yPairs, fieldHalf, initialBulgeX, initialBulgeY);

      //--- center measurements for the offset cross-terms ---
      centerMeasurements(xPairs, yPairs, fieldHalf, initialBulgeX, initialBulgeY,
                          _offset.x(), _offset.y());

      //--- bulge: least-squares fit per axis from centred data ---
      double bulgeX, bulgeY;
      if (!fitBulge(xPairs, yPairs, nominal, fieldHalf, bulgeX, bulgeY)) {
            Critical("GalvoCalibration::compute: unable to fit bulge coefficients");
            return false;
            }

      //--- refine offset with the fitted bulge ---
      // Re-estimate offset from the ORIGINAL (uncentred) measurements
      // using the now-known bulge.  This is a second pass that improves
      // accuracy when the initial machine bulge was far off.
      {
      PairSample xPairsOrig[3] = {
            {xTopLeft, xTopRight,       32, 32},
            {xMiddleLeft, xMiddleRight, 32,  0},
            {xBottomLeft, xBottomRight, 32, -32}
            };
      PairSample yPairsOrig[3] = {
            {yLeftTop, yLeftBottom,     -32, 32},
            {yCenterTop, yCenterBottom,   0, 32},
            {yRightTop, yRightBottom,    32, 32}
            };
      _offset = estimateOffset(xPairsOrig, yPairsOrig, fieldHalf, bulgeX, bulgeY);

      // Re-centre and re-fit with the refined offset.
      centerMeasurements(xPairsOrig, yPairsOrig, fieldHalf, bulgeX, bulgeY,
                          _offset.x(), _offset.y());
      if (!fitBulge(xPairsOrig, yPairsOrig, nominal, fieldHalf, bulgeX, bulgeY)) {
            Critical("GalvoCalibration::compute: unable to fit bulge coefficients (pass 2)");
            return false;
            }
      }

      //--- scale: correct center pair for bulge, then compute scale ---
      // The center pair (G=0, g=32) has r² = 32² + 0² = 1024.
      // The measured avg = fieldHalf * scaleFactor - bulge * r² * g * mmPT
      // So: avg_corrected = avg + bulge * r² * g * mmPT
      // And: scale = fieldHalf / avg_corrected
      const double tablePerMm = mmToTable(1.0, fieldHalf);
      const double mmPT       = 1.0 / tablePerMm;
      const double g          = MEASUREMENT_GRID;
      const double r2_center = g * g;  // G=0
      const double bulgeCorrX = bulgeX * r2_center * g * mmPT;
      const double bulgeCorrY = bulgeY * r2_center * g * mmPT;
      const double avgXCenter = (xMiddleLeft + xMiddleRight) * 0.5 + bulgeCorrX;
      const double avgYCenter = (yCenterTop + yCenterBottom) * 0.5 + bulgeCorrY;
      const double sx          = nominal / avgXCenter;
      const double sy          = nominal / avgYCenter;

      _scale  = QVector2D(sx, sy);
      _bulge  = QVector2D(bulgeX, bulgeY);
      _bulge4 = QVector2D(0.0, 0.0);   // not computed from 9-point pattern

      const Sample allSamples[6] = {
            {(xPairs[0].leftX + xPairs[0].rightX) * 0.5, xPairs[0].g1, xPairs[0].g2, true},
            {(xPairs[1].leftX + xPairs[1].rightX) * 0.5, xPairs[1].g1, xPairs[1].g2, true},
            {(xPairs[2].leftX + xPairs[2].rightX) * 0.5, xPairs[2].g1, xPairs[2].g2, true},
            {(yPairs[0].leftX + yPairs[0].rightX) * 0.5, yPairs[0].g1, yPairs[0].g2, false},
            {(yPairs[1].leftX + yPairs[1].rightX) * 0.5, yPairs[1].g1, yPairs[1].g2, false},
            {(yPairs[2].leftX + yPairs[2].rightX) * 0.5, yPairs[2].g1, yPairs[2].g2, false}
            };
      _rmsError = rmsResidual(allSamples, fieldHalf, bulgeX, bulgeY);

      _valid = true;
      Info("GalvoCalibration: scale=({:.3f}%,{:.3f}%) offset=({:.4f},{:.4f})mm bulge=({:.6e},{:.6e}) rms={:.4f} mm",
           _scale.x(), _scale.y(), _offset.x(), _offset.y(), bulgeX, bulgeY, _rmsError);
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
      j["version"]       = 2;
      j["type"]          = "GalvoCalibration9";
      j["xTopLeft"]      = xTopLeft;
      j["xTopRight"]     = xTopRight;
      j["xMiddleLeft"]   = xMiddleLeft;
      j["xMiddleRight"]  = xMiddleRight;
      j["xBottomLeft"]   = xBottomLeft;
      j["xBottomRight"]  = xBottomRight;
      j["yLeftTop"]      = yLeftTop;
      j["yLeftBottom"]   = yLeftBottom;
      j["yCenterTop"]    = yCenterTop;
      j["yCenterBottom"] = yCenterBottom;
      j["yRightTop"]     = yRightTop;
      j["yRightBottom"]  = yRightBottom;

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
//    Version 1 files (with diagonals) are loaded by ignoring
//    the diagonal keys; the 12 line-pair values are returned.
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
                  Warning("GalvoCalibration::loadParameters: unknown file type in {}",
                          filePath.toStdString());
                  return rv;
                  }
            auto get            = [&](const char* name) { return j.value(name, 0.0); };
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
            Warning("GalvoCalibration::loadParameters: parse error in {}: {}",
                    filePath.toStdString(), e.what());
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
      _offset   = QVector2D(0.0, 0.0);
      _bulge4   = QVector2D(0.0, 0.0);
      _rmsError = 0.0;
      nominal   = 0.0;
      emit resultsChanged();
      }

//---------------------------------------------------------
//   applyToMachine
//    Write the computed galvoScale, galvoOffset and galvoBulge
//    values to the machine and persist the machine configuration.
//    galvoBulge4 is set to (0, 0).
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
      laser->set_galvoOffset(_offset);
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