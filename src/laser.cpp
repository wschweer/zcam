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

#include "laser.h"
#include "laser_bjjcz.h"
#include "laser_rkq.h"
#include "recipe.h"
#include "zcam.h"
#include "project.h"
#include "cam.h"
#include "fixture.h"
#include "logger.h"

//---------------------------------------------------------
//   static pulse table
//---------------------------------------------------------

static const std::vector<Pulse33> _pulseTable {
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

const std::vector<Pulse33>& Laser::pulseTable() {
      return _pulseTable;
      }

int Laser::maxFrequency(int pw) {
      for (const auto& p : _pulseTable)
            if (p.pulseWidth == pw)
                  return p.maxFrequency;
      return -1;
      }

int Laser::cutoffFrequency(int pw) {
      for (const auto& p : _pulseTable)
            if (p.pulseWidth == pw)
                  return p.cutOffFrequency;
      return -1;
      }

QStringList Laser::laserPulseList() const {
      QStringList sl;
      for (const auto& p : pulseTable())
            sl << QString("%1").arg(p.pulseWidth);
      return sl;
      }

LaserPosition Laser::mapToGalvo(double, double) {
      return LaserPosition(0, 0);
      }

//---------------------------------------------------------
//   Laser
//---------------------------------------------------------

Laser::Laser(ZCam* zc, QObject* parent) : Machine(zc, parent) {
      Assert(zc != nullptr);
      set_stateText("off");
      state = LaserState::Off;

      //
      //  action: the framing thread actually stopped
      //
      connect(
          this, &Laser::framingStopped, this,
          [this] {
                Debug("framing stopped in state {}", int(state));
                if (framingThread && framingThread->joinable())
                      framingThread->join();
                delete framingThread;
                framingThread = nullptr;
                if (state == LaserState::FramingAboutToIdle)
                      changeState(LaserState::Idle);
                else if (state == LaserState::FramingAboutToMark) {
                      changeState(LaserState::Marking);
                      doStartMarking();
                      }
                else if (state == LaserState::Framing)
                      changeState(LaserState::Idle);
                else
                      Debug("unhandled event: framingStopped");
                },
          Qt::QueuedConnection);

      //
      //  action: the marking thread actually stopped
      //
      connect(
          this, &Laser::markingStopped, this,
          [this] {
                if (markingThread && markingThread->joinable())
                      markingThread->join();
                delete markingThread;
                markingThread = nullptr;
                if (state == LaserState::MarkingAboutToIdle) {
                      changeState(LaserState::Idle);
                      }
                else if (state == LaserState::MarkingAboutToFraming) {
                      refreshCamAndFraming();
                      doStartFraming();
                      changeState(LaserState::Framing);
                      }
                else if (state == LaserState::Marking) {
                      // switch back to framing
                      refreshCamAndFraming();
                      changeState(LaserState::Framing);
                      doStartFraming();
                      }
                else
                      Debug("unhandled event: markingStopped");
                },
          Qt::QueuedConnection);
      }

//---------------------------------------------------------
//   ~Laser
//    Ensure background threads are stopped and joined before
//    the object is destroyed.  This prevents use-after-free
//    when the QML engine tears down the singleton hierarchy.
//---------------------------------------------------------

Laser::~Laser() {
      shutdown();
      }

//---------------------------------------------------------
//   changeState
//    does only set state variables idling, marking, framing,
//    and stateText
//---------------------------------------------------------

void Laser::changeState(LaserState newState) {
      if (state == newState) {
            Debug("state is already set");
            return;
            }
      switch (newState) {
            case LaserState::Off:
                  set_enabled(false);
                  set_framing(false);
                  set_marking(false);
                  set_idling(false);
                  set_stateText("Off");
                  break;
            case LaserState::Idle:
                  set_enabled(true);
                  set_framing(false);
                  set_marking(false);
                  set_idling(true);
                  set_stateText("Idle");
                  break;
            case LaserState::Framing:
                  set_framing(true);
                  set_marking(false);
                  set_stateText("Framing");
                  set_idling(false);
                  break;
            case LaserState::FramingAboutToIdle: set_stateText("Framing stopped"); break;
            case LaserState::FramingAboutToMark: set_stateText("Marking started"); break;
            case LaserState::Marking:
                  set_marking(true);
                  set_framing(false);
                  set_stateText("Marking");
                  set_idling(false);
                  break;
            case LaserState::MarkingAboutToIdle: set_stateText("Marking stopped"); break;
            case LaserState::MarkingAboutToFraming: set_stateText("Marking stopped, Framing started"); break;
            }
      state = newState;
      }

//---------------------------------------------------------
//   init
//---------------------------------------------------------

void Laser::init() {
      if (!initEngine(dryRun())) {
            set_stateText("connection failed");
            return;
            }
      changeState(LaserState::Idle);
      }

//---------------------------------------------------------
//   exit
//---------------------------------------------------------

void Laser::exit() {
      exitEngine();
      changeState(LaserState::Off);
      }

//---------------------------------------------------------
//   stop
//---------------------------------------------------------

void Laser::stop() {
      Debug("state {}", int(state));
      switch (state) {
            case LaserState::Framing:
                  changeState(LaserState::FramingAboutToIdle);
                  stopFraming = true;
                  return;
            case LaserState::Marking:
                  // changeState(LaserState::MarkingAboutToIdle);
                  changeState(LaserState::MarkingAboutToFraming);
                  stopMarking = true;
                  return;
            default: break;
            }
      changeState(LaserState::Idle);
      }

//---------------------------------------------------------
//   startMarking
//    Start of marking is allowed only out of framing state.
//---------------------------------------------------------

void Laser::startMarking() {
      if (state == LaserState::Framing) {
            changeState(LaserState::FramingAboutToMark);
            stopFraming = true;
            }
      }

//---------------------------------------------------------
//   refreshCamAndFraming
//    Refresh cam data and rebuild the framing contour so the
//    laser follows the current geometry.  Called before every
//    framing start to ensure the convex hull / bounding box
//    is up to date even if the user edited shapes without pressing
//    the manual Cam refresh button.
//---------------------------------------------------------

void Laser::refreshCamAndFraming() {
      zcam->refreshCam();
      if (zcam->project() && zcam->project()->fixture() && zcam->project()->fixture()->framing())
            zcam->project()->fixture()->framing()->update();
      }

//---------------------------------------------------------
//   startFraming
//---------------------------------------------------------

void Laser::startFraming() {
      switch (state) {
            case LaserState::Framing:
                  changeState(LaserState::FramingAboutToIdle);
                  stopFraming = true;
                  break;
            case LaserState::Idle:
                  // Refresh cam data and rebuild the framing contour before
                  // starting framing so the laser follows the current
                  // geometry.  The user may have edited shapes since the
                  // last cam update, and camDirty only flags that a refresh
                  // is pending — it does not trigger one automatically.
                  refreshCamAndFraming();
                  if (doStartFraming())
                        changeState(LaserState::Framing);
                  break;
            case LaserState::Marking:
                  changeState(LaserState::MarkingAboutToFraming);
                  stopMarking = true;
                  break;
            default: break;
            }
      }

//---------------------------------------------------------
//   doStartMarking
//    we are in idle state and want to start marking
//---------------------------------------------------------

void Laser::doStartMarking() {
      if (!zcam->project() || !zcam->project()->fixture()) {
            Critical("incomplete project");
            return;
            }

      markingThread = new std::thread([this] {
            //
            // marking happens in this background task
            //
            Project* topLevel = zcam->project();
            Fixture* fixture  = topLevel->fixture();
            Laser* laser      = toType<Laser>(topLevel->machine());
            startMarkingEngine();
            markingRunning = true;
            stopMarking    = false;

            try {
                  for (auto e : fixture->children()) {
                        if (!isType<Recipe>(e))
                              continue;
                        auto ll = toType<Recipe>(e);
                        if (!ll->burn())
                              continue;
                        LaserPath spl             = ll->collectLaserPath();
                        const LaserRecipe* recipe = ll->recipe();
                        if (!recipe)
                              Fatal("no recipe for <{}>", ll->name());

                        for (int i = 0; i < recipe->numPasses(); ++i) { // global passes
                              // mark every sublayer
                              for (int i = 0; i < recipe->layers()->size(); ++i) {
                                    auto s            = recipe->layer(i);
                                    auto parameterSet = LaserParameterSet(s, laser);
                                    parameterSet.setOverride(ParameterType(ll->overrideType1()),
                                                             ll->overrideValue1());
                                    parameterSet.setOverride(ParameterType(ll->overrideType2()),
                                                             ll->overrideValue2());
                                    if (stopMarking) {
                                          stopMarkingEngine();
                                          break;
                                          }
                                    if (s->enabled()) {
                                          spl.check();
                                          markLayer(spl, parameterSet);
                                          }
                                    }
                              }
                        }
                  }
            catch (const std::string s) {
                  Debug("marking stopped: {}", s);
                  }

            endMarkingEngine();
            markingRunning = false;
            emit markingStopped();
            });
      }

//---------------------------------------------------------
//   doStartFraming
//---------------------------------------------------------

bool Laser::doStartFraming() {
      if (!zcam->project() || !zcam->project()->fixture()) {
            Critical("incomplete project");
            return false;
            }

      framingThread = new std::thread([this] {
            Project* project           = zcam->project();
            Clipper2Lib::PathD polygon = project->cam()->convexHull();
            framingRunning             = true;
            stopFraming                = false;
            try {
                  if (startFramingEngine()) {
                        while (!stopFraming) {
                              for (const auto& p : polygon) {
                                    if (stopFraming)
                                          break;
                                    move(p.x, p.y);
                                    }
                              }
                        }
                  }
            catch (std::string s) {
                  Debug("framing stopped: {}", s);
                  }
            Debug("framing end");
            framingRunning = false;
            stopFramingEngine();
            emit framingStopped();
            });
      return true;
      }

//---------------------------------------------------------
//   shutdown
//    Synchronously stop all background threads and join them.
//    This is called from aboutToQuit (before the event loop stops)
//    and from the destructor.  It sets the stop flags, calls the
//    engine stop methods, then joins both threads directly
//    (instead of relying on QueuedConnection signal handlers
//    which may never fire after the event loop has stopped).
//---------------------------------------------------------

void Laser::shutdown() {
      // Signal both threads to stop and tell the engine to abort
      // any blocking USB operations.
      stopFraming = true;
      stopMarking = true;

      setAbortFlag();

      // Join the framing thread directly (the QueuedConnection
      // handler that normally joins it won't fire once the
      // event loop has stopped).
      if (framingThread) {
            if (framingThread->joinable())
                  framingThread->join();
            delete framingThread;
            framingThread = nullptr;
            }

      // Join the marking thread directly.
      if (markingThread) {
            if (markingThread->joinable())
                  markingThread->join();
            delete markingThread;
            markingThread = nullptr;
            }

      // Put the laser in a clean Off state.  Only call exitEngine()
      // if the laser was actually initialized (state != Off) to avoid
      // sending USB commands to a device that was never opened.
      if (state != LaserState::Off)
            exitEngine();
      changeState(LaserState::Off);
      }