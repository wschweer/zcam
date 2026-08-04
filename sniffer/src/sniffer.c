// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2023, Alex Taradov <alex@taradov.com>. All rights reserved.
// Copyright (c) 2025-2026 Werner Schweer.  All rights reserved.
//
// USB Sniffer for the BJJCZ USBLMCV2 laser controller.
// Captures USB traffic via usbmon (default) or hardware USB sniffer (-s).
// Analyses and prints BJJCZ protocol transactions with sequence compression.

#include "os_common.h"

#include <libusb.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

/*- Definitions -------------------------------------------------------------*/

// BJJCZ USBLMCV2 device
#define BJJCZ_VID 0x9588
#define BJJCZ_PID 0x9899

// Hardware sniffer capture device
#define CAPTURE_VID 0x6666
#define CAPTURE_PID 0x6620

// Hardware sniffer constants
#define DATA_ENDPOINT      0x82
#define DATA_ENDPOINT_SIZE 512
#define TRANSFER_SIZE      (DATA_ENDPOINT_SIZE * 2000)
#define TRANSFER_TIMEOUT   1000
#define TRANSFER_COUNT     16
#define TIMEOUT            250

// FPGA capture format
#define DATA_HEADER_SIZE   7
#define STATUS_HEADER_SIZE 4
#define MAX_DATA_SIZE      1280

#define HEADER_STATUS      0x80
#define HEADER_TOGGLE      0x40
#define HEADER_ZERO        0x20
#define HEADER_TS_OVERFLOW 0x10

#define HEADER_OVERFLOW   0x08
#define HEADER_CRC_ERROR  0x10
#define HEADER_DATA_ERROR 0x20

#define HEADER_LS_OFFS    0
#define HEADER_LS_MASK    0x0f
#define HEADER_VBUS       0x10
#define HEADER_TRIGGER    0x20
#define HEADER_SPEED_OFFS 6
#define HEADER_SPEED_MASK 0x03

#define CTRL_REG_SIZE 4
// Capture control commands
enum {
      CaptureCtrl_Reset  = 0,
      CaptureCtrl_Enable = 1,
      CaptureCtrl_Speed0 = 2,
      CaptureCtrl_Speed1 = 3,
      CaptureCtrl_Test   = 4,
      };

enum {
      CaptureSpeed_LS = 0,
      CaptureSpeed_FS = 1,
      CaptureSpeed_HS = 2,
      };

// usbmon binary header: read() delivers first 48 bytes of struct mon_bin_hdr
// (kernel PKT_SZ_API0). Internal struct is 64 bytes but only 48 are copied.
#define MON_HDR_SIZE      48
#define MON_TYPE_SUBMIT   'S'
#define MON_TYPE_CALLBACK 'C'
#define EP_DIR_IN         0x80

// Sequence compression
#define MAX_SEQ_LEN    8
#define MAX_PENDING    32
#define MAX_PKT_SIZE   4096
#define MAX_DIFF_PCT   25
#define MAX_DIFF_ABS   3
#define MAX_SEQ_REPEAT 5000

#define READ_BUF_SIZE (256 * 1024)
/*- Types -------------------------------------------------------------------*/

typedef struct {
      uint16_t code;
      const char* name;
      } CmdEntry;
typedef struct __attribute__((packed)) {
      u64 id;        // 0
      u8 type;       // 8
      u8 xfer_type;  // 9
      u8 epnum;      // 10
      u8 devnum;     // 11
      u16 busnum;    // 12
      s8 flag_setup; // 14
      s8 flag_data;  // 15
      s64 ts_sec;    // 16
      s32 ts_usec;   // 24
      s32 status;    // 28
      u32 length;    // 32  (len_urb)
      u32 len_cap;   // 36
      u8 s[8];       // 40 (union: setup[8] / iso) — last 8 bytes of 48
      } MonBinHdr;
// A transaction is either a Packet6 command + IN response, or a list packet.
typedef struct {
      u8 out_data[MAX_PKT_SIZE];
      int out_size;
      u64 out_ts; // microseconds
      u8 in_data[16];
      int in_size;
      bool has_in;
      bool is_list;
      int repeat; // run-length compression: number of identical consecutive transactions
      } Transaction;
/*- Command Tables ----------------------------------------------------------*/

static const CmdEntry s_listCmds[] = {
         {0x8001,              "listJumpTo"},
         {0x8002,           "listEndOfList"},
         {0x8003,        "listLaserOnPoint"},
         {0x8004,           "listDelayTime"},
         {0x8005,              "listMarkTo"},
         {0x8006,           "listJumpSpeed"},
         {0x8007,        "listLaserOnDelay"},
         {0x8008,       "listLaserOffDelay"},
         {0x800A,            "listMarkFreq"},
         {0x800B,      "listMarkPowerRatio"},
         {0x800C,           "listMarkSpeed"},
         {0x800D,           "listJumpDelay"},
         {0x800F,        "listPolygonDelay"},
         {0x8011,           "listWritePort"},
         {0x8012,         "listMarkCurrent"},
         {0x8013,           "listMarkFreq2"},
         {0x801A,           "listFlyEnable"},
         {0x801B,       "listQSwitchPeriod"},
         {0x801C,   "listDirectLaserSwitch"},
         {0x801D,            "listFlyDelay"},
         {0x801E,           "listSetCo2FPK"},
         {0x801F,        "listFlyWaitInput"},
         {0x8021,         "listFiberOpenMO"},
         {0x8022,        "listWaitForInput"},
         {0x8023,     "listChangeMarkCount"},
         {0x8024,    "listSetWeldPowerWave"},
         {0x8025, "listEnableWeldPowerWave"},
         {0x8026, "listFiberYLPMPulseWidth"},
         {0x8028,     "listFlyEncoderCount"},
         {0x8029,          "listSetDaZWord"},
         {0x8050,         "listJptSetParam"},
         {0x8051,           "listReadyMark"},
      };

