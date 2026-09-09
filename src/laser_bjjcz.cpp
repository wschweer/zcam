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

#include <algorithm>
#include <cmath>
#include <unistd.h>
#include "usb.h"
#include "zcam.h"
#include "project.h"
#include "laser_bjjcz.h"
#include "cal.h"

using namespace Clipper2Lib;

static const int VENDOR  = 0x9588;
static const int PRODUCT = 0x9899;

// Correction table grid half-range (matches galvocalibration.cpp).
static constexpr double CORRECTION_GRID_HALF = 32.0;

//---------------------------------------------------------
//   LaserParameterSet
//---------------------------------------------------------

LaserParameterSet::LaserParameterSet(const LaserPass* s, const Laser* laser) {
      power      = s->power();
      speed      = s->speed();
      frequency  = s->frequency();
      pulseWidth = s->pulseWidth();
      if (s->overrideTimings()) {
            onDelay           = s->onDelay();
            offDelay          = s->offDelay();
            endDelay          = s->endDelay();
            polygonDelay      = s->polygonDelay();
            jumpSpeed         = s->jumpSpeed();
            minJumpDelay      = s->minJumpDelay();
            maxJumpDelay      = s->maxJumpDelay();
            jumpDistanceLimit = s->jumpDistanceLimit();
            }
      else {
            onDelay           = laser->onDelay();
            offDelay          = laser->offDelay();
            endDelay          = laser->endDelay();
            polygonDelay      = laser->polygonDelay();
            jumpSpeed         = laser->jumpSpeed();
            minJumpDelay      = laser->minJumpDelay();
            maxJumpDelay      = laser->maxJumpDelay();
            jumpDistanceLimit = laser->jumpDistanceLimit();
            }
      }

//---------------------------------------------------------
//   setOverride
//---------------------------------------------------------

void LaserParameterSet::setOverride(ParameterType t, double val) {
      switch (t) {
            case ParameterType::None:
            case ParameterType::Interval:
            case ParameterType::Count: break;
            case ParameterType::Speed: speed = val; break;
            case ParameterType::Power: power = val; break;
            case ParameterType::Frequency: frequency = val; break;
            case ParameterType::Pulse: pulseWidth = val;
            }
      }

//---------------------------------------------------------
//   LaserCmd
//---------------------------------------------------------

struct LaserCmd {
      uint16_t cmd;
      const std::string_view name;
      };

using LaserCmdList = std::vector<LaserCmd>;

static const LaserCmdList commandLookup {
   LaserCmd {             listJumpTo,              "listJumpTo"},
    LaserCmd {          listEndOfList,           "listEndOfList"},
   LaserCmd {       listLaserOnPoint,        "listLaserOnPoint"},
    LaserCmd {          listDelayTime,           "listDelayTime"},
   LaserCmd {             listMarkTo,              "listMarkTo"},
    LaserCmd {          listJumpSpeed,           "listJumpSpeed"},
   LaserCmd {       listLaserOnDelay,        "listLaserOnDelay"},
    LaserCmd {      listLaserOffDelay,       "listLaserOffDelay"},
   LaserCmd {           listMarkFreq,            "listMarkFreq"},
    LaserCmd {     listMarkPowerRatio,      "listMarkPowerRatio"},
   LaserCmd {          listMarkSpeed,           "listMarkSpeed"},
    LaserCmd {          listJumpDelay,           "listJumpDelay"},
   LaserCmd {       listPolygonDelay,        "listPolygonDelay"},
    LaserCmd {          listWritePort,           "listWritePort"},
   LaserCmd {        listMarkCurrent,         "listMarkCurrent"},
    LaserCmd {          listMarkFreq2,           "listMarkFreq2"},
   LaserCmd {          listFlyEnable,           "listFlyEnable"},
    LaserCmd {      listQSwitchPeriod,       "listQSwitchPeriod"},
   LaserCmd {  listDirectLaserSwitch,   "listDirectLaserSwitch"},
    LaserCmd {           listFlyDelay,            "listFlyDelay"},
   LaserCmd {          listSetCo2FPK,           "listSetCo2FPK"},
    LaserCmd {       listFlyWaitInput,        "listFlyWaitInput"},
   LaserCmd {        listFiberOpenMO,         "listFiberOpenMO"},
    LaserCmd {       listWaitForInput,        "listWaitForInput"},
   LaserCmd {    listChangeMarkCount,     "listChangeMarkCount"},
   LaserCmd {   listSetWeldPowerWave,    "listSetWeldPowerWave"},
   LaserCmd {listEnableWeldPowerWave, "listEnableWeldPowerWave"},
   LaserCmd {listFiberYLPMPulseWidth, "listFiberYLPMPulseWidth"},
   LaserCmd {    listFlyEncoderCount,     "listFlyEncoderCount"},
    LaserCmd {         listSetDaZWord,          "listSetDaZWord"},
   LaserCmd {        listJptSetParam,         "listJptSetParam"},
    LaserCmd {          listReadyMark,           "listReadyMark"},

   LaserCmd {          UnknownCmdx03,              "Unknown-03"},
    LaserCmd {           DisableLaser,            "DisableLaser"},
   LaserCmd {            EnableLaser,             "EnableLaser"},
    LaserCmd {            ExecuteList,             "ExecuteList"},
   LaserCmd {       SetPwmPulseWidth,        "SetPwmPulseWidth"},
    LaserCmd {              GetStatus,               "GetStatus"},
   LaserCmd {            GetSerialNo,             "GetSerialNo"},
    LaserCmd {          GetListStatus,           "GetListStatus"},
   LaserCmd {          GetPositionXY,           "GetPositionXY"},
    LaserCmd {                 GotoXY,                  "GotoXY"},
   LaserCmd {         LaserSignalOff,          "LaserSignalOff"},
    LaserCmd {          LaserSignalOn,           "LaserSignalOn"},
   LaserCmd {           WriteCorLine,            "WriteCorLine"},
    LaserCmd {              ResetList,               "ResetList"},
   LaserCmd {            RestartList,             "RestartList"},
    LaserCmd {          WriteCorTable,           "WriteCorTable"},
   LaserCmd {         SetControlMode,          "SetControlMode"},
    LaserCmd {           SetDelayMode,            "SetDelayMode"},
   LaserCmd {        SetMaxPolyDelay,         "SetMaxPolyDelay"},
    LaserCmd {           SetEndOfList,            "SetEndOfList"},
   LaserCmd {    SetFirstPulseKiller,     "SetFirstPulseKiller"},
    LaserCmd {           SetLaserMode,            "SetLaserMode"},
   LaserCmd {              SetTiming,               "SetTiming"},
    LaserCmd {             SetStandby,              "SetStandby"},
   LaserCmd {       SetPwmHalfPeriod,        "SetPwmHalfPeriod"},
    LaserCmd {            StopExecute,             "StopExecute"},
   LaserCmd {               StopList,                "StopList"},
    LaserCmd {              WritePort,               "WritePort"},
   LaserCmd {       WriteAnalogPort1,        "WriteAnalogPort1"},
    LaserCmd {       WriteAnalogPort2,        "WriteAnalogPort2"},
   LaserCmd {       WriteAnalogPortX,        "WriteAnalogPortX"},
    LaserCmd {               ReadPort,                "ReadPort"},
   LaserCmd {     SetAxisMotionParam,      "SetAxisMotionParam"},
    LaserCmd {     SetAxisOriginParam,      "SetAxisOriginParam"},
   LaserCmd {           AxisGoOrigin,            "AxisGoOrigin"},
    LaserCmd {             MoveAxisTo,              "MoveAxisTo"},
   LaserCmd {             GetAxisPos,              "GetAxisPos"},
    LaserCmd {        GetFlyWaitCount,         "GetFlyWaitCount"},
   LaserCmd {           GetMarkCount,            "GetMarkCount"},
    LaserCmd {           SetFpkParam2,            "SetFpkParam2"},
   LaserCmd {            Fiber_SetMo,             "Fiber_SetMo"},
    LaserCmd {       Fiber_GetStMO_AP,        "Fiber_GetStMO_AP"},
   LaserCmd {                EnableZ,                 "EnableZ"},
    LaserCmd {               DisableZ,                "DisableZ"},
    LaserCmd {               SetZData,                "SetZData"},
   LaserCmd {    SetSPISimmerCurrent,     "SetSPISimmerCurrent"},
    LaserCmd {            SetFpkParam,             "SetFpkParam"},
   LaserCmd {                  Reset,                   "Reset"},
    LaserCmd {            GetFlySpeed,             "GetFlySpeed"},
   LaserCmd {        FiberPulseWidth,         "FiberPulseWidth"},
    LaserCmd {   FiberGetConfigExtend,    "FiberGetConfigExtend"},
   LaserCmd {              InputPort,               "InputPort"},
    LaserCmd {            GetMarkTime,             "GetMarkTime"},
   LaserCmd {            GetUserData,             "GetUserData"},
    LaserCmd {              SetFlyRes,               "SetFlyRes"}
      };

