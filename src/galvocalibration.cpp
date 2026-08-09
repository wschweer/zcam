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
constexpr double CORNER_GRID          = 32.0;           // diagonals end at the outer corners (+/- 32)
// The fourth-order radial term is scaled by Laser::bulge4Scale so the
// stored galvoBulge4 values stay in a comfortable numerical range.
// The same factor is used in the calibration fit and in the controller
// correction table written by LaserBJJCZ::writeCorrectionTable().
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
//     (bulge*r² + bulge4*Laser::bulge4Scale*r⁴) * g
// to the nominal position, so the uncompensated physical error has
// the opposite sign.
double distortedCoord(int gx, int gy, double fieldHalf, double bulge, double bulge4, bool isX) {
      const double r2         = double(gx * gx + gy * gy);
      const double r4         = r2 * r2;
      const double nominal    = (isX ? gx : gy) * CORRECTION_SCALE;
      const double g          = isX ? gx : gy;
      const double distortion = -(bulge * r2 + bulge4 * r4 * Laser::bulge4Scale) * g;
      return tableToMm(nominal + distortion, fieldHalf);
      }

// Simulated length of one measured line pair.  The current bulge
// coefficients describe the correction written to the table; the
// simulated physical measurement uses the inverse distortion.
// gxPos/gyPos are the grid coordinates of the line (positive endpoint
// along the measured axis);  isX selects which axis the length is
// measured along (horizontal pair -> bulgeX, vertical pair -> bulgeY).
double simulatedLength(int gxPos, int gyPos, double fieldHalf, double bulgeX, double bulge4X, double bulgeY,
                        double bulge4Y, bool isX) {
      if (isX) {
            return distortedCoord( gxPos, gyPos, fieldHalf, bulgeX, bulge4X, true) -
                   distortedCoord(-gxPos, gyPos, fieldHalf, bulgeX, bulge4X, true);
            }
      return distortedCoord(gxPos,  gyPos, fieldHalf, bulgeY, bulge4Y, false) -
             distortedCoord(gxPos, -gyPos, fieldHalf, bulgeY, bulge4Y, false);
      }

// Simulated length of one measured diagonal from the centre (0,0) to an outer
// corner (gx, gy).  The physical corner position is computed with the same
// distortion model used for the horizontal/vertical line pairs.
double simulatedDiagonalLength(int gx, int gy, double fieldHalf, double bulgeX, double bulge4X, double bulgeY,
                                 double bulge4Y) {
      const double x = distortedCoord(gx, gy, fieldHalf, bulgeX, bulge4X, true);
      const double y = distortedCoord(gx, gy, fieldHalf, bulgeY, bulge4Y, false);
      return std::sqrt(x * x + y * y);
      }

// One measurement sample used for fitting and for RMS evaluation.
struct Sample {
      double value; // averaged measured length (mm)
      int gx;       // grid x coordinate of the positive endpoint
      int gy;       // grid y coordinate of the positive endpoint
      bool isX;     // true -> X-axis measurement
      };

// Fit the correction-table coefficients (axis-specific r², shared r⁴).
//
// Model: the correction table adds
//     corrX = (k2x*r² + k4*s*r⁴) * gx ,  corrY = (k2y*r² + k4*s*r⁴) * gy
// to the nominal position (LaserBJJCZ::writeCorrectionTable()).  The
// galvo/lens distortion this compensates shifts the physical spot by the
// opposite amount, so a measured target error equals +corr.
//
// Per-axis pair measurements constrain (k2x, k4) resp. (k2y, k4) through
// the coordinate offsets at their radius.  A diagonal from the centre to a
// corner measures only the *length*; with per-axis coefficients this would
// make the 4-parameter (k2x, k2y, k4x, k4y) system singular, so the r⁴ term
// is modelled as a single shared radial coefficient k4 — physically the r⁴
// error is dominated by the f-theta lens, which is rotationally symmetric.
//
// All equations use err(h) units (h = MEASUREMENT_GRID = 16) with the basis
// [r²/R_MAX, (r²/R_MAX)²], R_MAX = corner r² = 2048:
//   - line pair at +/-h (both ends move equally):
//       y = (nominal - avg) * tpm * 0.5          [avg = (left+right)/2]
//   - diagonal centre->corner: magnitude of the single corner point at g=32.
//     The length measurement loses the corner quadrant (sqrt); within the
//     calibrated range (corner displacement < nominal, i.e. the corner does
//     not flip across the field centre)
//         |corner| - nominal = -err(32) = -lensErr(32)
//     so the err(16) basis value is -(|corner|-nominal)*tpm*(16/32).
//     The two 'same-sign' corners and the two 'cross' corners are averaged
//     separately so no quadrant is weighted stronger than the others.  Past
//     the flip point the relation degrades gracefully (fit stays finite
//     thanks to the bulge4 clamp) — but such a lens would be unusable in
//     practice anyway, so the approximation is acceptable.
// The single least-squares system (10 equations: 3 pairs per axis + 2
// averaged diagonal equations, 3 unknowns) is well conditioned because the
// measurements span three distinct radii.
struct PairSample {
      double leftX;    // measured length of the left/top line segment (mm)
      double rightX;   // measured length of the right/bottom line segment (mm)
      int g1;          // grid coordinate on the measured axis of the + endpoint
      int g2;          // grid coordinate on the cross axis
      };

