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
#include <algorithm>
#include <cmath>

using json = nlohmann::json;
namespace {
// Correction table geometry used by LaserBJJCZ::writeCorrectionTable().
// The table itself is written for integer grid coordinates [-32, 32]
// in both axes; one grid step corresponds to 0x10000 / 64 = 1024 table
// units, so the table covers the full ±32768 galvo count range.
//
// The marking path (LaserBJJCZ::mapToGalvo) deliberately maps the
// machine travel (maxTravel = 2*fieldHalf mm) onto a restricted count
// range of ±GALVO_HALF_RANGE = ±25800 (21% safety margin) instead of
// the full ±32768.  The "9 point" test pattern is burned through that
// path, so the field edge (fieldHalf mm) lands at table-grid
// coordinate MEASUREMENT_GRID = GALVO_HALF_RANGE/1024 = 25.195 and
// the correction table is indexed by that same count/1024 grid.
// All evaluation geometry below uses these marking-path grid
// coordinates so that the fitted coefficients match the correction
// table's indexing.
constexpr double TABLE_GRID_HALF   = 32.0;                        // table grid half-range
constexpr double CORRECTION_SCALE  = 65536.0 / 64.0;              // = 1024 table units / grid unit
constexpr double GALVO_HALF_RANGE  = 25800.0;                     // marking path half-range (counts)
constexpr double MEASUREMENT_GRID  = GALVO_HALF_RANGE / CORRECTION_SCALE; // = 25.1953
static_assert(MEASUREMENT_GRID < TABLE_GRID_HALF);
// Convert a physical offset in mm to correction-table units.
// fieldHalf mm maps to MEASUREMENT_GRID grid units, i.e.
// MEASUREMENT_GRID * CORRECTION_SCALE table units (= GALVO_HALF_RANGE).
double mmToTable(double mm, double fieldHalf) {
      return mm * (MEASUREMENT_GRID * CORRECTION_SCALE) / fieldHalf;
      }

// Convert correction-table units to mm.
double tableToMm(double table, double fieldHalf) {
      return table * fieldHalf / (MEASUREMENT_GRID * CORRECTION_SCALE);
      }

// Convert mm to grid units (used for offset conversion).
double mmToGrid(double mm, double fieldHalf) {
      return mm * MEASUREMENT_GRID / fieldHalf;
      }

// Simulated position (mm) of the spot whose *physical* galvo position
// is grid coordinate (gx, gy), under the machine's physical model
//     phys = A_grid * g  -  K_bulge * r² * g
// where A_grid (mm per grid unit) is the physical gain along the axis
// and K_bulge (mm per grid³) is the physical lens distortion.
// G here is the *cross-axis* grid coordinate (same for all points of
// the measured line).  Used to simulate the measured pair lengths for
// the RMS residual.
double physicalCoord(double gx, double gy, double gain, double bulge, bool isX) {
      const double r2 = gx * gx + gy * gy;
      const double g  = isX ? gx : gy;
      return gain * g - bulge * r2 * g;
      }

// Length of one measured line-pair half: distance from the field
// centre to the endpoint at grid (+h, G) along the measured axis, mm.
double physicalHalf(double gxPos, double gyPos, double gain, double bulge, bool isX) {
      if (isX)
            return physicalCoord(gxPos, gyPos, gain, bulge, true) -
                   physicalCoord(0.0, gyPos, gain, bulge, true);
      return physicalCoord(gxPos, gyPos, gain, bulge, false) -
             physicalCoord(gxPos, 0.0, gain, bulge, false);
      }

// One measurement sample used for RMS evaluation.
struct Sample {
      double value; // averaged measured length (mm)
      double gx;    // grid x coordinate of the positive endpoint
      double gy;    // grid y coordinate of the positive endpoint
      bool isX;     // true -> X-axis measurement
      };

// One line-pair measurement: left/right (or top/bottom) half lengths.
struct PairSample {
      double leftX;  // measured length of the left/top line segment (mm)
      double rightX; // measured length of the right/bottom line segment (mm)
      double g1;     // grid coordinate on the measured axis of the + endpoint
      double g2;     // grid coordinate on the cross axis
      };

//---------------------------------------------------------
//   estimateOffset
//    Estimate the beam offset (dx, dy) in mm from the
//    left/right asymmetry of the measured line pairs.
//
//    With a beam offset (dx, dy) the radial distortion centre is
//    displaced from the origin.  The physical model becomes
//      phys(g, G) = gain*g - K*((g-dx)² + (G-dy)²)*g
//    Expanding the left half  = phys(0,G) - phys(-h,G) and
//    the right half = phys(h,G) - phys(0,G) gives (sympy):
//
//      asym  = (Right - Left) / 2  =  2*bulge*h²*dx            (exact)
//      avg   = (Left + Right) / 2  =  S*h - bulge*h*(h² + G²)
//                                     - bulge*h*(dx² + dy²)
//                                     + 2*bulge*h*G*dy
//    The asymmetry is constant across the three pairs (no G
//    dependence — the G²d term cancels between the two half-lengths),
//    so we average it and solve:
//      dx_grid = avg(asym) / (2 * bulge * h²)
//    Only the product bulge*d is observable, so the estimate uses the
//    current bulge (machine value on the first pass, the freshly
//    fitted bulge on the refinement pass).  If bulge is zero the
//    offset has no measurable effect and is reported as zero.
//
//    Units: the asymmetry term is in grid³ bulge units ==
//    correction-table units, so the measured asymmetry is converted
//    to table units (mmToTable) before dividing by (2*bulge*h²) to
//    get dx in grid units.
//---------------------------------------------------------

QVector2D estimateOffset(const PairSample xPairs[3], const PairSample yPairs[3], double fieldHalf,
    double bulgeX, double bulgeY, double machineBulgeX, double machineBulgeY) {
      const double h = MEASUREMENT_GRID;

      auto axisOffset = [&](const PairSample pairs[3], double bulge) {
            if (std::abs(bulge) < 1e-7)
                  return 0.0;
            double asymSum = 0.0;
            for (int i = 0; i < 3; ++i)
                  asymSum += (pairs[i].rightX - pairs[i].leftX) * 0.5; // mm
            // dx_grid = avg(asym) / (2 * bulge * h²), asym in table units
            return mmToTable(asymSum / 3.0, fieldHalf) / (2.0 * bulge * h * h);
            };

      // asym = 2*bulge*h²*d with bulge == 0 carries no offset
      // information.  To keep the estimator well-behaved when the
      // fitted bulge is small or noise-driven near zero, we combine
      // the pass bulge with a prior from the current machine bulge
      // and clamp the effective magnitude to a floor (the noise floor
      // of the fit is ~1e-7; below ~1e-5 the asymmetry signal is
      // < 0.05 mm and the estimate is dominated by noise anyway).
      auto effBulge = [&](double fitted, double machine) {
            double b = std::abs(fitted) >= 1e-7 ? fitted : (std::abs(machine) >= 1e-7 ? machine : 0.0);
            return b;
            };
      const double ebX = effBulge(bulgeX, machineBulgeX);
      const double ebY = effBulge(bulgeY, machineBulgeY);

      const double dxG = axisOffset(xPairs, ebX);
      const double dyG = axisOffset(yPairs, ebY);

      // Sanity clamp: a beam offset is bounded by the beam diameter
      // on the galvo mirror, i.e. a few mm.  The offset is only
      // observable through asym = 2*bulge*h²*d, so with a small or
      // noise-driven bulge a large offset would be reported for a
      // small measured asymmetry.  Clamp to ±20 mm to suppress those
      // outliers.
      constexpr double MAX_OFFSET_GRID = 6.4; // ~20 mm at 32 grid units = fieldHalf 100 mm; ~0.25 field
      const double dxGs                = std::clamp(dxG, -MAX_OFFSET_GRID, MAX_OFFSET_GRID);
      const double dyGs                = std::clamp(dyG, -MAX_OFFSET_GRID, MAX_OFFSET_GRID);
      return QVector2D(dxGs / MEASUREMENT_GRID * fieldHalf, dyGs / MEASUREMENT_GRID * fieldHalf);
      }

//---------------------------------------------------------
//   centerMeasurements
//    Correct the measured pair values for the effect of the beam
//    offset so that the residual data is centred and the scale/bulge
//    fit is not corrupted.
//
//    With a beam offset (dx, dy) the radial distortion centre is
//    displaced.  The average of a pair at cross coordinate G is,
//    up to O(d³) terms (sympy expansion with offset centre):
//      avg = gain*h - bulge*h*(h² + G²)            (pure model)
//            - bulge*h*(dx² + dy²)                (constant shift)
//            + 2*bulge*h*G*dcross                (cross term)
//    where dcross is the offset on the cross axis (dy for X pairs,
//    dx for Y pairs).  The G-dependent cross term and the constant
//    shift must be removed so the centred data matches the pure
//    (no-offset) model.
//
//    Centering correction (in mm):
//      X pairs: cor = bulge*h*(dx²+dy²) - 2*bulge*h*G*dy
//      Y pairs: cor = bulge*h*(dx²+dy²) - 2*bulge*h*G*dx
//
//    We also symmetrise left/right by removing the asymmetry
//    asym = (right-left)/2 so both halves equal the corrected average:
//      left  += asym + cor   →  left  = avg + cor
//      right -= asym - cor   →  right = avg + cor
//    which makes the averaging in fitAxis() exact.
//
//    X pairs: measured-axis offset dx, cross axis Y (g2 = G).
//    Y pairs: measured-axis offset dy, cross axis X (g1 = G).
//---------------------------------------------------------

void centerMeasurements(PairSample xPairs[3], PairSample yPairs[3], double fieldHalf, double bulgeX,
    double bulgeY, double dxMm, double dyMm) {
      const double h   = MEASUREMENT_GRID;
      const double dxG = mmToGrid(dxMm, fieldHalf); // offset in grid units
      const double dyG = mmToGrid(dyMm, fieldHalf);
      const double tpm = 1.0 / mmToTable(1.0, fieldHalf); // mm per table unit

      for (int i = 0; i < 3; ++i) {
            const double G = xPairs[i].g2; // cross-axis Y coordinate
            // remove the +2*bulgeX*h*G*dy cross term (wrong sign in the
            // avg) and the -bulgeX*h*(dx²+dy²) constant shift
            // (table units → mm via tpm)
            const double cor =
                (-2.0 * bulgeX * h * G * dyG + bulgeX * h * (dxG * dxG + dyG * dyG)) * tpm;
            const double asym  = (xPairs[i].rightX - xPairs[i].leftX) * 0.5;
            xPairs[i].leftX   += asym + cor;
            xPairs[i].rightX  -= asym - cor;
            }

      for (int i = 0; i < 3; ++i) {
            const double G = yPairs[i].g1; // cross-axis X coordinate
            // remove the +2*bulgeY*h*G*dx cross term and the
            // -bulgeY*h*(dx²+dy²) constant shift
            const double cor =
                (-2.0 * bulgeY * h * G * dxG + bulgeY * h * (dxG * dxG + dyG * dyG)) * tpm;
            const double asym  = (yPairs[i].rightX - yPairs[i].leftX) * 0.5;
            yPairs[i].leftX   += asym + cor;
            yPairs[i].rightX  -= asym - cor;
            }
      }

//---------------------------------------------------------
//   fitAxis
//    Fit the machine's physical response for one axis from the three
//    averaged, centred line-pair half-lengths.  The physical model is
//        phys_mm(g, G) = gain * g  -  K * (g² + G²) * g
//    with g the measured-axis grid coordinate and G the cross-axis
//    one.  Each pair i spans the grid 0..h at cross coordinate G_i;
//    its average half-length is  phys(h) - phys(0) = gain*h - K*h*r0_i²
//    with r0_i² = h² + G_i².  gain and K are solved from the 2×2
//    normal equations in recentered form.
//
//    Returns the physical gain A (mm per grid unit at the marked
//    half-length) and the physical r² lens distortion K (mm/grid³).
//    These describe the *measured* machine, not yet the correction.
//---------------------------------------------------------

bool fitAxis(const PairSample pairs[3], double& gainOut, double& physBulgeOut) {
      gainOut       = 0.0;
      physBulgeOut  = 0.0;

      const double h = MEASUREMENT_GRID;
      const double c1 = h; // linear column (mm/grid * grid = mm)

      double y[3], c2[3];
      double c2m = 0.0, ym = 0.0;
      for (int i = 0; i < 3; ++i) {
            const double avg  = (pairs[i].leftX + pairs[i].rightX) * 0.5;
            const double r0   = pairs[i].g1 * pairs[i].g1 + pairs[i].g2 * pairs[i].g2;
            c2[i]             = -h * r0;
            y[i]              = avg;
            c2m              += c2[i];
            ym               += y[i];
            }
      c2m /= 3.0;
      ym  /= 3.0;

      double sdd = 0.0, sdy = 0.0;
      for (int i = 0; i < 3; ++i) {
            const double d2  = c2[i] - c2m;
            sdd             += d2 * d2;
            sdy             += d2 * (y[i] - ym);
            }
      if (std::abs(sdd) < 1.0e-3 * h * h * h * h)
            return false; // all three pairs on the same radius: bulge unobservable
      physBulgeOut = sdy / sdd;
      gainOut      = (ym - physBulgeOut * c2m) / c1;
      return true;
      }

//---------------------------------------------------------
//   solveCorrection
//    Given the machine's measured physical response for one axis
//      phys_mm(g, G) = gain*g - K*(g²+G²)*g        [mm]
//    compute the correction values galvoScale/galvoBulge.
//
//    Correction path (LaserBJJCZ::mapToGalvo + writeCorrectionTable)
//    for a target t_mm on the measured axis at cross grid G:
//      counts = t * galvoScale * GALVO_HALF_RANGE/fieldHalf
//      g_cmd  = counts/CORRECTION_SCALE
//      g_act  = g_cmd + galvoBulge*(g_cmd² + G²)*g_cmd/CORRECTION_SCALE
//      phys   = gain*g_act - K*(g_act² + G²)*g_act
//
//    galvoScale/galvoBulge are solved with a Newton iteration so that
//    the corrected *pair spans* on the measured rows equal 2*fieldHalf:
//      centre row G=0:    spot(fh) - spot(-fh)  == 2*fieldHalf
//      edge row   G=G_e:  spot(fh) - spot(-fh)  == 2*fieldHalf
//    The edge-row cross-axis grid coordinate G_e depends on galvoScale
//    because the field edge maps to grid  s * MEASUREMENT_GRID, not
//    the constant MEASUREMENT_GRID.  Using a fixed G_e would solve
//    the edge-row equation at the wrong grid coordinate and produce a
//    bulge that is too small by up to ~1% for typical scale errors.
//    Because galvoScale changes the cross-axis grid coordinate, the
//    two corrections are coupled and must be solved, not factored.
//    Converges in a few iterations.
//---------------------------------------------------------

bool solveCorrection(double fieldHalf, double gain, double physBulge, double& scaleOut,
    double& bulgeOut) {
      const double h     = MEASUREMENT_GRID;
      const double perMm = GALVO_HALF_RANGE / fieldHalf; // counts per mm at galvoScale == 1

      // Physical position (mm from the field centre) of the galvo
      // point commanded to t_mm on the measured axis, when the cross
      // coordinate is G grid units, under (scale, bulge):
      auto spot = [&](double tMm, double G, double scale, double bulge) {
            const double counts = tMm * scale * perMm;
            const double g      = counts / CORRECTION_SCALE;
            const double gact   = g + bulge * (g * g + G * G) * g / CORRECTION_SCALE;
            return gain * gact - physBulge * (gact * gact + G * G) * gact;
            };

      auto spanAt = [&](double G, double scale, double bulge) {
            return spot(+fieldHalf, G, scale, bulge) - spot(-fieldHalf, G, scale, bulge);
            };

      // Newton solve for (scale, bulge).  The edge-row cross-axis
      // grid coordinate is G_e = scale * h (the field edge maps to
      // grid scale*h, not the constant h).  When the scale changes,
      // G_e changes too, so the partial derivative w.r.t. scale must
      // account for this coupling.
      double scale = 1.0, bulge = 0.0;
      for (int it = 0; it < 100; ++it) {
            const double ge = scale * h; // edge-row cross-axis grid coordinate
            const double e1 = spanAt(0.0, scale, bulge) - 2.0 * fieldHalf;  // centre row
            const double e2 = spanAt(ge, scale, bulge) - 2.0 * fieldHalf;   // edge row
            if (std::abs(e1) < 1e-10 && std::abs(e2) < 1e-10)
                  break;
            const double ds = 1e-6, db = 1e-9;
            // Partial derivatives for the centre row (G=0, no scale coupling):
            const double j11 = (spanAt(0.0, scale + ds, bulge) - spanAt(0.0, scale - ds, bulge)) / (2 * ds);
            const double j12 = (spanAt(0.0, scale, bulge + db) - spanAt(0.0, scale, bulge - db)) / (2 * db);
            // Partial derivatives for the edge row (G_e = scale*h, couples with scale):
            const double j21
                = (spanAt((scale + ds) * h, scale + ds, bulge) - spanAt((scale - ds) * h, scale - ds, bulge))
                  / (2 * ds);
            const double j22 = (spanAt(ge, scale, bulge + db) - spanAt(ge, scale, bulge - db)) / (2 * db);
            const double det = j11 * j22 - j12 * j21;
            if (std::abs(det) < 1e-12)
                  return false;
            scale += (-e1 * j22 + e2 * j12) / det;
            bulge += (-j11 * e2 + j21 * e1) / det;
            }
      scaleOut = std::clamp(scale, 0.5, 2.0);
      bulgeOut = bulge;
      return true;
      }

// RMS residual of the six averaged pair half-lengths against the
// fitted physical machine model (gain, physBulge).
double rmsResidual(const Sample pairSamples[6], double gainX, double gainY, double bulgeX, double bulgeY) {
      double sumSq = 0.0;
      for (int i = 0; i < 6; ++i) {
            const bool isX   = pairSamples[i].isX;
            const double sim = physicalHalf(pairSamples[i].gx, pairSamples[i].gy,
                isX ? gainX : gainY, isX ? bulgeX : bulgeY, isX);
            const double err  = pairSamples[i].value - sim;
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
//    Compute galvo scale, offset and bulge from 12 measured
//    line lengths of the "Galvo Test 9" pattern.
//    galvoBulge4 is set to (0, 0).
//
//    Pipeline (two passes; the second pass uses the freshly fitted
//    bulge to refine the offset estimate):
//      1. Offset: estimated from the left/right asymmetry of the
//         pairs (only observable as bulge*offset, so the current
//         machine bulge is used as the first approximation).
//      2. Centering: the measured values are corrected for the
//         offset cross terms and constant shift, and symmetrised.
//      3. Scale + Bulge: joint 2-parameter least-squares fit per
//         axis from the centred data.
//
//    The laser field is [-fieldHalf, fieldHalf] mm.
//    The "9 point" burn pattern places line pairs at the field
//    edges (grid ±32) and center (grid 0), spanning the full
//    correction table used by LaserBJJCZ::writeCorrectionTable().
//
//    The correction table spans grid coordinates [-32, 32] and stores
//    offsets in units of CORRECTION_SCALE = 0x10000/64 = 1024.
//    The stored galvoBulge is the coefficient of the physical spot
//    shift the table produces, +bulge * r² * g, which cancels the
//    physical lens distortion -bulge * r² * g.  No sign inversion
//    is needed between fit and machine value.
//---------------------------------------------------------

bool GalvoCalibration::compute(Machine* machine, double xTopLeft, double xTopRight, double xMiddleLeft,
    double xMiddleRight, double xBottomLeft, double xBottomRight, double yLeftTop, double yLeftBottom,
    double yCenterTop, double yCenterBottom, double yRightTop, double yRightBottom) {
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

      // Factory for the raw pair measurements; averaging and
      // correction happen per pass, so each pass starts from these.
      auto makePairs = [&]() {
            const double g = MEASUREMENT_GRID;
            PairSample xPairs[3] = {
                     {   xTopLeft,    xTopRight, g,   g},
                     {xMiddleLeft, xMiddleRight, g, 0.0},
                     {xBottomLeft, xBottomRight, g,  -g}
                  };
            PairSample yPairs[3] = {
                     {  yLeftTop,   yLeftBottom,  -g, g},
                     {yCenterTop, yCenterBottom, 0.0, g},
                     { yRightTop,  yRightBottom,   g, g}
                  };
            return std::pair {
               std::vector<PairSample>(xPairs, xPairs + 3), std::vector<PairSample>(yPairs, yPairs + 3)};
            };

      auto* laser = machine->laserEngine();

      double bulgeX = laser ? laser->galvoBulge().x() : 0.0;
      double bulgeY = laser ? laser->galvoBulge().y() : 0.0;
      double physGainX = 0.0, physGainY = 0.0;    // physical mm/grid at the half-length
      double physBulgeX = 0.0, physBulgeY = 0.0;  // physical mm/grid³ lens distortion
      double sx = 1.0, sy = 1.0;
      double k2x = 0.0, k2y = 0.0;                // galvoBulge in table units/grid³

      // Two passes: pass 1 uses the current machine bulge for the
      // offset estimate, pass 2 the freshly fitted one.
      const double machineBulgeX = laser ? laser->galvoBulge().x() : 0.0;
      const double machineBulgeY = laser ? laser->galvoBulge().y() : 0.0;

      for (int pass = 0; pass < 2; ++pass) {
            auto [xPairs, yPairs] = makePairs();

            _offset = estimateOffset(
                xPairs.data(), yPairs.data(), fieldHalf, bulgeX, bulgeY, machineBulgeX, machineBulgeY);
            centerMeasurements(
                xPairs.data(), yPairs.data(), fieldHalf, bulgeX, bulgeY, _offset.x(), _offset.y());
            // 1) physical response of the machine from the data:
            if (!fitAxis(xPairs.data(), physGainX, physBulgeX)) {
                  Critical("GalvoCalibration::compute: unable to fit X axis (pass {})", pass + 1);
                  return false;
                  }
            if (!fitAxis(yPairs.data(), physGainY, physBulgeY)) {
                  Critical("GalvoCalibration::compute: unable to fit Y axis (pass {})", pass + 1);
                  return false;
                  }
            // 2) invert through the actual marking path to obtain the
            //    correction values mapToGalvo / writeCorrectionTable expect:
            if (!solveCorrection(fieldHalf, physGainX, physBulgeX, sx, k2x)) {
                  Critical("GalvoCalibration::compute: unable to solve X correction (pass {})", pass + 1);
                  return false;
                  }
            if (!solveCorrection(fieldHalf, physGainY, physBulgeY, sy, k2y)) {
                  Critical("GalvoCalibration::compute: unable to solve Y correction (pass {})", pass + 1);
                  return false;
                  }
            // bulge for the next offset-estimation pass, in table units:
            bulgeX = k2x;
            bulgeY = k2y;
            }

      //--- RMS of the physical-model fit against the centred data ---
      auto [xPairsFit, yPairsFit] = makePairs();
      centerMeasurements(
          xPairsFit.data(), yPairsFit.data(), fieldHalf, bulgeX, bulgeY, _offset.x(), _offset.y());

      // Sample (gx, gy) is the grid coordinate of the positive endpoint
      // on the measured axis (X pairs: g1=+h, g2=G;  Y pairs: g1=G, g2=+h).
      const Sample allSamples[6] = {
               {(xPairsFit[0].leftX + xPairsFit[0].rightX) * 0.5, xPairsFit[0].g1, xPairsFit[0].g2,  true},
               {(xPairsFit[1].leftX + xPairsFit[1].rightX) * 0.5, xPairsFit[1].g1, xPairsFit[1].g2,  true},
               {(xPairsFit[2].leftX + xPairsFit[2].rightX) * 0.5, xPairsFit[2].g1, xPairsFit[2].g2,  true},
               {(yPairsFit[0].leftX + yPairsFit[0].rightX) * 0.5, yPairsFit[0].g1, yPairsFit[0].g2, false},
               {(yPairsFit[1].leftX + yPairsFit[1].rightX) * 0.5, yPairsFit[1].g1, yPairsFit[1].g2, false},
               {(yPairsFit[2].leftX + yPairsFit[2].rightX) * 0.5, yPairsFit[2].g1, yPairsFit[2].g2, false}
            };
      _rmsError = rmsResidual(allSamples, physGainX, physGainY, physBulgeX, physBulgeY);

      _scale  = QVector2D(sx, sy);
      _bulge  = QVector2D(k2x, k2y);
      _bulge4 = QVector2D(0.0, 0.0); // not computed from 9-point pattern

      _valid = true;
      Info("GalvoCalibration: scale=({:.6f},{:.6f}) offset=({:.4f},{:.4f})mm bulge=({:.6e},{:.6e}) "
           "rms={:.4f} mm",
          _scale.x(), _scale.y(), _offset.x(), _offset.y(), bulgeX, bulgeY, _rmsError);
      emit resultsChanged();
      return true;
      }

//---------------------------------------------------------
//   saveParameters
//    Persist the 12 raw measurement values (mm) as JSON.
//---------------------------------------------------------

bool GalvoCalibration::saveParameters(const QString& filePath, double xTopLeft, double xTopRight,
    double xMiddleLeft, double xMiddleRight, double xBottomLeft, double xBottomRight, double yLeftTop,
    double yLeftBottom, double yCenterTop, double yCenterBottom, double yRightTop, double yRightBottom) {
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
                  Warning(
                      "GalvoCalibration::loadParameters: unknown file type in {}", filePath.toStdString());
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
            Warning(
                "GalvoCalibration::loadParameters: parse error in {}: {}", filePath.toStdString(), e.what());
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
      auto* laser = machine->laserEngine();
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
            Info("GalvoCalibration: applied to '{}' and saved", machine->name());
            }
      return true;
      }