//---------------------------------------------------------
//   cmdName
//---------------------------------------------------------

static string cmdName(uint16_t cmd) {
      std::string name = std::format("??{:04x}", cmd);
      for (const auto& c : commandLookup) {
            if (c.cmd == cmd) {
                  name = c.name;
                  break;
                  }
            }
      return name;
      }

//---------------------------------------------------------
//   dump
//---------------------------------------------------------

void dump(const Packet6& data) {
      string line = format("{:20s} ", cmdName(data[0]));
      int n       = data[5] ? 6 : 5;
      for (int i = 1; i < n; ++i)
            line += format("{:04x} ", data[i]);
      line += ": ";
      for (int i = 1; i < n; ++i)
            line += format("{:5d} ", data[i]);
      Debug("{}", line);
      }

void dump(Packet6* p, bool single) {
      if (single)
            dump(*p);
      else {
            int repeat = 0;
            for (int i = 0; i < 256; ++i) {
                  if (i && (p[i] == p[i - 1])) {
                        ++repeat;
                        continue;
                        }
                  if (repeat)
                        Debug(" ===repeated {} times", repeat);
                  repeat = 0;
                  dump(p[i]);
                  }
            if (repeat)
                  Debug(" ===repeated {} times", repeat);
            }
      }

//---------------------------------------------------------
//   LaserBJJCZ
//---------------------------------------------------------

LaserBJJCZ::LaserBJJCZ(Machine* m, QObject* parent) : Laser(m, parent), list(this) {
      usb               = new Usb();
      _laserValuesValid = false;
      }

LaserBJJCZ::~LaserBJJCZ() {
      usb->close();
      delete usb;
      }

//---------------------------------------------------------
//   mapToGalvo
//---------------------------------------------------------

LaserPosition LaserBJJCZ::mapToGalvo(double x, double y) {
      const double maxX  = machine()->maxTravel().x();
      const double maxY  = machine()->maxTravel().y();
      const double halfX = maxX * 0.5;
      const double halfY = maxY * 0.5;

      // Center coordinates around the middle of the field.
      double xc = x - halfX;
      double yc = y - halfY;

      // The parameters below are stored as correction values: the value that
      // has to be sent to the scanner to compensate the measured physical
      // error.  mapToGalvo() therefore applies the inverse transformation so
      // that the physical beam ends up at the desired CAD position.

      // 1) Rotation (degrees).  Positive galvoRotate rotates the scanner
      //    coordinate system counter-clockwise; pre-rotate the desired point
      //    clockwise by the same angle.
            {
            const double angle = -galvoRotate() * M_PI / 180.0;
            const double cosA  = std::cos(angle);
            const double sinA  = std::sin(angle);
            const double xr    = xc * cosA - yc * sinA;
            const double yr    = xc * sinA + yc * cosA;
            xc                 = xr;
            yc                 = yr;
            }

      // 2) Shear.  Positive shearX shifts x by +shearX*y; subtract it.
      //    Values are stored as percent, divide by 100.
            {
            const double shearX  = galvoShear().x() / 100.0;
            const double shearY  = galvoShear().y() / 100.0;
            xc                  -= shearX * yc;
            yc                  -= shearY * xc; // use already sheared xc
            }

      // 3) Trapezoid.  The scanner scales each axis depending on the cross
      //    coordinate.  trapX positive means x is stretched by factor
      //    (1 + trapX * y / halfY); invert with exact division.
            {
            const double trapX = galvoTrapezoid().x() / 100.0;
            const double trapY = galvoTrapezoid().y() / 100.0;
            if (std::abs(trapX) > 1e-9)
                  xc /= (1.0 + trapX * yc / halfY);
            if (std::abs(trapY) > 1e-9)
                  yc /= (1.0 + trapY * xc / halfX);
            }

      // 4) Beam offset.  Positive galvoOffset.x shifts the distortion
      //    centre to the right; subtract it so the spot lands at the
      //    desired CAD position.
      xc -= galvoOffset().x();
      yc -= galvoOffset().y();

      // 5) Overall scale (factor, then raw galvo units).
      //    Use 2 × 25800 = 51600 as the full-range factor to match the
      //    safe galvo half-range (25800) used in initEngine().  The
      //    hardware supports ±32767 but 25800 keeps a ~21% safety margin;
      //    using 54000 (half-range 27000) left only ~17.5% and caused
      //    overflow at field corners with galvoScale slightly above 1.0.
      const double xScale = galvoScale().x() * 51600.0 / maxX;
      const double yScale = galvoScale().y() * 51600.0 / maxY;

      // Debug("======={} {}", galvoScale().x(), galvoScale().y());

      double rawX, rawY;
      if (galvoSwapxy()) {
            rawX = trunc(yc * yScale + 0x8000);
            rawY = trunc(xc * xScale + 0x8000);
            }
      else {
            rawX = trunc(xc * xScale + 0x8000);
            rawY = trunc(yc * yScale + 0x8000);
            }
      if (rawX < 0.0 || rawX > 0xffff || rawY < 0.0 || rawY > 0xffff) {
            Critical("position out of range ({:.2f}, {:.2f}) raw=({:.0f}, {:.0f}) "
                     "field=({:.1f}, {:.1f}) scale=({:.6f}, {:.6f})",
                x, y, rawX, rawY, maxX, maxY, galvoScale().x(), galvoScale().y());
            return LaserPosition((unsigned)std::clamp(rawX, 0.0, (double)0xffff),
                (unsigned)std::clamp(rawY, 0.0, (double)0xffff));
            }
      return LaserPosition((unsigned)rawX, (unsigned)rawY);
      }

//---------------------------------------------------------
//   initEngine
//---------------------------------------------------------

