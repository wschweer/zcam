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

#include "machines.h"
#include "machinemodel.h"

//---------------------------------------------------------
//   Machines
//---------------------------------------------------------

Machines::Machines(QObject* parent) : QObject(parent) {
      _machineModel = new MachineModel(this);
      }

Machines::~Machines() {
      }

//---------------------------------------------------------
//   machine
//    Return a raw pointer to the Machine at idx.
//    The pointer is valid until machinesModelChanged is emitted.
//---------------------------------------------------------

Machine* Machines::machine(int idx) {
      if (idx < 0 || idx >= static_cast<int>(machines.size()))
            return nullptr;
      return &machines[idx];
      }

//---------------------------------------------------------
//   updateMachine
//---------------------------------------------------------

void Machines::updateMachine(int idx, const Machine& r) {
      if (idx >= 0 && idx < static_cast<int>(machines.size())) {
            machines[idx] = r;
            emit machinesModelChanged();
            }
      }

void Machines::addMachine(const QString& name) {
      Machine m;
      m.set_name(name);
      machines.push_back(m);
      emit machinesModelChanged();
      }

void Machines::removeMachine(int idx) {
      if (idx >= 0 && idx < static_cast<int>(machines.size())) {
            machines.erase(machines.begin() + idx);
            emit machinesModelChanged();
            }
      }

QStringList Machines::machinesModel() const {
      QStringList names;
      for (const auto& m : machines)
            names.append(m.name());
      return names;
      }

//---------------------------------------------------------
//   toJson
//---------------------------------------------------------

json Machines::toJson() const {
      json data = json::array();
      for (const auto& m : machines)
            data.push_back(m.toJson());
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void Machines::fromJson(const json& data) {
      machines.clear();
      if (data.is_array()) {
            for (const auto& jm : data) {
                  Machine m;
                  m.fromJson(jm);
                  machines.push_back(m);
                  }
            }
      emit machinesModelChanged();
      }