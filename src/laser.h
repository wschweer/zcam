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

#include <thread>
#include <atomic>
#include "element.h"
#include "laserengine.h"

class ZCam;

enum LaserState { Off, Idle, Framing, FramingAboutToIdle, FramingAboutToMark, Marking, MarkingAboutToIdle, MarkingAboutToFraming };

//---------------------------------------------------------
//   Laser
//---------------------------------------------------------

class Laser : public QObject
      {
      Q_OBJECT
      QML_UNCREATABLE("no")
      QML_ELEMENT

      ZCam* zcam;
      PROPV(bool, enabled, false)
      PROPV(bool, framing, false)
      PROPV(bool, framing1, false)
      PROPV(bool, framing2, false)
      PROPV(bool, marking, false)
      PROPV(bool, idling, false)
      PROPV(bool, testMode, false)
      PROPV(bool, dryRun, false)
      PROPV(QString, stateText, QString("undefined"))
      PROPV(double, estimatedEnd, 0)
      PROPV(double, currentTime, 0)
      PROPV(LaserEngine*, engine, nullptr)

      LaserState state;
      std::thread* framingThread{nullptr};
      std::thread* markingThread{nullptr};
      std::atomic<bool> framingRunning;
      std::atomic<bool> stopFraming;
      std::atomic<bool> markingRunning;
      std::atomic<bool> stopMarking;

      void changeState(LaserState newState);

      // framing/marking threads
      bool doStartFraming();
      void doStopFraming();
      void doStartMarking();
      void doStopMarking();

    signals:
      void framingStopped();
      void markingStopped();

    public slots:
      void init();
      void exit();
      void stop();
      void startFraming();
      void startMarking();

    public:
      Laser(ZCam*, QObject* parent = nullptr);
      };
