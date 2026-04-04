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

#include "machine.h"

//---------------------------------------------------------
//   toJson
//---------------------------------------------------------

json Machine::toJson() const
      {
      json data;
      data["name"] = _name.toStdString();
      data["machineType"] = _machineType.toStdString();
      data["description"] = _description.toStdString();
      data["travelX"] = _travelX;
      data["travelY"] = _travelY;
      data["travelZ"] = _travelZ;
      data["travelSpeed"] = _travelSpeed;
      data["framingSpeed"] = _framingSpeed;
      data["safeDist1"] = _safeDist1;
      data["safeDist2"] = _safeDist2;
      data["maxXYFeed"] = _maxXYFeed;
      data["maxZFeed"] = _maxZFeed;
      data["maxXYAcceleration"] = _maxXYAcceleration;
      data["maxZAcceleration"] = _maxZAcceleration;
      data["minSpindle"] = _minSpindle;
      data["maxSpindle"] = _maxSpindle;
      data["precision"] = _precision;
      data["ncPrecision"] = _ncPrecision;
      data["circlePrecision"] = _circlePrecision;

      data["p1"] = _p1;
      data["p2"] = _p2;
      data["p3"] = _p3;
      data["scale_x"] = _scale.x();
      data["scale_y"] = _scale.y();
      data["shearX"] = _shearX;
      data["shearY"] = _shearY;
      data["rotate"] = _rotate;
      data["swapxy"] = _swapxy;

      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

bool Machine::fromJson(const json& data)
      {
      if (data.contains("name")) _name = QString::fromStdString(data["name"]);
      if (data.contains("machineType")) _machineType = QString::fromStdString(data["machineType"]);
      if (data.contains("description")) _description = QString::fromStdString(data["description"]);
      if (data.contains("travelX")) _travelX = data["travelX"];
      if (data.contains("travelY")) _travelY = data["travelY"];
      if (data.contains("travelZ")) _travelZ = data["travelZ"];
      if (data.contains("travelSpeed")) _travelSpeed = data["travelSpeed"];
      if (data.contains("framingSpeed")) _framingSpeed = data["framingSpeed"];
      if (data.contains("safeDist1")) _safeDist1 = data["safeDist1"];
      if (data.contains("safeDist2")) _safeDist2 = data["safeDist2"];
      if (data.contains("maxXYFeed")) _maxXYFeed = data["maxXYFeed"];
      if (data.contains("maxZFeed")) _maxZFeed = data["maxZFeed"];
      if (data.contains("maxXYAcceleration")) _maxXYAcceleration = data["maxXYAcceleration"];
      if (data.contains("maxZAcceleration")) _maxZAcceleration = data["maxZAcceleration"];
      if (data.contains("minSpindle")) _minSpindle = data["minSpindle"];
      if (data.contains("maxSpindle")) _maxSpindle = data["maxSpindle"];
      if (data.contains("precision")) _precision = data["precision"];
      if (data.contains("ncPrecision")) _ncPrecision = data["ncPrecision"];
      if (data.contains("circlePrecision")) _circlePrecision = data["circlePrecision"];

      if (data.contains("p1")) _p1 = data["p1"];
      if (data.contains("p2")) _p2 = data["p2"];
      if (data.contains("p3")) _p3 = data["p3"];
      if (data.contains("scale_x") && data.contains("scale_y")) _scale = QVector2D(data["scale_x"], data["scale_y"]);
      if (data.contains("shearX")) _shearX = data["shearX"];
      if (data.contains("shearY")) _shearY = data["shearY"];
      if (data.contains("rotate")) _rotate = data["rotate"];
      if (data.contains("swapxy")) _swapxy = data["swapxy"];

      return true;
      }

//---------------------------------------------------------
//   Machines
//---------------------------------------------------------

void Machines::updateMachine(int idx, const Machine& r) {
      if (idx >= 0 && idx < machines.size()) {
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
      if (idx >= 0 && idx < machines.size()) {
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