bool LaserBJJCZ::initEngine(bool _dryRun) {
      set_dryRun(_dryRun);
      Assert(machine()->getZcam());

      try {
            if (!dryRun())
                  usb->lookupDevice(VENDOR, PRODUCT);
            usb->open(dryRun());
            }
      catch (std::string s) {
            Debug("======init usb failed (dryRun={}): {}", dryRun(), s);
            return false;
            }

      // galvo range    -32767 -> 32767
      // aktually used: -25800 -> 25800 ( 175mmx175mm for 250mm Lens)
      // safety margin is typical 20%-21%
      double xScale = galvoScale().x();
      double yScale = galvoScale().y();
      xScale        = xScale * 25800 / machine()->maxTravel().x();
      yScale        = yScale * 25800 / machine()->maxTravel().y();

      galvos = (abs(xScale) + abs(yScale)) * .5;
      Debug("native scale {:.2f} {:.2f} galvos(scale): {} {:04x}", xScale, yScale, galvos, int(galvos));

      command({GetSerialNo});
      gpioInit();
      statusFlags(); // initialize status flags

      set_inputPort(command(InputPort)[1]);

      if (type() == MachineType::MOPA_LASER)
            get_fiber_st_mo_ap();

      command({UnknownCmdx03, 0, 0, 0, 0});

      if (!_dryRun && !is_ready()) {
            Critical("laser not ready");
            usb->close();
            return false;
            }
      writeCorrectionTable();

      enable_laser();
      if (type() == MachineType::MOPA_LASER)
            set_control_mode(0);
      else if (type() == MachineType::UV_LASER)
            set_control_mode(1);
      set_laser_mode(1);
      set_delay_mode(1);
      set_timing(1);

      set_standby(2000, 20);
      setFirstPulseKiller(200);

      double fres = (32767.0 / 2.0) / machine()->maxTravel().x();

      if (type() == MachineType::MOPA_LASER) {
            set_pwm_half_period(2);
            set_pwm_pulse_width(2);
            fiber_pulse_width(1);
            get_fiber_config_extend(); // ??
            set_fiber_mo(moRunningOnly() ? 0 : 1);

            // für bewegt achsen:
            //    0 - axis X
            //    fres - resolution ticks/mm
            //    1000 - period/max_speed (1000mm/s)
            //    24    - bit_depth / config    hardware counter
            set_fly_res(0, fres, 1000, 24);
            write_analog_port_1(3275);
            }
      else if (type() == MachineType::UV_LASER) {
            set_pwm_half_period(66);
            set_pwm_pulse_width(66);
            write_analog_port_2(0);
            //            set_pfk_param_2(fpk_max_voltage, fpk_min_voltage, fpk_t1, fpk_t2);
            set_pfk_param_2(4091, 1, 409, 100);
            set_fly_res(0, fres, 1000, 24);
            enable_z();
            write_analog_port_1(2047);
            }
      enable_z();
      gpioWrite(0);
      gotoXY(0x8000, 0x8000);
      return true;
      }

//---------------------------------------------------------
//   initPosition
//---------------------------------------------------------

void LaserBJJCZ::initPosition() {
      auto d = get_position_xy();
      gotoXY(d[1] + 1, d[2] + 1);
      };

//-----------------------------------------------------------------------------
//   command
//    send command  in command mode. Every command is a Packet6 and gets
//    a Packet4 answer. Every answer contains the LaserStatusFlags
//-----------------------------------------------------------------------------

Packet4 LaserBJJCZ::command(Packet6 data) const {
      Packet4 rv {0xffff, 0xffff, 0xffff, 0xffff};
      if (!send(data)) {
            Critical("failed");
            return rv;
            }
      if (!usb->read((uchar*)rv.data(), sizeof(rv)))
            Critical("usb receive failed");
      else
            _status = LaserStatusFlags(rv[3]);
      return rv;
      }

//---------------------------------------------------------
//   send
//---------------------------------------------------------

bool LaserBJJCZ::send(const CmdList& data) const {
      if (!waitReady())
            return false;
//      if (stopFraming || stopMarking) {
//            Debug("send aborted");
//            return true;
//            }
      if (!usb->write((uchar*)data[0].data(), LIST_SIZE * 12)) {
            Critical("usb send failed");
            return false;
            }
      return true;
      }

bool LaserBJJCZ::send(const Packet6& data) const {
      if (!usb->write((uchar*)data.data(), sizeof(Packet6))) {
            Critical("usb send failed");
            return false;
            }
      return true;
      }

//---------------------------------------------------------
//   wait_finished
//---------------------------------------------------------

void LaserBJJCZ::wait_finished() const {
      for (int i = 1;; ++i) {
            if (stopMarking)
                  stopMarkingEngine();
            auto status = statusFlags();
            if (status.isReady() && !status.isBusy())
                  break;
            usleep(1000 * 10);
            }
      }

//---------------------------------------------------------
//   wait_axis
//---------------------------------------------------------

void LaserBJJCZ::wait_axis() const {
      for (int i = 1; is_axis(); ++i) {
            usleep(1000 * 10);
            if (aborting)
                  return;
            if (!(i % 200))
                  Debug("...{}", i / 200);
            if (i > 100 * 30)
                  throw(std::string("waitAxis timeout"));
            }
      }

//---------------------------------------------------------
//   waitReady
//    return false if interrupted by stopFraming or stopMarking
//    or timeout
//---------------------------------------------------------

bool LaserBJJCZ::waitReady() const {
      if (_status.isReady()) // status from last command
            return true;
      // wait for ready
      for (int i = 0; !is_ready(); ++i) {
            //            Debug("{} {}    {}", stopFraming.load(), stopMarking.load(), i);
            if (stopFraming || stopMarking)
                  return false;
            usleep(100);               // 100µs
            if (i > 10 * 1000 * 100) { // 100sec
                  throw(std::string("waitReady timeout"));
                  return false;
                  }
            }
      return true;
      }

//---------------------------------------------------------
//   wait_idle
//---------------------------------------------------------

void LaserBJJCZ::wait_idle() const {
      for (int i = 1; is_busy(); ++i) {
            usleep(1000 * 10); // 10 ms
            if (aborting) {
                  Debug("===========================aborting");
                  return;
                  }
            if (!(i % 200))
                  Critical("...{} {}", i / 200, bool(aborting));
            if (i > 100 * 30)
                  throw(std::string("waitIdle timeout"));
            }
      }

//---------------------------------------------------------
//   exitEngine
//---------------------------------------------------------

void LaserBJJCZ::exitEngine() {
      stop_execute();
      stop_list();
      aborting = true;
      wait_idle();
      if (type() == MachineType::MOPA_LASER)
            set_fiber_mo(0);
      Debug("disable laser");
      /**/ disable_laser();
      usb->close();
      }

//---------------------------------------------------------
//   mark
//---------------------------------------------------------

void LaserBJJCZ::mark(double x, double y) {
      LaserPosition pos = mapToGalvo(x, y);
      mark(pos.x, pos.y);
      }

//---------------------------------------------------------
//   move
//---------------------------------------------------------

void LaserBJJCZ::move(double x, double y) {
      LaserPosition pos = mapToGalvo(x, y);
      move(pos.x, pos.y);
      }

//---------------------------------------------------------
//   markLines
//---------------------------------------------------------

void LaserBJJCZ::markLines(PathsD& pl, bool reverse) {
      if (pl.empty())
            return;

      PointD current = pl.front().front();

      for (const auto& p : pl) {
            auto p1 = p[0];
            auto p2 = p[1];

            move(p1.x, p1.y);
            mark(p2.x, p2.y);

            current = p2;
            list_delay_time(laserValues.endDelay);
            }
      }

//---------------------------------------------------------
//   dotCorrection
//---------------------------------------------------------

PathsD dotCorrection(const PathsD& paths, double offset) {
      if (offset)
            return InflatePaths(paths, offset, JoinType::Miter, EndType::Polygon, 2, 3);
      return paths;
      }

//---------------------------------------------------------
//   mark
//---------------------------------------------------------

void LaserBJJCZ::mark(const PathD& p) {
      bool first     = true;
      bool firstMove = true;
      for (const auto& pt : p) {
            if (first) {
                  move(pt.x, pt.y);
                  first = false;
                  }
            else if (firstMove) {
                  if (type() == MachineType::MOPA_LASER) {
                        set_fiber_mo(1);
                        list_delay_time(1000);
                        list_laser_on_point(10);
                        }
                  mark(pt.x, pt.y);
                  firstMove = false;
                  }
            else
                  mark(pt.x, pt.y);
            }
      }

//---------------------------------------------------------
//   setLaser
//---------------------------------------------------------

