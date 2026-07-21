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
#include "logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
//---------------------------------------------------------
//   Machines
//---------------------------------------------------------

Machines::Machines(ZCam* zc, QObject* parent) : QObject(parent), zcam(zc) {
      _machineModel = new MachineModel(this);
}
Machines::~Machines() {
      // Machines are children of this QObject, so they are automatically
      // deleted by the QObject destructor.  No manual cleanup needed.
}
//---------------------------------------------------------
//   machine
//    Return a raw pointer to the Machine at idx.
//    The pointer is valid until machinesModelChanged is emitted.
//---------------------------------------------------------

Machine* Machines::machine(int idx) {
      if (idx < 0 || idx >= static_cast<int>(machines.size()))
            return nullptr;
      return machines[idx];
}
//---------------------------------------------------------
//   updateMachine
//---------------------------------------------------------

void Machines::updateMachine(int idx, Machine* r) {
      if (idx >= 0 && idx < static_cast<int>(machines.size())) {
            if (r)
                  r->setParent(this);
            if (machines[idx])
                  delete machines[idx];
            machines[idx] = r;
            emit machinesModelChanged();
      }
}
void Machines::addMachine(const QString& name) {
      Machine* m = new Machine(zcam, this);
      m->set_name(name);
      machines.push_back(m);
      emit machinesModelChanged();
}
void Machines::removeMachine(int idx) {
      if (idx >= 0 && idx < static_cast<int>(machines.size())) {
            delete machines[idx];
            machines.erase(machines.begin() + idx);
            emit machinesModelChanged();
      }
}
QStringList Machines::machinesModel() const {
      QStringList names;
      for (const auto& m : machines)
            names.append(m->name());
      return names;
}
//---------------------------------------------------------
//   toJson
//---------------------------------------------------------

json Machines::toJson() const {
      json data = json::array();
      for (const auto& m : machines)
            data.push_back(m->toJson());
      return data;
}
//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void Machines::fromJson(const json& data) {
      qDeleteAll(machines);
      machines.clear();
      if (data.is_array()) {
            for (const auto& jm : data) {
                  Machine* m = new Machine(zcam, this);
                  m->fromJson(jm);
                  machines.push_back(m);
            }
      }
      emit machinesModelChanged();
}
//---------------------------------------------------------
//   loadFromDirectory
//    Load all .json machine files from the given directory.
//    Each file contains a single machine JSON object.
//---------------------------------------------------------

void Machines::loadFromDirectory(const QString& dir) {
      qDeleteAll(machines);
      machines.clear();

      QDir d(dir);
      if (!d.exists()) {
            emit machinesModelChanged();
            return;
      }

      const auto files = d.entryList({"*.json"}, QDir::Files, QDir::Name);
      for (const QString& fileName : files) {
            QString filePath = d.filePath(fileName);
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                  Warning("Machines::loadFromDirectory: cannot open {}", filePath.toStdString());
                  continue;
            }
            QByteArray data = file.readAll();
            file.close();
            try {
                  json jm    = json::parse(data.toStdString());
                  Machine* m = new Machine(zcam, this);
                  m->fromJson(jm);
                  machines.push_back(m);
            }
            catch (const json::parse_error& e) {
                  Warning("Machines::loadFromDirectory: parse error in {}: {}", filePath.toStdString(),
                          e.what());
            }
      }
      emit machinesModelChanged();
}
//---------------------------------------------------------
//   saveToDirectory
//    Save each machine as an individual .json file in dir.
//    The file name is derived from the machine name (sanitised).
//---------------------------------------------------------

void Machines::saveToDirectory(const QString& dir) const {
      QDir d(dir);
      if (!d.exists())
            d.mkpath(".");

      for (const auto& m : machines) {
            QString name = m->name();
            if (name.isEmpty())
                  name = QStringLiteral("unnamed");
            // Sanitise: replace characters that are problematic in file names
            name.replace(QRegularExpression("[^a-zA-Z0-9_\\-]"), "_");
            QString filePath = d.filePath(name + ".json");
            QFile file(filePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                  Warning("Machines::saveToDirectory: cannot open {} for writing", filePath.toStdString());
                  continue;
            }
            json jm = m->toJson();
            QTextStream out(&file);
            out << QString::fromStdString(jm.dump(4));
            file.close();
      }
}