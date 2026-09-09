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

#include "remotecontrol.h"
#include "zcam.h"
#include "ai_agent.h"
#include "project.h"
#include "element3d.h"
#include "logger.h"

#include <QCoreApplication>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>

#include <unistd.h>

using json = nlohmann::json;

//--------------------------------------------------------------------
//     RemoteControl
//--------------------------------------------------------------------

RemoteControl::RemoteControl(QObject* parent) : QObject(parent) {
      }

//--------------------------------------------------------------------
//     ~RemoteControl
//--------------------------------------------------------------------

RemoteControl::~RemoteControl() {
      disable();
      }

//--------------------------------------------------------------------
//     refreshAgent
//--------------------------------------------------------------------

void RemoteControl::refreshAgent() {
      if (!_zc)
            _zc = ZCam::instance();
      if (!_ai && _zc)
            _ai = _zc->aiAgent();
      }

//--------------------------------------------------------------------
//     enable / disable
//--------------------------------------------------------------------

void RemoteControl::enable() {
      if (_active)
            return;
      refreshAgent();
      _active = true;
      Debug("stdin interface enabled (AI agent: {})", _ai ? "available" : "MISSING");
      }

void RemoteControl::disable() {
      if (_stdinNotifier) {
            _stdinNotifier->setEnabled(false);
            _stdinNotifier->deleteLater();
            _stdinNotifier = nullptr;
            }
      _active = false;
      }

//--------------------------------------------------------------------
//     start
//--------------------------------------------------------------------

void RemoteControl::start() {
      enable();
      // The QSocketNotifier on fd 0 only fires when there is actually
      // data available (Qt internally uses poll/select), so attaching
      // it unconditionally is harmless even when stdin is a terminal.
      if (_stdinNotifier)
            return;
      _stdinNotifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
      connect(_stdinNotifier, &QSocketNotifier::activated, this, &RemoteControl::onStdinRead);
      _stdinNotifier->setEnabled(true);
      }

//--------------------------------------------------------------------
//     onStdinRead
//    Read all currently-available bytes from stdin, split them into
//    complete lines and process each one.  Any trailing partial line
//    (no newline yet) is kept in _buffer for the next read.
//--------------------------------------------------------------------

void RemoteControl::onStdinRead() {
      // Read as much as is available right now.
      char block[8192];
      for (;;) {
            std::size_t n = ::read(STDIN_FILENO, block, sizeof(block));
            if (n > 0)
                  _buffer.append(block, static_cast<int>(n));
            if (n < sizeof(block))
                  break; // drained (or non-blocking EOF)
            }

      // Emit every complete line.
      int nl;
      while ((nl = _buffer.indexOf('\n')) >= 0) {
            QByteArray line = _buffer.left(nl);
            _buffer.remove(0, nl + 1);
            // Strip a trailing '\r' (CRLF input) and surrounding spaces.
            while (!line.isEmpty() && (line.back() == '\r' || line.back() == ' '))
                  line.chop(1);
            if (line.trimmed().isEmpty())
                  continue;
            processCommand(line);
            }
      }

//--------------------------------------------------------------------
//     helpResponse
//--------------------------------------------------------------------

QString RemoteControl::helpResponse() const {
      json out;
      out["ok"]  = true;
      out["cmd"] = "help";
      out["note"] =
          "Command grammar: '<tool> [arguments-json]'  — arguments-json is a single-line JSON object.";
      json tools  = json::array();
      AIAgent* ai = _zc ? _zc->aiAgent() : (ZCam::instance() ? ZCam::instance()->aiAgent() : nullptr);
      if (ai) {
            const QStringList names = ai->toolNames();
            for (const QString& name : names) {
                  QString schema = ai->toolSchema(name);
                  json t;
                  t["name"] = name.toStdString();
                  if (!schema.isEmpty()) {
                        try {
                              json s = json::parse(schema.toStdString());
                              if (s.contains("description"))
                                    t["description"] = s["description"];
                              if (s.contains("inputSchema"))
                                    t["parameters"] = s["inputSchema"];
                              }
                        catch (...) {
                              }
                        }
                  tools.push_back(t);
                  }
            }
      out["tools"] = tools;
      return QString::fromStdString(out.dump());
      }

//--------------------------------------------------------------------
//     statusResponse
//--------------------------------------------------------------------

