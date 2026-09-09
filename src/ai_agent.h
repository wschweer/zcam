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
#include <QQmlEngine>
#include <QString>
#include <QList>
#include <QNetworkAccessManager>
#include <nlohmann/json.hpp>

#include "logger.h"

using json = nlohmann::json;

class ZCam;
class Element;
class QNetworkReply;
class QTimer;

//---------------------------------------------------------
//   MCPToolBuilder
//    Convenience helper for building OpenAI/Ollama compatible
//    tool schemas.
//
//    The produced JSON matches the "inputSchema" convention:
//
//      {
//        "name":        "tool_name",
//        "description": "what the tool does",
//        "inputSchema": {
//          "type": "object",
//          "properties": { ... },
//          "required": [ ... ]
//        }
//      }
//
//    AIAgent::buildTools() converts these into Ollama's
//    { "type": "function", "function": { "name", "description",
//    "parameters" } } format automatically.
//---------------------------------------------------------

class MCPToolBuilder
      {
      json tool;

    public:
      MCPToolBuilder(const std::string& name, const std::string& description) {
            tool["name"]                      = name;
            tool["description"]               = description;
            tool["inputSchema"]["type"]       = "object";
            tool["inputSchema"]["properties"] = json::object();
            tool["inputSchema"]["required"]   = json::array();
            }
      MCPToolBuilder& add_parameter(const std::string& name, const std::string& type,
          const std::string& description, bool required = true) {
            tool["inputSchema"]["properties"][name] = {
                     {       "type",        type},
                     {"description", description},
                  };
            if (required)
                  tool["inputSchema"]["required"].push_back(name);
            return *this;
            }
      json build() const { return tool; }
      };

//---------------------------------------------------------
//   AIAgent
//    LLM-driven agent that lets a language model control the
//    ZCam application through a small set of "tools".  The
//    agent talks to an Ollama server over HTTP and exposes the
//    session to the QML AiPanel.
//
//    Tools exposed to the LLM:
//      - project commands:    new_project, save_project,
//                             start_session, end_session,
//                             undo, redo
//      - element commands:    create_element, delete_element,
//                             move_element
//      - property commands:   read_property, write_property,
//                             list_properties, describe_property
//---------------------------------------------------------

