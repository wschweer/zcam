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
#include "laserengine.h"
#include "usb.h"
#include "layer.h"
#include "zcam.h"
#include "project.h"
#include "types.h"
//#include "clipper.h"
// #include "cal.h"

using namespace Clipper2Lib;

static const int VENDOR  = 0x9588;
static const int PRODUCT = 0x9899;
// const static double XSCALE = 316.17;
// const static double YSCALE = 293.15;

//---------------------------------------------------------
//   LaserParameterSet
//---------------------------------------------------------

LaserParameterSet::LaserParameterSet(const LaserPass* s) {
      power       = s->power();
      speed       = s->speed();
      travelSpeed = s->travelSpeed();
      frequency   = s->frequency();
      pulseWidth  = s->pulseWidth();
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

//---------------------------------------------------------
//   LaserCmdList
//---------------------------------------------------------

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
      int n       = data[5] ? 6 : 5; // data[5] is always zero
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
//   LaserEngine
//---------------------------------------------------------

LaserEngine::LaserEngine(ZCam* w, QObject* parent) : QObject(parent), laserState(this), list(this), zcam(w) {
      usb = new Usb();
      laserState.clear();
      }

LaserEngine::~LaserEngine() {
      delete usb;
      }

//---------------------------------------------------------
//   init
//---------------------------------------------------------

bool LaserEngine::init(bool _dryRun) {
      setDryRun(_dryRun);

      try {
            if (!dryRun())
                  usb->lookupDevice(VENDOR, PRODUCT);
            usb->open(dryRun());
            }
      catch (std::string s) {
            Debug("======init usb failed (dryRun={}): {}", dryRun(), s);
            return false;
            }

      Machine* machine = zcam->project() ? zcam->project()->machine() : nullptr;
      if (!machine) {
            Critical("no laser machine configured");
            usb->close();
            return false;
            }

      // galvo range -32768  --  32767
      double xScale = machine->galvoScale().x() / 100.0; // m->scale() is in %
      double yScale = machine->galvoScale().y() / 100.0;
      //      double rotate = machine->galvoRotate();
      xScale = xScale * 0x10000 / machine->maxTravel().x();
      yScale = yScale * 0x10000 / machine->maxTravel().y();

      /*      transform = QTransform();
      if (machine->swapxy()) {
            rotate    += -90.0;
            xScale    *= -1;
            transform.rotate(rotate);
            transform.scale(yScale, xScale);
                              }
      else {
            transform.scale(xScale, yScale);
                              }
      transform.translate(0x8000, 0x8000);
  */
      // for correct speed calculation:
      galvos = (abs(xScale) + abs(yScale)) * .5;
      Debug("native scale {:.2f} {:.2f} galvos(scale): {}", xScale, yScale,
            galvos); // was wrong: 595.78, should be 411.25?

      getSerialNumber();
      port_bits = 0;
      statusFlags();
      reset();
      usleep(1000 * 100); // 50ms
      if (!statusFlags().isReady()) {
            Critical("laser not ready");
            usb->close();
            return false;
            }
      writeCorrectionTable();
      enable_laser();
      set_control_mode(0);                 // 0
      set_laser_mode(laser_mode);          // 1
      set_delay_mode(delay_mode);          // 1
      set_timing(timing_mode);             // 1
      set_standby(standby_p1, standby_p2); // 2000 20
      setFirstPulseKiller(0xc8);

      set_pwm_half_period(pwm_half_period); // 2
      set_pwm_pulse_width(pwm_pulse_width); // 2

      set_fiber_mo(0); // Close
      set_pfk_param_2(fpk_max_voltage, fpk_min_voltage, fpk_t1, fpk_t2);
      // set_fly_res(0, 0x5e, 0x3e8, 0x18);
      // set_fly_res(0, 0x63, 0x3e8, 0x19);
      set_fly_res(0, 99, 1000, 25);
      enable_z();
      //      write_analog_port_1(0xccb); // 0x7FF
      write_analog_port_1(0x7ff); // 0x7FF
      enable_z();

      gotoXY(0x8000, 0x8000);

      //      port_on(light_pin);
      //      write_port();
      list_executing         = false;
      number_of_list_packets = 0;
      reset_list();
      initPosition();

      //      set_pfk_param_2(fpk_max_voltage, fpk_min_voltage, fpk_t1, fpk_t2);
      //      enable_z();

      usleep(1000 * 50); // 50ms
      return true;
      }

//---------------------------------------------------------
//   initPosition
//---------------------------------------------------------

void LaserEngine::initPosition() {
      auto d = get_position_xy();
      gotoXY(d[1] + 1, d[2] + 1);
      };

//---------------------------------------------------------
//   getSerialNumber
//---------------------------------------------------------

Packet4 LaserEngine::getSerialNumber() {
      return command({GetSerialNo});
      }

//---------------------------------------------------------
//   command
//---------------------------------------------------------

Packet4 LaserEngine::command(Packet6 data, bool read) const {
      if (aborting)
            return {0xffff, 0xffff, 0xffff, 0xffff};
      if (!send(data))
            return {0xffff, 0xffff, 0xffff, 0xffff};
      if (read) {
            Packet4 rv;
            if (!usb->read((uchar*)rv.data(), sizeof(rv)))
                  Critical("usb receive failed");
            //            if (rv[0] != 0xffff || rv[1] != 0xffff || rv[2] != 0xffff || rv[3] != 0xffff)
            //                  Debug("************<{}> return {:04x} {:04x} {:04x} {:04x}", cmdName(data[0]), rv[0], rv[1], rv[2], rv[3]);
            return rv;
            }
      return {0xffff, 0xffff, 0xffff, 0xffff};
      }

//---------------------------------------------------------
//   send
//---------------------------------------------------------

bool LaserEngine::send(const CmdList& data) const {
      if (aborting)
            return false;
      if (!usb->write((uchar*)data[0].data(), LIST_SIZE * 12)) {
            Critical("usb send failed");
            return false;
            }
      return true;
      }

//---------------------------------------------------------
//   send
//---------------------------------------------------------

bool LaserEngine::send(const Packet6& data) const {
      if (aborting)
            return false;
      if (!usb->write((uchar*)data.data(), sizeof(Packet6))) {
            Critical("usb send failed");
            return false;
            }
      return true;
      }

//---------------------------------------------------------
//   set_travel_speed
//---------------------------------------------------------

void LaserEngine::set_travel_speed(double speed) {
      if (laserState.currentTravelSpeed == speed)
            return;
      list_jump_speed(uint16_t(speed * galvos * 0.001));
      laserState.currentTravelSpeed = speed;
      }

//---------------------------------------------------------
//   set_mark_speed
//---------------------------------------------------------

void LaserEngine::set_mark_speed(double speed) {
      Debug("{}", speed);
      if (laserState.markSpeed == speed)
            return;
      laserState.markSpeed = speed;
      list_mark_speed(int(speed * abs(galvos) * 0.001));
      }

//---------------------------------------------------------
//   set_delay_on
//---------------------------------------------------------

void LaserEngine::set_delay_on(double delay) {
      if (laserState.delay_on == delay)
            return;
      list_laser_on_delay(delay);
      laserState.delay_on = delay;
      }

//---------------------------------------------------------
//   set_delay_off
//---------------------------------------------------------

void LaserEngine::set_delay_off(double delay) {
      if (laserState.delay_off == delay)
            return;
      list_laser_off_delay(delay);
      laserState.delay_off = delay;
      }

//---------------------------------------------------------
//   set_delay_polygon
//---------------------------------------------------------

void LaserEngine::set_delay_polygon(double delay) {
      if (laserState.delay_poly == delay)
            return;
      list_polygon_delay(delay);
      laserState.delay_poly = delay;
      }

//---------------------------------------------------------
//   set_power
//    Accepts power in percent, automatically converts to power command.
//---------------------------------------------------------

void LaserEngine::set_power(double power) {
      if (_testMode)
            power = 15.0;

      if (laserState.setPower(power))
            list_mark_current(uint16_t(round(power * 0xFFF / 100.0)));
      }

//---------------------------------------------------------
//   set_pulse_width
//---------------------------------------------------------

void LaserEngine::set_pulse_width(double pw) {
      if (laserState.pulseWidth == pw)
            return;
      list_fiber_open_mo(0);
      list_fiber_ylpm_pulse_width(pw);
      laserState.pulseWidth = pw;
      list_delay_time(10000);
      list_fiber_open_mo(1);
      //      list_delay_time(800);
      list_delay_time(10000);
      }

//---------------------------------------------------------
//   list_jump_speed
//---------------------------------------------------------

void LaserEngine::list_jump_speed(uint16_t speed) {
      if (speed > 0xffff) {
            Critical("jump speed too hight: {}", speed);
            speed = 0xffff;
            }
      list_write({listJumpSpeed, speed});
      }

//---------------------------------------------------------
//   list_write
//---------------------------------------------------------

void LaserEngine::list_write(const Packet6& p) {
      list.write(p);
      }

//---------------------------------------------------------
//   list_end
//---------------------------------------------------------

void LaserEngine::list_end() {
      //      Debug("--{} packets {}", list.size(), number_of_list_packets);
      if (list.empty()) // if list is empty
            return;
      for (int i = 0; !is_ready(); ++i) {
            if (aborting)
                  return;
            usleep(1000 * 10); // 10ms
            if (!(i % 200) && i > 0)
                  Critical("busy...{}", i / 200);
            }
      send(list);
      set_end_of_list(0);
      ++number_of_list_packets;
      list.clear();
      if ((number_of_list_packets > 2) && !list_executing) {
            execute_list();
            list_executing = true;
            }
      }

//---------------------------------------------------------
//   light_off
//---------------------------------------------------------

void LaserEngine::light_off(bool use_list) {
      if (!is_port(light_pin)) // Was already off.
            return;
      port_off(light_pin);
      if (use_list)
            list_write_port();
      else
            write_port();
      }

//---------------------------------------------------------
//   light_on
//---------------------------------------------------------

void LaserEngine::light_on(bool use_list) {
      if (is_port(light_pin)) // Was already on.
            return;
      port_on(light_pin);
      if (use_list)
            list_write_port();
      else
            write_port();
      }

//---------------------------------------------------------
//   wait_finished
//---------------------------------------------------------

void LaserEngine::wait_finished() const {
      for (int i = 0;; ++i) {
            if (aborting)
                  return;
            auto data   = command({GetStatus});
            auto status = LaserStatusFlags(data[3]);
            if (status.isReady() && !status.isBusy())
                  break;
            usleep(1000 * 10); // 10ms
            if (!(i % 200))
                  Debug("...{}", i / 200);
            }
      }

//---------------------------------------------------------
//   wait_axis
//---------------------------------------------------------

void LaserEngine::wait_axis() const {
      for (int i = 0; is_axis(); ++i) {
            usleep(1000 * 10); // 10ms
            if (aborting)
                  return;
            if (!(i % 200))
                  Debug("...{}", i / 200);
            }
      }

//---------------------------------------------------------
//   wait_ready
//---------------------------------------------------------

void LaserEngine::wait_ready() {
      for (int i = 0; !is_ready(); ++i) {
            usleep(1000 * 10); // 10ms
            if (aborting) {
                  Debug("===========================aborting");
                  return;
                  }
            if (!(i % 200))
                  Critical("...{}", i / 200);
            }
      }

//---------------------------------------------------------
//   wait_idle
//---------------------------------------------------------

void LaserEngine::wait_idle() const {
      for (int i = 0; is_busy(); ++i) {
            usleep(1000 * 10);
            if (aborting) {
                  Debug("===========================aborting");
                  return;
                  }
            if (!(i % 200))
                  Critical("...{} {}", i / 200, bool(aborting));
            }
      }

//---------------------------------------------------------
//   exit
//---------------------------------------------------------

void LaserEngine::exit() {
      stop_list();
      stop_execute();
      aborting = true;
      LaserEngine::abort(false);
      stop();
      usb->close();
      }

//---------------------------------------------------------
//   abort
//---------------------------------------------------------

void LaserEngine::abort(bool dummyPacket) {
      Debug("abort {}", dummyPacket);
      command({StopExecute});
      command({StopList});
      set_fiber_mo(0);
      reset_list();
      if (dummyPacket) {
            list.clear();
            list_end_of_list(); // Ensure packet is sent on end.
            list_end();
            if (!list_executing)
                  execute_list();
            list_executing         = false;
            number_of_list_packets = 0;
            set_fiber_mo(0);
            port_off(laser_pin);
            write_port();
            }
      }

//---------------------------------------------------------
//   distance
//    compute distance between x/y and last_x/last_y
//---------------------------------------------------------

int FiberLaserState::distance(int x, int y) {
      double dx = x - lastPosX;
      double dy = y - lastPosY;
      double d  = sqrt(dx * dx + dy * dy);

      if (d < 0)
            Fatal("return negative ??");
      if (d > 0xffff)
            d = 0xffff;
      return int(d);
      }

//---------------------------------------------------------
//   mark
//---------------------------------------------------------

void LaserEngine::mark(double x, double y) {
      LaserPosition pos = mapToGalvo(x, y);
      Debug("mark {} {}", pos.x, pos.y);
      laserState.mark(pos.x, pos.y);
      }

//---------------------------------------------------------
//   mark
//---------------------------------------------------------

void FiberLaserState::mark(int x, int y) {
      double dx  = x - lastPosX;
      double dy  = y - lastPosY;
      double dir = atan2(dx, dy);
      double dr  = abs(dir - lastDir);
      while (dr > (M_PI * .5))
            dr -= (M_PI * .5);
      uint16_t dv = dirValid ? dr * 0x10000 / M_PI : 0;

      laser->list_write({listMarkTo, (uint16_t)x, (uint16_t)y, dv, (uint16_t)distance(x, y)});
      lastPosX = x;
      lastPosY = y;
      lastDir  = dir;
      dirValid = true;
      }

//---------------------------------------------------------
//   lineLength
//---------------------------------------------------------

//static double lineLength(const PointD& p1, const PointD& p2) {
//      auto d3 = p2 - p1;
//      return sqrt(d3.x * d3.x + d3.y * d3.y);
//      }

//-------------------------------------------------------------------
//   markLines
//    mark one scanline which may contain several line segments
//-------------------------------------------------------------------

void LaserEngine::markLines(PathsD& pl, bool reverse) {
      if (pl.empty())
            return;

      PointD current = pl.front().front();

      for (const auto& p : pl) {
            auto p1 = p[0];
            auto p2 = p[1];

            //            double l1 = lineLength(current, p1);
            //            double l2 = lineLength(current, p2);
            //            double l3 = lineLength(p1, p2);

            //            int row = int(p1.z);
            //            if (l2 < l1)
            //                 Debug("bad line orientation {} {}", row, row & 0x1);

            move(p1.x, p1.y);
            mark(p2.x, p2.y);

            current = p2;
            list_delay_time(10);
            }
      }

//---------------------------------------------------------
//   setFrequency
//    f in kHz
//---------------------------------------------------------

void FiberLaserState::setFrequency(double f) {
      if (f != _frequency) {
            laser->list_qswitch_period(uint16_t(round(20000.0 / f)) & 0xffff);
            _frequency = f;
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
//    PathD represents a line strip
//---------------------------------------------------------

void LaserEngine::mark(const PathD& p) {
      bool first     = true;
      bool firstMove = true;
      for (const auto& pt : p) {
            if (first) {
                  move(pt.x, pt.y);
                  first = false;
                  }
            else {
                  if (firstMove) {
                        // give laser more time to startup
                        //laserState.mark(laserState.lastPosX+1, laserState.lastPosY+1);
                        set_fiber_mo(1);
                        list_delay_time(1000);
                        list_laser_on_point(10); // dwell 100us
                        mark(pt.x, pt.y);
                        }
                  else
                        mark(pt.x, pt.y);
                  firstMove = false;
                  }
            }
      }

//---------------------------------------------------------
//   pulseList
//---------------------------------------------------------

QStringList LaserEngine::laserPulseList() const {
      QStringList sl;
      for (const auto& p : _pulseTable)
            sl << QString("%1").arg(p.pulseWidth);
      return sl;
      }

//---------------------------------------------------------
//   setLaser
//---------------------------------------------------------

void LaserEngine::setLaser(const LaserParameterSet& l) {
      set_pulse_width(l.pulseWidth);

      Debug("{}% markSpeed {} travelSpeed {} {}kHz {}ns delay {} {} {}", l.power, l.speed, l.travelSpeed,
            l.frequency, l.pulseWidth, delay_laser_on, delay_laser_off, delay_polygon);

      if (l.speed == 0)
            Fatal("zero speed");
      laserState.setFrequency(l.frequency);
      set_power(l.power);

      set_travel_speed(l.travelSpeed);
      set_mark_speed(l.speed);
      set_delay_on(delay_laser_on);
      set_delay_off(delay_laser_off);
      set_delay_polygon(delay_polygon);

      list_jump_delay(40);
      list_delay_time(100);
      }

//---------------------------------------------------------
//   startFraming
//    start framing from idle state
//---------------------------------------------------------

bool LaserEngine::startFraming() {
      aborting = false;
      laserState.clear();
      reset_list();
      list_ready(); // at the start of every new command list

      port_bits = 0;
      port_off(laser_pin);
      port_on(light_pin);
      list_write_port();

      list_jump_delay(0);
      set_delay_polygon(0);
      Machine* m = zcam->project() ? zcam->project()->machine() : nullptr;
      if (!m)
            return true;
      int speed = m->framingSpeed();
      set_travel_speed(speed);
      return true;
      }

//---------------------------------------------------------
//   stopFraming
//    stop framing and go to idle
//---------------------------------------------------------

void LaserEngine::stopFraming() {
      stop_list();
      stop_execute();
      aborting = true;
      Debug("=========== set aborting to true");
      LaserEngine::abort(false);
      //      stop();
      }

//---------------------------------------------------------
//   startMarking
//---------------------------------------------------------

void LaserEngine::startMarking() {
      aborting = false;
      laserState.clear();
      reset_list();
      list_ready(); // start a new command list

      port_bits = 0;
      port_on(laser_pin);
      write_port();
      port_bits = 0;
      write_port();

      initPosition();
      port_off(laser_pin);
      write_port();

      set_fiber_mo(1);          // 0 close, 1 open
      list_delay_time(8 * 100); // 8ms
      }

//---------------------------------------------------------
//   stopMarking
//---------------------------------------------------------

void LaserEngine::stopMarking() {
      Debug("stop marking");
      stop_list();
      stop_execute();
      LaserEngine::abort(false);
      stop();
      }

//---------------------------------------------------------
//   endMarking
//    all markings are send to the list
//    flush list
//    wait for finish
//---------------------------------------------------------

void LaserEngine::endMarking() {
      list_end();
      if (!list_executing && number_of_list_packets)
            execute_list();
      wait_finished();
      list_executing         = false;
      number_of_list_packets = 0;
      set_fiber_mo(0);
      port_off(laser_pin);
      port_off(light_pin);
      write_port();
      }

//---------------------------------------------------------
//   stop
//---------------------------------------------------------

void LaserEngine::stop() {
      list_end_of_list();
      list_end();
      if (!list_executing && number_of_list_packets)
            execute_list();
      wait_idle();
      list.clear();
      list_executing         = false;
      number_of_list_packets = 0;
      set_fiber_mo(0);
      port_off(laser_pin);
      port_off(light_pin);
      write_port();
      }

//---------------------------------------------------------
//   move (goto)
//---------------------------------------------------------

void LaserEngine::move(double x, double y) {
      LaserPosition pos = mapToGalvo(x, y);
      laserState.move(pos.x, pos.y);
      }

//---------------------------------------------------------
//   mapToGalvo
//    mm to galvo coordinates
//    return false if result is out of range
//---------------------------------------------------------

LaserPosition LaserEngine::mapToGalvo(double x, double y) {
      Machine* machine = zcam->project() ? zcam->project()->machine() : nullptr;
      if (!machine)
            return LaserPosition(0, 0);
      double xScale = machine->galvoScale().x() / 100.0; // m->scale() is in %
      double yScale = machine->galvoScale().y() / 100.0;
      double maxX   = machine->maxTravel().x();
      double maxY   = machine->maxTravel().y();
      xScale        = xScale * 0x10000 / maxX;
      yScale        = yScale * 0x10000 / maxY;

      // Input coordinates are in [0, maxTravel] range (corner origin).
      // Shift to centered range [-maxTravel/2, +maxTravel/2] then scale
      // and add galvo center offset (0x8000) to map to [0, 0xffff].
      double xc = x - maxX / 2.0;
      double yc = y - maxY / 2.0;

      double rawX, rawY;
      if (machine->galvoSwapxy()) {
            rawX = trunc(yc * yScale + 0x8000);
            rawY = trunc(xc * xScale + 0x8000);
            }
      else {
            rawX = trunc(xc * xScale + 0x8000);
            rawY = trunc(yc * yScale + 0x8000);
            }
      if (rawX < 0.0 || rawX > 0xffff || rawY < 0.0 || rawY > 0xffff) {
            Critical("position out of range 0x{:04x} {} ----  0x{:04x} {}",
                     (unsigned)std::clamp(rawX, 0.0, (double)0xffff), x,
                     (unsigned)std::clamp(rawY, 0.0, (double)0xffff), y);
            return LaserPosition((unsigned)std::clamp(rawX, 0.0, (double)0xffff),
                                 (unsigned)std::clamp(rawY, 0.0, (double)0xffff));
            }
      return LaserPosition((unsigned)rawX, (unsigned)rawY);
      }

//---------------------------------------------------------
//   move
//---------------------------------------------------------

void FiberLaserState::move(int x, int y) {
      //      Debug("{} {}", x, y);
      int d = distance(x, y);
      laser->list_write({listJumpTo, (uint16_t)x, (uint16_t)y, 0, (uint16_t)d});
      lastPosX = x;
      lastPosY = y;
      dirValid = false;
      }

//---------------------------------------------------------
//   list_delay_time
//    delay time in 10 microseconds units
//---------------------------------------------------------

void LaserEngine::list_delay_time(double time) {
      list_write({listDelayTime, uint16_t(time)});
      }

//---------------------------------------------------------
//   markLayer
//    Marks one sublayer with fixed laser settings
//---------------------------------------------------------

void LaserEngine::markLayer(const LaserPath& path, const LaserParameterSet& sl) {
      setLaser(sl); // configure the laser engine

      bool first  = true;
      bool moving = true;

      for (const auto& p : path) {
            if (p.type == LaserPathElementType::MoveTo) {
                  move(p.x(), p.y());
                  moving = true;
                  }
            else if (first) {
                  // first element in the list must be a move
                  Critical("First segment in layer is not a move");
                  move(p.x(), p.y());
                  }
            else {
                  if (moving) {
                        // give laser more time to startup
                        list_delay_time(100);
                        list_laser_on_point(10); // dwell 100us
                        }
                  mark(p.x(), p.y());
                  moving = false;
                  }
            first = false;
            list_delay_time(10);
            }
      }

//---------------------------------------------------------
//   writeCorrectionTable
//---------------------------------------------------------

void LaserEngine::writeCorrectionTable() {
      Machine* machine = zcam->project() ? zcam->project()->machine() : nullptr;
#if 0
      QString corFile = machine->corfile();
      CalData corData(zcam);
      double xScale = 0x10000 / machine->travelX();
      double yScale = 0x10000 / machine->travelY();

      bool errorReadingCorFile = false;
      if (!corFile.isEmpty() && corData.read(corFile.toStdString()))
            Debug("cor file <{}> loaded", corFile);
      else {
            Critical("error reading cor file <{}>", corFile);
            errorReadingCorFile = true;
                              }
      if (corFile.isEmpty() || errorReadingCorFile) {
            Debug("no cor file for laser <{}> available", machine->name());

            //*******************************************************
            //    create correction table from lens properties
            //*******************************************************

            QVector2D barrel = (zcam->project() && zcam->project()->machine()) ? zcam->project()->machine()->barrel() : QVector2D();
            double kx = barrel.x();
            double ky = barrel.y();

            for (double y = -32; y <= 32; ++y) {
                  for (double x = -32; x <= 32; ++x) {
                        double r2 = x * x + y * y;
                        int corrX = ((x / (1.0 + kx * 0.00001 * r2)) - x) * xScale;
                        int corrY = ((y / (1.0 + ky * 0.00001 * r2)) - y) * yScale;
                        if (abs(corrX) >= 0x8000 || abs(corrY) >= 0x8000) {
                              Critical("{}:{} overflow {:x} {:x}   {}*{} {}*{}", x, y, corrX, corrY, y, r2 * kx, x, r2 * ky);
                              return;
                                                }
                        corData.setValue(x, y, { corrX, corrY });
                                          }
                                    }
                              }
      write_cor_table(true);
      bool first = true;
      for (auto pt : corData) {
            int x = pt.x;
            int y = pt.y;
            x = x < 0 ? 0x8000 - x : x;
            y = y < 0 ? 0x8000 - y : y;
            if (x == 0x8000)
                  x = 0;
            if (y == 0x8000)
                  y = 0;
            write_cor_line(x, y, !first);
            first = false;
                              }
#endif
      }
