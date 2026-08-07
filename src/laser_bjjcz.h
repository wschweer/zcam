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

#include "logger.h"
#include "laser.h"

class Usb;
class Group;
class Recipe;
class LaserSettings;
class Fixture;
class ZCam;
class LaserBJJCZ;

using PathsD = Clipper2Lib::PathsD;
using PathD  = Clipper2Lib::PathD;

//---------------------------------------------------------
//   BjjczStatus
//---------------------------------------------------------

enum class BjjczStatus : int {
      LIST  = 1, // cmd list underflow
      BUSY  = 3,
      ERROR = 5, // out of range?
      READY = 6,
      AXIS  = 7
      };

//---------------------------------------------------------
//   Status
//---------------------------------------------------------

class LaserStatusFlags
      {
      uint16_t flags {0};

    public:
      LaserStatusFlags() {}
      LaserStatusFlags(uint16_t f) : flags(f) {};
      bool isBusy() const { return flags & 0x04; }
      bool isReady() const { return flags & 0x20; }
      bool isAxis() const { return flags & 0x40; }
      bool isUnderflow() const { return flags & (1 << int(BjjczStatus::LIST)); }
      uint16_t data() const { return flags; }
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
      LaserBJJCZ* laser;
      double _frequency;
      double power;
      double jumpSpeed;
      double delay_jump;
      double delayOn;
      double delayOff;
      double delayPolygon;
      double pulseWidth;
      int x;
      int y;
      double lastDir;
      bool dirValid;
      double markSpeed;

    public:
      FiberLaserState(LaserBJJCZ* l) : laser(l) { clear(); }
      void clear() {
            power        = 0.0;
            markSpeed    = 0.0;
            jumpSpeed    = 0.0;
            _frequency   = 0.0;
            delay_jump   = 0.0;
            delayOn      = 0.0;
            delayOff     = 0.0;
            delayPolygon = 0.0;
            pulseWidth   = 0.0;
            x            = 0x8000;
            y            = 0x8000;
            lastDir      = 0.0;
            dirValid     = false;
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
      void setPosition(uint16_t newX, uint16_t newY) {
            x = newX;
            y = newY;
            }
      };

//---------------------------------------------------------
//   Command
//---------------------------------------------------------

enum Command : uint16_t {
      listJumpTo        = 0x8001,
      listEndOfList     = 0x8002,
      listLaserOnPoint  = 0x8003,
      listDelayTime     = 0x8004,
      listMarkTo        = 0x8005,
      listJumpSpeed     = 0x8006,
      listLaserOnDelay  = 0x8007,
      listLaserOffDelay = 0x8008,

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

      DisableLaser     = 0x0002,
      UnknownCmdx03    = 0x0003, // reset?
      EnableLaser      = 0x0004,
      ExecuteList      = 0x0005,
      SetPwmPulseWidth = 0x0006,
      GetStatus        = 0x0007,

      GetSerialNo   = 0x0009,
      GetListStatus = 0x000A,

      GetPositionXY  = 0x000C,
      GotoXY         = 0x000D,
      LaserSignalOff = 0x000E,
      LaserSignalOn  = 0x000F,
      WriteCorLine   = 0x0010,

      ResetList   = 0x0012,
      RestartList = 0x0013,

      WriteCorTable       = 0x0015,
      SetControlMode      = 0x0016,
      SetDelayMode        = 0x0017,
      SetMaxPolyDelay     = 0x0018,
      SetEndOfList        = 0x0019,
      SetFirstPulseKiller = 0x001A,
      SetLaserMode        = 0x001B,
      SetTiming           = 0x001C,
      SetStandby          = 0x001D,
      SetPwmHalfPeriod    = 0x001E,
      StopExecute         = 0x001F,
      StopList            = 0x0020,
      WritePort           = 0x0021,
      WriteAnalogPort1    = 0x0022,
      WriteAnalogPort2    = 0x0023,
      WriteAnalogPortX    = 0x0024,
      ReadPort            = 0x0025,
      SetAxisMotionParam  = 0x0026,
      SetAxisOriginParam  = 0x0027,
      AxisGoOrigin        = 0x0028,
      MoveAxisTo          = 0x0029,
      GetAxisPos          = 0x002A,
      GetFlyWaitCount     = 0x002B,

      GetMarkCount         = 0x002D,
      SetFpkParam2         = 0x002E,
      FiberPulseWidth      = 0x002F,
      FiberGetConfigExtend = 0x0030,
      InputPort            = 0x0031,
      SetFlyRes            = 0x0032,
      Fiber_SetMo          = 0x0033, // IPG_OpenMO
      Fiber_GetStMO_AP     = 0x0034, // IPG_GetStMO_AP(unsigned char

      GetUserData = 0x0036,

      GetFlySpeed         = 0x0038,
      DisableZ            = 0x0039,
      EnableZ             = 0x003A,
      SetZData            = 0x003B,
      SetSPISimmerCurrent = 0x003C,

      Reset       = 0x0040,
      GetMarkTime = 0x0041,
      SetFpkParam = 0x0062
      };

//---------------------------------------------------------
//   Packet4
//---------------------------------------------------------

class Packet4 : public std::array<uint16_t, 4>
      {
    public:
      };

//---------------------------------------------------------
//   Packet6
//---------------------------------------------------------

class Packet6 : public std::array<uint16_t, 6>
      {
    public:
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
      LaserBJJCZ* laser;
      int index {0};
      int packetsSend {0};
      bool executing {false};

    public:
      CmdList(LaserBJJCZ* l) : laser(l) {}
      bool empty() const { return index == 0; }
      int size() const { return index; }
      // normal use cycle:
      void start();
      void write(const Packet6& p);
      void end(int param = 0);
      };


//---------------------------------------------------------
//   LaserBJJCZ
//    Concrete Laser implementation for the BJJCZ controller board.
//    Communication is via USB (libusb).
//---------------------------------------------------------

class LaserBJJCZ : public Laser
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no no")

      PROPV(int, lightPin, -1) // -1 means "there is no red light available"
      PROPV(bool, lightPinInvert, true)
      PROPV(int, footPin, -1) // 15
      PROPV(bool, footPinInvert, false)
      PROPV(bool, moRunningOnly, true)   // MO enabled only when running
      PROPV(QString, corFile, QString()) // MO enabled only when running

      // machine status:
      uint16_t currentX;
      uint16_t currentY;
      double lastDir {0};
      bool dirValid {false};
      LaserParameterSet laserValues; // current laser values
      bool _laserValuesValid {false};
      mutable LaserStatusFlags _status {0};

      double galvos;

      Usb* usb {nullptr};
      CmdList list;
      Packet4 getSerialNumber() const { return command({GetSerialNo}); }
      Packet4 getStatus() const;
      bool send(const Packet6&) const;
      bool send(const CmdList&) const;
      bool write(Packet6& packet, int index = 0, int attempt = 0);
      Packet4 read(int index) const;

      void list_jump_speed(uint16_t speed);
      void list_jump(int x, int y, int angle = 0);
      void list_mark(uint16_t x, uint16_t y, uint16_t angle = 0);
      void list_jump_delay(double delay) {
            list.write({listJumpDelay, uint16_t(fabs(delay)), uint16_t(delay > 0.0 ? 0 : 0x8000)});
            }
      void list_fiber_ylpm_pulse_width(uint16_t w) { list.write({listFiberYLPMPulseWidth, 0, w}); }
      void list_write_port(int bits) { list.write({listWritePort, (uint16_t)bits}); }
      void list_laser_on_point(uint16_t dwell_time) { list.write({listLaserOnPoint, dwell_time}); }
      void list_delay_time(double time);
      void list_laser_on_delay(double delay) {
            list.write({listLaserOnDelay, uint16_t(fabs(delay)), uint16_t(delay > 0 ? 0 : 0x8000)});
            }
      void list_laser_off_delay(double delay) {
            list.write({listLaserOffDelay, uint16_t(fabs(delay)), uint16_t(delay > 0 ? 0 : 0x8000)});
            }
      void list_mark_frequency(uint16_t frequency) { list.write({listMarkFreq, frequency}); }
      void list_mark_power_ratio(uint16_t power_ratio) { list.write({listMarkPowerRatio, power_ratio}); }
      void list_polygon_delay(double delay) {
            auto d = fabs(delay) / 10.0;
            list.write({listPolygonDelay, uint16_t(d), uint16_t(delay > 0 ? 0 : 0x8000)});
            }
      void list_mark_current(uint16_t current) { list.write({listMarkCurrent, current}); }
      void list_mark_frequency_2(int frequency) { Fatal("not implemented"); }
      void list_fly_enable(uint16_t enabled = 1) { list.write({listFlyEnable, enabled}); }
      void list_direct_laser_switch() { Fatal("not implemented"); }
      void list_fly_delay(double delay) {
            list.write({listFlyDelay, uint16_t(fabs(delay)), uint16_t(delay > 0 ? 0 : 0x8000)});
            }
      void list_set_co2_fpk(uint16_t fpk1, uint16_t fpk2 = 0) { list.write({listSetCo2FPK, fpk1, fpk2}); }
      void list_fly_wait_input() { list.write({listFlyWaitInput}); }
      void list_fiber_open_mo(uint16_t open_mo) { list.write({listFiberOpenMO, open_mo}); }
      void list_wait_for_input(uint16_t mask, uint16_t level) { list.write({listWaitForInput, mask, level}); }
      void list_change_mark_count(uint16_t count) { list.write({listChangeMarkCount, count}); }
      void list_set_weld_power_wave(uint16_t wpw) { list.write({listSetWeldPowerWave, wpw}); }
      void list_enable_weld_power_wave(uint16_t enabled) { list.write({listEnableWeldPowerWave, enabled}); }
      void list_fly_encoder_count(uint16_t count) { list.write({listFlyEncoderCount, count}); }
      void list_set_da_z_word(uint16_t word) { list.write({listSetDaZWord, word}); }
      void list_jpt_set_param(uint16_t param) { list.write({listJptSetParam, param}); }
      void list_mark_speed(uint16_t speed) {
            if (speed > 0xFFFF)
                  speed = 0xFFFF;
            list.write({listMarkSpeed, speed});
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
      bool waitReady() const;
      void wait_idle() const;
      Packet4 gotoXY(uint16_t x, uint16_t y, uint16_t angle = 0, uint16_t dist = 0) {
            currentX = x;
            currentY = y;
            return command({GotoXY, x, y, angle, dist});
            }
      Packet4 setFirstPulseKiller(uint16_t v) { return command({SetFirstPulseKiller, v}); }
      Packet4 disable_laser() { return command({DisableLaser}); }
      Packet4 enable_laser() { return command({EnableLaser}); }
      //
      //    list commands
      //
      Packet4 execute_list() { return command({ExecuteList}); }
      Packet4 reset_list() { return command({ResetList}); }
      void set_end_of_list(uint16_t end) { command({SetEndOfList, end, 0, 0, 0, 0}); }
      Packet4 stop_execute() const { return command(StopExecute); }
      //
      Packet4 stop_list() { return command(StopList); }
      //    nowhere used:
      Packet4 get_list_status() { return command({GetListStatus}); }
      Packet4 restart_list() { return command({RestartList}); }
      //
      //
      Packet4 set_pwm_pulse_width(uint16_t pw) { return command({SetPwmPulseWidth, pw}); }
      LaserStatusFlags statusFlags() const;
      Packet4 get_serial_number() { return command({GetSerialNo}); }
      Packet4 get_position_xy() { return command({GetPositionXY}); }
      Packet4 laser_signal_off() { return command({LaserSignalOff}); }
      Packet4 laser_signal_on() { return command({LaserSignalOn}); }
      Packet4 write_cor_table(bool table = true) { return command({WriteCorTable, uint16_t(table)}); }
      void write_cor_line(uint16_t dx, uint16_t dy, uint16_t non_first) {
            send({WriteCorLine, dx, dy, non_first, 0, 0});
            }
      Packet4 set_control_mode(uint16_t mode) { return command({SetControlMode, mode}); }
      Packet4 set_delay_mode(uint16_t mode) { return command({SetDelayMode, mode}); }
      Packet4 set_max_poly_delay(uint16_t delay) {
            return command({SetMaxPolyDelay, uint16_t(fabs(delay)), uint16_t(delay > 0 ? 0 : 0x8000)});
            }
      Packet4 set_laser_mode(uint16_t mode) { return command({SetLaserMode, mode}); }
      Packet4 set_timing(uint16_t timing) { return command({SetTiming, timing}); }
      Packet4 set_standby(uint16_t a, uint16_t b) { return command({SetStandby, a, b}); }
      Packet4 set_pwm_half_period(uint16_t pwm_half_period) {
            return command({SetPwmHalfPeriod, pwm_half_period});
            }
      Packet4 write_port(int bits) const { return command({WritePort, (uint16_t)bits}); }
      Packet4 write_analog_port_1(uint16_t port) { return command({WriteAnalogPort1, port}); }
      Packet4 write_analog_port_2(uint16_t port) { return command({WriteAnalogPort2, port}); }
      Packet4 write_analog_port_x(uint16_t port) { return command({WriteAnalogPortX, port}); }
      int readPort() {
            auto p4 = command({ReadPort});
            return p4[0];
            }
      Packet4 axis_go_origin(uint16_t p0 = 0, uint16_t p1 = 0) { return command({AxisGoOrigin, p0, p1}); }
      Packet4 get_axis_pos(uint16_t index = 0) { return command({GetAxisPos, index}); }
      Packet4 get_fly_wait_count() { return command({GetFlyWaitCount}); }
      Packet4 get_mark_count() { return command({GetMarkCount}); }
      void set_fiber_mo(uint16_t mo) { command({Fiber_SetMo, mo, 0, 0, 0, 0}); }
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
      Packet4 get_input_port() const { return command({InputPort}); }
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
      Packet4 set_axis_motion_param(uint16_t p0 = 0, uint16_t p1 = 0, uint16_t p2 = 0, uint16_t p3 = 0) {
            return command({SetAxisMotionParam, p0, p1, p2, p3});
            }
      Packet4 set_axis_origin_param(uint16_t p0 = 0, uint16_t p1 = 0, uint16_t p2 = 0, uint16_t p3 = 0) {
            return command({SetAxisOriginParam, p0, p1, p2, p3});
            }
      void writeCorrectionTable();

      Packet4 command(Packet6 data) const;
      PathsD frameFixture(Fixture* fixture);

      void initPosition();

      void setLight(bool on);

      bool gpioValue(int bit) { return _outputPort & (1 << bit); }
      void gpioListWrite();
      void gpioInit() {
            _outputPort = 0;
            gpioWrite();
            }
      void gpioRegisterOn(int bit) { _outputPort |= (1 << bit); }
      void gpioRegisterOff(int bit) { _outputPort &= ~(1 << bit); }
      void gpioWrite();
      void gpioWrite(int data);
      void gpioOn(int bit); // switch immediate
      void gpioOff(int bit);
      void gpioSet(int bit, bool on);
      void gpioToggle(int bit);

    protected:
      //      void list_end();
      void list_qswitch_period(uint16_t qswitch) { list.write({listQSwitchPeriod, qswitch}); }
      void markLines(PathsD&, bool reverse);
      void mark(double x, double y);
      void mark(uint16_t x, uint16_t y);
      void setLaser(const LaserParameterSet&);
      void move(uint16_t x, uint16_t y);
      int distance(int x, int y);

    public:
      LaserBJJCZ(ZCam* w, QObject* parent = nullptr);
      virtual ~LaserBJJCZ();

      friend class CmdList;
      friend class FiberLaserState;

      // ── LaserEngine interface overrides ───────────────────────
      virtual bool initEngine(bool dryRun) override;
      virtual void exitEngine() override;

      virtual bool startFramingEngine() override;
      virtual void stopFramingEngine() override;

      virtual void startMarkingEngine() override;
      virtual void stopMarkingEngine() const override;
      virtual void endMarkingEngine() override;

      virtual void mark(const PathD&) override;
      virtual void move(double x, double y) override;
      virtual void markLayer(const LaserPath& path, const LaserParameterSet& sl) override;

      virtual LaserPosition mapToGalvo(double, double) override;
      virtual const std::string_view properties() const override;
      void setLaserValuesValid(bool v) { _laserValuesValid = v; }

      Q_INVOKABLE virtual void toggleOutputBit(int bit) override { gpioToggle(bit); }
      };

extern void dump(Packet6* p, bool single);
extern PathsD dotCorrection(const PathsD& paths, double offset);