void LaserBJJCZ::setLaser(const LaserParameterSet& l) {
      if (!_laserValuesValid || l.speed != laserValues.speed)
            list.write({listMarkSpeed, uint16_t(l.speed * abs(galvos) * 0.001)});
      if (!_laserValuesValid || l.jumpSpeed != laserValues.jumpSpeed)
            list.write({listJumpSpeed, uint16_t(l.jumpSpeed * galvos * 0.001)});
      if (!_laserValuesValid || l.onDelay != laserValues.onDelay)
            list_laser_on_delay(l.onDelay);
      if (!_laserValuesValid || l.offDelay != laserValues.offDelay)
            list_laser_off_delay(l.offDelay);
      if (!_laserValuesValid || l.polygonDelay != laserValues.polygonDelay)
            list_polygon_delay(l.polygonDelay);
      if (!_laserValuesValid || l.endDelay != laserValues.endDelay)
            list_delay_time(l.endDelay);
      if (!_laserValuesValid || l.minJumpDelay != laserValues.minJumpDelay)
            list_jump_delay(l.minJumpDelay);

      if (type() == MachineType::UV_LASER) {
            if (!_laserValuesValid || l.frequency != laserValues.frequency)
                  list_mark_frequency(uint16_t(round(20000.0 / l.frequency)) & 0xffff);
            list_set_co2_fpk(20, 20);
            list_mark_power_ratio(200); // ??
            }
      else if (type() == MachineType::MOPA_LASER) {
            if (!_laserValuesValid || l.pulseWidth != laserValues.pulseWidth) {
                  list_fiber_open_mo(0);
                  list_fiber_ylpm_pulse_width(l.pulseWidth);
                  list_delay_time(1000);
                  list_fiber_open_mo(1);
                  list_delay_time(800);
                  }
            if (!_laserValuesValid || l.frequency != laserValues.frequency)
                  list_qswitch_period(uint16_t(round(20000.0 / l.frequency)) & 0xffff);
            if (!_laserValuesValid || l.power != laserValues.power)
                  list_mark_current(uint16_t(round(testMode() ? 15.0 : l.power * 0xFFF / 100.0)));
            // double pulseWidth;
            }
      laserValues       = l;
      _laserValuesValid = true;
      }

//---------------------------------------------------------
//   startFramingEngine
//---------------------------------------------------------

bool LaserBJJCZ::startFramingEngine() {
      try {
            aborting = false;
            waitReady();
            setLight(true);
            initPosition();
            _laserValuesValid = false;

            // Use framingSpeed() when configured, otherwise fall back to
            // travelSpeed().  Previously framing always used travelSpeed(),
            // ignoring the dedicated framingSpeed machine property.
            double frameSpd = framingSpeed();
            if (frameSpd <= 0.0)
                  frameSpd = travelSpeed();

            waitReady();
            list.start();
            list.write({listJumpSpeed, uint16_t(frameSpd * galvos * 0.001)});
            list.write({listMarkSpeed, uint16_t(frameSpd * galvos * 0.001)});
            list.write({listLaserOnDelay, 0});
            list.write({listLaserOffDelay, 0});
            list.write({listPolygonDelay, 0});
            list.write({listJumpDelay, 0});
            }
      catch (const std::string s) {
            Debug("failed: {}", s);
            return false;
            }
      return true;
      }

//---------------------------------------------------------
//   stopFramingEngine
//---------------------------------------------------------

void LaserBJJCZ::stopFramingEngine() {
      stopFraming = false;
      stop_execute();

      if (type() == MachineType::MOPA_LASER)
            set_fiber_mo(0);

      // gpioWrite(0x100);
      setLight(false);

      waitReady();
      initPosition();

      list.start();
      list.write({listJumpSpeed, uint16_t(travelSpeed() * galvos * 0.001)});
      list.write({listJumpDelay, uint16_t(minJumpDelay())});
      list.end(1);

      //      gpioWrite(0x100);
      set_standby(2000, 20);
      //      if (isMOPALaser())
      //            gpioWrite(0x100);
      //      else
      //            gpioWrite(0x300);
      gotoXY(0x8000, 0x8000, 0, 0);
      readPort();
      }

//---------------------------------------------------------
//   stopMarkingEngine
//---------------------------------------------------------

void LaserBJJCZ::stopMarkingEngine() const {
      stop_execute();
      stop_list();
      }

//---------------------------------------------------------
//   startMarkingEngine
//---------------------------------------------------------

void LaserBJJCZ::startMarkingEngine() {
      aborting          = false;
      _laserValuesValid = false;

      list.start();
      initPosition();
      waitReady();

      if (type() == MachineType::MOPA_LASER) {
            //list_fiber_open_mo(1);
            // set_fiber_mo(1);  ??
            //list_delay_time(800);
            }
      if (type() == MachineType::UV_LASER) {
            //            gpioWrite(0x100);
            set_standby(2000, 20);
            //            gpioWrite(0x300);
            gotoXY(0x8000, 0x8000, 0, 0);
            //            gpioWrite(0x300);
            initPosition();
            waitReady();
            }
      }

//---------------------------------------------------------
//   endMarkingEngine
//    called at the end of the marking thread
//---------------------------------------------------------

void LaserBJJCZ::endMarkingEngine() {
      // If the marking was aborted (stopMarking was set), the catch
      // block already called stop_execute().  The CmdList buffer may
      // still contain stale partial-list data from the interrupted
      // markLayer() — sending it would corrupt the board state.
      // Clear the buffer and reset the board's list so the next
      // operation (framing or a fresh marking) starts clean.
      if (markingAborted) {
            stop_execute();
            list.clear();
            reset_list();
            waitReady();
            if (type() == MachineType::MOPA_LASER)
                  set_fiber_mo(0);
            return;
            }
      list.end();
      wait_finished();
      if (type() == MachineType::MOPA_LASER)
            set_fiber_mo(0);
      }

//---------------------------------------------------------
//   list_delay_time
//---------------------------------------------------------

void LaserBJJCZ::list_delay_time(double time) {
      list.write({listDelayTime, uint16_t(time)});
      }

//---------------------------------------------------------
//   markLayer
//---------------------------------------------------------

void LaserBJJCZ::markLayer(const LaserPath& path, const LaserParameterSet& sl) {
      setLaser(sl);

      bool first  = true;
      bool moving = true;

      for (const auto& p : path) {
            // Check the abort flag between every path element so the
            // stop takes effect promptly instead of processing the
            // entire remaining layer.  Without this, markLayer keeps
            // filling the CmdList buffer and sending partial lists to
            // the board even after stopMarking is set.
            if (stopMarking)
                  throw std::string("stopped");
            if (p.type == LaserPathElementType::MoveTo) {
                  move(p.x(), p.y());
                  moving = true;
                  }
            else if (first) {
                  Critical("First segment in layer is not a move");
                  move(p.x(), p.y());
                  }
            else {
                  if (moving) {
                        list_delay_time(sl.endDelay);
                        // list_laser_on_point(10);
                        }
                  mark(p.x(), p.y());
                  moving = false;
                  }
            first = false;
            // list_delay_time(10);
            }
      }

