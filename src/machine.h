//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#pragma once

#include <QObject>
#include <QVector3D>
#include <QVector2D>
#include <QQmlEngine>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "types.h"
#include "macros.h"

class ZCam;
class Machines;
class Laser;
class LaserBJJCZ;
class LaserRKQ;
enum MachineType { Q_LASER, MOPA_LASER, UV_LASER, GCODE_CNC };
inline const std::vector<std::string> machineTypes {"Q-switched Laser", "MOPA Laser", "UV Laser",
                                                    "GCode CNC"};

inline const std::vector<std::string> boardTypes {"BJJCZ", "RKQ-LM-441"};

//---------------------------------------------------------
//   Machine
//    Virtual base class for all machine types.
//---------------------------------------------------------

class Machine : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("Machine objects are created by Machines")

      PROP(QString, name)
      PROP(QString, type)
      PROP(QString, boardType)
      PROP(QString, description)
      PROPV(QVector3D, maxTravel, QVector3D(100.0, 100.0, 100.0))
      PROPV(double, travelSpeed, 2000.0)
      PROP(double, framingSpeed)
      PROP(double, safeDist1)
      PROP(double, safeDist2)
      PROP(QVector3D, maxFeed)
      PROP(QVector3D, maxAcceleration)
      PROP(double, minSpindle)
      PROP(double, maxSpindle)
      PROP(double, precision)
      PROP(double, ncPrecision)
      PROP(double, circlePrecision)
      ZCam* zcam;

    public:
      Machine(ZCam* zc, QObject* parent = nullptr) : QObject(parent), zcam(zc) {}
      virtual ~Machine() = default;
      json toJson() const;
      bool fromJson(const json&);
      virtual const std::string_view properties() const = 0;

      /// Factory: create a concrete Machine subclass based on the
      /// machine type and board type strings.
      static Machine* create(ZCam* zc, const QString& machineType, const QString& boardType);
      bool isUVLaser() const { return type() == "UV Laser"; }
      bool isMOPALaser() const { return type() == "MOPA Laser"; }
      };