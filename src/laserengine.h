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
// #include <QTransform>
#include <QTimer>
#include <QElapsedTimer>
#include <QtQml/qqmlregistration.h>
#include "recipe.h"

// #include <vector>
// #include <thread>
#include "logger.h"
#include "clipper2/clipper.h"
#include "types.h"

class Usb;
class LaserEngine;
class Layer;
class LaserLayer;
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

//---------------------------------------------------------
//   FiberLaserState
//---------------------------------------------------------

class FiberLaserState
      {
      LaserEngine* laser;
      double _frequency;

    public:
      double power;
      double currentTravelSpeed;
      double delay_jump;
      double delay_on;
      double delay_off;
      double delay_poly;
      double pulseWidth;
      int lastPosX;
      int lastPosY;
      double lastDir;
      bool dirValid;
      double markSpeed;
      FiberLaserState(LaserEngine* l) : laser(l) { clear(); }
      void clear() {
            power              = -1;
            _frequency         = -1;
            markSpeed          = -1;
            delay_jump         = -1;
            delay_on           = -1;
            delay_off          = -1;
            delay_poly         = -1;
            currentTravelSpeed = -1;
            pulseWidth         = -1;
            lastPosX           = 0x8000;
            lastPosY           = 0x8000;
            lastDir            = 0.0;
            dirValid           = false;
            }
      bool setPower(double v) {
            auto rv = v != power;
            power   = v;
            return rv;
            }
      double frequency() const { return _frequency; }
      void setFrequency(double);
      void move(int x, int y);
      void mark(int x, int y);
      int distance(int x, int y);
      void setPosition(uint16_t x, uint16_t y) {
            Debug("setPos 0x{:04x} 0x{:04x}", x, y);
            lastPosX = x;
            lastPosY = y;
            }
      };

//---------------------------------------------------------
//   Command
//---------------------------------------------------------

enum Command : uint16_t {
      listJumpTo              = 0x8001,
      listEndOfList           = 0x8002,
      listLaserOnPoint        = 0x8003,
      listDelayTime           = 0x8004,
      listMarkTo              = 0x8005,
      listJumpSpeed           = 0x8006,
      listLaserOnDelay        = 0x8007,
      listLaserOffDelay       = 0x8008,
      listMarkFreq            = 0x800A,
      listMarkPowerRatio      = 0x800B,
      listMarkSpeed           = 0x800C,
      listJumpDelay           = 0x800D,
      listPolygonDelay        = 0x800F,
      listWritePort           = 0x8011,
      listMarkCurrent         = 0x8012,
      listMarkFreq2           = 0x8013,
      listFlyEnable           = 0x801A,
      listQSwitchPeriod       = 0x801B,
      listDirectLaserSwitch   = 0x801C,
      listFlyDelay            = 0x801D,
      listSetCo2FPK           = 0x801E,
      listFlyWaitInput        = 0x801F,
      listFiberOpenMO         = 0x8021,
      listWaitForInput        = 0x8022,
      listChangeMarkCount     = 0x8023,
      listSetWeldPowerWave    = 0x8024,
      listEnableWeldPowerWave = 0x8025,
      listFiberYLPMPulseWidth = 0x8026,
      listFlyEncoderCount     = 0x8028,
      listSetDaZWord          = 0x8029,
      listJptSetParam         = 0x8050,
      listReadyMark           = 0x8051,

