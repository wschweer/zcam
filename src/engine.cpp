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

#include "engine.h"
#include "machine.h"
#include "laser.h"
#include "laser_bjjcz.h"
#include "laser_rkq.h"
#include "machinegcode.h"
#include "logger.h"

//---------------------------------------------------------
//   _properties
//---------------------------------------------------------

const std::string propertiesMachine =
    //
    R"json(
      {
        "label": " ",
        "cells": [
          {
            "name": "name",
            "sublabel": "Name",
            "type": "string"
          },
          {
            "name": "type",
            "sublabel": "Type",
            "type": "machineType"
          }
        ]
      },
          {
            "label": "Description",
            "cells": [
              {
                "name": "description",
                "type": "multiline"
              }
            ]
          },
          {
            "cells": [
              {
                "name": "line",
                "type": "line"
              }
            ]
          },
    )json";

//---------------------------------------------------------
//   Engine::toJson
//---------------------------------------------------------

json Engine::toJson() const {
      return json::object();
      }

//---------------------------------------------------------
//   Engine::fromJson
//---------------------------------------------------------

bool Engine::fromJson(const json&) {
      return true;
      }

//---------------------------------------------------------
//   Engine::create
//    Factory: create a concrete Engine subclass based on the
//    machine type enum and board type string.
//    Laser types create the appropriate Laser subclass based
//    on boardType; GCode types create a MachineGCode.
//---------------------------------------------------------

Engine* Engine::create(Machine* m, MachineType machineType, const QString& boardType) {
//      Debug("Engine::create type={} board={}", int(machineType), boardType.toStdString());
      Engine* e = nullptr;
      if (boardType == QStringLiteral("RKQ-LM-441"))
            e = new LaserRKQ(m);
      else {
            switch (machineType) {
                  case MachineType::Q_LASER:
                  case MachineType::MOPA_LASER:
                  case MachineType::UV_LASER:
                        e = new LaserBJJCZ(m);
                        break;
                  case MachineType::GCODE_LASER:
                  case MachineType::GCODE_MILL:
                  case MachineType::UNKNOWN:
                        e = new MachineGCode(m);
                        break;
                  }
            }
      return e;
      }

//---------------------------------------------------------
//   Engine::type
//    Convenience: delegate to the owning Machine.
//---------------------------------------------------------

MachineType Engine::type() const {
      return _machine->type();
      }

//---------------------------------------------------------
//   Engine::maxTravel / precision / etc.
//    Convenience: delegate shared Machine properties.
//---------------------------------------------------------

QVector3D Engine::maxTravel() const {
      return _machine->maxTravel();
      }

double Engine::precision() const {
      return _machine->precision();
      }

double Engine::circlePrecision() const {
      return _machine->circlePrecision();
      }

double Engine::travelSpeed() const {
      return _machine->travelSpeed();
      }

double Engine::framingSpeed() const {
      return _machine->framingSpeed();
      }