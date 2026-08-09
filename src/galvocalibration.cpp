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

#include <cmath>

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
//    The galvo maps the field [-fieldHalf, fieldHalf] mm
//    to galvo units [-25800, 25800] with scale factor:
//      galvo = mm * 25800 / fieldHalf * scale
//
//    scale compensates the average linear deviation.
//    The bulge (pincushion/barrel) correction uses the
//    cubic formula from LaserBJJCZ::writeCorrectionTable():
//      corr = bulge * r² * pos
//    where pos is in grid units [-32, 32] and r² = x²+y².
//
//    The six measured pairs (left/right of the X-axis
//    cross and top/bottom of the Y-axis cross) are each
//    averaged to eliminate any translation offset.
//    Each measurement point sits at grid distance h = 16
//    (half the field) in the relevant axis, so the mean
//    of each pair cancels the cross-axis bulge contribution.
//
//      X axis:  Δx_measured (mm) → galvo error at (±16, y)
//        pair mean cancels y bulge: r²_x = 256 + y²
//      Y axis:  Δy_measured (mm) → galvo error at (x, ±16)
//        pair mean cancels x bulge: r²_y = 256 + x²
//
//    Using averaged r² per cross arm:
//      X: r² = 256 + 256/3  (y ∈ {16, 0, -16})
//      Y: r² = 256 + 256/3  (x ∈ {16, 0, -16})
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
      const double xTop    = (xTopLeft + xTopRight) * 0.5;
      const double xMiddle = (xMiddleLeft + xMiddleRight) * 0.5;
      const double xBottom = (xBottomLeft + xBottomRight) * 0.5;

      const double yLeft   = (yLeftTop + yLeftBottom) * 0.5;
      const double yCenter = (yCenterTop + yCenterBottom) * 0.5;
      const double yRight  = (yRightTop + yRightBottom) * 0.5;

      //--- scale: use center measurement only ---
      // The center measurement (xMiddle / yCenter) is least
      // affected by pincushion/barrel distortion and best
      // represents the pure linear scale.  Using the mean of
      // all three measurements would incorrectly fold the
      // bulge distortion into the scale factor.
      // galvoScale is stored in percent: 100 = factor 1.0 (see
      // LaserBJJCZ::initEngine/mapToGalvo, which divide by 100).
      const double sx = nominal / xMiddle;
      const double sy = nominal / yCenter;

      //--- bulge: least-squares fit of deviations ---
      // Convert each mean to galvo-unit error:
      //   measured galvo = mean [mm] * 25800 / fieldHalf
      //   expected galvo = 25800
      //   err = measured - expected = galvo bulge at grid pos h=16
      //
      // The bulge formula: corr = bulge * r² * h
      //   with r² = 256 + 256/3 (mean of y² or x² across the three cross arms)
      //   so h * r² = 16 * (256 + 85.333) = 16 * 341.333 = 5461.333
      const double galvoScaleFactor = 25800.0 / fieldHalf;
      const double r2mean           = 256.0 + 256.0 / 3.0;
      const double denom            = 16.0 * r2mean;

      const double errXTop    = xTop * galvoScaleFactor - 25800.0;
      const double errXMiddle = xMiddle * galvoScaleFactor - 25800.0;
      const double errXBottom = xBottom * galvoScaleFactor - 25800.0;

      const double errYLeft   = yLeft * galvoScaleFactor - 25800.0;
      const double errYCenter = yCenter * galvoScaleFactor - 25800.0;
      const double errYRight  = yRight * galvoScaleFactor - 25800.0;

      // Least-squares: bulge = Σ(err * h * r²) / Σ((h * r²)²)
      // All terms share the same h * r² = denom, so this simplifies
      // to the mean of (err / denom).
      const double bulgeX = (errXTop + errXMiddle + errXBottom) / (3.0 * denom);
      const double bulgeY = (errYLeft + errYCenter + errYRight) / (3.0 * denom);

      _scale = QVector2D(sx * 100.0, sy * 100.0);
      _bulge = QVector2D(bulgeX, bulgeY);

      //--- RMS error after correction (in mm) ---
      double sumSq = 0.0;
      auto rms     = [&](double measured, double bulgeVal, bool isX) {
            // corrected galvo = measured galvo - bulge * r² * h
            const double galvo          = measured * galvoScaleFactor;
            const double corr           = bulgeVal * denom;
            const double correctedGalvo = galvo - corr;
            // back to mm with new scale
            const double correctedMm = correctedGalvo / galvoScaleFactor / (isX ? sx : sy);
            const double err         = correctedMm - nominal;
            return err * err;
            };
      sumSq     += rms(xTop, bulgeX, true);
      sumSq     += rms(xMiddle, bulgeX, true);
      sumSq     += rms(xBottom, bulgeX, true);
      sumSq     += rms(yLeft, bulgeY, false);
      sumSq     += rms(yCenter, bulgeY, false);
      sumSq     += rms(yRight, bulgeY, false);
      _rmsError  = std::sqrt(sumSq / 6.0);

      _valid = true;
      Info("GalvoCalibration: scale=({:.3f}%,{:.3f}%) bulge=({:.6e},{:.6e}) rms={:.4f} mm", _scale.x(),
           _scale.y(), bulgeX, bulgeY, _rmsError);
      emit resultsChanged();
      return true;
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
