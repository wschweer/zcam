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
#include <QtQml/qqmlregistration.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "types.h"
#include "macros.h"

class ZCam;

//---------------------------------------------------------
//   Machine
//---------------------------------------------------------

class Machine {
      Q_GADGET

      PROP_GADGET(QString,  name      )
      PROP_GADGET(QString,  machineType      )
      PROP_GADGET(QString,  description      )
      PROP_GADGET(double,  travelX           )
      PROP_GADGET(double,  travelY           )
      PROP_GADGET(double,  travelZ           )
      PROP_GADGET(double,  travelSpeed       )
      PROP_GADGET(double,  framingSpeed      )
      PROP_GADGET(double,  safeDist1         )
      PROP_GADGET(double,  safeDist2         )
      PROP_GADGET(double,  maxXYFeed         )
      PROP_GADGET(double,  maxZFeed          )
      PROP_GADGET(double,  maxXYAcceleration )
      PROP_GADGET(double,  maxZAcceleration  )
      PROP_GADGET(double,  minSpindle        )
      PROP_GADGET(double,  maxSpindle        )
      PROP_GADGET(double,  precision         )
      PROP_GADGET(double,  ncPrecision       )
      PROP_GADGET(double,  circlePrecision   )

      // galvolaser
      PROP_GADGET(double,  p1                )
      PROP_GADGET(double,  p2                )
      PROP_GADGET(double,  p3                )
      PROP_GADGET(QVector2D,  scale          )
      PROP_GADGET(double,  shearX            )
      PROP_GADGET(double,  shearY            )
      PROP_GADGET(double,  rotate            )
      PROP_GADGET(bool,    swapxy            )

   public:
      Machine() : _travelX(0), _travelY(0), _travelZ(0), _travelSpeed(0),
                  _framingSpeed(0), _safeDist1(0), _safeDist2(0), _maxXYFeed(0),
                  _maxZFeed(0), _maxXYAcceleration(0), _maxZAcceleration(0),
                  _minSpindle(0), _maxSpindle(0), _precision(0), _ncPrecision(0),
                  _circlePrecision(0), _p1(0), _p2(0), _p3(0), _shearX(0),
                  _shearY(0), _rotate(0), _swapxy(false) {}
      json toJson() const;
      bool fromJson(const json&);
      };

//---------------------------------------------------------
//   MAchines
//---------------------------------------------------------

class Machines : public QObject {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      Q_PROPERTY(QStringList machinesModel READ machinesModel NOTIFY machinesModelChanged)

      std::vector<Machine> machines;

   public:
      Machines(QObject* parent = nullptr) : QObject(parent) {}

      Q_INVOKABLE Machine machine(int idx) { return machines[idx]; }
      Q_INVOKABLE void updateMachine(int idx, const Machine& r);
      Q_INVOKABLE void addMachine(const QString& name);
      Q_INVOKABLE void removeMachine(int idx);

      QStringList machinesModel() const;

      json toJson() const;
      void fromJson(const json&);

   signals:
      void machinesModelChanged();
      };