static const CmdEntry s_singleCmds[] = {
         {0x0002,         "DisableLaser"},
         {0x0003,        "UnknownCmdx03"},
         {0x0004,          "EnableLaser"},
         {0x0005,          "ExecuteList"},
         {0x0006,     "SetPwmPulseWidth"},
         {0x0007,            "GetStatus"},
         {0x0009,          "GetSerialNo"},
         {0x000A,        "GetListStatus"},
         {0x000C,        "GetPositionXY"},
         {0x000D,               "GotoXY"},
         {0x000E,       "LaserSignalOff"},
         {0x000F,        "LaserSignalOn"},
         {0x0010,         "WriteCorLine"},
         {0x0012,            "ResetList"},
         {0x0013,          "RestartList"},
         {0x0015,        "WriteCorTable"},
         {0x0016,       "SetControlMode"},
         {0x0017,         "SetDelayMode"},
         {0x0018,      "SetMaxPolyDelay"},
         {0x0019,         "SetEndOfList"},
         {0x001A,  "SetFirstPulseKiller"},
         {0x001B,         "SetLaserMode"},
         {0x001C,            "SetTiming"},
         {0x001D,           "SetStandby"},
         {0x001E,     "SetPwmHalfPeriod"},
         {0x001F,          "StopExecute"},
         {0x0020,             "StopList"},
         {0x0021,            "WritePort"},
         {0x0022,     "WriteAnalogPort1"},
         {0x0023,     "WriteAnalogPort2"},
         {0x0024,     "WriteAnalogPortX"},
         {0x0025,             "ReadPort"},
         {0x0026,   "SetAxisMotionParam"},
         {0x0027,   "SetAxisOriginParam"},
         {0x0028,         "AxisGoOrigin"},
         {0x0029,           "MoveAxisTo"},
         {0x002A,           "GetAxisPos"},
         {0x002B,      "GetFlyWaitCount"},
         {0x002D,         "GetMarkCount"},
         {0x002E,         "SetFpkParam2"},
         {0x002F,      "FiberPulseWidth"},
         {0x0030, "FiberGetConfigExtend"},
         {0x0031,            "InputPort"},
         {0x0032,            "SetFlyRes"},
         {0x0033,          "Fiber_SetMo"},
         {0x0034,     "Fiber_GetStMO_AP"},
         {0x0036,          "GetUserData"},
         {0x0038,          "GetFlySpeed"},
         {0x0039,             "DisableZ"},
         {0x003A,              "EnableZ"},
         {0x003B,             "SetZData"},
         {0x003C,  "SetSPISimmerCurrent"},
         {0x0040,                "Reset"},
         {0x0041,          "GetMarkTime"},
         {0x0062,          "SetFpkParam"},
      };

/*- Variables ---------------------------------------------------------------*/

static int g_running    = 1;
static bool g_use_hw    = false;
static FILE* g_out_file = NULL;
static u64 g_first_ts   = 0;
static int g_speed      = CaptureSpeed_HS;

// Hardware sniffer state
static libusb_device_handle* g_handle = NULL;
static struct libusb_transfer* g_transfers[TRANSFER_COUNT];
static u8* g_buffers[TRANSFER_COUNT];

// usbmon state
static u16 g_target_bus = 0;
static u8 g_target_dev  = 0;

// FPGA capture state machine
static u8 capture_data[2048];
static int capture_data_ptr    = 0;
static int capture_size        = 0;
static bool capture_header     = true;
static bool capture_status     = false;
static int capture_toggle      = 0;
static u64 capture_ts_int      = 0;
static u64 capture_ts          = 0;
static bool capture_overflow   = false;
static bool capture_crc_error  = false;
static bool capture_data_error = false;

// USB direction tracking (hardware sniffer)
static int g_last_dir = -1;
static int g_last_ep  = -1;

// Incomplete Packet6 (waiting for IN response)
static Transaction g_pending_cmd;
static bool g_pending_cmd_valid = false;

// Pending transactions for sequence detection
static Transaction g_pending[MAX_PENDING];
static int g_pending_count = 0;

// Sequence tracking
static Transaction g_seq_ref[MAX_SEQ_LEN];
static int g_seq_len    = 0;
static int g_seq_repeat = 0;
static int g_seq_pos    = 0;

// Run-length staging (pre-compression before sequence detection)
static Transaction g_staging;
static bool g_staging_valid = false;
/*- Helpers -----------------------------------------------------------------*/

static inline uint16_t rd16(const u8* p) {
      return p[0] | (p[1] << 8);
      }

static const char* lookup_cmd(uint16_t code) {
      const CmdEntry* tbl = (code >= 0x8000) ? s_listCmds : s_singleCmds;
      int n               = (code >= 0x8000) ? (int)(sizeof(s_listCmds) / sizeof(s_listCmds[0]))
                                             : (int)(sizeof(s_singleCmds) / sizeof(s_singleCmds[0]));
      for (int i = 0; i < n; i++)
            if (tbl[i].code == code)
                  return tbl[i].name;
      return NULL;
      }

static u64 get_ts_us(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (u64)ts.tv_sec * 1000000ULL + (u64)ts.tv_nsec / 1000ULL;
      }

/*- Output ------------------------------------------------------------------*/

// Write to stdout and optionally to the output file
static void out_printf(const char* fmt, ...) {
      va_list args;
      va_start(args, fmt);
      vprintf(fmt, args);
      va_end(args);
      if (g_out_file) {
            va_start(args, fmt);
            vfprintf(g_out_file, fmt, args);
            va_end(args);
            }
      }

//-----------------------------------------------------------------------------
//   format_status
//    Format 16-bit status as 4 groups of 4 chars: ---- ---- --R- -B--
//-----------------------------------------------------------------------------

static void format_status(uint16_t status, char* buf) {
      int idx = 0;
      for (int nibble = 3; nibble >= 0; nibble--) {
            if (idx > 0)
                  buf[idx++] = ' ';
            for (int bit = 3; bit >= 0; bit--) {
                  int bit_pos = nibble * 4 + bit;
                  if (status & (1 << bit_pos)) {
                        switch (bit_pos) {
                              case 1: buf[idx++] = 'L'; break; // List  0x02
                              case 2: buf[idx++] = 'B'; break; // Busy  0x04
                              case 5: buf[idx++] = 'R'; break; // Ready 0x20
                              case 6: buf[idx++] = 'A'; break; // Axis  0x40
                              default: buf[idx++] = 'X'; break;
                              }
                        }
                  else {
                        buf[idx++] = '-';
                        }
                  }
            }
      buf[idx] = '\0';
      }

static void print_timestamp(u64 ts_us) {
      u64 rel = ts_us - g_first_ts;
      u64 sec = rel / 1000000;
      u64 ms  = (rel % 1000000) / 1000;
      out_printf("%04lu.%03lu: ", sec, ms);
      }

// Determine the number of decodable parameters for a command.
// Returns 0 (no params), 1 (single param — can be inlined), or 2 (multiple).
static int param_count(uint16_t cmd) {
      switch (cmd) {
            // No parameters
            case 0x0002:
            case 0x0004:
            case 0x0005:
            case 0x0007:
            case 0x0009:
            case 0x000A:
            case 0x000C:
            case 0x000E:
            case 0x000F:
            case 0x0012:
            case 0x0013:
            case 0x001F:
            case 0x0020:
            case 0x0025:
            case 0x002B:
            case 0x002D:
            case 0x0028:
            case 0x0030:
            case 0x0034:
            case 0x0036:
            case 0x0038:
            case 0x0039:
            case 0x003A:
            case 0x0040:
            case 0x8002:
            case 0x8051:
            case 0x801F: return 0;

            // Single parameter (one logical value, even if encoded in 2 words)
            case 0x0006:
            case 0x0016:
            case 0x0017:
            case 0x0018:
            case 0x0019:
            case 0x001A:
            case 0x001B:
            case 0x001C:
            case 0x001E:
            case 0x0021:
            case 0x0022:
            case 0x0023:
            case 0x0024:
            case 0x002A:
            case 0x0031:
            case 0x0033:
            case 0x003B:
            case 0x003C:
            case 0x0041:
            case 0x002F:
            case 0x8003:
            case 0x8004:
            case 0x8006:
            case 0x8007:
            case 0x8008:
            case 0x800A:
            case 0x800B:
            case 0x800C:
            case 0x800D:
            case 0x800F:
            case 0x8011:
            case 0x8012:
            case 0x8013:
            case 0x801A:
            case 0x801B:
            case 0x801C:
            case 0x801D:
            case 0x8021:
            case 0x8023:
            case 0x8024:
            case 0x8025:
            case 0x8026:
            case 0x8028:
            case 0x8029:
            case 0x8050: return 1;

            // Multiple parameters
            default: return 2;
            }
      }