      DisableLaser = 0x0002,
      //      WhatsThis            = 0x0003,
      EnableLaser          = 0x0004,
      ExecuteList          = 0x0005,
      SetPwmPulseWidth     = 0x0006,
      GetStatus            = 0x0007, // GetVersion?
      GetSerialNo          = 0x0009,
      GetListStatus        = 0x000A,
      GetPositionXY        = 0x000C,
      GotoXY               = 0x000D,
      LaserSignalOff       = 0x000E,
      LaserSignalOn        = 0x000F,
      WriteCorLine         = 0x0010,
      ResetList            = 0x0012,
      RestartList          = 0x0013,
      WriteCorTable        = 0x0015,
      SetControlMode       = 0x0016,
      SetDelayMode         = 0x0017,
      SetMaxPolyDelay      = 0x0018,
      SetEndOfList         = 0x0019,
      SetFirstPulseKiller  = 0x001A,
      SetLaserMode         = 0x001B,
      SetTiming            = 0x001C,
      SetStandby           = 0x001D,
      SetPwmHalfPeriod     = 0x001E,
      StopExecute          = 0x001F,
      StopList             = 0x0020,
      WritePort            = 0x0021,
      WriteAnalogPort1     = 0x0022,
      WriteAnalogPort2     = 0x0023,
      WriteAnalogPortX     = 0x0024,
      ReadPort             = 0x0025,
      SetAxisMotionParam   = 0x0026,
      SetAxisOriginParam   = 0x0027,
      AxisGoOrigin         = 0x0028,
      MoveAxisTo           = 0x0029,
      GetAxisPos           = 0x002A,
      GetFlyWaitCount      = 0x002B,
      GetMarkCount         = 0x002D,
      SetFpkParam2         = 0x002E,
      FiberPulseWidth      = 0x002F,
      FiberGetConfigExtend = 0x0030,
      InputPort =
          0x0031, //  ClearLockInputPort calls 0x04, then if EnableLockInputPort 0x02 else 0x01, GetLockInputPort
      SetFlyRes           = 0x0032,
      Fiber_SetMo         = 0x0033, // open and close set by value
      Fiber_GetStMO_AP    = 0x0034,
      GetUserData         = 0x0036,
      GetFlySpeed         = 0x0038,
      DisableZ            = 0x0039,
      EnableZ             = 0x003A,
      SetZData            = 0x003B,
      SetSPISimmerCurrent = 0x003C,
      Reset               = 0x0040,
      GetMarkTime         = 0x0041,
      SetFpkParam         = 0x0062
      };

//---------------------------------------------------------
//   Packet4
//---------------------------------------------------------

class Packet4 : public std::array<uint16_t, 4>
      {
    public:
      };

//---------------------------------------------------------
//   Packet
//---------------------------------------------------------

class Packet6 : public std::array<uint16_t, 6>
      {
    public:
      // Default: NOP command
      Packet6(uint16_t cmd = Command::listEndOfList, uint16_t a = 0, uint16_t b = 0, uint16_t c = 0,
              uint16_t d = 0, uint16_t e = 0) {
            data()[0] = cmd;
            data()[1] = a;
            data()[2] = b;
            data()[3] = c;
            data()[4] = d;
            data()[5] = e;
            }
      };

//---------------------------------------------------------
//   CmdList
//---------------------------------------------------------

