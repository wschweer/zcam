//=============================================================================
//  ZCam - manufactoring tool for G-code machines and Fiber Laser
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
#include <QSocketNotifier>
#include <QByteArray>
#include <QString>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class ZCam;
class AIAgent;

//--------------------------------------------------------------------
//     RemoteControl
//--------------------------------------------------------------------
//  Line-based stdin remote-control interface for ZCam (test /
//  automation use).
//
//  When active (see active()), the process reads single-line commands
//  from standard input (fd 0), dispatches each command and writes
//  exactly one line of JSON to standard output (fd 1), flushing it
//  immediately.  This lets an external program drive the live,
//  already-running GUI application through the same tool interface
//  that the AI agent (AIAgent) uses.
//
//  Command grammar (one command per line):
//
//      <tool> [arguments-json]
//
//    *tool* is one of the AIAgent tool names (e.g. list_elements,
//    write_property, ...).  *arguments-json* is an optional
//    single-line JSON object with the tool's parameters.
//
//  Special commands:
//
//      help                 -> list all available tools + their schemas
//      tools                -> same as help (alias)
//      status               -> basic application state
//      quit | exit          -> close the application
//
//  Every response is a single compact JSON object:
//
//      { "ok": true,  "cmd": "write_property", ... tool result ... }
//      { "ok": false, "cmd": "...", "error": "..." }
//
//  The protocol is intentionally simple and self-describing so that an
//  LLM can operate the application by first issuing `help`.
//--------------------------------------------------------------------

class RemoteControl : public QObject
      {
      Q_OBJECT

    private:
      ZCam* _zc {nullptr};
      AIAgent* _ai {nullptr};

      bool _active {false};
      QSocketNotifier* _stdinNotifier {nullptr};
      QByteArray _buffer; // partial line carried over across reads

      /// Resolve the ZCam singleton and its AIAgent (lazily, so the
      /// first command works even if ZCam was created after we attached).
      void refreshAgent();

      void enable();
      void disable();

      // ── Command handling ────────────────────────────────────────────
      /// Process one complete command line: dispatch and write the
      /// JSON response to stdout (single line, flushed).
      void processCommand(const QByteArray& line);

      /// Build the response payload for the `help` / `tools` command.
      QString helpResponse() const;
      /// Build the response payload for the `status` command.
      QString statusResponse() const;

      /// Append *text* to stdout as a single line and flush.
      void emitLine(const QString& text) const;

    public:
      explicit RemoteControl(QObject* parent = nullptr);
      ~RemoteControl() override;
      /// True while the stdin reader is attached.
      bool active() const { return _active; }
      /// Start listening on stdin.  Safe to call even if stdin is a
      /// terminal (it simply won't receive piped commands).  Calling
      /// twice is a no-op.
      void start();

    private slots:
      void onStdinRead();
      };