// Format a single parameter into buf.  Only called when param_count()==1.
static void format_single_param(const uint16_t p[6], char* buf, int bufsize) {
      uint16_t cmd = p[0];
      switch (cmd) {
            // ── Signed delays: value in p[1], sign in p[2] ──
            case 0x8007:
            case 0x8008:
            case 0x800D:
            case 0x800F:
            case 0x801D:
            case 0x0018:
                  snprintf(buf, bufsize, "delay=%d%s", (int16_t)(p[1] & 0x7fff),
                           (p[2] & 0x8000) ? " (neg)" : "");
                  break;
            // ── List commands ──
            case 0x8003: snprintf(buf, bufsize, "dwell=%d us", p[1]); break;
            case 0x8004: snprintf(buf, bufsize, "delay=%d us", p[1]); break;
            case 0x8006: snprintf(buf, bufsize, "speed=%d", p[1]); break;
            case 0x800A: snprintf(buf, bufsize, "freq=%d (%.1f kHz)", p[1], 10000.0 / p[1]); break;
            case 0x800B: snprintf(buf, bufsize, "power_ratio=%d", p[1]); break;
            case 0x800C: snprintf(buf, bufsize, "speed=%d", p[1]); break;
            case 0x8011: snprintf(buf, bufsize, "port=0x%04x", p[1]); break;
            case 0x8012: snprintf(buf, bufsize, "current=%d (%.1f%%)", p[1], p[1] * 100.0 / 0xfff); break;
            case 0x8013: snprintf(buf, bufsize, "freq2=%d (%.1f kHz)", p[1], 10000.0 / p[1]); break;
            case 0x801A: snprintf(buf, bufsize, "enabled=%d", p[1]); break;
            case 0x801B: snprintf(buf, bufsize, "qswitch_period=%d (%.1f kHz)", p[1], 20000.0 / p[1]); break;
            case 0x801C: snprintf(buf, bufsize, "value=0x%04x", p[1]); break;
            case 0x8021: snprintf(buf, bufsize, "open_mo=%d", p[1]); break;
            case 0x8023: snprintf(buf, bufsize, "count=%d", p[1]); break;
            case 0x8024: snprintf(buf, bufsize, "wpw=%d", p[1]); break;
            case 0x8025: snprintf(buf, bufsize, "enabled=%d", p[1]); break;
            case 0x8026: snprintf(buf, bufsize, "pulse_width=%d", p[2]); break;
            case 0x8028: snprintf(buf, bufsize, "count=%d", p[1]); break;
            case 0x8029: snprintf(buf, bufsize, "z_word=0x%04x", p[1]); break;
            case 0x8050: snprintf(buf, bufsize, "param=%d", p[1]); break;
            // ── Single commands ──
            case 0x0006: snprintf(buf, bufsize, "pulse_width=%d", p[1]); break;
            case 0x0016: snprintf(buf, bufsize, "mode=%d", p[1]); break;
            case 0x0017: snprintf(buf, bufsize, "mode=%d", p[1]); break;
            case 0x0019: snprintf(buf, bufsize, "end=%d", p[1]); break;
            case 0x001A: snprintf(buf, bufsize, "value=%d", p[1]); break;
            case 0x001B: snprintf(buf, bufsize, "mode=%d", p[1]); break;
            case 0x001C: snprintf(buf, bufsize, "timing=%d", p[1]); break;
            case 0x001E: snprintf(buf, bufsize, "half_period=%d", p[1]); break;
            case 0x0021: snprintf(buf, bufsize, "port=0x%04x", p[1]); break;
            case 0x0022: snprintf(buf, bufsize, "port1=%d", p[1]); break;
            case 0x0023: snprintf(buf, bufsize, "port2=%d", p[1]); break;
            case 0x0024: snprintf(buf, bufsize, "portX=%d", p[1]); break;
            case 0x002A: snprintf(buf, bufsize, "index=%d", p[1]); break;
            case 0x002F: snprintf(buf, bufsize, "pw=%d", p[1]); break;
            case 0x0031: snprintf(buf, bufsize, "port=0x%04x", p[1]); break;
            case 0x0033: snprintf(buf, bufsize, "mo=%d", p[1]); break;
            case 0x003B: snprintf(buf, bufsize, "zdata=%d", p[1]); break;
            case 0x003C: snprintf(buf, bufsize, "current=%d", p[1]); break;
            case 0x0041: snprintf(buf, bufsize, "param=%d", p[1]); break;
            default: snprintf(buf, bufsize, "%d", p[1]); break;
            }
      }

// Print multiple parameters on a second (indented) line.
// Only called when param_count()==2.
static void decode_multi_params(const uint16_t p[6]) {
      uint16_t cmd = p[0];
      switch (cmd) {
            // ── List commands ──
            case 0x8001:
            case 0x8005: // listJumpTo / listMarkTo
                  out_printf("x=0x%04x y=0x%04x angle=0x%04x dist=%d\n", p[1], p[2], p[3], p[4]);
                  break;
            case 0x801E: // listSetCo2FPK
                  out_printf("fpk1=%d fpk2=%d\n", p[1], p[2]);
                  break;
            case 0x8022: // listWaitForInput
                  out_printf("mask=0x%04x level=0x%04x\n", p[1], p[2]);
                  break;
            // ── Single commands ──
            case 0x000D: // GotoXY
                  out_printf("x=0x%04x y=0x%04x angle=0x%04x dist=%d\n", p[1], p[2], p[3], p[4]);
                  break;
            case 0x0010: // WriteCorLine
                  out_printf("dx=0x%04x dy=0x%04x non_first=%d\n", p[1], p[2], p[3]);
                  break;
            case 0x001D: // SetStandby
                  out_printf("p1=%d p2=%d\n", p[1], p[2]);
                  break;
            case 0x0026:
            case 0x0027:
            case 0x0029: out_printf("p0=%d p1=%d p2=%d p3=%d\n", p[1], p[2], p[3], p[4]); break;
            case 0x002E: // SetFpkParam2
                  out_printf("max_v=%d min_v=%d t1=%d t2=%d\n", p[1], p[2], p[3], p[4]);
                  break;
            case 0x0032: // SetFlyRes
                  out_printf("f1=%d f2=%d f3=%d f4=%d\n", p[1], p[2], p[3], p[4]);
                  break;
            case 0x0062: // SetFpkParam
                  out_printf("p1=0x%04x p2=0x%04x p3=0x%04x p4=0x%04x\n", p[1], p[2], p[3], p[4]);
                  break;
            default: out_printf("0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n", p[1], p[2], p[3], p[4], p[5]); break;
            }
      }