QString RemoteControl::statusResponse() const {
      json out;
      out["ok"]  = true;
      out["cmd"] = "status";
      ZCam* zc   = ZCam::instance();
      if (zc) {
            out["hasProject"]    = zc->project() != nullptr;
            out["aiAgentActive"] = zc->aiAgent() != nullptr;
            if (zc->project()) {
                  out["projectPath"] = zc->project()->projectPath().toStdString();
                  out["projectName"] = zc->project()->projectName().toStdString();
                  }
            if (zc->currentElement())
                  out["currentElement"] = zc->currentElement()->name().toStdString();
            json sel = json::array();
            for (Element3d* e : zc->selectedElements())
                  sel.push_back(e->name().toStdString());
            out["selectedElements"] = sel;
            }
      else {
            out["error"] = "ZCam instance not ready";
            }
      return QString::fromStdString(out.dump());
      }

//--------------------------------------------------------------------
//     emitLine
//--------------------------------------------------------------------

void RemoteControl::emitLine(const QString& text) const {
      // The protocol uses stdout exclusively.  Write the (already
      // single-line) JSON object and a newline, then flush so the
      // client sees it immediately.
      std::cout << text.toStdString() << '\n';
      std::cout.flush();
      }

//--------------------------------------------------------------------
//     processCommand
//--------------------------------------------------------------------

void RemoteControl::processCommand(const QByteArray& line) {
      QString raw = QString::fromUtf8(line).trimmed();

      // ── Special commands ────────────────────────────────────────────
      const QString cmdWord = raw.section(' ', 0, 0).toLower();
      if (cmdWord == "help" || cmdWord == "tools") {
            emitLine(helpResponse());
            return;
            }
      if (cmdWord == "status") {
            emitLine(statusResponse());
            return;
            }
      if (cmdWord == "quit" || cmdWord == "exit" || cmdWord == "bye") {
            json out;
            out["ok"]  = true;
            out["cmd"] = "quit";
            out["msg"] = "quitting";
            emitLine(QString::fromStdString(out.dump()));
            if (QCoreApplication::instance())
                  QCoreApplication::quit();
            return;
            }

      // ── Tool command: "<tool> [arguments-json]" ─────────────────────
      int space        = raw.indexOf(' ');
      QString toolName = space >= 0 ? raw.left(space).trimmed() : raw;
      QString argsJson = space >= 0 ? raw.mid(space + 1).trimmed() : QString();

      refreshAgent();
      if (!_ai) {
            json out;
            out["ok"]    = false;
            out["cmd"]   = toolName.toStdString();
            out["error"] = "AI agent not available (ZCam not ready?)";
            emitLine(QString::fromStdString(out.dump()));
            return;
            }

      Debug("RemoteControl: <{}> {}", toolName.toStdString(), argsJson.toStdString());

      // The `screenshot` tool embeds a large base64 payload by default.
      // Over the stdin protocol that floods stdout — the PNG file is
      // already written to disk and its path is returned, which is
      // usually all an external driver needs.  Suppress the image unless
      // the caller explicitly asked for it.
      QString invokeArgs = argsJson;
      if (toolName == "screenshot" && !invokeArgs.contains("\"with_image\"")) {
            json a;
            try {
                  a = invokeArgs.isEmpty() ? json::object() : json::parse(invokeArgs.toStdString());
                  }
            catch (...) {
                  a = json::object();
                  }
            a["with_image"] = false;
            a["inline"]     = false;
            invokeArgs      = QString::fromStdString(a.dump());
            }
      QString result = _ai->invokeTool(toolName, invokeArgs);

      // Merge the raw tool result into a single JSON object that also
      // carries the command name and a top-level ok flag.
      json out;
      out["cmd"] = toolName.toStdString();
      try {
            json parsed = json::parse(result.toStdString());
            if (parsed.is_object())
                  for (auto it = parsed.begin(); it != parsed.end(); ++it)
                        if (it.key() != "ok")
                              out[it.key()] = it.value();
            out["ok"] = parsed.value("ok", false);
            }
      catch (...) {
            // Result was not valid JSON (unexpected) — embed verbatim.
            out["ok"]     = false;
            out["result"] = result.toStdString();
            }
      emitLine(QString::fromStdString(out.dump()));
      }
