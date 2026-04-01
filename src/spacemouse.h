//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
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
#include "logger.h"

class QSocketNotifier;

//---------------------------------------------------------
//   SpaceMouse
//---------------------------------------------------------

class SpaceMouse : public QObject
      {
      Q_OBJECT
      QML_ELEMENT

      QSocketNotifier* notifier;

    signals:
      void translate(QVector3D);
      void rotate(QVector3D);
      void buttonPressed(int);

    public:
      SpaceMouse(QObject* parent = nullptr);
      ~SpaceMouse();
      Q_INVOKABLE bool init(); // return true if spacemouse is found and operational
      };