// Build the command name field (always 33 chars wide) into buf.
// For single-param commands the parameter is inlined: CmdName(param=wert)
static void build_name_field(const char* name, uint16_t cmd, const uint16_t p[6], int np, char* buf,
                             int bufsize) {
      if (np == 1) {
            char parm[128];
            format_single_param(p, parm, sizeof(parm));
            snprintf(buf, bufsize, "%s(%s)", name ? name : "unknown", parm);
            }
      else if (name) {
            snprintf(buf, bufsize, "%s", name);
            }
      else {
            snprintf(buf, bufsize, "unknown(0x%04x)", cmd);
            }
      // Pad to 33 chars
      int len = strlen(buf);
      if (len < 33) {
            memset(buf + len, ' ', 33 - len);
            buf[33] = '\0';
            }
      }

// Print a single 12-byte list slot.
// Returns true if this was the last slot (listEndOfList), false otherwise.
static bool print_list_slot(const u8* slot) {
      uint16_t cmd = rd16(slot);
      if (cmd == 0x8002) {
            out_printf("      %-33s\n", "listEndOfList");
            return true;
            }
      const char* name = lookup_cmd(cmd);
      uint16_t p[6];
      for (int i = 0; i < 6; i++)
            p[i] = rd16(slot + i * 2);
      int np = param_count(cmd);
      char namebuf[128];
      build_name_field(name, cmd, p, np, namebuf, sizeof(namebuf));
      if (np >= 2) {
            out_printf("      %s ", namebuf);
            decode_multi_params(p);
            }
      else {
            out_printf("      %s\n", namebuf);
            }
      return false;
      }

// Compare two list slots by opcode only (matching the list comparison logic
// in transactions_similar).
static bool slots_similar(const u8* a, const u8* b) {
      uint16_t ca = rd16(a);
      uint16_t cb = rd16(b);
      return ca == cb;
      }

// Print a list transaction with intra-list sequence compression.
// Detects repeating runs of slots (by opcode) and compresses them the same
// way as transaction-level sequences:
//
//   ---
//   <slots printed once>
//   --- [sequence repeated N times]
//
static void print_list(const Transaction* t) {
      if (t->repeat > 1)
            out_printf("List  [×%d]\n", t->repeat);
      else
            out_printf("List\n");
      int nslots = t->out_size / 12;

      // We process slots in a single pass with a small look-back window
      // for sequence detection, mirroring the transaction-level approach.
      // pending: slots not yet committed to output
      // We use a simple approach: scan forward, for each position try to
      // detect a repeating pattern of length 1..MAX_SEQ_LEN starting there.

      int i = 0;
      while (i < nslots) {
            uint16_t cmd = rd16(t->out_data + i * 12);
            if (cmd == 0x8002) {
                  print_list_slot(t->out_data + i * 12);
                  break;
                  }

            // Try to find the repeating pattern starting at position i that
            // covers the most slots.  For each candidate length L we count
            // how many times the pattern repeats consecutively, then pick
            // the L with the highest coverage (L * reps).  Ties are broken
            // in favour of the shorter pattern so that e.g. 256 identical
            // listJumpTo slots compress to "1 line × 256" rather than
            // "8 lines × 32".
            //
            // We need at least 2*L slots available (i + 2*L <= nslots) and
            // the pattern must not contain listEndOfList.
            int max_L = (nslots - i) / 2;
            if (max_L > MAX_SEQ_LEN)
                  max_L = MAX_SEQ_LEN;

            int best_L    = 0;
            int best_reps = 0;
            int best_cov  = 0;

            for (int L = 1; L <= max_L; L++) {
                  // Check that pattern does not contain listEndOfList
                  bool has_eol = false;
                  for (int k = 0; k < L; k++) {
                        if (rd16(t->out_data + (i + k) * 12) == 0x8002) {
                              has_eol = true;
                              break;
                              }
                        }
                  if (has_eol)
                        continue;

                  // Compare first occurrence [i..i+L-1] with second [i+L..i+2L-1]
                  bool match = true;
                  for (int k = 0; k < L; k++) {
                        if (!slots_similar(t->out_data + (i + k) * 12, t->out_data + (i + L + k) * 12)) {
                              match = false;
                              break;
                              }
                        }
                  if (!match)
                        continue;

                  // Count how many times the pattern repeats
                  int reps = 2;
                  int next = i + 2 * L;
                  while (next + L <= nslots) {
                        bool m = true;
                        for (int k = 0; k < L; k++) {
                              if (!slots_similar(t->out_data + (i + k) * 12, t->out_data + (next + k) * 12)) {
                                    m = false;
                                    break;
                                    }
                              }
                        if (!m)
                              break;
                        reps++;
                        next += L;
                        }

                  int cov = L * reps;
                  if (cov > best_cov) {
                        best_L    = L;
                        best_reps = reps;
                        best_cov  = cov;
                        }
                  }

            if (best_L == 0) {
                  // No repetition detected — print single slot
                  if (print_list_slot(t->out_data + i * 12))
                        break;
                  i++;
                  continue;
                  }

            // Print the compressed sequence
            out_printf("      ---\n");
            for (int k = 0; k < best_L; k++)
                  if (print_list_slot(t->out_data + (i + k) * 12))
                        goto done;
            out_printf("      --- [sequence repeated %d times]\n", best_reps);
            i += best_L * best_reps;
            }

done:;
      }

static void print_transaction(const Transaction* t) {
      print_timestamp(t->out_ts);

      if (t->is_list) {
            print_list(t);
            }
      else {
            // Packet6 transaction
            uint16_t cmd     = rd16(t->out_data);
            const char* name = lookup_cmd(cmd);
            uint16_t p[6];
            for (int i = 0; i < 6; i++)
                  p[i] = rd16(t->out_data + i * 2);
            int np = param_count(cmd);

            char namebuf[128];
            build_name_field(name, cmd, p, np, namebuf, sizeof(namebuf));
            out_printf("%s", namebuf);

            if (t->has_in && t->in_size >= 8) {
                  uint16_t r[4];
                  for (int i = 0; i < 4; i++)
                        r[i] = rd16(t->in_data + i * 2);
                  // Skip r[0] when it is zero (the usual case)
                  if (r[0] != 0)
                        out_printf(" 0x%04x", r[0]);
                  out_printf(" 0x%04x 0x%04x", r[1], r[2]);
                  char status_buf[24];
                  format_status(r[3], status_buf);
                  out_printf(" %s", status_buf);
                  if (t->repeat > 1)
                        out_printf("  [×%d]", t->repeat);
                  out_printf("\n");
                  }
            else {
                  out_printf(" (no response)");
                  if (t->repeat > 1)
                        out_printf("  [×%d]", t->repeat);
                  out_printf("\n");
                  }

            // Second line: multi-param commands only
            if (np >= 2) {
                  out_printf("%-9s ", "");
                  decode_multi_params(p);
                  }
            }
      }