static const std::string _propertiesQ = // Q-switched Laser
    R"json(
      {
        "class": "Machine",
        "rows": [
          {
            "label": " ",
            "cells": [
              {
                "type": "string",
                "name": "name",
                "sublabel": "Name"
              },
              {
                "type": "machineType",
                "name": "type",
                "sublabel": "Type"
              },
              {
                "type": "boardType",
                "name": "boardType",
                "sublabel": "Board"
              }
            ]
          },
          {
            "label": "Description",
            "cells": [
              {
                "name": "description",
                "type": "multiline"
              }
            ]
          },
          {
            "cells": [
              {
                "name": "line",
                "type": "line"
              }
            ]
          },
          {
            "columns": 2,
            "cells": [
              {
                "name": "maxTravel",
                "label": "Travel",
                "type": "vector3d",
                "unit": "mm",
                "default": [
                  100.0,
                  100.0,
                  100.0
                ],
                "tooltip": "Maximum travel range of the laser galvo/head in X, Y, Z"
              },
              {
                "name": "travelSpeed",
                "label": "Travel Speed",
                "type": "float",
                "unit": "mm/s",
                "min": 0.0,
                "max": 100000.0,
                "default": 0.0,
                "tooltip": "Speed used for rapid travel moves between marking regions"
              },
              {
                "name": "framingSpeed",
                "label": "Framing Speed",
                "type": "float",
                "unit": "mm/s",
                "min": 0.0,
                "max": 100000.0,
                "default": 0.0,
                "tooltip": "Speed used when tracing the job frame outline before marking"
              },
              {
                "name": "maxFeed",
                "label": "Max Feed",
                "type": "vector3d",
                "unit": "mm/s",
                "default": [
                  0.0,
                  0.0,
                  0.0
                ]
              },
              {
                "name": "maxAcceleration",
                "label": "Max Accel",
                "type": "vector3d",
                "unit": "mm/s²",
                "default": [
                  0.0,
                  0.0,
                  0.0
                ]
              },
              {
                "name": "line",
                "type": "line",
                "colSpan": 2
              },
              {
                "label": "Precision",
                "cells": [
                  {
                    "type": "float",
                    "unit": "mm",
                    "min": 0.001,
                    "max": 10.0,
                    "precision": 3,
                    "default": 0.001,
                    "name": "precision",
                    "sublabel": "Prec"
                  },
                  {
                    "type": "float",
                    "unit": "mm",
                    "min": 0.001,
                    "max": 10.0,
                    "precision": 3,
                    "default": 0.001,
                    "name": "ncPrecision",
                    "sublabel": "NC Prec"
                  }
                ]
              },
              {
                "name": "circlePrecision",
                "label": "Circle Prec",
                "type": "float",
                "unit": "mm",
                "min": 0.001,
                "max": 10.0,
                "precision": 3,
                "default": 0.001
              },
              {
                "name": "line",
                "type": "line",
                "colSpan": 2
              },
              {
                "label": "Galvo",
                "cells": [
                  {
                    "type": "float",
                    "min": 0.0,
                    "max": 10.0,
                    "default": 0.0,
                    "precision": 4,
                    "name": "galvoP1",
                    "sublabel": "P1"
                  },
                  {
                    "type": "float",
                    "min": 0.0,
                    "max": 10.0,
                    "precision": 4,
                    "default": 0.0,
                    "name": "galvoP2",
                    "sublabel": "P2"
                  },
                  {
                    "type": "float",
                    "min": 0.0,
                    "max": 10.0,
                    "precision": 4,
                    "default": 0.0,
                    "name": "galvoP3",
                    "sublabel": "P3"
                  }
                ]
              },
              {
                "label": "Bulge",
                "cells": [
                  {
                    "name": "galvoBulge",
                    "type": "vector2d",
                    "min": -5.0,
                    "max": 5.0,
                    "default": 0.0,
                    "precision": 4
                  }
                ]
              },
              {
                "label": "Bulge4",
                "cells": [
                  {
                    "name": "galvoBulge4",
                    "type": "vector2d",
                    "min": -5.0,
                    "max": 5.0,
                    "default": 0.0,
                    "precision": 6
                  }
                ]
              },
              {
                "label": "Offset",
                "cells": [
                  {
                    "name": "galvoOffset",
                    "type": "vector2d",
                    "min": -10.0,
                    "max": 10.0,
                    "default": 0.0,
                    "precision": 4,
                    "unit": "mm"
                  }
                ]
              },
              {
                "name": "galvoScale",
                "label": "Galvo Scale",
                "type": "vector2d",
                "default": [
                  1.0,
                  1.0
                ],
                "precision": 6
              },
              {
                "label": " ",
                "cells": [
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoShearX",
                    "sublabel": "Shear X"
                  },
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoShearY",
                    "sublabel": "Shear Y"
                  }
                ]
              },
              {
                "label": "Trapezoid",
                "cells": [
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoTrapezoidX",
                    "sublabel": "X"
                  },
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoTrapezoidY",
                    "sublabel": "Y"
                  }
                ]
              },
              {
                "name": "galvoRotate",
                "label": "Galvo Rotate",
                "type": "float",
                "unit": "°",
                "min": 0.0,
                "max": 360.0,
                "default": 0.0,
                "precision": 3
              },
              {
                "name": "galvoSwapxy",
                "label": "Galvo Swap XY",
                "type": "bool",
                "default": false
              },
              {
                "name": "ethDevice",
                "label": "Ethernet Device",
                "type": "ethDevice",
                "default": ""
              }
            ]
          }
        ]
      }

    )json";

// MOPA Laser
static const std::string _propertiesMOPA =
    R"json(
      {
        "class": "Machine",
        "rows": [
          {
            "label": " ",
            "cells": [
              {
                "type": "string",
                "name": "name",
                "sublabel": "Name"
              },
              {
                "type": "machineType",
                "name": "type",
                "sublabel": "Type"
              },
              {
                "type": "boardType",
                "name": "boardType",
                "sublabel": "Board"
              }
            ]
          },
          {
            "label": "Description",
            "cells": [
              {
                "name": "description",
                "type": "multiline"
              }
            ]
          },
          {
            "cells": [
              {
                "name": "line",
                "label": "Field",
                "type": "line"
              }
            ]
          },
          {
            "columns": 2,
            "cells": [
              {
                "name": "maxTravel",
                "label": "Travel",
                "type": "vector3d",
                "unit": "mm",
                "default": [
                  100.0,
                  100.0,
                  100.0
                ]
              },
              {
                "label": "Speed",
                "cells": [
                  {
                    "type": "float",
                    "unit": "mm/s",
                    "min": 0.0,
                    "max": 100000.0,
                    "default": 0.0,
                    "name": "travelSpeed",
                    "sublabel": "Travel"
                  },
                  {
                    "type": "float",
                    "unit": "mm/s",
                    "min": 0.0,
                    "max": 100000.0,
                    "default": 0.0,
                    "name": "framingSpeed",
                    "sublabel": "Framing"
                  }
                ]
              },
              {
                "name": "line",
                "type": "line",
                "colSpan": 2
              },
              {
                "label": "Precision",
                "cells": [
                  {
                    "type": "float",
                    "unit": "mm",
                    "min": 0.001,
                    "max": 10.0,
                    "precision": 3,
                    "default": 0.001,
                    "name": "precision",
                    "sublabel": "Prec"
                  },
                  {
                    "type": "float",
                    "unit": "mm",
                    "min": 0.001,
                    "max": 10.0,
                    "precision": 3,
                    "default": 0.001,
                    "name": "ncPrecision",
                    "sublabel": "NC Prec"
                  },
                  {
                    "type": "float",
                    "unit": "mm",
                    "min": 0.001,
                    "max": 10.0,
                    "precision": 3,
                    "default": 0.001,
                    "name": "circlePrecision",
                    "sublabel": "Circle Prec"
                  }
                ]
              },
              {
                "name": "line",
                "type": "line",
                "label": "Galvo Scanner",
                "colSpan": 2
              },
              {
                "label": "Offset",
                "cells": [
                  {
                    "name": "galvoOffset",
                    "type": "vector2d",
                    "min": -10.0,
                    "max": 10.0,
                    "default": 0.0,
                    "precision": 4,
                    "unit": "mm"
                  }
                ]
              },
              {
                "name": "galvoScale",
                "label": "Scale",
                "type": "vector2d",
                "default": [
                  1.0,
                  1.0
                ],
                "precision": 6
              },
              {
                "label": "Bulge2",
                "cells": [
                  {
                    "name": "galvoBulge",
                    "type": "vector2d",
                    "min": -5.0,
                    "max": 5.0,
                    "default": 0.0,
                    "precision": 4
                  }
                ]
              },
              {
                "label": "Bulge4",
                "cells": [
                  {
                    "name": "galvoBulge4",
                    "type": "vector2d",
                    "min": -5.0,
                    "max": 5.0,
                    "default": 0.0,
                    "precision": 6
                  }
                ]
              },
              {
                "label": "Shear",
                "cells": [
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoShearX",
                    "sublabel": "X"
                  },
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoShearY",
                    "sublabel": "Y"
                  }
                ]
              },
              {
                "label": "Trapezoid",
                "cells": [
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoTrapezoidX",
                    "sublabel": "X"
                  },
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoTrapezoidY",
                    "sublabel": "Y"
                  }
                ]
              },
              {
                "label": "Rotate",
                "cells": [
                  {
                    "name": "galvoRotate",
                    "type": "float",
                    "unit": "°",
                    "min": 0.0,
                    "max": 360.0,
                    "default": 0.0,
                    "precision": 3
                  },
                  {
                    "name": "galvoSwapxy",
                    "label": "Swap XY",
                    "type": "bool",
                    "default": false
                  }
                ]
              },
              {
                "label": "Jump",
                "cells": [
                  {
                    "name": "jumpSpeed",
                    "sublabel": "speed",
                    "type": "float",
                    "unit": "mm/s"
                  },
                  {
                    "name": "jumpDistanceLimit",
                    "sublabel": "limit",
                    "type": "float",
                    "unit": "mm"
                  }
                ]
              },
              {
                "label": "JumpDelay",
                "cells": [
                  {
                    "name": "minJumpDelay",
                    "sublabel": "min",
                    "type": "float",
                    "unit": "µs"
                  },
                  {
                    "name": "maxJumpDelay",
                    "sublabel": "max",
                    "type": "float",
                    "unit": "µs"
                  }
                ]
              },
              {
                "name": "line",
                "label": "Laser",
                "type": "line",
                "colSpan": 2
              },
              {
                "label": "Frequency",
                "cells": [
                  {
                    "name": "minFreq",
                    "sublabel": "min",
                    "type": "float",
                    "unit": "kHz",
                    "default": "1.000"
                  },
                  {
                    "name": "maxFreq",
                    "sublabel": "max",
                    "type": "float",
                    "unit": "kHz",
                    "default": "4000.000"
                  }
                ]
              },
              {
                "label": "Delay",
                "cells": [
                  {
                    "name": "onDelay",
                    "sublabel": "on",
                    "type": "float",
                    "unit": "µs"
                  },
                  {
                    "name": "offDelay",
                    "sublabel": "off",
                    "type": "float",
                    "unit": "µs"
                  },
                  {
                    "name": "endDelay",
                    "sublabel": "end",
                    "type": "float",
                    "unit": "µs"
                  },
                  {
                    "name": "polygon",
                    "sublabel": "polygon",
                    "type": "float",
                    "unit": "µs"
                  }
                ]
              },
              {
                "label": "MO",
                "cells": [
                  {
                    "name": "moRunningOnly",
                    "sublabel": "only if running",
                    "type": "bool",
                    "default": true
                  },
                  {
                    "type": "empty"
                  }
                ]
              },
              {
                "name": "line",
                "label": "I/O",
                "type": "line",
                "colSpan": 2
              },
              {
                "label": "Gpio",
                "cells": [
                  {
                    "type": "int",
                    "sublabel": "RedLight",
                    "name": "lightPin",
                    "min": -1,
                    "max": 15,
                    "default": 8
                  },
                  {
                    "type": "bool",
                    "sublabel": "invert",
                    "name": "lightPinInvert",
                    "default": false
                  },
                  {
                    "type": "int",
                    "sublabel": "FootPedal",
                    "name": "footPin",
                    "min": -1,
                    "max": 15,
                    "default": 15
                  },
                  {
                    "type": "bool",
                    "sublabel": "invert",
                    "name": "footPinInvert",
                    "default": false
                  }
                ]
              }
            ]
          }
        ]
      }

    )json";

