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
#include "zcam.h"

//---------------------------------------------------------
//   Laser
//---------------------------------------------------------

Laser::Laser(ZCam* zc, QObject* parent) : QObject(parent) {
      zcam = zc;
      set_engine(new LaserEngine(zcam, this));
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
                      doStartFraming();
                      changeState(LaserState::Framing);
                      }
                else if (state == LaserState::Marking) {
                      // switch back to framing
                      changeState(LaserState::Framing);
                      doStartFraming();
                      }
                else
                      Debug("unhandled event: markingStopped");
          },
          Qt::QueuedConnection);
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
/*      if (!zcam->topLevel() || !zcam->topLevel()->fixture()) {
            Critical("incomplete project");
            return;
            }
*/
      markingThread = new std::thread([this] {
            //
            // marking happens in this background task
            //
#if 0
            TopLevel* topLevel = zcam->topLevel();
            Fixture* fixture   = topLevel->fixture();
            engine()->startMarking();
            markingRunning = true;
            stopMarking    = false;

            for (auto e : fixture->children()) {
                  if (!isType<LaserLayer>(e))
                        continue;
                  auto ll = toType<LaserLayer>(e);
                  if (!ll->active())
                        continue;
                  LaserPath spl                           = ll->collectLaserPath();
                  const LaserLayerSettings* layerSettings = ll->laserLayer();
                  if (!layerSettings)
                        Fatal("no laser settings for <{}>", ll->name());

                  for (int i = 0; i < layerSettings->numPasses(); ++i) { // global passes
                        if (stopMarking)
                              break;
                        // mark every sublayer
                        for (int i = 0; i < layerSettings->rowCount(); ++i) { // for every FiberLaserSubLayer
                              auto s = layerSettings->settings(i);
                              auto parameterSet = LaserParameterSet(s);
                              parameterSet.setOverride(ParameterType(ll->overrideType1()), ll->overrideValue1());
                              parameterSet.setOverride(ParameterType(ll->overrideType2()), ll->overrideValue2());
                              if (stopMarking)
                                    break;
                              if (s->enabled()) {
                                    spl.check();
//TODO                                    engine()->markLayer(spl, parameterSet);
                                    }
                              }
                        }
                  }
#endif
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
/*      if (!zcam->topLevel() || !zcam->topLevel()->fixture()) {
            Critical("incomplete project");
            return false;
            }
*/
      framingThread = new std::thread([this] {
#if 0
            TopLevel* topLevel         = zcam->topLevel();
            Fixture* fixture           = topLevel->fixture();
            Clipper2Lib::PathD polygon = fixture->convexHull();
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
#endif
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