static const int LIST_SIZE = 256; // number of Packet6 structures
class CmdList : public std::array<Packet6, LIST_SIZE>
      {
      int index {0};
      LaserEngine* laser;

    public:
      CmdList(LaserEngine* l) : laser(l) {}
      void write(const Packet6& p);
      void clear() {
            fill(Packet6());
            index = 0;
            }
      bool empty() const { return index == 0; }
      int size() const { return index; }
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

      //      double p1 = 0.0;
      //      double p2 = 0.0;
      //      double p3 = 0.0;
      //      double p4 = 1;
      //      double d;
      //      QTransform transform;

      FiberLaserState laserState;

      std::string source {"fiber"};
      //      double travel_speed           { 4000.0   };
      //      double framing_speed          { 6000.0   };

      int laser_pin {0};
      int light_pin {8};
      int foot_pin {15};
      double galvos;

      int pwm_pulse_width {2}; // 125  -  2
      int pwm_half_period {2}; // 125  -  2
      int standby_p1 {2000};   // 2000
      int standby_p2 {20};
      int timing_mode {1};
      int delay_mode {1};
      int laser_mode {1};
      int control_mode {0};
      bool fpk {false};

      int fpk_max_voltage {4091}; // 0xffb
      int fpk_min_voltage {1};
      int fpk_t1 {409};
      int fpk_t2 {100};

      double fly_resolution_1 {0};
      double fly_resolution_2 {94}; // 94
      double fly_resolution_3 {1000};
      double fly_resolution_4 {24}; // 25

      double delay_laser_on {100.0};  // balor: 100g
      double delay_laser_off {180.0}; // balor: 100

      double delay_polygon {20.0}; // too low will round corners (balor: 100)
      double jump_delay {200.0};

      double delay_open_mo {8.0};

      //      int timeout{1000};
      Usb* usb {nullptr};

      CmdList list;
      bool list_executing {false};
      bool canSend {true};
      int number_of_list_packets {0};
      uint16_t port_bits {0};

      volatile bool aborting {false};

      QTimer markTimer;

      double _currentPower {50.0};

      Packet4 getSerialNumber();
      Packet4 getStatus() const;
      bool send(const Packet6&) const;
      bool send(const CmdList&) const;
      bool write(Packet6& packet, int index = 0, int attempt = 0);
      Packet4 read(int index);
      bool lookupDevice();

      void set_travel_speed(double);
      void set_pulse_width(double pw);
      void set_mark_speed(double s);
      void set_delay_on(double d);
      void set_delay_off(double delay);
      void set_delay_polygon(double delay);
      void set_power(double p);

      void list_write(const Packet6&);

      void list_jump_speed(uint16_t speed);
      void list_jump(int x, int y, int angle = 0);
      void list_mark(uint16_t x, uint16_t y, uint16_t angle = 0);
      void list_jump_delay(double delay) {
            list_write({listJumpDelay, uint16_t(fabs(delay)), uint16_t(delay > 0.0 ? 0 : 0x8000)});
            }
      void list_fiber_ylpm_pulse_width(uint16_t w) { list_write({listFiberYLPMPulseWidth, 0, w}); }
      void list_write_port() { list_write({listWritePort, port_bits}); }
      void list_laser_on_point(uint16_t dwell_time) { list_write({listLaserOnPoint, dwell_time}); }
      void list_delay_time(double time);
      void list_laser_on_delay(double delay) {
            list_write({listLaserOnDelay, uint16_t(fabs(delay)), uint16_t(delay > 0 ? 0 : 0x8000)});
            }
      void list_laser_off_delay(double delay) {
            list_write({listLaserOffDelay, uint16_t(fabs(delay)), uint16_t(delay > 0 ? 0 : 0x8000)});
            }
      void list_mark_frequency(uint16_t frequency) { list_write({listMarkFreq, frequency}); }
      void list_mark_power_ratio(uint16_t power_ratio) { list_write({listMarkPowerRatio, power_ratio}); }
      void list_polygon_delay(double delay) {
            list_write({listPolygonDelay, uint16_t(fabs(delay)), uint16_t(delay > 0 ? 0 : 0x8000)});
            }
      void list_mark_current(uint16_t current) { list_write({listMarkCurrent, current}); }
      void list_mark_frequency_2(int frequency) { Fatal("not implemented"); }
      void list_fly_enable(uint16_t enabled = 1) { list_write({listFlyEnable, enabled}); }
      void list_direct_laser_switch() { Fatal("not implemented"); }
      void list_fly_delay(double delay) {
            list_write({listFlyDelay, uint16_t(fabs(delay)), uint16_t(delay > 0 ? 0 : 0x8000)});
            }
      void list_set_co2_fpk(uint16_t fpk1, uint16_t fpk2 = 0) { list_write({listSetCo2FPK, fpk1, fpk2}); }
      void list_fly_wait_input() { list_write({listFlyWaitInput}); }
      void list_fiber_open_mo(uint16_t open_mo) { list_write({listFiberOpenMO, open_mo}); }
      void list_wait_for_input(uint16_t mask, uint16_t level) { list_write({listWaitForInput, mask, level}); }
      void list_change_mark_count(uint16_t count) { list_write({listChangeMarkCount, count}); }
      void list_set_weld_power_wave(uint16_t wpw) { list_write({listSetWeldPowerWave, wpw}); }
      void list_enable_weld_power_wave(uint16_t enabled) { list_write({listEnableWeldPowerWave, enabled}); }
      void list_fly_encoder_count(uint16_t count) { list_write({listFlyEncoderCount, count}); }
      void list_set_da_z_word(uint16_t word) { list_write({listSetDaZWord, word}); }
      void list_jpt_set_param(uint16_t param) { list_write({listJptSetParam, param}); }
      void list_ready() { list_write({listReadyMark}); }
      void list_end_of_list() { list_write({listEndOfList}); }
      void list_mark_speed(uint16_t speed) {
            if (speed > 0xFFFF)
                  speed = 0xFFFF;
            list_write({listMarkSpeed, speed});
            }
      bool is_busy() const { return statusFlags().isBusy(); }
      bool is_ready() const { return statusFlags().isReady(); }
      bool is_axis() const { return statusFlags().isAxis(); }
      bool is_ready_and_not_busy() const {
            auto s = statusFlags();
            return s.isReady() && !s.isBusy();
            }
      void wait_finished() const;
      void wait_axis() const;
      void wait_ready();
      void wait_idle() const;
      void port_on(int bit) { port_bits |= (1 << bit); }
      void port_off(int bit) { port_bits &= ~(1 << bit); }
      bool is_port(int bit) { return port_bits & (1 << bit); }
      void port_set(int mask, int values) {
            port_bits &= ~mask;
            port_bits |= (values & mask);
            }
      Packet4 gotoXY(uint16_t x, uint16_t y) { return command({GotoXY, x, y}); }
      //      Packet4 whatsThis() { return command({WhatsThis}); }
      Packet4 setFirstPulseKiller(uint16_t v) { return command({SetFirstPulseKiller, v}); }
      Packet4 disable_laser() { return command({DisableLaser}); }
      Packet4 enable_laser() { return command({EnableLaser}); }
      Packet4 execute_list() { return command({ExecuteList}); }
      Packet4 set_pwm_pulse_width(uint16_t pw) { return command({SetPwmPulseWidth, pw}); }
      LaserStatusFlags statusFlags() const {
            auto status = LaserStatusFlags(command({GetStatus})[3]);
            // Debug("{}", status);
            return status;
            }
      Packet4 get_serial_number() { return command({GetSerialNo}); }
      Packet4 get_list_status() { return command({GetListStatus}); }
      Packet4 get_position_xy() { return command({GetPositionXY}); }
      Packet4 laser_signal_off() { return command({LaserSignalOff}); }
      Packet4 laser_signal_on() { return command({LaserSignalOn}); }
      Packet4 reset_list() { return command({ResetList}); }
      Packet4 restart_list() { return command({RestartList}); }
      Packet4 write_cor_table(bool table = true) { return command({WriteCorTable, uint16_t(table)}); }
      Packet4 set_control_mode(uint16_t mode) { return command({SetControlMode, mode}); }
      Packet4 set_delay_mode(uint16_t mode) { return command({SetDelayMode, mode}); }
      Packet4 set_max_poly_delay(uint16_t delay) {
            return command({SetMaxPolyDelay, uint16_t(fabs(delay)), uint16_t(delay > 0 ? 0 : 0x8000)});
            }
      void set_end_of_list(uint16_t end) { command({SetEndOfList, end, 0, 0, 0, 0}); }
      //      Packet4 set_end_of_list(uint16_t end)                 { return command({SetEndOfList, end}); }
      Packet4 set_laser_mode(uint16_t mode) { return command({SetLaserMode, mode}); }
      Packet4 set_timing(uint16_t timing) { return command({SetTiming, timing}); }
      Packet4 set_standby(uint16_t a, uint16_t b) { return command({SetStandby, a, b}); }
      Packet4 set_pwm_half_period(uint16_t pwm_half_period) {
            return command({SetPwmHalfPeriod, pwm_half_period});
            }
      Packet4 stop_execute() { return command({StopExecute}); }
      Packet4 stop_list() { return command({StopList}); }
      Packet4 write_port() { return command({WritePort, port_bits}); }
      Packet4 write_analog_port_1(uint16_t port) { return command({WriteAnalogPort1, port}); }
      Packet4 write_analog_port_2(uint16_t port) { return command({WriteAnalogPort2, port}); }
      Packet4 write_analog_port_x(uint16_t port) { return command({WriteAnalogPortX, port}); }
      Packet4 read_port() { return command({ReadPort}); }
      Packet4 axis_go_origin(uint16_t p0 = 0, uint16_t p1 = 0) { return command({AxisGoOrigin, p0, p1}); }
      Packet4 get_axis_pos(uint16_t index = 0) { return command({GetAxisPos, index}); }
      Packet4 get_fly_wait_count() { return command({GetFlyWaitCount}); }
      Packet4 get_mark_count() { return command({GetMarkCount}); }
      void set_fiber_mo(uint16_t mo) { command({Fiber_SetMo, mo, 0, 0, 0, 0}); } // 0 - close, 1 - open
      //      Packet4 set_fiber_mo(uint16_t mo)                     { return command({Fiber_SetMo, mo}); }
      Packet4 get_fiber_st_mo_ap() { return command({Fiber_GetStMO_AP}); }
      Packet4 enable_z() { return command({EnableZ}); }
      Packet4 disable_z() { return command({DisableZ}); }
      Packet4 set_z_data(uint16_t zdata) { return command({SetZData, zdata}); }
      Packet4 set_spi_simmer_current(uint16_t current) { return command({SetSPISimmerCurrent, current}); }
      Packet4 reset() { return command({Reset}); }
      Packet4 get_fly_speed() { return command({GetFlySpeed}); }
      Packet4 fiber_pulse_width(uint16_t v) { return command({FiberPulseWidth, v}); }
      Packet4 get_fiber_config_extend() { return command({FiberGetConfigExtend}); }
      Packet4 input_port(uint16_t port) { return command({InputPort, port}); }
      Packet4 clear_lock_input_port() { return command({InputPort, 0x04}); }
      Packet4 enable_lock_input_port() { return command({InputPort, 0x02}); }
      Packet4 disable_lock_input_port() { return command({InputPort, 0x01}); }
      Packet4 get_input_port() { return command({InputPort}); }
      Packet4 get_mark_time() { return command({GetMarkTime, 3}); }
      Packet4 get_user_data() { return command({GetUserData}); }
      Packet4 move_axis_to(uint16_t p0, uint16_t p1 = 0, uint16_t p2 = 0, uint16_t p3 = 0) {
            return command({MoveAxisTo, p0, p1, p2, p3});
            }
      Packet4 set_pfk_param_2(uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4) {
            return command({SetFpkParam2, p1, p2, p3, p4});
            }
      Packet4 set_fly_res(uint16_t f1, uint16_t f2, uint16_t f3, uint16_t f4) {
            return command({SetFlyRes, f1, f2, f3, f4});
            }
      Packet4 write_cor_line(uint16_t dx, uint16_t dy, uint16_t non_first) {
            return command({WriteCorLine, dx, dy, non_first, 0, 0}, false);
            }
      Packet4 set_axis_motion_param(uint16_t p0 = 0, uint16_t p1 = 0, uint16_t p2 = 0, uint16_t p3 = 0) {
            return command({SetAxisMotionParam, p0, p1, p2, p3});
            }
      Packet4 set_axis_origin_param(uint16_t p0 = 0, uint16_t p1 = 0, uint16_t p2 = 0, uint16_t p3 = 0) {
            return command({SetAxisOriginParam, p0, p1, p2, p3});
            }
      void writeCorrectionTable();

      Packet4 command(Packet6 data, bool read = true) const;
      void light_on(bool use_list);
      void light_off(bool use_list);
      //      QVector2D barrelCorrection(const QVector2D& v);

      PathsD frameFixture(Fixture* fixture, FramingType);

      void initPosition();

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
      void list_end();
      void list_qswitch_period(uint16_t qswitch) { list_write({listQSwitchPeriod, qswitch}); }
      void markLines(PathsD&, bool reverse);
      void mark(double x, double y);
      void setLaser(const LaserParameterSet&);

    public:
      LaserEngine(ZCam* w, QObject* parent = nullptr);
      ~LaserEngine();
      bool init(bool dryRun);
      void exit();

      void abort(bool dummyPacket = true);
      void setAbortFlag() { aborting = true; }

      friend class CmdList;
      friend class FiberLaserState;
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
      QStringList laserPulseList() const;

      void stop();

      bool startFraming();
      void stopFraming();

      void startMarking();
      void stopMarking();
      void endMarking();
      void mark(const PathD&);
      void move(double x, double y);
      void markLayer(const LaserPath& path, const LaserParameterSet& sl);
      LaserPosition mapToGalvo(double, double);
      };

//---------------------------------------------------------
//   CmdList::write
//---------------------------------------------------------

inline void CmdList::write(const Packet6& p) {
      if (index >= LIST_SIZE)
            laser->list_end();
      at(index++) = p;
      }

extern void dump(Packet6* p, bool single);
extern PathsD dotCorrection(const PathsD& paths, double offset);