/*- Sequence Compression ----------------------------------------------------*/
//
// Two-stage compression:
//
// Stage 1 — Run-length pre-compression (staging layer):
//   Consecutive identical transactions are merged into a single Transaction
//   with an incremented 'repeat' count, e.g.  6× GetStatus(Busy) → [×6].
//   This normalises variable-length polling runs so that macro-blocks
//   (poll + list-load) become uniform-length sequences.
//
// Stage 2 — Sequence detection:
//   Detects repeating sequences of transactions and compresses them to a
//   single occurrence with a repetition count, using "---" markers.
//   Self-similarity reduction ensures the shortest fundamental pattern is used.
//
//   ---
//   <sequence printed once>
//   --- [sequence repeated N times]
//

// Forward declarations (needed due to mutual references)
static bool transactions_similar(const Transaction* a, const Transaction* b);
static void add_transaction_inner(const Transaction* t);
// Strict comparison for run-length detection: every byte must match.
static bool transactions_identical(const Transaction* a, const Transaction* b) {
      if (a->is_list != b->is_list)
            return false;
      if (a->out_size != b->out_size)
            return false;
      if (memcmp(a->out_data, b->out_data, a->out_size) != 0)
            return false;
      if (!a->is_list) {
            if (a->has_in != b->has_in)
                  return false;
            if (a->has_in && a->in_size > 0) {
                  if (a->in_size != b->in_size)
                        return false;
                  if (memcmp(a->in_data, b->in_data, a->in_size) != 0)
                        return false;
                  }
            }
      return true;
      }

// Check if a detected pattern of length L is itself a repetition of a shorter
// sub-pattern.  If so, return the shortest such sub-pattern length.
static int reduce_self_similarity(int L) {
      while (L > 1) {
            bool reduced = false;
            for (int d = 1; d <= L / 2; d++) {
                  if (L % d != 0)
                        continue;
                  bool self_sim = true;
                  for (int seg = 1; seg < L / d; seg++) {
                        for (int i = 0; i < d; i++) {
                              if (!transactions_similar(&g_pending[g_pending_count - L + i],
                                                        &g_pending[g_pending_count - L + seg * d + i])) {
                                    self_sim = false;
                                    break;
                                    }
                              }
                        if (!self_sim)
                              break;
                        }
                  if (self_sim) {
                        L       = d;
                        reduced = true;
                        break;
                        }
                  }
            if (!reduced)
                  break;
            }
      return L;
      }

static bool transactions_similar(const Transaction* a, const Transaction* b) {
      if (a->is_list != b->is_list)
            return false;
      if (a->out_size != b->out_size)
            return false;

      if (a->is_list) {
            // List: compare each command's opcode
            int nslots = a->out_size / 12;
            for (int s = 0; s < nslots; s++) {
                  uint16_t ca = rd16(a->out_data + s * 12);
                  uint16_t cb = rd16(b->out_data + s * 12);
                  if (ca != cb)
                        return false;
                  if (ca == 0x8002)
                        break; // listEndOfList
                  }
            return true;
            }

      // Packet6: opcode must match, parameters may differ (fuzzy)
      uint16_t ca = rd16(a->out_data);
      uint16_t cb = rd16(b->out_data);
      if (ca != cb)
            return false;

      // The laser status (last uint16 of the IN response) must match
      // exactly.  A status change (e.g. Busy -> Ready) is semantically
      // significant and must break the sequence.
      if (a->has_in && b->has_in && a->in_size >= 8 && b->in_size >= 8) {
            uint16_t sa = rd16(a->in_data + 6); // 4th uint16 = status
            uint16_t sb = rd16(b->in_data + 6);
            if (sa != sb)
                  return false;
            }

      int plen      = a->out_size;
      int threshold = (plen * MAX_DIFF_PCT) / 100;
      if (threshold > MAX_DIFF_ABS)
            threshold = MAX_DIFF_ABS;
      if (threshold < 1)
            threshold = 1;

      int diff = 0;
      for (int i = 2; i < plen; i++) {
            if (a->out_data[i] != b->out_data[i])
                  diff++;
            if (diff > threshold)
                  return false;
            }
      return true;
      }

static int detect_sequence(void) {
      int max_len = g_pending_count / 2;
      if (max_len > MAX_SEQ_LEN)
            max_len = MAX_SEQ_LEN;

      for (int L = max_len; L >= 1; L--) {
            bool match = true;
            for (int i = 0; i < L; i++) {
                  if (!transactions_similar(&g_pending[g_pending_count - L + i],
                                            &g_pending[g_pending_count - 2 * L + i])) {
                        match = false;
                        break;
                        }
                  }
            if (match) {
                  L = reduce_self_similarity(L);
                  return L;
                  }
            }
      return 0;
      }

static void flush_pending(void) {
      for (int i = 0; i < g_pending_count; i++)
            print_transaction(&g_pending[i]);
      g_pending_count = 0;
      }

// Print the closing marker for a tracked sequence
static void flush_sequence(void) {
      if (g_seq_len == 0)
            return;
      int total = g_seq_repeat + (g_seq_pos > 0 ? 1 : 0);
      if (total > 1)
            out_printf("--- [sequence repeated %d times]\n", total);
      else
            out_printf("---\n");
      g_seq_len    = 0;
      g_seq_repeat = 0;
      g_seq_pos    = 0;
      }

// Start tracking a repeating sequence: flush remaining pending, print marker
// and the first occurrence
static void begin_sequence(int L) {
      for (int i = 0; i < L; i++)
            g_seq_ref[i] = g_pending[g_pending_count - 2 * L + i];
      g_seq_len        = L;
      g_seq_repeat     = 2;
      g_seq_pos        = 0;
      g_pending_count -= 2 * L;
      flush_pending();
      out_printf("---\n");
      for (int i = 0; i < g_seq_len; i++)
            print_transaction(&g_seq_ref[i]);
      }

// Flush the staging buffer (run-length accumulator) to the sequence/pending logic.
static void flush_staging(void) {
      if (g_staging_valid) {
            add_transaction_inner(&g_staging);
            g_staging_valid = false;
            }
      }

// Run-length pre-compression wrapper: merges consecutive identical transactions
// into a single Transaction with an incremented repeat count before passing to
// the sequence detection logic.
static void add_transaction(const Transaction* t) {
      if (g_staging_valid && transactions_identical(&g_staging, t)) {
            g_staging.repeat++;
            return;
            }
      flush_staging();
      g_staging        = *t;
      g_staging.repeat = 1;
      g_staging_valid  = true;
      }

