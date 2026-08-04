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
#include <QVector2D>
#include <QElapsedTimer>
#include <QtQml/qqmlregistration.h>
#include <nlohmann/json.hpp>

#include "machine.h"
#include "laser_recipe.h"
#include "macros.h"

#include <thread>
#include <atomic>

class ZCam;
class Machine;
class Project;
class Fixture;
class Recipe;
class Group;

using PathsD = Clipper2Lib::PathsD;
using PathD  = Clipper2Lib::PathD;

//---------------------------------------------------------
//   LaserPathElement
//    This is a line segment of the LaserPath. Traveling along
//    the path either with laser on ("marking") or
//    laser off ("moving").
//---------------------------------------------------------

enum class LaserPathElementType { MoveTo, MarkTo };
struct LaserPathElement {
      LaserPathElementType type {LaserPathElementType::MoveTo};
      Vec2d p;
      LaserPathElement() {}
      LaserPathElement(LaserPathElementType t, Vec2d v) : type(t), p(v) {}
      LaserPathElement(LaserPathElementType type, double x, double y) : LaserPathElement(type, {x, y}) {}
      double x() const { return p.x(); }
      double y() const { return p.y(); }
      };

//---------------------------------------------------------
//   LaserPath
//    This is a list of points representing a path the
//    laser has to travel along.
//---------------------------------------------------------

class LaserPath : public std::vector<LaserPathElement>
      {
    public:
      void moveTo(double x, double y) { push_back({LaserPathElementType::MoveTo, x, y}); }
      void markTo(double x, double y) { push_back({LaserPathElementType::MarkTo, x, y}); }
      void append(Clipper2Lib::PathD p) {
            bool first = true;
            for (const auto& pt : p)
                  if (first) {
                        moveTo(pt.x, pt.y);
                        first = false;
                        }
                  else
                        markTo(pt.x, pt.y);
            }
      bool check();
      };

//---------------------------------------------------------
//   LineSegment
//---------------------------------------------------------

struct LineSegment {
      Vec2d p1; // start position of Line
      Vec2d p2; // end position of Line
      };

//---------------------------------------------------------
//   LineSegments
//---------------------------------------------------------

class LineSegments : public std::vector<LineSegment>
      {
    public:
      LaserPath toLaserPath();
      };

//---------------------------------------------------------
//   LaserPosition
//---------------------------------------------------------

struct LaserPosition {
      uint16_t x;
      uint16_t y;
      };

//---------------------------------------------------------
//   ParameterType
//---------------------------------------------------------

enum class ParameterType : int { None, Speed, Power, Interval, Frequency, Count, Pulse };

//---------------------------------------------------------
//   LaserParameterSet
//---------------------------------------------------------

struct LaserParameterSet {
      double power;
      double speed;
      double frequency;
      double pulseWidth;

      double onDelay;
      double offDelay;
      double endDelay;
      double polygonDelay;
      double jumpSpeed;
      double minJumpDelay;
      double maxJumpDelay;
      double jumpDistanceLimit;

      LaserParameterSet() {}
      LaserParameterSet(const LaserPass*, const Laser*);
      void setOverride(ParameterType t, double val);
      };

//---------------------------------------------------------
//   Pulse
//---------------------------------------------------------

struct Pulse33 {
      int pulseWidth;      // ns
      int cutOffFrequency; // above this the laser will have expected output power
      int maxFrequency;
      };

enum class LaserState {
      Off,
      Idle,
      Framing,
      FramingAboutToIdle,
      FramingAboutToMark,
      Marking,
      MarkingAboutToIdle,
      MarkingAboutToFraming
      };

//---------------------------------------------------------
//   Laser
//    Virtual base class for laser machines.  Inherits from Machine
//    and integrates the LaserEngine interface (framing/marking
//    state machine, background threads, board communication).
//---------------------------------------------------------

class Laser : public Machine
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("Laser objects are created by Machines")

      // Laser state properties (exposed to QML)
      // galvolaser
      PROPV(double, galvoP1, 0.0)
      PROPV(double, galvoP2, 0.0)
      PROPV(double, galvoP3, 0.0)
      PROPV(QVector2D, galvoScale, QVector2D(100.0, 100.0))
      PROPV(double, galvoShearX, 0.0)
      PROPV(double, galvoShearY, 0.0)
      PROPV(double, galvoRotate, 0.0)
      PROPV(bool, galvoSwapxy, false)

      PROPV(double, onDelay, 100.0)
      PROPV(double, offDelay, 100.0)
      PROPV(double, endDelay, 100.0)
      PROPV(double, polygonDelay, 100.0)

      PROPV(double, jumpSpeed, 6000.0)
      PROPV(double, minJumpDelay, 200.0)
      PROPV(double, maxJumpDelay, 400.0)
      PROPV(double, jumpDistanceLimit, 10.0)

      // Fiber-Laser
      PROPV(double, minFreq, 1.000)
      PROPV(double, maxFreq, 4000.000)

      // UV-LAser
      PROPV(double, ticklePulse, 1.0)
      PROPV(double, tickleFreq, 5.0)

      PROPV(bool, enableFPK, false)
      PROPV(double, fpkStartPower, 10.00)
      PROPV(double, fpkIncrement, 10.000)

      PROPV(double, uvMinPulse, 1.00)
      PROPV(double, uvMaxPulse, 20.00)

      PROP(QString, ethDevice)

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

      LaserState state;
      std::thread* framingThread {nullptr};
      std::thread* markingThread {nullptr};
      std::atomic<bool> framingRunning;
      std::atomic<bool> stopFraming;
      std::atomic<bool> markingRunning;
      std::atomic<bool> stopMarking;

      volatile bool aborting {false};

      void changeState(LaserState newState);

      // Refresh cam data and rebuild the framing contour so the
      // laser follows the current geometry.  Called before every
      // framing start to ensure the convex hull / bounding box is
      // up to date even if the user edited shapes without pressing
      // the manual Cam refresh button.
      void refreshCamAndFraming();

      // framing/marking threads
      bool doStartFraming();
      void doStartMarking();

    signals:
      void framingStopped();
      void markingStopped();

    public slots:
      void init();
      void exit();
      void shutdown();
      void stop();
      void startFraming();
      void startMarking();

    public:
      Laser(ZCam* zc, QObject* parent = nullptr);
      virtual ~Laser();

      // ── LaserEngine interface (pure virtual) ──────────────────
      virtual bool initEngine(bool dryRun)                                       = 0;
      virtual void exitEngine()                                                  = 0;
      virtual bool startFramingEngine()                                          = 0;
      virtual void stopFramingEngine()                                           = 0;
      virtual void startMarkingEngine()                                          = 0;
      virtual void stopMarkingEngine() const                                     = 0;
      virtual void endMarkingEngine()                                            = 0;
      virtual void mark(const PathD&)                                            = 0;
      virtual void move(double x, double y)                                      = 0;
      virtual void markLayer(const LaserPath& path, const LaserParameterSet& sl) = 0;
      // ── LaserEngine helpers ───────────────────────────────────
      void setAbortFlag() { aborting = true; }
      QElapsedTimer markTime;

      // Pulse table (shared by all laser variants)
      static const std::vector<Pulse33>& pulseTable();
      static int maxFrequency(int pw);
      static int cutoffFrequency(int pw);
      Q_INVOKABLE QStringList laserPulseList() const;

      virtual LaserPosition mapToGalvo(double, double);
      };
