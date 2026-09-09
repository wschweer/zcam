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

#include "macros.h"
#include "machinetypes.h"

class ZCam;
class Machines;
class Engine;
class Laser;
class LaserBJJCZ;
class LaserRKQ;
class MachineGCode;

//---------------------------------------------------------
//   Machine
//    Concrete, instantiable class holding shared machine
//    properties (travel, precision, etc.) and a pointer to
//    an Engine that encapsulates the type-specific behaviour.
//    The machine type can be changed dynamically by swapping
//    the Engine.
//---------------------------------------------------------

class Machine : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("Machine objects are created by Machines")

      PROP(QString, name)
      PROP(MachineType, type)
      /// Read-only string name for QML display and serialization.
      Q_PROPERTY(QString machineTypeName READ machineTypeName NOTIFY typeChanged)
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

      friend class Engine;

      Engine* _engine {nullptr};

      Q_PROPERTY(Engine* engine READ engine NOTIFY engineChanged)

    signals:
      void engineChanged();

    public:
      Machine(ZCam* zc, QObject* parent = nullptr);
      virtual ~Machine();
      /// Access the owning ZCam instance.
      ZCam* getZcam() const { return zcam; }
      json toJson() const;
      bool fromJson(const json&);
      /// Human-readable name for the current MachineType, via machineTypeMap.
      QString machineTypeName() const { return QString::fromUtf8(machineTypeMap.name(_type)); }
      /// Set the machine type from its string name (e.g. from JSON).
      void set_typeFromName(const QString& name);
      /// The owning Engine (may be null before first createEngine()).
      Engine* engine() const { return _engine; }
      /// Change the machine type dynamically.  Creates a new Engine
      /// of the appropriate subclass, copies over shared properties,
      /// and replaces the old engine.  The old engine is deleted.
      void changeType(MachineType newType, const QString& boardType = {});

      /// Create or replace the Engine based on the current type()
      /// and boardType().  Called during construction and after a
      /// type change.
      void createEngine();

      /// Convenience: return the engine as a Laser, or nullptr.
      Q_INVOKABLE Laser* laserEngine() const;

      /// Factory: create a Machine with the appropriate Engine
      /// based on the machine type enum and board type string.
      static Machine* create(ZCam* zc, MachineType, const QString& boardType);
      };