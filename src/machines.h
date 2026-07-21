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
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "machine.h"

class MachineModel;

//---------------------------------------------------------
//   Machines
//---------------------------------------------------------

class Machines : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      Q_PROPERTY(QStringList machinesModel READ machinesModel NOTIFY machinesModelChanged)
      Q_PROPERTY(MachineModel* machineModel READ machineModel CONSTANT)

      std::vector<Machine*> machines;
      MachineModel* _machineModel;
      ZCam* zcam;

    signals:
      void machinesModelChanged();

    public:
      Machines(ZCam*, QObject* parent = nullptr);
      ~Machines();

      Q_INVOKABLE Machine* machine(int idx);
      Q_INVOKABLE void updateMachine(int idx, Machine* r);
      Q_INVOKABLE void addMachine(const QString& name);
      Q_INVOKABLE void removeMachine(int idx);

      QStringList machinesModel() const;
      MachineModel* machineModel() const { return _machineModel; }

      json toJson() const;
      void fromJson(const json&);
      };