// UVLaser
static const std::string _propertiesUV =
    R"json(
      {
        "class": "Machine",
        "rows": [
          {
            "label": " ",
            "cells": [
              {
                "type": "string",
                "name": "name",
                "sublabel": "Name"
              },
              {
                "type": "machineType",
                "name": "type",
                "sublabel": "Type"
              },
              {
                "type": "boardType",
                "name": "boardType",
                "sublabel": "Board"
              }
            ]
          },
          {
            "label": "Description",
            "cells": [
              {
                "name": "description",
                "type": "multiline"
              }
            ]
          },
          {
            "cells": [
              {
                "name": "line",
                "type": "line",
                "label": "Field"
              }
            ]
          },
          {
            "columns": 2,
            "cells": [
              {
                "name": "maxTravel",
                "label": "Travel",
                "type": "vector3d",
                "unit": "mm",
                "default": [
                  100.0,
                  100.0,
                  100.0
                ]
              },
              {
                "label": "Speed",
                "cells": [
                  {
                    "type": "float",
                    "unit": "mm/s",
                    "min": 0.0,
                    "max": 100000.0,
                    "default": 0.0,
                    "name": "travelSpeed",
                    "sublabel": "Travel"
                  },
                  {
                    "type": "float",
                    "unit": "mm/s",
                    "min": 0.0,
                    "max": 100000.0,
                    "default": 0.0,
                    "name": "framingSpeed",
                    "sublabel": "Framing"
                  }
                ]
              },
              {
                "name": "line",
                "type": "line",
                "colSpan": 2
              },
              {
                "label": "Precision",
                "cells": [
                  {
                    "type": "float",
                    "unit": "mm",
                    "min": 0.001,
                    "max": 10.0,
                    "precision": 3,
                    "default": 0.001,
                    "name": "precision",
                    "sublabel": "Prec"
                  },
                  {
                    "type": "float",
                    "unit": "mm",
                    "min": 0.001,
                    "max": 10.0,
                    "precision": 3,
                    "default": 0.001,
                    "name": "ncPrecision",
                    "sublabel": "NC Prec"
                  },
                  {
                    "type": "float",
                    "unit": "mm",
                    "min": 0.001,
                    "max": 10.0,
                    "precision": 3,
                    "default": 0.001,
                    "name": "circlePrecision",
                    "sublabel": "Circle Prec"
                  }
                ]
              },
              {
                "name": "line",
                "type": "line",
                "label": "Galvo Scanner",
                "colSpan": 2
              },
              {
                "label": "Offset",
                "cells": [
                  {
                    "name": "galvoOffset",
                    "type": "vector2d",
                    "min": -10.0,
                    "max": 10.0,
                    "default": 0.0,
                    "precision": 4,
                    "unit": "mm"
                  }
                ]
              },
              {
                "name": "galvoScale",
                "label": "Scale",
                "type": "vector2d",
                "default": [
                  1.0,
                  1.0
                ],
                "precision": 6
              },
              {
                "label": "Bulge2",
                "cells": [
                  {
                    "name": "galvoBulge",
                    "type": "vector2d",
                    "min": -5.0,
                    "max": 5.0,
                    "default": 0.0,
                    "precision": 4
                  }
                ]
              },
              {
                "label": "Bulge4",
                "cells": [
                  {
                    "name": "galvoBulge4",
                    "type": "vector2d",
                    "min": -5.0,
                    "max": 5.0,
                    "default": 0.0,
                    "precision": 6
                  }
                ]
              },
              {
                "label": "Shear",
                "cells": [
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoShearX",
                    "sublabel": "X"
                  },
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoShearY",
                    "sublabel": "Y"
                  }
                ]
              },
              {
                "label": "Trapezoid",
                "cells": [
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoTrapezoidX",
                    "sublabel": "X"
                  },
                  {
                    "type": "float",
                    "min": -100.0,
                    "max": 100.0,
                    "precision": 3,
                    "default": 0.0,
                    "name": "galvoTrapezoidY",
                    "sublabel": "Y"
                  }
                ]
              },
              {
                "label": "Rotate",
                "cells": [
                  {
                    "name": "galvoRotate",
                    "type": "float",
                    "unit": "°",
                    "min": 0.0,
                    "max": 360.0,
                    "default": 0.0,
                    "precision": 3
                  },
                  {
                    "name": "galvoSwapxy",
                    "label": "Swap XY",
                    "type": "bool",
                    "default": false
                  }
                ]
              },
              {
                "label": "Jump",
                "cells": [
                  {
                    "name": "jumpSpeed",
                    "sublabel": "speed",
                    "type": "float",
                    "unit": "mm/s"
                  },
                  {
                    "name": "jumpDistanceLimit",
                    "sublabel": "limit",
                    "type": "float",
                    "unit": "mm"
                  }
                ]
              },
              {
                "label": "JumpDelay",
                "cells": [
                  {
                    "name": "minJumpDelay",
                    "sublabel": "min",
                    "type": "float",
                    "unit": "µs"
                  },
                  {
                    "name": "maxJumpDelay",
                    "sublabel": "max",
                    "type": "float",
                    "unit": "µs"
                  }
                ]
              },
              {
                "name": "line",
                "type": "line",
                "label": "Laser",
                "colSpan": 2
              },
              {
                "label": "Frequency",
                "cells": [
                  {
                    "name": "minFreq",
                    "sublabel": "min",
                    "type": "float",
                    "unit": "kHz",
                    "default": "1.000"
                  },
                  {
                    "name": "maxFreq",
                    "sublabel": "max",
                    "type": "float",
                    "unit": "kHz",
                    "default": "4000.000"
                  }
                ]
              },
              {
                "label": "Tickle",
                "cells": [
                  {
                    "name": "ticklePulse",
                    "sublabel": "pulse",
                    "type": "float",
                    "default": "1.0",
                    "unit": "µsec"
                  },
                  {
                    "name": "tickleFreq",
                    "sublabel": "freq.",
                    "type": "float",
                    "default": "5.0",
                    "unit": "kHz"
                  }
                ]
              },
              {
                "label": "FPK",
                "cells": [
                  {
                    "type": "bool",
                    "default": false,
                    "name": "enableFPK",
                    "sublabel": "enable"
                  },
                  {
                    "type": "float",
                    "precision": 2,
                    "default": 10.0,
                    "name": "fpkStartPower",
                    "sublabel": "start"
                  },
                  {
                    "type": "float",
                    "precision": 2,
                    "default": 10.0,
                    "name": "fpkIncrement",
                    "sublabel": "inc."
                  }
                ]
              },
              {
                "name": "line",
                "type": "line",
                "label": "Z-Axis",
                "colSpan": 2
              },
              {
                "label": " ",
                "cells": [
                  {
                    "type": "bool",
                    "default": false,
                    "name": "enableZ",
                    "sublabel": "enable"
                  },
                  {
                    "type": "float",
                    "sublabel": "steps/mm",
                    "name": "stepsMMZ",
                    "min": 0.0,
                    "max": 10000.0,
                    "default": 300.0
                  }
                ]
              },
              {
                "label": "Speed",
                "cells": [
                  {
                    "type": "int",
                    "sublabel": "min",
                    "name": "minSpeedZ",
                    "min": -1,
                    "max": 15,
                    "default": 15
                  },
                  {
                    "type": "int",
                    "sublabel": "max",
                    "name": "maxSpeedZ",
                    "min": -1,
                    "max": 15,
                    "default": 15
                  }
                ]
              },
              {
                "name": "line",
                "type": "line",
                "label": "Rotary",
                "colSpan": 2
              },
              {
                "label": " ",
                "cells": [
                  {
                    "type": "bool",
                    "default": false,
                    "name": "enableR",
                    "sublabel": "enable"
                  },
                  {
                    "type": "float",
                    "sublabel": "steps/mm",
                    "name": "stepsMMR",
                    "min": 0.0,
                    "max": 10000.0,
                    "default": 300.0
                  }
                ]
              },
              {
                "label": "Speed",
                "cells": [
                  {
                    "type": "int",
                    "sublabel": "min",
                    "name": "minSpeedR",
                    "min": 1,
                    "max": 1000,
                    "default": 15
                  },
                  {
                    "type": "int",
                    "sublabel": "max",
                    "name": "maxSpeedR",
                    "min": 1,
                    "max": 1000,
                    "default": 15
                  }
                ]
              },
              {
                "name": "line",
                "type": "line",
                "label": "I/O",
                "colSpan": 2
              },
              {
                "label": " ",
                "cells": [
                  {
                    "type": "int",
                    "sublabel": "RedLight",
                    "name": "lightPin",
                    "min": -1,
                    "max": 15,
                    "default": 8
                  },
                  {
                    "type": "int",
                    "sublabel": "FootPedal",
                    "name": "footPin",
                    "min": -1,
                    "max": 15,
                    "default": 15
                  }
                ]
              }
            ]
          }
        ]
      }

    )json";