struct DiagSample {
      double len;      // measured diagonal length centre -> corner (mm)
      int gxCorner;    // grid x coordinate of the corner (+/- 32)
      int gyCorner;    // grid y coordinate of the corner (+/- 32)
      };

// Solve the 3x3 normal system by Gaussian elimination with partial pivoting.
bool solve3(double m[3][3], double b[3], double x[3]) {
      for (int col = 0; col < 3; ++col) {
            int piv = col;
            for (int r = col + 1; r < 3; ++r)
                  if (std::abs(m[r][col]) > std::abs(m[piv][col]))
                        piv = r;
            if (std::abs(m[piv][col]) < 1.0e-12)
                  return false;
            if (piv != col) {
                  for (int k = col; k < 3; ++k)
                        std::swap(m[piv][k], m[col][k]);
                  std::swap(b[piv], b[col]);
                  }
            for (int r = col + 1; r < 3; ++r) {
                  const double f = m[r][col] / m[col][col];
                  for (int k = col; k < 3; ++k)
                        m[r][k] -= f * m[col][k];
                  b[r] -= f * b[col];
                  }
            }
      for (int r = 2; r >= 0; --r) {
            double s = b[r];
            for (int k = r + 1; k < 3; ++k)
                  s -= m[r][k] * x[k];
            x[r] = s / m[r][r];
            }
      return true;
      }

// Fit k2x, k2y (axis-specific r²) and shared k4 (radial r⁴) in one
// least-squares system over all measurements of both axes (10 equations,
// 3 unknowns).  Returns bulge as (k2x, k2y) and the shared k4 in bulge4.
bool fitCalibration(const PairSample xPairs[3], const PairSample yPairs[3], const DiagSample diagonals[4],
                    double nominal, double fieldHalf, double& k2xOut, double& k2yOut, double& k4Out) {
      k2xOut = 0.0;
      k2yOut = 0.0;
      k4Out  = 0.0;

      constexpr double R_MAX = 2.0 * CORNER_GRID * CORNER_GRID;   // 2048
      const double tablePerMm = mmToTable(1.0, fieldHalf);
      // normal matrix accumulation: columns [r² term x, r² term y, r⁴ term]
      double n_[3][3] = {};
      double ns_[3]   = {};
      auto accumulate = [&](double ax, double ay, double a4, double y) {
            const double m[3] = {ax, ay, a4};
            for (int r = 0; r < 3; ++r) {
                  ns_[r] += m[r] * y;
                  for (int c = 0; c < 3; ++c)
                        n_[r][c] += m[r] * m[c];
                  }
            };

      // line pairs: y = (nominal - avg)*tpm*0.5 = err(h)
      for (int i = 0; i < 3; ++i) {
            const double avg = (xPairs[i].leftX + xPairs[i].rightX) * 0.5;
            const double r2  = double(xPairs[i].g1 * xPairs[i].g1 + xPairs[i].g2 * xPairs[i].g2);
            const double rr  = r2 / R_MAX;
            accumulate(rr, 0.0, rr * rr, (nominal - avg) * tablePerMm * 0.5);
            }
      for (int i = 0; i < 3; ++i) {
            const double avg = (yPairs[i].leftX + yPairs[i].rightX) * 0.5;
            const double r2  = double(yPairs[i].g1 * yPairs[i].g1 + yPairs[i].g2 * yPairs[i].g2);
            const double rr  = r2 / R_MAX;
            accumulate(0.0, rr, rr * rr, (nominal - avg) * tablePerMm * 0.5);
            }

      // Diagonals: the measured centre->corner length only yields the corner
      // magnitude  len/sqrt(2) = sqrt(px²+py²)/sqrt(2),  where (px, py) is the
      // physical corner position.  The pair equations measure single
      // coordinates (at g = h = 16), the diagonal the corner point (g = 32).
      // A line pair at +/-h measures 2*err(h), a diagonal 1*err(32); in the
      // err(16) basis this gives (|corner| - nominal)*tpm*(16/32), negated
      // because the corner displacement points opposite to the pair error.
      const double diagScale = MEASUREMENT_GRID / CORNER_GRID;
      const double w         = 0.5;                                  // per-axis weight
      const double invSqrt2  = 0.7071067811865475244;
      double diagErrSame  = 0.0;
      double diagErrCross = 0.0;
      for (int i = 0; i < 4; ++i) {
            const double coord = diagonals[i].len * invSqrt2;   // |corner| in mm
            const double e     = coord - nominal;
            if (diagonals[i].gxCorner == diagonals[i].gyCorner)
                  diagErrSame += e;
            else
                  diagErrCross += e;
            }
      const double yS = -(diagErrSame * 0.5) * tablePerMm * diagScale;
      const double yC = -(diagErrCross * 0.5) * tablePerMm * diagScale;
      accumulate(w, w, 1.0, yS);
      accumulate(w, w, 1.0, yC);

      double x[3];
      double m[3][3];
      for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                  m[r][c] = n_[r][c];
      double b[3] = {ns_[0], ns_[1], ns_[2]};
      if (!solve3(m, b, x))
            return false;
      // coefficients of err(h) basis: err(h) = x0*rr + x1*rr (axis) + x2*rr² (shared r⁴)
      k2xOut = x[0] / (MEASUREMENT_GRID * R_MAX);
      k2yOut = x[1] / (MEASUREMENT_GRID * R_MAX);
      k4Out  = x[2] / (MEASUREMENT_GRID * Laser::bulge4Scale * R_MAX * R_MAX);
      constexpr double maxBulge4 = 5.0;
      k4Out  = std::clamp(k4Out, -maxBulge4, maxBulge4);
      return true;
      }

