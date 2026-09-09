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

#include "machinetypes.h"

class ZCam;
class Machine;

//---------------------------------------------------------
//   Engine
//    Virtual base class for all machine engine types.
//    An Engine encapsulates the machine-type-specific behaviour
//    (laser state machine, G-code generation, hardware I/O).
//
//    Machine owns an Engine pointer and delegates type-specific
//    operations to it.  The Engine can be swapped at runtime to
//    change the machine type dynamically.
//---------------------------------------------------------

class Engine : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("Engine objects are created by Machine")

      Machine* _machine;  ///< owning Machine (never null after construction)

   protected:
      Engine(Machine* m, QObject* parent = nullptr) : QObject(parent), _machine(m) {}

   public:
      virtual ~Engine() = default;

      /// The owning Machine.
      Machine* machine() const { return _machine; }

      /// Convenience: the machine type.
      MachineType type() const;

      /// Convenience: access shared machine properties.
      QVector3D maxTravel() const;
      double precision() const;
      double circlePrecision() const;
      double travelSpeed() const;
      double framingSpeed() const;

      /// Property-set JSON for the inspector / serialisation.
      virtual const std::string properties() const = 0;

      /// Serialize engine-specific properties to JSON.
      virtual json toJson() const;
      /// Deserialize engine-specific properties from JSON.
      virtual bool fromJson(const json&);

      /// Factory: create a concrete Engine subclass based on the
      /// machine type enum and board type string.
      static Engine* create(Machine* m, MachineType type, const QString& boardType);
      };

extern const std::string propertiesMachine;