//---------------------------------------------------------
//   properties
//---------------------------------------------------------

const std::string LaserBJJCZ::properties() const {
      switch (type()) {
            case MachineType::Q_LASER: return _propertiesQ;
            case MachineType::MOPA_LASER: return _propertiesMOPA;
            case MachineType::UV_LASER: return _propertiesUV;
            case MachineType::GCODE_LASER:
            case MachineType::GCODE_MILL:
            case MachineType::UNKNOWN: return "";
            }
      }

//---------------------------------------------------------
//   listWrite
//---------------------------------------------------------

void LaserBJJCZ::gpioListWrite() {
      list_write_port(_outputPort);
      }

//---------------------------------------------------------
//   on
//    sets gpio pin "bit" to on
//---------------------------------------------------------

void LaserBJJCZ::gpioOn(int bit) {
      _outputPort |= (1 << bit);
      gpioWrite();
      }

//---------------------------------------------------------
//   off
//    sets gpio pin "bit" to off
//---------------------------------------------------------

void LaserBJJCZ::gpioOff(int bit) {
      _outputPort &= ~(1 << bit);
      gpioWrite();
      }

//---------------------------------------------------------
//   toggle
//    toggle gpio pin "bit"
//---------------------------------------------------------

void LaserBJJCZ::gpioToggle(int bit) {
      _outputPort ^= (1 << bit);
      Debug("{} = {:04x}", bit, _outputPort);
      gpioWrite();
      }

//---------------------------------------------------------
//   set
//---------------------------------------------------------

