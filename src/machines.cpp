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
      }

Machine* Machines::machine(int idx) {
      if (idx < 0 || idx >= static_cast<int>(machines.size()))
            return nullptr;
      return machines[idx];
      }

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
      // Create a default laser machine (Q-switched Laser with BJJCZ board)
      Machine* m = Machine::create(zcam, QStringLiteral("Q-switched Laser"), QStringLiteral("BJJCZ"));
      m->set_name(name);
      machines.push_back(m);
      emit machinesModelChanged();
      }

void Machines::removeMachine(int idx) {
      if (idx < 0 || idx >= static_cast<int>(machines.size()))
            return;

      // Rename the corresponding .json file on disk by appending
      // a ".del" extension so it is not picked up on the next load.
      if (!_loadedDirectory.isEmpty()) {
            Machine* m = machines[idx];
            if (m) {
                  QString name = m->name();
                  if (name.isEmpty())
                        name = QStringLiteral("unnamed");
                  name.replace(QRegularExpression("[^a-zA-Z0-9_\\-]"), "_");
                  QString filePath = QDir(_loadedDirectory).filePath(name + ".json");
                  QFile file(filePath);
                  if (file.exists()) {
                        QString delPath = filePath + ".del";
                        if (!file.rename(delPath))
                              Warning("Machines::removeMachine: cannot rename {} to {}",
                                      filePath.toStdString(), delPath.toStdString());
                        }
                  }
            }

      delete machines[idx];
      machines.erase(machines.begin() + idx);
      emit machinesModelChanged();
      }

QStringList Machines::machinesModel() const {
      QStringList names;
      for (const auto& m : machines)
            names.append(m->name());
      return names;
      }

json Machines::toJson() const {
      json data = json::array();
      for (const auto& m : machines)
            data.push_back(m->toJson());
      return data;
      }

void Machines::fromJson(const json& data) {
      qDeleteAll(machines);
      machines.clear();
      if (data.is_array()) {
            for (const auto& jm : data) {
                  // Read the type and boardType from JSON to determine
                  // which concrete Machine subclass to create.
                  QString machineType;
                  QString boardType;
                  if (jm.contains("type") && jm["type"].is_string())
                        machineType = QString::fromStdString(jm["type"].get<std::string>());
                  if (jm.contains("boardType") && jm["boardType"].is_string())
                        boardType = QString::fromStdString(jm["boardType"].get<std::string>());

                  // Migration: map legacy type names
                  if (machineType == QStringLiteral("Fiber Laser"))
                        machineType = QStringLiteral("Q-switched Laser");

                  Machine* m = Machine::create(zcam, machineType, boardType);
                  m->setParent(this);
                  m->fromJson(jm);
                  machines.push_back(m);
                  }
            }
      emit machinesModelChanged();
      }

void Machines::loadFromDirectory(const QString& dir) {
      _loadedDirectory = dir;

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
                  json jm = json::parse(data.toStdString());
                  Assert(zcam != nullptr);

                  QString machineType;
                  QString boardType;
                  if (jm.contains("type") && jm["type"].is_string())
                        machineType = QString::fromStdString(jm["type"].get<std::string>());
                  if (jm.contains("boardType") && jm["boardType"].is_string())
                        boardType = QString::fromStdString(jm["boardType"].get<std::string>());

                  // Migration: map legacy type names
                  if (machineType == QStringLiteral("Fiber Laser"))
                        machineType = QStringLiteral("Q-switched Laser");

                  Machine* m = Machine::create(zcam, machineType, boardType);
                  m->setParent(this);
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

void Machines::saveToDirectory(const QString& dir) const {
      QDir d(dir);
      if (!d.exists())
            d.mkpath(".");

      for (const auto& m : machines) {
            QString name = m->name();
            if (name.isEmpty())
                  name = QStringLiteral("unnamed");
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