// RMS residual after applying the correction table model to all six
// averaged pair samples and the four raw diagonal measurements.
double rmsResidual(const Sample pairSamples[6], const double diagonals[4], double fieldHalf, double bulgeX,
                   double bulge4X, double bulgeY, double bulge4Y) {
      double sumSq = 0.0;
      for (int i = 0; i < 6; ++i) {
            const double simulated = simulatedLength(pairSamples[i].gx, pairSamples[i].gy, fieldHalf, bulgeX, bulge4X,
                                                     bulgeY, bulge4Y, pairSamples[i].isX);
            const double err       = pairSamples[i].value - simulated;
            sumSq                 += err * err;
            }
      // The four diagonals end at the four outer corners in grid coordinates.
      constexpr int cornerGx[4] = {32, -32, -32, 32};
      constexpr int cornerGy[4] = {32, 32, -32, -32};
      for (int i = 0; i < 4; ++i) {
            const double simulated = simulatedDiagonalLength(cornerGx[i], cornerGy[i], fieldHalf, bulgeX, bulge4X,
                                                             bulgeY, bulge4Y);
            const double err       = diagonals[i] - simulated;
            sumSq                 += err * err;
            }
      return std::sqrt(sumSq / 10.0);
      }

      } //namespace

//---------------------------------------------------------
//   GalvoCalibration
//---------------------------------------------------------
GalvoCalibration::GalvoCalibration(ZCam* zc, QObject* parent) : QObject(parent), zcam(zc) {
      }

//---------------------------------------------------------
//   compute
//    Compute galvo scale, bulge and bulge4 from 12 measured line lengths
//    plus 4 diagonal lengths of the "Galvo Test 9" pattern.
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
//        corr = (bulge * r² + bulge4 * Laser::bulge4Scale * r⁴) * g
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
//    bulge: The six measured pairs are averaged to cancel
//    translation.  For each axis we fit the r² distortion coefficient
//    from the three averaged pair lengths using the actual r² of each
//    sample and the factor 2 that comes from moving both line ends.
//
//    bulge/bulge4: A single least-squares fit over all measurements of
//    both axes estimates the axis-specific r² coefficients (k2x, k2y) and a
//    shared radial r⁴ coefficient.  Each axis contributes three line pairs
//    (endpoint radii r² = 256 and 512) and the four corner diagonals
//    contribute two symmetric equations at r² = 2048.  The three distinct
//    radii decouple the r² and r⁴ terms (with pairs alone, r⁴ was exactly
//    2.048 * r² and the fit was ill conditioned).  The r⁴ term removes the
//    S-shaped (mustache) distortion visible along the diagonals of the
//    field.  It is modelled as a shared, rotationally-symmetric coefficient
//    because the diagonal only measures corner length; fitting independent
//    k4x/k4y from it would be singular.  The correction-table entry is an
//    offset added to the nominal position, so the computed coefficients are
//    stored with the sign convention of LaserBJJCZ::writeCorrectionTable().
//
//    The values stored in the machine and sent to the controller are the
//    negatives of the physical distortion coefficients.

//---------------------------------------------------------