void LaserBJJCZ::gpioSet(int bit, bool on) {
      bit         = 1 << bit;
      _outputPort = on ? _outputPort | bit : _outputPort & (~bit);
      gpioWrite();
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

void LaserBJJCZ::gpioWrite() {
      write_port(_outputPort);
      // emit outputPortChanged() may be called from a background
      // thread (framing/marking).  Use QMetaObject::invokeMethod with
      // QueuedConnection to ensure the signal is delivered on the
      // GUI thread so QML bindings re-evaluate correctly.
      QMetaObject::invokeMethod(this, [this]() { emit outputPortChanged(); }, Qt::QueuedConnection);
      }

void LaserBJJCZ::gpioWrite(int data) {
      _outputPort = data;
      gpioWrite();
      }

//---------------------------------------------------------
//   setLight
//---------------------------------------------------------

void LaserBJJCZ::setLight(bool on) {
      if (lightPin() < 0) // is the pin configured?
            return;
      if (lightPinInvert())
            on = !on;
      gpioSet(lightPin(), on);
      }

//---------------------------------------------------------
//   statusFlags
//---------------------------------------------------------

LaserStatusFlags LaserBJJCZ::statusFlags() const {
      if (!send(GetStatus)) {
            Critical("failed");
            return LaserStatusFlags(0xffff);
            }
      Packet4 rv {0xffff, 0xffff, 0xffff, 0xffff};
      if (!usb->read((uchar*)rv.data(), sizeof(rv)))
            Critical("usb receive failed");
      auto status = LaserStatusFlags(rv[3]);
      return status;
      }

//---------------------------------------------------------
//   start
//    starts pipeline command processing
//---------------------------------------------------------

void CmdList::start() {
      laser->reset_list();
      index       = 0;
      packetsSend = 0;
      executing   = false;
      write(listReadyMark);
      }

//---------------------------------------------------------
//   end
//    - dont know what param means
//      0 (default)
//      1 at end of program
//---------------------------------------------------------

void CmdList::end(int param) {
      write(listEndOfList);
      if (!empty()) {
            // Only send + execute if the board is ready and not being
            // stopped.  When send() returns false the board is in an
            // abort state and sending a partial list would corrupt it.
            if (laser->send(*this)) {
                  if (!executing)
                        laser->execute_list();
                  laser->set_end_of_list(param);
                  }
            }
      else {
            // Buffer is empty (only listEndOfList was written).  Still
            // send end_of_list when the board is ready.
            if (laser->send(*this))
                  laser->set_end_of_list(param);
            }
      packetsSend = 0;
      executing   = false;
      index       = 0;
      fill(Packet6());
      }

//---------------------------------------------------------
//   CmdList::write
//---------------------------------------------------------

void CmdList::write(const Packet6& p) {
      if (index >= LIST_SIZE) {
            // If the list send was aborted (stopMarking / stopFraming),
            // do NOT send set_end_of_list / execute_list — the board is
            // being stopped and sending partial list control commands
            // would corrupt its state.  Just clear the buffer and continue
            // buffering; the abort path (stop_execute + reset_list) will
            // clean up the board.
            if (laser->send(*this)) {
                  laser->set_end_of_list(0);
                  ++packetsSend;
                  // if two packets are send to the laser we start
                  // executing the list
                  if ((packetsSend >= 2) && !executing) {
                        laser->execute_list();
                        executing = true;
                        }
                  }
            fill(Packet6());
            index = 0;
            }
      at(index++) = p; // copy packet
      }

//---------------------------------------------------------
//   distance
//---------------------------------------------------------

int LaserBJJCZ::distance(int x, int y) {
      double dx = x - currentX;
      double dy = y - currentY;
      double d  = sqrt(dx * dx + dy * dy);

      if (d < 0)
            Fatal("return negative ??");
      if (d > 0xffff)
            d = 0xffff;
      return int(d);
      }

//---------------------------------------------------------
//   computeJumpDelay
//    Compute a distance-dependent jump delay using the EzCAD
//    formula:  total = minJumpDelay + (distance/galvos * jumpDistanceTC)
//    where jumpDistanceTC = (maxJumpDelay - minJumpDelay) / jumpDistanceLimit
//    The result is clamped to [minJumpDelay, maxJumpDelay].
//    galvoDistance is in raw galvo units (0-65535).
//---------------------------------------------------------

double LaserBJJCZ::computeJumpDelay(int galvoDistance) const {
      double base     = laserValues.minJumpDelay;
      double maxDelay = laserValues.maxJumpDelay;
      double limit    = laserValues.jumpDistanceLimit;
      if (limit <= 0.0 || maxDelay <= base)
            return base;
      // Convert galvo distance to mm using galvos scale factor
      double distanceMm = galvoDistance / abs(galvos);
      if (distanceMm <= 0.0)
            return base;
      // Linear interpolation: delay increases with distance up to jumpDistanceLimit
      double tc    = (maxDelay - base) / limit;
      double delay = base + distanceMm * tc;
      return std::clamp(delay, base, maxDelay);
      }

//---------------------------------------------------------
//   move
//---------------------------------------------------------

void LaserBJJCZ::move(uint16_t x, uint16_t y) {
      uint16_t d = distance(x, y);
      if (state == LaserState::Framing)
            list_jump_delay(0);
      else
            list_jump_delay(computeJumpDelay(d));
      list.write({listJumpTo, x, y, 0, d});
      currentX = x;
      currentY = y;
      dirValid = false;
      }

//---------------------------------------------------------
//   mark
//---------------------------------------------------------

void LaserBJJCZ::mark(uint16_t x, uint16_t y) {
      double dx  = x - currentX;
      double dy  = y - currentY;
      double dir = atan2(dx, dy);

      uint16_t dv;
      if (dirValid) {
            double dr = abs(dir - lastDir);
            while (dr > (M_PI * .5))
                  dr -= (M_PI * .5);
            dv = dr * 0x10000 / M_PI;
            }
      else
            dv = 0;
      list.write({listMarkTo, x, y, dv, (uint16_t)distance(x, y)});
      currentX = x;
      currentY = y;
      lastDir  = dir;
      dirValid = true;
      }

//---------------------------------------------------------
//   writeCorrectionTable
//    create correction table from lens properties
//
//    The correction values are offsets from the nominal position.
//    The actual position plus correction value cannot exceed the
//    16 bit range and must be clamped.
//
//    HARDWARE BEHAVIOUR (verified empirically):
//    The hardware subtracts the table values from the nominal
//    galvo position without any scaling:
//        actual = nominal - corr
//    To produce a positive spot shift (push the beam outward
//    to compensate the inward lens distortion), the correction
//    values must be negative:
//        corr = -k * r² * g
//    so that  actual = nominal + k * r² * g
//    yields the desired outward correction.
//
//    The previously used factor -4 was based on the assumption
//    that the hardware divides by 4 (actual = nominal - corr/4).
//    Empirical verification with GalvoCalibration showed the
//    computed bulge values were ~4× larger than the empirically
//    determined correct values, proving the hardware does NOT
//    divide by 4.
//---------------------------------------------------------

void LaserBJJCZ::writeCorrectionTable() {
      CalData corData(machine()->getZcam());

      bool errorReadingCorFile = false;
      if (!corFile().isEmpty() && corData.read(corFile().toStdString())) {
            Debug("cor file <{}> loaded", corFile());
            }
      else {
            if (!corFile().isEmpty())
                  Critical("error reading cor file <{}>", corFile());
            errorReadingCorFile = true;
            }
      if (corFile().isEmpty() || errorReadingCorFile) {
            // < 0 Kissen
            // > 0 barrel distortion
            //
            // The correction table contains ONLY nonlinear (radial)
            // distortion terms (bulge, bulge4).  All linear corrections
            // (scale, offset, shear, trapezoid, rotation) are applied
            // in mapToGalvo() so the hardware correction table stays
            // small and the linear part can be adjusted without
            // rewriting the table.
            const double k2x = galvoBulge().x();
            const double k2y = galvoBulge().y();
            const double k4x = galvoBulge4().x();
            const double k4y = galvoBulge4().y();

            double kx       = k2x;
            double ky       = k2y;
            double k4xlocal = k4x;
            double k4ylocal = k4y;

            if (galvoSwapxy()) {
                  std::swap(kx, ky);
                  std::swap(k4xlocal, k4ylocal);
                  }

            // Factor -1: the hardware subtracts corr from the nominal
            // position (no /4 scaling).  Negative sign produces the
            // desired outward shift: actual = nominal - (-k*r²*g) = nominal + k*r²*g.
            constexpr double tableScaleFactor = -1.0;

            //            int scale = 0x10000 / 64;
            for (double y = -32; y <= 32; ++y) {
                  for (double x = -32; x <= 32; ++x) {
                        const double r2 = x * x + y * y;
                        const double r4 = r2 * r2;
                        int corrX       = int(std::lround(
                            tableScaleFactor * (kx * r2 + k4xlocal * r4 * Laser::bulge4Scale) * x));
                        int corrY       = int(std::lround(
                            tableScaleFactor * (ky * r2 + k4ylocal * r4 * Laser::bulge4Scale) * y));

                        // Avoid 16-bit signed overflow in the packed correction value.
                        constexpr int corrMin = -0x7FFF;
                        constexpr int corrMax = 0x7FFF;
                        Assert(corrX >= corrMin);
                        Assert(corrX <= corrMax);
                        Assert(corrY >= corrMin);
                        Assert(corrY <= corrMax);
                        //                        corrX                 = std::clamp(corrX, corrMin, corrMax);
                        //                        corrY                 = std::clamp(corrY, corrMin, corrMax);

                        corData.setValue(x, y, {corrX, corrY});
                        }
                  }
            }
      write_cor_table(true);
      bool first = true;
      for (auto pt : corData) {
            int x       = pt.x;
            int y       = pt.y;
            uint16_t xx = x < 0 ? 0x8000 + abs(x) : x;
            uint16_t yy = y < 0 ? 0x8000 + abs(y) : y;
            if (x == 0x8000)
                  x = 0;
            if (y == 0x8000)
                  y = 0;
            write_cor_line(xx, yy, !first);
            first = false;
            }
      }
