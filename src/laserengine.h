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
#include <QVector2D>
#include <QTimer>
#include <QElapsedTimer>
#include <QtQml/qqmlregistration.h>
#include "laser_recipe.h"
#include "logger.h"
#include "types.h"

class Usb;
class LaserEngine;
class Group;
class Recipe;
class LaserSettings;
class Fixture;
class ZCam;

using PathsD = Clipper2Lib::PathsD;
using PathD  = Clipper2Lib::PathD;

//-------------------------------------------------------------------
//   LaserPathElement
//    This is a line segment of the LaserPath. Traveling along
//    the path either with laser on ("marking") or
//    laser off ("moving").
//-------------------------------------------------------------------

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
      unsigned x;
      unsigned y;
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
      double travelSpeed;
      double frequency;
      double pulseWidth;
      LaserParameterSet() {}
      LaserParameterSet(const LaserPass* s);
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

//---------------------------------------------------------
//   Status
//---------------------------------------------------------

class LaserStatusFlags
      {
      uint16_t flags;

    public:
      LaserStatusFlags(uint16_t f) : flags(f) {};
      bool isBusy() const { return flags & 0x04; }
      bool isReady() const { return flags & 0x20; }
      bool isAxis() const { return flags & 0x40; }
      int data() const { return flags; }
      };

//---------------------------------------------------------
//   formatter LaserStatusFlags
//---------------------------------------------------------

template <> struct std::formatter<LaserStatusFlags> {
      constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
      auto format(const LaserStatusFlags& f, auto& ctx) const {
            std::string s = ":";
            if (f.isBusy())
                  s += "BUSY:";
            if (f.isReady())
                  s += "READY:";
            if (f.isAxis())
                  s += "AXIS:";
            return std::format_to(ctx.out(), "0x{:x}-{}", f.data(), s);
            }
      };

//-------------------------------------------------------------------------------------------------
//   Laser
//    Galvo controller is tasked with sending queued data to the controller board and ensuring that
//    the connection to the controller board is established to perform these actions.
//
//    This should serve as a next generation command sequencer written from scratch for galvo
//    lasers. The goal is to provide all the given commands in a coherent queue structure which
//    provides correct sequences between list and single commands.
//-------------------------------------------------------------------------------------------------

class LaserEngine : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no no")

      Q_PROPERTY(bool testMode READ testMode WRITE setTestMode NOTIFY testModeChanged)
      Q_PROPERTY(bool dryRun READ dryRun WRITE setDryRun NOTIFY dryRunChanged)
      Q_PROPERTY(int estimatedEnd READ estimatedEnd WRITE setEstimatedEnd NOTIFY estimatedEndChanged)

    protected:
      bool _testMode    = false;
      bool _dryRun      = false;
      int _estimatedEnd = 0;
      volatile bool aborting {false};

    signals:
      void testModeChanged();
      void dryRunChanged();
      void estimatedEndChanged();

    public:
      void setTestMode(bool v) {
            if (v != testMode()) {
                  _testMode = v;
                  emit testModeChanged();
                  }
            }
      void setDryRun(bool v) {
            if (v != dryRun()) {
                  _dryRun = v;
                  emit dryRunChanged();
                  }
            }
      void setEstimatedEnd(int v) {
            if (v != estimatedEnd()) {
                  _estimatedEnd = v;
                  emit estimatedEndChanged();
                  }
            }
      bool testMode() const { return _testMode; }
      bool dryRun() const { return _dryRun; }
      int estimatedEnd() const { return _estimatedEnd; }

    public:

    public:
      enum class FramingType { Bbox, Contour };

    signals:
      void pulseListChanged();

    private:
      Q_PROPERTY(QStringList laserPulseList READ laserPulseList NOTIFY pulseListChanged);

      std::string source {"fiber"};

      std::vector<Pulse33> _pulseTable {
               {  2, 1950, 4000},
               {  4, 1350, 4000},
               {  6,  975, 4000},
               {  9,  600, 4000},
               { 13,  412, 3000},
               { 20,  225, 3000},
               { 30,  187, 3000},
               { 45,  150, 2000},
               { 60,  135, 2000},
               { 80,  112, 2000},
               {100,  105, 1000},
               {150,   57, 1000},
               {200,   45, 1000},
               {250,   42,  900},
               {350,   40,  600},
               {500,   30,  500},
            };

    protected:
      ZCam* zcam;
      void markLines(PathsD&, bool reverse);
      void mark(double x, double y);
      void setLaser(const LaserParameterSet&);

    public:
      LaserEngine(ZCam* w, QObject* parent = nullptr) {}
      virtual ~LaserEngine() {}
      void abort(bool dummyPacket = true);
      void setAbortFlag() { aborting = true; }

      friend class CmdList;
      QElapsedTimer markTime;
      const std::vector<Pulse33>& pulseTable() const { return _pulseTable; }
      int maxFrequency(int pw) const {
            for (const auto& p : _pulseTable)
                  if (p.pulseWidth == pw)
                        return p.maxFrequency;
            return -1;
            }
      int cutoffFrequency(int pw) const {
            for (const auto& p : _pulseTable)
                  if (p.pulseWidth == pw)
                        return p.cutOffFrequency;
            return -1;
            }
      LaserPosition mapToGalvo(double, double);
      QStringList laserPulseList() const {
            QStringList sl;
            for (const auto& p : pulseTable())
                  sl << QString("%1").arg(p.pulseWidth);
            return sl;
            }
      // ********************
      // interface
      // ********************

      virtual bool init(bool dryRun)                                             = 0;
      virtual void exit()                                                        = 0;
      virtual void stop()                                                        = 0;
      virtual bool startFraming()                                                = 0;
      virtual void stopFraming()                                                 = 0;
      virtual void startMarking()                                                = 0;
      virtual void stopMarking()                                                 = 0;
      virtual void endMarking()                                                  = 0;
      virtual void mark(const PathD&)                                            = 0;
      virtual void move(double x, double y)                                      = 0;
      virtual void markLayer(const LaserPath& path, const LaserParameterSet& sl) = 0;
      };
