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

#pragma once

#include <QObject>
#include <QVector2D>
#include <QQmlEngine>
#include <array>

class ZCam;
class Machine;

//---------------------------------------------------------
//   GalvoCalibration
//    Computes galvo correction values (galvoScale and
//    galvoBulge) from 12 measured line lengths of the
//    "Galvo Test 9" burn pattern.
//
//    The 3×3 grid burned on laser paper has line pairs at
//    three positions (top/middle/bottom for the X axis,
//    left/center/right for the Y axis).  The user measures
//    the distances between the two lines of each pair on
//    both sides of the grid cross (left/right for X axis,
//    top/bottom for Y axis) which yields 12 values:
//
//      x axis (measured horizontally, left/right):
//        xTopLeft  xTopRight  xMiddleLeft xMiddleRight  xBottomLeft xBottomRight
//      y axis (measured vertically, top/bottom):
//        yLeftTop  yLeftBottom  yCenterTop yCenterBottom yRightTop yRightBottom
//
//    If all values equal the nominal grid spacing
//    (field width * 0.5), the galvo is perfectly calibrated:
//      galvoScale = (1, 1) and galvoBulge = (0, 0).
//---------------------------------------------------------
class GalvoCalibration : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("GalvoCalibration objects are created by ZCam")

      ZCam* zcam;

      // nominal grid spacing: half of the field size
      double nominal {0.0};

      // computed results
      QVector2D _scale {1.0, 1.0};
      QVector2D _bulge {0.0, 0.0};
      double _rmsError {0.0};
      bool _valid {false};

    signals:
      void resultsChanged();

    public:
      explicit GalvoCalibration(ZCam* zc, QObject* parent = nullptr);

      /// Compute galvoScale and galvoBulge from 12 measured line
      /// lengths (in mm).  The machine supplies the field size.
      /// Returns true on success.
      Q_INVOKABLE bool compute(Machine* machine, double xTopLeft, double xTopRight, double xMiddleLeft,
                               double xMiddleRight, double xBottomLeft, double xBottomRight, double yLeftTop,
                               double yLeftBottom, double yCenterTop, double yCenterBottom, double yRightTop,
                               double yRightBottom);

      /// Reset computed results to invalid state.
      Q_INVOKABLE void clear();

      /// Apply the computed correction values to the machine and
      /// persist the machine configuration to disk.
      Q_INVOKABLE bool applyToMachine(Machine* machine);

      Q_PROPERTY(QVector2D scale READ scale NOTIFY resultsChanged)
      QVector2D scale() const { return _scale; }

      Q_PROPERTY(QVector2D bulge READ bulge NOTIFY resultsChanged)
      QVector2D bulge() const { return _bulge; }

      Q_PROPERTY(double rmsError READ rmsError NOTIFY resultsChanged)
      double rmsError() const { return _rmsError; }

      Q_PROPERTY(bool valid READ valid NOTIFY resultsChanged)
      bool valid() const { return _valid; }

      /// Nominal grid spacing (field width * 0.5) in mm.
      Q_PROPERTY(double nominalSpacing READ nominalSpacing NOTIFY resultsChanged)
      double nominalSpacing() const { return nominal; }
      };
