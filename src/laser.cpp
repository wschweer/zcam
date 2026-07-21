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

//---------------------------------------------------------
//   Laser
//---------------------------------------------------------

Laser::Laser(ZCam* zc, Machine* m, QObject* parent) : QObject(parent) {
      zcam = zc;
      machine = m;
      Debug("{}: board type <{}>", machine->name(), machine->boardType());
      if (machine->boardType() == "RKQ-LM-441") {
            set_engine(new LaserRKQ(zcam, this));
            }
      else {
            set_engine(new LaserBJJCZ(zcam, this));   // default
            }

      connect(machine, &Machine::boardTypeChanged, [this] {
            delete engine();
            Debug("machine board type changed");
            if (machine->boardType() == "RKQ-LM-441")
                  set_engine(new LaserBJJCZ(zcam, this));
            else
                  set_engine(new LaserRKQ(zcam, this));
            });

      set_stateText("off");
      state = LaserState::Off;

      //
      //  action: the framing thread actually stopped
      //
      connect(
          this, &Laser::framingStopped, this,
          [this] {
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
      if (!engine()->init(dryRun())) {
            set_stateText("connection failed");
            return;
            }
      changeState(LaserState::Idle);
      }

//---------------------------------------------------------
//   exit
//---------------------------------------------------------

void Laser::exit() {
      engine()->exit();
      changeState(LaserState::Off);
      }

//---------------------------------------------------------
//   stop
//---------------------------------------------------------

void Laser::stop() {
      Debug("==");
      switch (state) {
            case LaserState::Framing:
                  changeState(LaserState::FramingAboutToIdle);
                  doStopFraming(); // toggle framing state
                  return;
            case LaserState::Marking:
                  // changeState(LaserState::MarkingAboutToIdle);
                  changeState(LaserState::MarkingAboutToFraming);
                  doStopMarking();
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
      Debug("==");
      if (state == LaserState::Framing) {
            changeState(LaserState::FramingAboutToMark);
            doStopFraming();
            }
      }

//---------------------------------------------------------
//   refreshCamAndFraming
//    Refresh cam data and rebuild the framing contour so the
//    laser follows the current geometry.  Called before every
//    framing start to ensure the convex hull / bounding box
//    is up to date even if the user edited shapes without
//    pressing the manual Cam refresh button.
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
      Debug("==");
      switch (state) {
            case LaserState::Framing:
                  changeState(LaserState::FramingAboutToIdle);
                  doStopFraming(); // toggle framing state
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
                  doStopMarking();
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
            engine()->startMarking();
            markingRunning = true;
            stopMarking    = false;

            for (auto e : fixture->children()) {
                  if (!isType<Recipe>(e))
                        continue;
                  auto ll = toType<Recipe>(e);
                  if (!ll->burn())
                        continue;
                  LaserPath spl        = ll->collectLaserPath();
                  const LaserRecipe* recipe = ll->recipe();
                  if (!recipe)
                        Fatal("no recipe for <{}>", ll->name());

                  for (int i = 0; i < recipe->numPasses(); ++i) { // global passes
                        if (stopMarking)
                              break;
                        // mark every sublayer
                        for (int i = 0; i < recipe->layers()->size(); ++i) { // for every FiberLaserSubLayer
                              auto s            = recipe->layer(i);
                              auto parameterSet = LaserParameterSet(s);
                              parameterSet.setOverride(ParameterType(ll->overrideType1()),
                                                       ll->overrideValue1());
                              parameterSet.setOverride(ParameterType(ll->overrideType2()),
                                                       ll->overrideValue2());
                              if (stopMarking)
                                    break;
                              if (s->enabled()) {
                                    spl.check();
                                    engine()->markLayer(spl, parameterSet);
                                    }
                              }
                        }
                  }
            // HACK:
            //            for (int i = 0; i < 32; ++i)
            //                  engine()->move(i*2, i*2);

            engine()->endMarking();
            markingRunning = false;
            emit markingStopped();
            });
      }

//---------------------------------------------------------
//   doStopMarking
//---------------------------------------------------------

void Laser::doStopMarking() {
      stopMarking = true;
      engine()->stop();
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
            engine()->startFraming();
            while (!stopFraming) {
                  for (const auto& p : polygon) {
                        if (stopFraming)
                              break;
                        engine()->move(p.x, p.y);
                        }
                  }
            framingRunning = false;
            engine()->stop();
            emit framingStopped();
            });
      return true;
      }

//---------------------------------------------------------
//   doStopFraming
//    stop framing requested
//---------------------------------------------------------

void Laser::doStopFraming() {
      stopFraming = true;
      engine()->stopFraming();
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

      if (engine())
            engine()->setAbortFlag();

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

      // Put the laser in a clean Off state.  Only call engine()->exit()
      // if the laser was actually initialized (state != Off) to avoid
      // sending USB commands to a device that was never opened.
      if (engine() && state != LaserState::Off)
            engine()->exit();
      changeState(LaserState::Off);
      }