class AIAgent : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("Access via ZCam.aiAgent")

      // ── Configurable, persisted in Config (assets.json) ────────────
      Q_PROPERTY(QString ollamaModel READ ollamaModel WRITE set_ollamaModel NOTIFY ollamaModelChanged)
      Q_PROPERTY(QString ollamaBaseUrl READ ollamaBaseUrl WRITE set_ollamaBaseUrl NOTIFY ollamaBaseUrlChanged)
      Q_PROPERTY(double temperature READ temperature WRITE set_temperature NOTIFY temperatureChanged)
      Q_PROPERTY(int contextSize READ contextSize WRITE set_contextSize NOTIFY contextSizeChanged)

      // ── Available Ollama models (fetched from server) ─────────────
      Q_PROPERTY(QStringList ollamaModels READ ollamaModels NOTIFY ollamaModelsChanged)

      // ── Session state ───────────────────────────────────────────────
      Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
      Q_PROPERTY(QString sessionName READ sessionName NOTIFY sessionNameChanged)
      Q_PROPERTY(QString currentMessage READ currentMessage NOTIFY currentMessageChanged)
      Q_PROPERTY(QStringList sessionList READ sessionList NOTIFY sessionListChanged)
      Q_PROPERTY(int currentSession READ currentSession WRITE setCurrentSession NOTIFY currentSessionChanged)

      ZCam* _zc {nullptr};

      QNetworkAccessManager _network;
      QNetworkReply* _reply {nullptr};
      bool _busy {false};
      bool _stopRequested {false};
      QString _streamBuffer;
      QString _currentMessage;
      QString _sessionName;
      QStringList _sessionList;
      int _currentSessionIndex {-1};
      json _history;

      // Config parameters (in-memory defaults, loaded from / saved to
      // Config on demand so we don't require Config to be initialised
      // before the AI is constructed).
      QString _ollamaModel {"llama3.1"};
      QString _ollamaBaseUrl {"http://localhost:11434/api/chat"};
      double _temperature {0.2};
      int _contextSize {4096};

      // ── Available models (cached from last /api/tags request)
      QStringList _ollamaModels;
      QNetworkReply* _modelsReply {nullptr};

      // ── Tool registry ──────────────────────────────────────────────
      std::vector<json> _tools;
      void buildTools();

      // ── Session helpers ────────────────────────────────────────────
      QString sessionDirectory() const;
      QString nextSessionPath() const;
      void loadSession(int index);
      void saveCurrentSession();
      QString sessionFileName(int index) const;

      // ── LLM round-trip ─────────────────────────────────────────────
      json buildRequestJson() const;
      void trimHistory();
      std::string executeTool(const std::string& functionName, const json& arguments);
      std::string errorResponse(const std::string& message) const;

      // ── Tool implementations (return a JSON-serialised result) ──────
      std::string toolNewProject() const;
      std::string toolSaveProject() const;
      std::string toolStartSession() const;
      std::string toolEndSession() const;
      std::string toolUndo() const;
      std::string toolRedo() const;
      std::string toolCreateElement(const json& args);
      std::string toolDeleteElement(const json& args);
      std::string toolRenameElement(const json& args);
      std::string toolMoveElement(const json& args);
      std::string toolReadProperty(const json& args);
      std::string toolWriteProperty(const json& args);
      std::string toolListProperties(const json& args);
      std::string toolDescribeProperty(const json& args);
      std::string toolInvokeMethod(const json& args);
      std::string toolListMethods(const json& args);
      std::string toolListElements(const json& args);
      std::string toolScreenshot(const json& args);
      std::string toolGetCurrentElement(const json& args) const;
      std::string toolSelectElement(const json& args);
      std::string toolGetSelectedElements(const json& args) const;
      std::string toolSelectElements(const json& args);
      std::string toolClearSelection(const json& args);
      std::string toolIsElementInside(const json& args);
      std::string toolSetMops(const json& args);
      std::string toolRunScript(const json& args);

      // ── Element resolution ─────────────────────────────────────────
      Element* resolveElement(const QString& name) const;

      // ── Config access ──────────────────────────────────────────────
      void syncConfig();

    signals:
      void ollamaModelChanged();
      void ollamaBaseUrlChanged();
      void temperatureChanged();
      void contextSizeChanged();

      void ollamaModelsChanged();

      void busyChanged();
      void sessionNameChanged();
      void sessionListChanged();
      void currentSessionChanged();
      void currentMessageChanged();

      /// Emitted after a session has been loaded (selectSession) or
      /// cleared (newSession) so QML can rebuild the conversation view.
      void sessionLoaded();

      /// Emitted for each incremental token-chunk from the LLM.
      /// The first argument is the "thinking" stream (may be empty),
      /// the second the visible text.
      void chunkReceived(const QString& thought, const QString& text);

      /// Emitted when a tool call has been received and its result
      /// produced.  role = "tool"; name = tool name; result = text.
      void toolCallResult(const QString& name, const QString& result);

      /// Emitted when the agent finishes a full response.
      void finished(const QString& fullText);

      /// Emitted when a network / LLM error occurred.
      void agentError(const QString& message);

    public:
      explicit AIAgent(QObject* parent = nullptr);
      ~AIAgent() override;

      /// Called once after the ZCam singleton is known, so that
      /// tools can operate on the live project tree.
      void setZCam(ZCam* zc);
      ZCam* zcam() const { return _zc; }
      // ── Config accessors ───────────────────────────────────────────
      QString ollamaModel() const { return _ollamaModel; }
      void set_ollamaModel(const QString& v);
      QString ollamaBaseUrl() const { return _ollamaBaseUrl; }
      void set_ollamaBaseUrl(const QString& v);
      double temperature() const { return _temperature; }
      void set_temperature(double v);
      int contextSize() const { return _contextSize; }
      void set_contextSize(int v);
      // ── Available models ─────────────────────────────────────────
      QStringList ollamaModels() const { return _ollamaModels; }
      /// Query the Ollama server for the list of installed models.
      /// The result is cached and the ollamaModelsChanged signal is
      /// emitted when the list is updated.
      Q_INVOKABLE void refreshOllamaModels();
      // ── Session ────────────────────────────────────────────────────
      bool busy() const { return _busy; }
      QString sessionName() const { return _sessionName; }
      QStringList sessionList() const { return _sessionList; }
      int currentSession() const { return _currentSessionIndex; }
      void setCurrentSession(int index);
      QString currentMessage() const { return _currentMessage; }
      /// Refresh the list of sessions on disk and re-read the
      /// currently-selected one.
      Q_INVOKABLE void refreshSessionList();
      /// Clear the in-memory history and start a new session.
      Q_INVOKABLE void newSession();
      /// Load the session at the given index (into memory + disk).
      Q_INVOKABLE void selectSession(int index);
      /// Delete the session file at the given index.
      Q_INVOKABLE void deleteSession(int index);

      /// Returns the conversation history as a list of objects with
      /// "role" ("user"/"assistant"/"tool") and "text" (displayable
      /// content).  Used by QML to rebuild the conversation view after
      /// loading a session.
      Q_INVOKABLE QVariantList sessionConversation() const;

      /// User-facing entry point: send the given text to the LLM
      /// and begin the (possibly multi-turn) tool-execution loop.
      Q_INVOKABLE void sendMessage(const QString& text);
      /// Abort the current LLM / tool loop.
      Q_INVOKABLE void stop();

      /// Returns the JSON tool schema (OpenAI / Ollama format)
      /// for a given tool name — used in tests and debugging.
      Q_INVOKABLE QString toolSchema(const QString& name) const;
      /// Return a list of all known tool names (for debugging).
      Q_INVOKABLE QStringList toolNames() const;

      //--------------------------------------------------------------------
      //     invokeTool
      //--------------------------------------------------------------------
      /// Public entry point that dispatches a single tool call by name.
      /// *argumentsJson* is a JSON object (or an empty string for no
      /// arguments) with the tool's parameters, e.g.
      ///   invokeTool("write_property", "{\"element\":\"r1\",\"property\":\"width\",\"value\":\"120\"}")
      /// Returns the tool's result as a compact JSON string
      /// (e.g. `{"ok":true,...}` or `{"ok":false,"error":"..."}`).
      ///
      /// This is the same dispatch used by the LLM chat loop
      /// (executeTool) and is exposed publicly so that the stdin
      /// remote-control interface (RemoteControl) and QML can invoke any
      /// AI tool without going through the Ollama round-trip.
      Q_INVOKABLE QString invokeTool(const QString& name, const QString& argumentsJson);

    private slots:
      void onReadyRead();
      void onFinished();
      void onModelsReplyFinished();
      };
