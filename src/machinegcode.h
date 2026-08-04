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
#include "machine.h"
#include "macros.h"

class ZCam;

//---------------------------------------------------------
//   MachineGCode
//    G-code CNC machine — concrete Machine subclass.
//---------------------------------------------------------

class MachineGCode : public Machine
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("MachineGCode objects are created by Machines")

    public:
      MachineGCode(ZCam* zc, QObject* parent = nullptr) : Machine(zc, parent) {}
      ~MachineGCode() = default;
      virtual const std::string_view properties() const override;
      };