bool GalvoCalibration::compute(Machine* machine, double xTopLeft, double xTopRight, double xMiddleLeft,
                                double xMiddleRight, double xBottomLeft, double xBottomRight, double yLeftTop,
                                double yLeftBottom, double yCenterTop, double yCenterBottom, double yRightTop,
                                double yRightBottom, double dTopLeft, double dTopRight, double dBottomLeft,
                                double dBottomRight) {
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
      const PairSample xPairs[3] = {
            {xTopLeft, xTopRight,       16, 16},
            {xMiddleLeft, xMiddleRight, 16,  0},
            {xBottomLeft, xBottomRight, 16, -16}
            };
      const PairSample yPairs[3] = {
            {yLeftTop, yLeftBottom,     -16, 16},
            {yCenterTop, yCenterBottom,   0, 16},
            {yRightTop, yRightBottom,    16, 16}
            };
      // diagonals run from the centre (0,0) to the four outer corners (+/- 32, +/- 32)
      const DiagSample diagonalsIn[4] = {
            {dTopLeft, -32, -32},
            {dTopRight, 32, -32},
            {dBottomLeft, -32, 32},
            {dBottomRight, 32, 32}
            };

      //--- scale: use center measurement only ---
      const double sx = nominal / ((xPairs[1].leftX + xPairs[1].rightX) * 0.5);
      const double sy = nominal / ((yPairs[1].leftX + yPairs[1].rightX) * 0.5);

      //--- bulge/bulge4: single least-squares fit over both axes ---
      double bulgeX, bulgeY, bulge4;
      if (!fitCalibration(xPairs, yPairs, diagonalsIn, nominal, fieldHalf, bulgeX, bulgeY, bulge4)) {
            Critical("GalvoCalibration::compute: unable to fit bulge coefficients");
            return false;
            }

      _scale  = QVector2D(sx * 100.0, sy * 100.0);
      _bulge  = QVector2D(bulgeX, bulgeY);
      _bulge4 = QVector2D(bulge4, bulge4);   // shared radial r⁴ (model constraint)

      const Sample allSamples[6] = {
            {(xPairs[0].leftX + xPairs[0].rightX) * 0.5, xPairs[0].g1, xPairs[0].g2, true},
            {(xPairs[1].leftX + xPairs[1].rightX) * 0.5, xPairs[1].g1, xPairs[1].g2, true},
            {(xPairs[2].leftX + xPairs[2].rightX) * 0.5, xPairs[2].g1, xPairs[2].g2, true},
            {(yPairs[0].leftX + yPairs[0].rightX) * 0.5, yPairs[0].g1, yPairs[0].g2, false},
            {(yPairs[1].leftX + yPairs[1].rightX) * 0.5, yPairs[1].g1, yPairs[1].g2, false},
            {(yPairs[2].leftX + yPairs[2].rightX) * 0.5, yPairs[2].g1, yPairs[2].g2, false}
            };
      const double diagonalsValues[4] = {dTopLeft, dTopRight, dBottomLeft, dBottomRight};
      _rmsError                       = rmsResidual(allSamples, diagonalsValues, fieldHalf, bulgeX, bulge4, bulgeY,
                                                    bulge4);

      _valid = true;
      Info("GalvoCalibration: scale=({:.3f}%,{:.3f}%) bulge=({:.6e},{:.6e}) bulge4={:.6e} rms={:.4f} mm",
           _scale.x(), _scale.y(), bulgeX, bulgeY, bulge4, _rmsError);
      emit resultsChanged();
      return true;
      }
//---------------------------------------------------------
//   saveParameters
//    Persist the 16 raw measurement values (mm) as JSON.
//---------------------------------------------------------
bool GalvoCalibration::saveParameters(const QString& filePath, double xTopLeft, double xTopRight,
                                       double xMiddleLeft, double xMiddleRight, double xBottomLeft,
                                       double xBottomRight, double yLeftTop, double yLeftBottom,
                                       double yCenterTop, double yCenterBottom, double yRightTop,
                                       double yRightBottom, double dTopLeft, double dTopRight,
                                       double dBottomLeft, double dBottomRight) {
      json j;
      j["version"]       = 1;
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
      j["dTopLeft"]      = dTopLeft;
      j["dTopRight"]     = dTopRight;
      j["dBottomLeft"]   = dBottomLeft;
      j["dBottomRight"]  = dBottomRight;

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
//    Read a JSON parameter set and return the 16 values.
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
            rv["dTopLeft"]      = get("dTopLeft");
            rv["dTopRight"]     = get("dTopRight");
            rv["dBottomLeft"]   = get("dBottomLeft");
            rv["dBottomRight"]  = get("dBottomRight");
            Info("GalvoCalibration: loaded parameters from '{}'", filePath.toStdString());
            }
      catch (const std::exception& e) {
            Warning("GalvoCalibration::loadParameters: parse error in {}: {}", filePath.toStdString(),
                    e.what());
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