static void add_transaction_inner(const Transaction* t) {
      // If we are tracking a sequence, check if this transaction matches
      if (g_seq_len > 0) {
            const Transaction* ref = &g_seq_ref[g_seq_pos];
            if (transactions_similar(t, ref)) {
                  g_seq_pos++;
                  if (g_seq_pos == g_seq_len) {
                        g_seq_repeat++;
                        g_seq_pos = 0;
                        if (g_seq_repeat >= MAX_SEQ_REPEAT)
                              flush_sequence();
                        }
                  return; // absorbed
                  }
            // Mismatch — close the sequence and fall through
            flush_sequence();
            }

      // Add to pending buffer
      if (g_pending_count >= MAX_PENDING) {
            print_transaction(&g_pending[0]);
            memmove(&g_pending[0], &g_pending[1], (g_pending_count - 1) * sizeof(Transaction));
            g_pending_count--;
            }
      g_pending[g_pending_count] = *t;
      g_pending_count++;

      // Try to detect a new repeating sequence
      if (g_pending_count >= 2 * MAX_SEQ_LEN) {
            int L = detect_sequence();
            if (L > 0)
                  begin_sequence(L);
            }
      }

static void flush_all(void) {
      if (g_pending_cmd_valid) {
            g_pending_cmd.has_in = false;
            add_transaction(&g_pending_cmd);
            g_pending_cmd_valid = false;
            }
      flush_staging();
      flush_sequence();
      flush_pending();
      if (g_out_file)
            fflush(g_out_file);
      }

/*- Transaction Processor ---------------------------------------------------*/
//
// Called by both backends with (timestamp_us, direction, payload, payload_len).
// Collects OUT+IN pairs into Packet6 transactions and emits list transactions
// directly.
//

static void feed_packet(u64 ts_us, int dir, const u8* payload, int plen) {
      if (g_first_ts == 0)
            g_first_ts = ts_us;

      if (dir == 0) { // OUT (host → device)
            if (plen == 12) {
                  // Packet6 command — buffer and wait for IN response
                  if (g_pending_cmd_valid) {
                        // Previous command had no response — flush it
                        g_pending_cmd.has_in = false;
                        add_transaction(&g_pending_cmd);
                        }
                  memset(&g_pending_cmd, 0, sizeof(g_pending_cmd));
                  memcpy(g_pending_cmd.out_data, payload, 12);
                  g_pending_cmd.out_size = 12;
                  g_pending_cmd.out_ts   = ts_us;
                  g_pending_cmd.is_list  = false;
                  g_pending_cmd_valid    = true;
                  }
            else if (plen >= 24 && (plen % 12) == 0) {
                  // List packet (0xC00 bytes = 256 × Packet6) — no response expected
                  if (g_pending_cmd_valid) {
                        g_pending_cmd.has_in = false;
                        add_transaction(&g_pending_cmd);
                        g_pending_cmd_valid = false;
                        }
                  Transaction t;
                  memset(&t, 0, sizeof(t));
                  int copy = plen > MAX_PKT_SIZE ? MAX_PKT_SIZE : plen;
                  memcpy(t.out_data, payload, copy);
                  t.out_size = copy;
                  t.out_ts   = ts_us;
                  t.is_list  = true;
                  add_transaction(&t);
                  }
            else {
                  // Unknown OUT packet — flush pending and ignore
                  if (g_pending_cmd_valid) {
                        g_pending_cmd.has_in = false;
                        add_transaction(&g_pending_cmd);
                        g_pending_cmd_valid = false;
                        }
                  }
            }
      else { // IN (device → host)
            if (g_pending_cmd_valid) {
                  int copy = plen > 16 ? 16 : plen;
                  memcpy(g_pending_cmd.in_data, payload, copy);
                  g_pending_cmd.in_size = copy;
                  g_pending_cmd.has_in  = true;
                  add_transaction(&g_pending_cmd);
                  g_pending_cmd_valid = false;
                  }
            // IN without preceding OUT — ignore
            }
      }

/*- usbmon Backend ----------------------------------------------------------*/

static bool find_bjjcz_device(void) {
      libusb_device** devices;
      int count = libusb_get_device_list(NULL, &devices);
      if (count < 0)
            return false;
      bool found = false;
      for (int i = 0; i < count; i++) {
            struct libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(devices[i], &desc) < 0)
                  continue;
            if (desc.idVendor == BJJCZ_VID && desc.idProduct == BJJCZ_PID) {
                  g_target_bus = libusb_get_bus_number(devices[i]);
                  g_target_dev = libusb_get_device_address(devices[i]);
                  found        = true;
                  break;
                  }
            }
      libusb_free_device_list(devices, 1);
      return found;
      }

static int open_usbmon(int busnum) {
      char path[64];
      snprintf(path, sizeof(path), "/dev/usbmon%d", busnum);
      int fd = open(path, O_RDONLY);
      if (fd < 0) {
            fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
            fprintf(stderr, "Make sure usbmon is loaded: modprobe usbmon\n");
            return -1;
            }
      return fd;
      }

static void process_event(const MonBinHdr* hdr, const u8* data) {
      if (hdr->busnum != g_target_bus || hdr->devnum != g_target_dev)
            return;

      int dir = (hdr->epnum & EP_DIR_IN) ? 1 : 0;

      // OUT: use 'S' (submit — data leaving host)
      // IN:  use 'C' (callback — data arrived at host)
      if (dir == 0 && hdr->type != MON_TYPE_SUBMIT)
            return;
      if (dir == 1 && hdr->type != MON_TYPE_CALLBACK)
            return;

      u32 len_cap = hdr->len_cap;
      if (len_cap == 0 || len_cap > MAX_PKT_SIZE)
            return;

      u64 ts_us = get_ts_us();
      feed_packet(ts_us, dir, data, (int)len_cap);
      }

static void capture_loop_usbmon(int fd) {
      u8 buf[READ_BUF_SIZE];
      int buf_len = 0;

      while (g_running) {
            int r = read(fd, buf + buf_len, sizeof(buf) - buf_len);
            if (r < 0) {
                  if (errno == EINTR)
                        continue;
                  if (errno == EAGAIN) {
                        usleep(1000);
                        continue;
                        }
                  fprintf(stderr, "Error reading usbmon: %s\n", strerror(errno));
                  break;
                  }
            if (r == 0) {
                  usleep(1000);
                  continue;
                  }
            buf_len += r;

            // Process complete events: 48-byte header + len_cap bytes data
            int offset = 0;
            while (offset + MON_HDR_SIZE <= buf_len) {
                  MonBinHdr* hdr = (MonBinHdr*)(buf + offset);
                  u32 len_cap    = hdr->len_cap;
                  int pkt_size   = MON_HDR_SIZE + (int)len_cap;

                  if (offset + pkt_size > buf_len)
                        break; // incomplete event, wait for more data

                  const u8* data = (len_cap > 0) ? (buf + offset + MON_HDR_SIZE) : NULL;
                  process_event(hdr, data);
                  offset += pkt_size;
                  }

            // Move remaining data to start of buffer
            int remaining = buf_len - offset;
            if (remaining > 0 && offset > 0)
                  memmove(buf, buf + offset, remaining);
            buf_len = remaining;
            }
      }

