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
#include <QtQml/qqmlregistration.h>
#include "engine.h"
#include "macros.h"

class Machine;

//---------------------------------------------------------
//   MachineGCode
//    G-code CNC machine — concrete Engine subclass.
//---------------------------------------------------------

class MachineGCode : public Engine
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("MachineGCode objects are created by Machine")

    public:
      MachineGCode(Machine* m, QObject* parent = nullptr) : Engine(m, parent) {}
      ~MachineGCode() = default;
      virtual const std::string properties() const override;
      };