/*- Hardware Sniffer Backend ------------------------------------------------*/

// Parse a raw USB packet from the FPGA capture and feed to the transaction
// processor.  Filters token/handshake/SOF packets and extracts the payload
// from DATA0/DATA1 packets.
static void handle_hw_packet(u64 ts_ns, u8* data, int size) {
      if (size < 1)
            return;
      u8 pid_type = data[0] & 0x0f;

      // Token packets: track direction, then discard
      if (pid_type == 0x01 || pid_type == 0x09 || pid_type == 0x0d) {
            if (size >= 3) {
                  int ep0     = data[1] & 1;
                  int ep_rest = (data[2] >> 5) & 0x07;
                  g_last_ep   = (ep_rest << 1) | ep0;
                  g_last_dir  = (pid_type == 0x09) ? 1 : 0; // IN=0x09, OUT=0x01
                  }
            return;
            }
      // Discard SOF, ACK, NAK, STALL, NYET
      if (pid_type == 0x05 || pid_type == 0x02 || pid_type == 0x0a || pid_type == 0x0e || pid_type == 0x06)
            return;
      // Only process DATA0 (0x03) and DATA1 (0x0b)
      if (pid_type != 0x03 && pid_type != 0x0b)
            return;

      int pkt_dir = g_last_dir;

      // Extract payload: strip PID byte (offset 0) and CRC16 (last 2 bytes)
      int plen = size - 3;
      if (plen <= 0)
            return;
      const u8* payload = data + 1;

      u64 ts_us = ts_ns / 1000; // convert nanoseconds → microseconds
      feed_packet(ts_us, pkt_dir, payload, plen);
      }

// FPGA capture state machine — parses the byte stream from the hardware
// sniffer into individual USB packets.
static inline void capture_sm(u8 byte) {
      if (capture_header && capture_data_ptr == 0) {
            capture_status = (0 == (byte & HEADER_STATUS));
            capture_size   = capture_status ? STATUS_HEADER_SIZE : DATA_HEADER_SIZE;
            }
      capture_data[capture_data_ptr++] = byte;
      if (capture_data_ptr < capture_size)
            return;

      if (capture_header) {
            int ts     = ((capture_data[0] & 0xf) << 16) | (capture_data[1] << 8) | capture_data[2];
            int toggle = (capture_data[0] & HEADER_TOGGLE) ? 1 : 0;
            int zero   = (capture_data[0] & HEADER_ZERO) ? 1 : 0;
            if (toggle != capture_toggle)
                  fprintf(stderr, "Warning: toggle mismatch (%d vs %d)\n", toggle, capture_toggle);
            if (zero)
                  fprintf(stderr, "Warning: zero bit set in header\n");
            if (capture_data[0] & HEADER_TS_OVERFLOW)
                  capture_ts_int += 0x100000;
            capture_ts     = ((capture_ts_int | ts) * 100) / 6;
            capture_toggle = 1 - toggle;

            if (capture_status) {
                  capture_data_ptr = 0;
                  }
            else {
                  int size = (((int)capture_data[3] & 0x7) << 8) | capture_data[4];
                  if (size < DATA_HEADER_SIZE || size > MAX_DATA_SIZE) {
                        fprintf(stderr, "Error: invalid data size (%d)\n", size);
                        capture_data_ptr = 0;
                        capture_header   = true;
                        return;
                        }
                  capture_size       = size - DATA_HEADER_SIZE;
                  capture_header     = (0 == capture_size);
                  capture_overflow   = (capture_data[3] & HEADER_OVERFLOW) ? true : false;
                  capture_crc_error  = (capture_data[3] & HEADER_CRC_ERROR) ? true : false;
                  capture_data_error = (capture_data[3] & HEADER_DATA_ERROR) ? true : false;
                  }
            }
      else {
            capture_header = true;
            if (capture_overflow)
                  fprintf(stderr, "Warning: hardware buffer overflow\n");
            if (capture_crc_error)
                  fprintf(stderr, "Warning: CRC error\n");
            if (capture_data_error)
                  fprintf(stderr, "Warning: USB PHY data error\n");
            handle_hw_packet(capture_ts, capture_data, capture_size);
            }
      capture_data_ptr = 0;
      }

static void LIBUSB_CALL capture_callback(struct libusb_transfer* transfer) {
      if (transfer->actual_length > 0)
            for (int i = 0; i < transfer->actual_length; i++)
                  capture_sm(transfer->buffer[i]);
      if (transfer->status != LIBUSB_TRANSFER_COMPLETED && transfer->status != LIBUSB_TRANSFER_TIMED_OUT)
            fprintf(stderr, "Transfer error: %d\n", transfer->status);
      if (g_running) {
            int rc = libusb_submit_transfer(transfer);
            if (rc < 0)
                  fprintf(stderr, "Failed to resubmit transfer: %s\n", libusb_error_name(rc));
            }
      }

static void ctrl(int index, int value) {
      int val = index | ((value ? 1 : 0) << CTRL_REG_SIZE);
      int rc  = libusb_control_transfer(
          g_handle, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE, 0xd0, val, 0,
          NULL, 0, TIMEOUT);
      if (rc < 0)
            os_error("ctrl transfer failed: %s", libusb_error_name(rc));
      }

static bool open_capture_device(void) {
      libusb_device** devices;
      int count = libusb_get_device_list(NULL, &devices);
      if (count < 0)
            return false;
      for (int i = 0; i < count; i++) {
            struct libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(devices[i], &desc) < 0)
                  continue;
            if (desc.idVendor == CAPTURE_VID && desc.idProduct == CAPTURE_PID) {
                  int rc = libusb_open(devices[i], &g_handle);
                  if (rc < 0) {
                        fprintf(stderr, "Cannot open sniffer device: %s\n", libusb_error_name(rc));
                        continue;
                        }
                  break;
                  }
            }
      libusb_free_device_list(devices, 1);
      if (!g_handle)
            return false;
      libusb_set_auto_detach_kernel_driver(g_handle, 1);
      int rc = libusb_claim_interface(g_handle, 0);
      if (rc < 0)
            os_error("Cannot claim interface: %s", libusb_error_name(rc));
      return true;
      }

static void flush_data(void) {
      u8 buf[DATA_ENDPOINT_SIZE];
      int size;
      for (int k = 0; k < 100; k++) {
            int rc = libusb_bulk_transfer(g_handle, DATA_ENDPOINT, buf, sizeof(buf), &size, 20);
            if (rc == LIBUSB_ERROR_TIMEOUT)
                  break;
            if (rc < 0)
                  os_error("flush bulk transfer failed: %s", libusb_error_name(rc));
            }
      }

static void hw_init(void) {
      ctrl(CaptureCtrl_Reset, 1);
      ctrl(CaptureCtrl_Enable, 0);
      ctrl(CaptureCtrl_Test, 0);
      ctrl(CaptureCtrl_Speed0, 1);
      ctrl(CaptureCtrl_Speed0, 0);
      ctrl(CaptureCtrl_Speed1, 1);
      ctrl(CaptureCtrl_Speed1, 0);
      }

static void stop_capture(void) {
      ctrl(CaptureCtrl_Enable, 0);
      hw_init();
      }

static void start_capture_hw(void) {
      fprintf(stderr,
              "USB Sniffer (hardware) — capturing BJJCZ USBLMCV2 "
              "(VID=%04x PID=%04x)\n",
              BJJCZ_VID, BJJCZ_PID);
      fprintf(stderr, "Speed: %s\n\n",
              g_speed == CaptureSpeed_LS   ? "Low-Speed"
              : g_speed == CaptureSpeed_FS ? "Full-Speed"
                                           : "High-Speed");

      hw_init();
      ctrl(CaptureCtrl_Enable, 0);
      ctrl(CaptureCtrl_Reset, 1);
      flush_data();
      ctrl(CaptureCtrl_Speed0, g_speed & 1);
      ctrl(CaptureCtrl_Speed1, g_speed & 2);
      ctrl(CaptureCtrl_Reset, 0);
      ctrl(CaptureCtrl_Enable, 1);

      for (int i = 0; i < TRANSFER_COUNT; i++) {
            g_buffers[i]   = os_alloc(TRANSFER_SIZE);
            g_transfers[i] = libusb_alloc_transfer(0);
            os_check(g_transfers[i], "libusb_alloc_transfer()");
            libusb_fill_bulk_transfer(g_transfers[i], g_handle, DATA_ENDPOINT, g_buffers[i], TRANSFER_SIZE,
                                      capture_callback, NULL, TRANSFER_TIMEOUT);
            int rc = libusb_submit_transfer(g_transfers[i]);
            if (rc < 0)
                  os_error("libusb_submit_transfer(): %s", libusb_error_name(rc));
            }

      while (g_running) {
            struct timeval tv = {0, 100000};
            libusb_handle_events_timeout(NULL, &tv);
            }

      // Stop hardware first so it stops feeding the FIFO
      stop_capture();

      // Cancel and drain transfers
      for (int i = 0; i < TRANSFER_COUNT; i++)
            if (g_transfers[i])
                  libusb_cancel_transfer(g_transfers[i]);
      struct timeval tv_drain = {0, 100000};
      libusb_handle_events_timeout(NULL, &tv_drain);

      for (int i = 0; i < TRANSFER_COUNT; i++) {
            if (g_transfers[i])
                  libusb_free_transfer(g_transfers[i]);
            if (g_buffers[i])
                  os_free(g_buffers[i]);
            }
      }

/*- Signal Handler ----------------------------------------------------------*/

static void sig_handler(int sig) {
      (void)sig;
      g_running = 0;
      }

/*- Usage and Args ----------------------------------------------------------*/

static void usage(const char* prog) {
      printf("USB Sniffer for BJJCZ USBLMCV2\n\n");
      printf("Usage: %s [options]\n\n", prog);
      printf("Options:\n");
      printf("  -o <file>    Write output also to <file>\n");
      printf("  -s           Read from sniffer hardware instead of usbmon\n");
      printf("  --speed <s>  USB speed (hardware mode): ls, fs, hs (default: hs)\n");
      printf("  -h, --help   Show this help message\n");
      }

static void parse_args(int argc, char* argv[]) {
      for (int i = 1; i < argc; i++) {
            char* arg = argv[i];
            if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
                  usage(argv[0]);
                  exit(0);
                  }
            else if (!strcmp(arg, "-s")) {
                  g_use_hw = true;
                  }
            else if (!strcmp(arg, "-o")) {
                  if (++i >= argc)
                        os_error("-o requires an argument");
                  g_out_file = fopen(argv[i], "w");
                  if (!g_out_file)
                        os_error("cannot open output file '%s': %s", argv[i], strerror(errno));
                  }
            else if (!strcmp(arg, "--speed")) {
                  if (++i >= argc)
                        os_error("--speed requires an argument");
                  if (!strcmp(argv[i], "ls"))
                        g_speed = CaptureSpeed_LS;
                  else if (!strcmp(argv[i], "fs"))
                        g_speed = CaptureSpeed_FS;
                  else if (!strcmp(argv[i], "hs"))
                        g_speed = CaptureSpeed_HS;
                  else
                        os_error("unknown speed: '%s'", argv[i]);
                  }
            else {
                  os_error("unknown option: '%s' (use -h for help)", arg);
                  }
            }
      }

/*- Main --------------------------------------------------------------------*/

int main(int argc, char* argv[]) {
      parse_args(argc, argv);
      setvbuf(stdout, NULL, _IONBF, 0);

      struct sigaction sa;
      sa.sa_handler = sig_handler;
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = 0;
      sigaction(SIGINT, &sa, NULL);
      sigaction(SIGTERM, &sa, NULL);

      int rc = libusb_init(NULL);
      if (rc < 0)
            os_error("libusb_init(): %s", libusb_error_name(rc));

      if (g_use_hw) {
            // Hardware sniffer mode
            if (!open_capture_device())
                  os_error("could not open USB sniffer device (VID=%04x PID=%04x). "
                           "Is it connected and udev rules installed?",
                           CAPTURE_VID, CAPTURE_PID);
            start_capture_hw();
            }
      else {
            // usbmon mode (default)
            if (!find_bjjcz_device()) {
                  fprintf(stderr, "BJJCZ USBLMCV2 (VID=%04x PID=%04x) not found.\n", BJJCZ_VID, BJJCZ_PID);
                  fprintf(stderr, "Is it connected via USB?\n");
                  libusb_exit(NULL);
                  return 1;
                  }
            int fd = open_usbmon(g_target_bus);
            if (fd < 0) {
                  libusb_exit(NULL);
                  return 1;
                  }
            fprintf(stderr,
                    "USB Sniffer (usbmon) — capturing BJJCZ USBLMCV2 "
                    "(VID=%04x PID=%04x)\n",
                    BJJCZ_VID, BJJCZ_PID);
            fprintf(stderr, "Bus %d, Device %d\n\n", g_target_bus, g_target_dev);

            capture_loop_usbmon(fd);
            close(fd);
            }

      flush_all();
      out_printf("\n--- Capture stopped ---\n");

      if (g_out_file)
            fclose(g_out_file);
      libusb_exit(NULL);
      return 0;
      }
