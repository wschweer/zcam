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

#include "ai_agent.h"
#include "zcam.h"
#include "project.h"
#include "element.h"
#include "element3d.h"
#include "mop.h"
#include "group.h"
#include "rectangle.h"
#include "polygon.h"
#include "ellipse.h"
#include "text.h"
#include "config.h"
#include "treemodel.h"
#include "scriptengine.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QByteArray>
#include <QBuffer>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QVariant>
#include <QMetaProperty>
#include <QVector2D>
#include <QVector3D>
#include <QColor>
#include <QJSEngine>

#include <algorithm>
#include <cctype>
#include <functional>

//--------------------------------------------------------------------
//     AIAgent
//--------------------------------------------------------------------

AIAgent::AIAgent(QObject* parent) : QObject(parent) {
      buildTools();
      refreshSessionList();
      }

//--------------------------------------------------------------------
//     ~AIAgent
//--------------------------------------------------------------------

AIAgent::~AIAgent() {
      // abort() may synchronously emit finished(), which runs onFinished()
      // and nulls the member pointer — so capture the pointer first.
      if (QNetworkReply* reply = _reply) {
            _reply = nullptr;
            reply->disconnect();
            reply->abort();
            reply->deleteLater();
            }
      if (QNetworkReply* reply = _modelsReply) {
            _modelsReply = nullptr;
            reply->disconnect();
            reply->abort();
            reply->deleteLater();
            }
      }

//--------------------------------------------------------------------
//     setZCam
//--------------------------------------------------------------------

void AIAgent::setZCam(ZCam* zc) {
      _zc = zc;
      if (!_zc)
            return;
      // Sync config parameters from the Config singleton (if any).
      syncConfig();
      // Fetch the list of available models from the Ollama server.
      refreshOllamaModels();
      }

//--------------------------------------------------------------------
//     syncConfig
//    Read AI parameters from the Config singleton (if present).
//--------------------------------------------------------------------

void AIAgent::syncConfig() {
      if (!_zc || !_zc->config())
            return;
      Config* c = _zc->config();
      // Use the Qt meta-object system to read the properties; if the
      // Config does not yet declare them (older projects) we keep the
      // in-memory defaults.
      if (c->metaObject()->indexOfProperty("ollamaModel") >= 0)
            set_ollamaModel(c->property("ollamaModel").toString());
      if (c->metaObject()->indexOfProperty("ollamaBaseUrl") >= 0)
            set_ollamaBaseUrl(c->property("ollamaBaseUrl").toString());
      if (c->metaObject()->indexOfProperty("aiTemperature") >= 0)
            set_temperature(c->property("aiTemperature").toDouble());
      if (c->metaObject()->indexOfProperty("aiContextSize") >= 0)
            set_contextSize(c->property("aiContextSize").toInt());
      }

//--------------------------------------------------------------------
//     Config setters — push the value back into Config if available.
//--------------------------------------------------------------------

void AIAgent::set_ollamaModel(const QString& v) {
      if (_ollamaModel == v)
            return;
      _ollamaModel = v;
      if (_zc && _zc->config() && _zc->config()->metaObject()->indexOfProperty("ollamaModel") >= 0)
            _zc->config()->setProperty("ollamaModel", v);
      emit ollamaModelChanged();
      }

void AIAgent::set_ollamaBaseUrl(const QString& v) {
      if (_ollamaBaseUrl == v)
            return;
      _ollamaBaseUrl = v;
      if (_zc && _zc->config() && _zc->config()->metaObject()->indexOfProperty("ollamaBaseUrl") >= 0)
            _zc->config()->setProperty("ollamaBaseUrl", v);
      emit ollamaBaseUrlChanged();
      // Re-fetch the model list from the new server.
      refreshOllamaModels();
      }

void AIAgent::set_temperature(double v) {
      if (_temperature == v)
            return;
      _temperature = v;
      if (_zc && _zc->config() && _zc->config()->metaObject()->indexOfProperty("aiTemperature") >= 0)
            _zc->config()->setProperty("aiTemperature", v);
      emit temperatureChanged();
      }

void AIAgent::set_contextSize(int v) {
      if (_contextSize == v)
            return;
      _contextSize = v;
      if (_zc && _zc->config() && _zc->config()->metaObject()->indexOfProperty("aiContextSize") >= 0)
            _zc->config()->setProperty("aiContextSize", v);
      emit contextSizeChanged();
      }

//--------------------------------------------------------------------
//     refreshOllamaModels
//    Send a GET request to the Ollama server's /api/tags endpoint
//    to retrieve the list of installed models.  The base URL is
//    derived from _ollamaBaseUrl by replacing "/api/chat" with
//    "/api/tags".  When the reply arrives, onModelsReplyFinished()
//    parses the JSON response and updates _ollamaModels.
//--------------------------------------------------------------------

void AIAgent::refreshOllamaModels() {
      // Derive the /api/tags URL from the chat URL.
      QString tagsUrl = _ollamaBaseUrl;
      int idx         = tagsUrl.indexOf("/api/chat");
      if (idx >= 0)
            tagsUrl = tagsUrl.left(idx) + "/api/tags";
      else
            tagsUrl = tagsUrl.left(tagsUrl.lastIndexOf('/')) + "/api/tags";

      QNetworkRequest request;
      request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
      request.setUrl(QUrl(tagsUrl));
      request.setTransferTimeout(10000);

      if (QNetworkReply* old = _modelsReply) {
            _modelsReply = nullptr;
            old->disconnect();
            old->abort();
            old->deleteLater();
            }
      _modelsReply = _network.get(request);
      connect(_modelsReply, &QNetworkReply::finished, this, &AIAgent::onModelsReplyFinished);
      }

//--------------------------------------------------------------------
//     onModelsReplyFinished
//    Parse the /api/tags response and populate _ollamaModels.
//    The Ollama response format is:
//      { "models": [ { "name": "llama3.1:latest", ... }, ... ] }
//--------------------------------------------------------------------

void AIAgent::onModelsReplyFinished() {
      QNetworkReply* reply = _modelsReply;
      _modelsReply         = nullptr;
      if (!reply)
            return;
      reply->deleteLater();

      if (reply->error() != QNetworkReply::NoError) {
            Warning("AI agent: failed to fetch Ollama models: {}", reply->errorString().toStdString());
            return;
            }

      QByteArray data = reply->readAll();
      try {
            json response = json::parse(data.toStdString());
            QStringList models;
            if (response.contains("models") && response["models"].is_array()) {
                  for (const auto& m : response["models"])
                        if (m.contains("name") && m["name"].is_string())
                              models.append(QString::fromStdString(m["name"].get<std::string>()));
                  }
            _ollamaModels = models;
            emit ollamaModelsChanged();
            Debug("AI agent: fetched {} Ollama models", models.size());
            }
      catch (const std::exception& e) {
            Warning("AI agent: error parsing models response: {}", e.what());
            }
      }

//--------------------------------------------------------------------
//     buildTools
//--------------------------------------------------------------------

void AIAgent::buildTools() {
      _tools.clear();

      // ── Project commands ────────────────────────────────────────────
      _tools.push_back(
          MCPToolBuilder("new_project", "Create a fresh, empty ZCam project.  Existing project contents are "
                                        "replaced; if the current project is dirty the user must save first.")
              .build());

      _tools.push_back(MCPToolBuilder("save_project",
          "Save the current project to its current path.  Fails if the project has no path "
          "(use save_project_as in that case — not yet exposed).")
              .build());

      _tools.push_back(MCPToolBuilder("start_session",
          "Begin a sequence of undoable commands.  All element / property changes issued after this call up "
          "until end_session are recorded as a single undo step.  Always pair with end_session.")
              .build());

      _tools.push_back(
          MCPToolBuilder("end_session", "End the sequence of undoable commands started with start_session.")
              .build());

      _tools.push_back(MCPToolBuilder("undo", "Undo the last change on the project.").build());

      _tools.push_back(MCPToolBuilder("redo", "Redo the previously undone change on the project.").build());

      // ── Element commands ────────────────────────────────────────────
      _tools.push_back(MCPToolBuilder("create_element", "Create a new element in the project.")
              .add_parameter("type", "string",
                  "Element type: one of 'rectangle', 'polygon', 'ellipse', 'text', 'group', 'nest' (lowercase).")
              .add_parameter("x", "number",
                  "Initial X position of the new element in the parent layer's coordinate system (mm).",
                  false)
              .add_parameter("y", "number",
                  "Initial Y position of the new element in the parent layer's coordinate system (mm).",
                  false)
              .add_parameter("name", "string",
                  "Optional initial name.  If omitted the element gets a generated unique name.", false)
              .add_parameter("parent", "string",
                  "Optional name of the parent element.  If omitted the element is added to the current "
                  "visible Layer (fallback: first visible Layer).",
                  false)
              .build());

      _tools.push_back(MCPToolBuilder("delete_element", "Delete the named element (and all its descendants) "
                                                        "from the project tree.  The operation is undoable.")
              .add_parameter("name", "string", "Name of the element to delete.")
              .build());

      _tools.push_back(MCPToolBuilder("rename_element",
          "Rename an element.  Names must be unique project-wide and are enforced centrally: the "
          "requested name is sanitized to a valid identifier and, if it collides with an existing "
          "element name, a numeric suffix is appended (e.g. 'box-1').  The actual resulting name is "
          "returned in 'name'.  The operation is undoable.")
              .add_parameter("name", "string", "Current name of the element to rename.")
              .add_parameter("new_name", "string", "The new name.")
              .build());

      _tools.push_back(MCPToolBuilder("move_element",
          "Move an element to a new parent (re-parenting) or to a new position within its current parent.  "
          "The element's world-space transform is preserved on re-parenting.  The operation is undoable.")
              .add_parameter("name", "string", "Name of the element to move.")
              .add_parameter("new_parent", "string", "Name of the new parent element.")
              .add_parameter("new_row", "integer",
                  "Optional row inside the new parent.  -1 (default) appends at the end.", false)
              .build());

      // ── Property commands ───────────────────────────────────────────
      _tools.push_back(
          MCPToolBuilder("read_property", "Read the current value of a property of a named element.")
              .add_parameter("element", "string", "Name of the element.")
              .add_parameter("property", "string", "Name of the property.")
              .build());

      _tools.push_back(MCPToolBuilder("write_property",
          "Write a new value to a property of a named element.  The value is converted to the property's Qt "
          "type.  The operation is undoable (recorded via the project's undo stack).")
              .add_parameter("element", "string", "Name of the element.")
              .add_parameter("property", "string", "Name of the property.")
              .add_parameter("value", "string",
                  "New value as a string.  Numeric / boolean values are converted automatically; vector3d / "
                  "vector2d may be passed as JSON arrays, e.g. [10, 20, 0]; color as '#RRGGBB' or 'name'.")
              .build());

      _tools.push_back(MCPToolBuilder("list_properties",
          "Introspect an element: returns the list of Q_PROPERTY names, their Qt types "
          "and current values.  Useful before reading / writing a property.")
              .add_parameter("element", "string", "Name of the element.")
              .add_parameter("include_values", "boolean",
                  "If true (default) include the current value of each property in the response.", false)
              .build());

      _tools.push_back(MCPToolBuilder("describe_property",
          "Introspect a single property: name, Qt type name, current value, min/max (if declared in the "
          "element's properties() JSON), default (if declared) and whether the property is read/write.")
              .add_parameter("element", "string", "Name of the element.")
              .add_parameter("property", "string", "Name of the property.")
              .build());

      // ── Methods ─────────────────────────────────────────────────────
      _tools.push_back(MCPToolBuilder("invoke_method",
          "Invoke a Q_INVOKABLE method on a named element with optional named arguments.  "
          "Use list_methods first to see what is available.")
              .add_parameter("element", "string", "Name of the element that owns the method.")
              .add_parameter("method", "string", "Name of the method to invoke.")
              .add_parameter("arguments", "string",
                  "Optional JSON-encoded argument list (array) or object, e.g. '[1,2]' or '{\"x\":1}'.  "
                  "Empty if the method takes no arguments.",
                  false)
              .build());

      _tools.push_back(MCPToolBuilder("list_methods",
          "List the Q_INVOKABLE methods (i.e. scriptable actions) exposed by a named element, with their "
          "signatures (name + parameter types).  Use this before invoke_method.")
              .add_parameter("element", "string", "Name of the element.")
              .build());

      // ── Project / app level helpers ─────────────────────────────────
      _tools.push_back(
          MCPToolBuilder("list_elements", "List all named elements currently in the project, with their "
                                          "type.  Useful for navigation before reading / writing properties.")
              .add_parameter(
                  "max_depth", "integer", "Maximum tree depth to traverse.  -1 (default) = unlimited.", false)
              .build());

      _tools.push_back(MCPToolBuilder("screenshot",
          "Capture an image of the 3-D canvas exactly as it is currently displayed (camera, "
          "zoom, rotation and all visible elements).  Use this when the user asks what is on "
          "the canvas, to verify the visual result of edits, or to 'see' the current state.  "
          "The image is embedded in your context so you can inspect it; a full-resolution PNG "
          "is also saved to ~/ZCam/screenshots (path returned in 'file').")
              .add_parameter("inline", "boolean",
                  "If true (default) and 'with_image' is true, also include a base64 'dataUrl' field.  "
                  "Default true.",
                  false)
              .add_parameter("max_width", "integer",
                  "Maximum pixel width of the embedded image (aspect ratio preserved).  Default 1280.", false)
              .add_parameter("with_image", "boolean",
                  "Include the base64 'image' payload.  Set false when only the saved PNG file path is "
                  "needed (e.g. over the stdin remote-control).  Default true.",
                  false)
              .build());

      // ── Selection commands ───────────────────────────────────────
      _tools.push_back(MCPToolBuilder("get_current_element",
          "Return the name, type and path of the currently selected element "
          "(ZCam::currentElement).  Returns {current: null} when nothing is selected.")
              .build());

      _tools.push_back(MCPToolBuilder("select_element",
          "Set the current element (primary selection) to the named element.  "
          "Replaces any existing multi-selection.  Pass an empty string or "
          "null to deselect everything.")
              .add_parameter("name", "string",
                  "Name of the element to select.  Pass an empty string or omit to "
                  "clear the selection.",
                  false)
              .build());

      _tools.push_back(MCPToolBuilder("get_selected_elements",
          "Return the list of all elements currently in the multi-selection "
          "(ZCam::selectedElements).  Each entry has name, type and depth.")
              .build());

      _tools.push_back(MCPToolBuilder("select_elements",
          "Replace the multi-selection with the given list of named elements.  "
          "The first element in the list becomes the current (primary) element.  "
          "Unknown names are reported as errors.")
              .add_parameter("names", "array",
                  "JSON array of element name strings to select, e.g. [\"rect1\",\"poly2\"].  "
                  "An empty array clears the selection.")
              .build());

      _tools.push_back(
          MCPToolBuilder("clear_selection", "Clear both the current element and the multi-selection list.  "
                                            "Equivalent to selecting nothing.")
              .build());

      // ── Spatial query ─────────────────────────────────────────────
      _tools.push_back(MCPToolBuilder("is_element_inside",
          "Test whether one element's geometry is fully contained inside another element's geometry.  "
          "Both elements must have closed 2D outlines (polygons, rectangles, ellipses, etc.).  "
          "The containment test uses the Clipper2 polygon difference: if the difference of the "
          "inner element minus the outer element is empty, the inner element is fully inside.  "
          "Returns {ok:true, inside:true/false, outer, inner, method}.  "
          "The geometry is compared in world (root/project) coordinates, so transforms (pos, rot, scale) are "
          "respected.")
              .add_parameter("inner", "string",
                  "Name of the element to test for containment (the element that might be inside).")
              .add_parameter("outer", "string",
                  "Name of the containing element (the element that might contain the inner one).")
              .build());

      // ── Laser layer assignment ────────────────────────────────────
      _tools.push_back(MCPToolBuilder("set_mops",
          "Assign a named laser MOP (laser layer) to an element by setting its 'mop' property.  "
          "Use an empty mops_name to clear the assignment so the element inherits the MOP from its parent.  "
          "The change is undoable.  Returns {ok, element, mops, oldMops, newMops}.")
              .add_parameter("element", "string", "Name of the element to assign the MOP to.")
              .add_parameter("mops_name", "string",
                  "Name of the laser MOP (laser layer) to assign.  Pass an empty string to clear.", false)
              .build());

      // ── Imperative scripting ───────────────────────────────────────
      _tools.push_back(MCPToolBuilder("run_script",
          "Execute a JavaScript snippet in the ZCam scripting engine.  "
          "Access 'zcam' (imperative API: create/delete/modify elements, geometry ops), "
          "'geom' (geometry helpers: paths, boolean ops, grids), 'project' (element tree, read), "
          "'config'. Use for complex multi-step operations: grids, patterns, batch modifications, "
          "geometric computations. Wrap mutations in zcam.beginBatch()/endBatch() for a single undo step.")
              .add_parameter("script", "string",
                  "JavaScript code to execute.  Use zcam.createElement(...), zcam.setProperty(...), "
                  "geom.regularPolygon(...), etc.  Multi-statement scripts are supported.")
              .add_parameter(
                  "timeout_ms", "integer", "Maximum execution time in milliseconds.  Default 10000.", false)
              .build());
      }

//--------------------------------------------------------------------
//     toolSchema / toolNames
//--------------------------------------------------------------------

QString AIAgent::toolSchema(const QString& name) const {
      for (const auto& t : _tools)
            if (t.contains("name") && QString::fromStdString(t["name"].get<std::string>()) == name)
                  return QString::fromStdString(t.dump(2));
      return QString();
      }

QStringList AIAgent::toolNames() const {
      QStringList out;
      for (const auto& t : _tools)
            if (t.contains("name"))
                  out.append(QString::fromStdString(t["name"].get<std::string>()));
      return out;
      }

//--------------------------------------------------------------------
//     invokeTool
//--------------------------------------------------------------------

QString AIAgent::invokeTool(const QString& name, const QString& argumentsJson) {
      // Parse the arguments.  An empty string means "no arguments";
      // otherwise it must be a valid JSON object.
      json args             = json::object();
      const QString trimmed = argumentsJson.trimmed();
      if (!trimmed.isEmpty()) {
            try {
                  json parsed = json::parse(trimmed.toStdString());
                  if (parsed.is_object())
                        args = parsed;
                  else
                        return QString::fromStdString(errorResponse("arguments must be a JSON object"));
                  }
            catch (const std::exception& e) {
                  return QString::fromStdString(
                      errorResponse(std::string("invalid arguments JSON: ") + e.what()));
                  }
            }
      return QString::fromStdString(executeTool(name.toStdString(), args));
      }

//--------------------------------------------------------------------
//     resolveElement
//    Look up an Element by its (sanitised) name in the global
//    Element::names registry.
//--------------------------------------------------------------------

Element* AIAgent::resolveElement(const QString& name) const {
      if (name.isEmpty())
            return nullptr;
      Element* e = Element::byName(name);
      if (e)
            return e;
      // Fallback: walk the names registry case-insensitively.
      const auto& names = Element::namesMap();
      for (auto it = names.constBegin(); it != names.constEnd(); ++it)
            if (it.key().compare(name, Qt::CaseInsensitive) == 0)
                  return it.value();
      return nullptr;
      }

//--------------------------------------------------------------------
//     errorResponse
//--------------------------------------------------------------------

std::string AIAgent::errorResponse(const std::string& message) const {
      json j;
      j["ok"]    = false;
      j["error"] = message;
      return j.dump();
      }

//--------------------------------------------------------------------
//     tool implementations
//--------------------------------------------------------------------

std::string AIAgent::toolNewProject() const {
      if (!_zc)
            return errorResponse("no ZCam instance");
      _zc->newProject(true);
      json j;
      j["ok"]  = true;
      j["msg"] = "new project created";
      return j.dump();
      }

std::string AIAgent::toolSaveProject() const {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      bool ok = _zc->save();
      json j;
      j["ok"]  = ok;
      j["msg"] = ok ? "project saved" : "save failed";
      return j.dump();
      }

std::string AIAgent::toolStartSession() const {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      _zc->project()->undo()->beginMacro();
      json j;
      j["ok"]  = true;
      j["msg"] = "session started (undoable macro active)";
      return j.dump();
      }

std::string AIAgent::toolEndSession() const {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      _zc->project()->undo()->endMacro();
      json j;
      j["ok"]  = true;
      j["msg"] = "session ended";
      return j.dump();
      }

std::string AIAgent::toolUndo() const {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      _zc->project()->undo()->undo();
      json j;
      j["ok"]  = true;
      j["msg"] = "undo performed";
      return j.dump();
      }

std::string AIAgent::toolRedo() const {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      _zc->project()->undo()->redo();
      json j;
      j["ok"]  = true;
      j["msg"] = "redo performed";
      return j.dump();
      }

//--------------------------------------------------------------------
//     toolCreateElement
//--------------------------------------------------------------------

std::string AIAgent::toolCreateElement(const json& args) {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      std::string type      = args.value("type", "");
      double x              = args.value("x", 0.0);
      double y              = args.value("y", 0.0);
      std::string nameStr   = args.value("name", "");
      std::string parentStr = args.value("parent", "");

      // Resolve the target parent.  If a parent name was given use it;
      // otherwise pick the current element's layer or the first
      // visible layer (same policy as ZCam::createRectangle).
      Element* target = nullptr;
      if (!parentStr.empty()) {
            target = resolveElement(QString::fromStdString(parentStr));
            if (!target)
                  return errorResponse("parent not found: " + parentStr);
            }
      // Use the existing ZCam create* helpers when possible — they
      // take care of layer fallback + tree-model updates.
      if (type == "rectangle") {
            Element3d* el = _zc->createRectangle(x, y);
            if (!el)
                  return errorResponse("no layer available for rectangle");
            if (!nameStr.empty())
                  el->setName(QString::fromStdString(nameStr));
            json j;
            j["ok"]   = true;
            j["name"] = el->name().toStdString();
            j["type"] = "rectangle";
            return j.dump();
            }
      else if (type == "polygon") {
            Element3d* el = _zc->createPolygon(x, y);
            if (!el)
                  return errorResponse("no layer available for polygon");
            if (!nameStr.empty())
                  el->setName(QString::fromStdString(nameStr));
            json j;
            j["ok"]   = true;
            j["name"] = el->name().toStdString();
            j["type"] = "polygon";
            return j.dump();
            }
      else if (type == "ellipse") {
            Element3d* el = _zc->createEllipse(x, y);
            if (!el)
                  return errorResponse("no layer available for ellipse");
            if (!nameStr.empty())
                  el->setName(QString::fromStdString(nameStr));
            json j;
            j["ok"]   = true;
            j["name"] = el->name().toStdString();
            j["type"] = "ellipse";
            return j.dump();
            }
      else if (type == "text") {
            Element3d* el = _zc->createText(x, y);
            if (!el)
                  return errorResponse("no layer available for text");
            if (!nameStr.empty())
                  el->setName(QString::fromStdString(nameStr));
            json j;
            j["ok"]   = true;
            j["name"] = el->name().toStdString();
            j["type"] = "text";
            return j.dump();
            }
      else if (type == "group") {
            // Group is a bit special: create one under the current
            // element's layer (or the Cad root as a fallback).
            Group* layer = nullptr;
            Element* cur = _zc->currentElement();
            if (cur) {
                  for (Element* p = cur; p; p = p->parent()) {
                        if (auto g = qobject_cast<Group*>(p)) {
                              layer = g;
                              break;
                              }
                        }
                  }
            if (!layer && _zc->project() && _zc->project()->cad())
                  layer = qobject_cast<Group*>(_zc->project()->cad());
            if (!layer)
                  return errorResponse("no group / layer target available");
            // Create the Group directly and insert via the tree model.
            Group* g = new Group(_zc, layer);
            int row  = layer->children().size();
            if (_zc->treeModel())
                  _zc->treeModel()->beginInsertChild(layer, row);
            layer->addChild(g);
            if (_zc->treeModel())
                  _zc->treeModel()->endInsertChild();
            emit _zc->add3dElement(g);
            _zc->setCamDirty(true);
            if (!nameStr.empty())
                  g->setName(QString::fromStdString(nameStr));
            json j;
            j["ok"]   = true;
            j["name"] = g->name().toStdString();
            j["type"] = "group";
            return j.dump();
            }
      return errorResponse("unknown element type: " + type);
      }

//--------------------------------------------------------------------
//     toolDeleteElement
//--------------------------------------------------------------------

std::string AIAgent::toolDeleteElement(const json& args) {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      std::string name = args.value("name", "");
      Element* el      = resolveElement(QString::fromStdString(name));
      if (!el)
            return errorResponse("element not found: " + name);
      Element3d* el3d = qobject_cast<Element3d*>(el);
      if (el3d) {
            _zc->setCurrentElement(el3d);
            _zc->deleteCurrentElement();
            json j;
            j["ok"]  = true;
            j["msg"] = "deleted " + name;
            return j.dump();
            }
      return errorResponse("element is not deletable: " + name);
      }

//--------------------------------------------------------------------
//     toolRenameElement
//    Rename a named element.  Uniqueness of the name is enforced
//    centrally by Project::renameElement() / Element::setName(); the
//    actual (de-duplicated) resulting name is returned.
//--------------------------------------------------------------------

std::string AIAgent::toolRenameElement(const json& args) {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      std::string name    = args.value("name", "");
      std::string newName = args.value("new_name", "");
      Element* el         = resolveElement(QString::fromStdString(name));
      if (!el)
            return errorResponse("element not found: " + name);
      if (newName.empty())
            return errorResponse("new_name is empty");
      QString oldName    = el->name();
      QString actualName = _zc->project()->renameElement(el, QString::fromStdString(newName));
      json j;
      j["ok"]      = true;
      j["name"]    = actualName.toStdString();
      j["oldName"] = oldName.toStdString();
      j["msg"]     = "renamed " + oldName.toStdString() + " to " + actualName.toStdString();
      return j.dump();
      }

//--------------------------------------------------------------------
//     toolMoveElement
//--------------------------------------------------------------------

std::string AIAgent::toolMoveElement(const json& args) {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      std::string name      = args.value("name", "");
      std::string newParent = args.value("new_parent", "");
      int newRow  = args.contains("new_row") && args["new_row"].is_number() ? args["new_row"].get<int>() : -1;
      Element* el = resolveElement(QString::fromStdString(name));
      if (!el)
            return errorResponse("element not found: " + name);
      Element* parent = resolveElement(QString::fromStdString(newParent));
      if (!parent)
            return errorResponse("new parent not found: " + newParent);
      _zc->project()->moveElement(el, parent, newRow);
      json j;
      j["ok"]  = true;
      j["msg"] = "moved " + name + " to " + newParent;
      return j.dump();
      }

//--------------------------------------------------------------------
//     variantToJson
//    Convert a QVariant (from a Q_PROPERTY) into JSON.
//--------------------------------------------------------------------

static json variantToJson(const QVariant& v) {
      int typeId = v.metaType().id();
      if (typeId == QMetaType::Int)
            return json(v.toInt());
      if (typeId == QMetaType::LongLong)
            return json(v.value<qlonglong>());
      if (typeId == QMetaType::Double)
            return json(v.toDouble());
      if (typeId == QMetaType::Bool)
            return json(v.toBool());
      if (typeId == QMetaType::QString)
            return json(v.toString().toStdString());
      if (typeId == QMetaType::QVector3D) {
            auto vec = v.value<QVector3D>();
            json arr = json::array();
            arr.push_back(vec.x());
            arr.push_back(vec.y());
            arr.push_back(vec.z());
            return arr;
            }
      if (typeId == QMetaType::QVector2D) {
            auto vec = v.value<QVector2D>();
            json arr = json::array();
            arr.push_back(vec.x());
            arr.push_back(vec.y());
            return arr;
            }
      if (typeId == QMetaType::QColor) {
            auto c = v.value<QColor>();
            json o;
            o["r"]   = c.red();
            o["g"]   = c.green();
            o["b"]   = c.blue();
            o["str"] = c.name().toStdString();
            return o;
            }
      if (typeId == QMetaType::QStringList) {
            json arr = json::array();
            for (const auto& s : v.toStringList())
                  arr.push_back(s.toStdString());
            return arr;
            }
      if (typeId != QMetaType::UnknownType && typeId != QMetaType::VoidStar) {
            // Opaque pointer — serialise as string.
            return json(v.toString().toStdString());
            }
      // Fallback
      if (v.canConvert<QString>())
            return json(v.toString().toStdString());
      return json(nullptr);
      }

//--------------------------------------------------------------------
//     jsonToVariant
//    Convert a JSON value (from a tool call argument) into a
//    QVariant that can be written via QMetaProperty::write() for
//    the given property meta-type.
//--------------------------------------------------------------------

static QVariant jsonToVariant(const json& j, int metaType) {
      // The LLM often passes values as strings (e.g. "[30, 30]", "0.5",
      // "#FF0000") because the tool schema declares value as string.
      // When the target type is not QString, try to parse the string as
      // JSON first so we can handle arrays, numbers and objects.
      json parsed = j;
      if (j.is_string() && metaType != QMetaType::QString) {
            const std::string& s = j.get<std::string>();
            try {
                  parsed = json::parse(s);
                  }
            catch (...) {
                  // Not valid JSON — keep the original string value.
                  }
            }

      switch (metaType) {
            case QMetaType::Int:
            case QMetaType::Short:
            case QMetaType::UShort:
                  if (parsed.is_number())
                        return QVariant(parsed.get<int>());
                  if (parsed.is_string()) {
                        bool ok = false;
                        int v   = QString::fromStdString(parsed.get<std::string>()).toInt(&ok);
                        if (ok)
                              return QVariant(v);
                        }
                  return QVariant(0);
            case QMetaType::Long:
            case QMetaType::LongLong:
                  if (parsed.is_number())
                        return QVariant(parsed.get<qint64>());
                  if (parsed.is_string()) {
                        bool ok  = false;
                        qint64 v = QString::fromStdString(parsed.get<std::string>()).toLongLong(&ok);
                        if (ok)
                              return QVariant(v);
                        }
                  return QVariant(0LL);
            case QMetaType::ULong:
            case QMetaType::ULongLong:
                  if (parsed.is_number())
                        return QVariant(parsed.get<quint64>());
                  if (parsed.is_string()) {
                        bool ok   = false;
                        quint64 v = QString::fromStdString(parsed.get<std::string>()).toULongLong(&ok);
                        if (ok)
                              return QVariant(v);
                        }
                  return QVariant(0ULL);
            case QMetaType::Double:
            case QMetaType::Float:
                  if (parsed.is_number())
                        return QVariant(parsed.get<double>());
                  if (parsed.is_string()) {
                        bool ok  = false;
                        double v = QString::fromStdString(parsed.get<std::string>()).toDouble(&ok);
                        if (ok)
                              return QVariant(v);
                        }
                  return QVariant(0.0);
            case QMetaType::Bool:
                  if (parsed.is_boolean())
                        return QVariant(parsed.get<bool>());
                  if (parsed.is_string()) {
                        QString s = QString::fromStdString(parsed.get<std::string>()).toLower();
                        if (s == "true" || s == "1")
                              return QVariant(true);
                        if (s == "false" || s == "0")
                              return QVariant(false);
                        }
                  return QVariant();
            case QMetaType::QString:
                  return QVariant(QString::fromStdString(j.is_string() ? j.get<std::string>() : j.dump()));
            case QMetaType::QVector3D: {
                  if (parsed.is_array() && parsed.size() == 3)
                        return QVariant(QVector3D(
                            parsed[0].get<double>(), parsed[1].get<double>(), parsed[2].get<double>()));
                  if (parsed.is_object() && parsed.contains("x"))
                        return QVariant(QVector3D(
                            parsed.value("x", 0.0), parsed.value("y", 0.0), parsed.value("z", 0.0)));
                  return QVariant();
                  }
            case QMetaType::QVector2D: {
                  if (parsed.is_array() && parsed.size() == 2)
                        return QVariant(QVector2D(parsed[0].get<double>(), parsed[1].get<double>()));
                  if (parsed.is_object() && parsed.contains("x"))
                        return QVariant(QVector2D(parsed.value("x", 0.0), parsed.value("y", 0.0)));
                  return QVariant();
                  }
            case QMetaType::QColor: {
                  QColor c;
                  if (parsed.is_string())
                        c = QColor(QString::fromStdString(parsed.get<std::string>()));
                  else if (parsed.is_object())
                        c = QColor(
                            parsed.at("r").get<int>(), parsed.at("g").get<int>(), parsed.at("b").get<int>());
                  return c.isValid() ? QVariant(c) : QVariant();
                  }
            case QMetaType::QStringList: {
                  QStringList out;
                  if (parsed.is_array())
                        for (const auto& item : parsed)
                              out.append(QString::fromStdString(
                                  item.is_string() ? item.get<std::string>() : item.dump()));
                  return QVariant(out);
                  }
            default:
                  if (j.is_string())
                        return QVariant(QString::fromStdString(j.get<std::string>()));
                  return QVariant(QString::fromStdString(j.dump()));
            }
      }

//--------------------------------------------------------------------
//     toolReadProperty
//--------------------------------------------------------------------

std::string AIAgent::toolReadProperty(const json& args) {
      std::string elementName  = args.value("element", "");
      std::string propertyName = args.value("property", "");
      Element* el              = resolveElement(QString::fromStdString(elementName));
      if (!el)
            return errorResponse("element not found: " + elementName);
      QByteArray pn = QByteArray::fromStdString(propertyName);
      int idx       = el->metaObject()->indexOfProperty(pn.constData());
      if (idx < 0)
            return errorResponse("property not found: " + propertyName);
      QMetaProperty mp = el->metaObject()->property(idx);
      QVariant v       = mp.read(el);
      json out;
      out["ok"]       = true;
      out["element"]  = el->name().toStdString();
      out["property"] = propertyName;
      out["type"]     = mp.typeName();
      out["value"]    = variantToJson(v);
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolWriteProperty
//--------------------------------------------------------------------

std::string AIAgent::toolWriteProperty(const json& args) {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      std::string elementName  = args.value("element", "");
      std::string propertyName = args.value("property", "");
      json value               = args.value("value", json(nullptr));
      Element* el              = resolveElement(QString::fromStdString(elementName));
      if (!el)
            return errorResponse("element not found: " + elementName);
      QByteArray pn = QByteArray::fromStdString(propertyName);
      int idx       = el->metaObject()->indexOfProperty(pn.constData());
      if (idx < 0)
            return errorResponse("property not found: " + propertyName);
      QMetaProperty mp = el->metaObject()->property(idx);
      if (!mp.isWritable())
            return errorResponse("property is read-only: " + propertyName);
      QVariant newVariant = jsonToVariant(value, mp.metaType().id());
      if (!newVariant.isValid())
            return errorResponse(std::format("cannot convert value to {}", mp.typeName()));
      QVariant oldVariant = mp.read(el);
      // Route through the project's changeProperty helper so the
      // undo stack records the change (PropertyChangeCommand).
      _zc->project()->changeProperty(el, QString::fromStdString(propertyName), newVariant);
      json out;
      out["ok"]       = true;
      out["element"]  = el->name().toStdString();
      out["property"] = propertyName;
      out["oldValue"] = variantToJson(oldVariant);
      out["newValue"] = variantToJson(mp.read(el));
      out["msg"] = el->name().toStdString() + "." + propertyName + " = " + variantToJson(mp.read(el)).dump();
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolListProperties
//--------------------------------------------------------------------

std::string AIAgent::toolListProperties(const json& args) {
      bool includeValues      = args.value("include_values", true);
      std::string elementName = args.value("element", "");
      Element* el             = resolveElement(QString::fromStdString(elementName));
      if (!el)
            return errorResponse("element not found: " + elementName);
      json props            = json::array();
      const QMetaObject* mo = el->metaObject();
      int count             = mo->propertyCount();
      for (int i = 0; i < count; ++i) {
            QMetaProperty mp = mo->property(i);
            json p;
            p["name"]     = QString::fromUtf8(mp.name()).toStdString();
            p["type"]     = mp.typeName();
            p["writable"] = mp.isWritable();
            p["readable"] = mp.isReadable();
            if (includeValues) {
                  QVariant v = mp.read(el);
                  p["value"] = variantToJson(v);
                  }
            props.push_back(p);
            }
      json out;
      out["ok"]         = true;
      out["element"]    = el->name().toStdString();
      out["type"]       = el->typeName().toStdString();
      out["properties"] = props;
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolDescribeProperty
//--------------------------------------------------------------------

std::string AIAgent::toolDescribeProperty(const json& args) {
      std::string elementName  = args.value("element", "");
      std::string propertyName = args.value("property", "");
      Element* el              = resolveElement(QString::fromStdString(elementName));
      if (!el)
            return errorResponse("element not found: " + elementName);
      QByteArray pn = QByteArray::fromStdString(propertyName);
      int idx       = el->metaObject()->indexOfProperty(pn.constData());
      if (idx < 0)
            return errorResponse("property not found: " + propertyName);
      QMetaProperty mp = el->metaObject()->property(idx);
      QVariant v       = mp.read(el);
      json out;
      out["ok"]       = true;
      out["element"]  = el->name().toStdString();
      out["property"] = propertyName;
      out["type"]     = mp.typeName();
      out["writable"] = mp.isWritable();
      out["value"]    = variantToJson(v);
      // Add declared metadata from properties() JSON if available.
      // (min / max / default / unit / precision / scriptable)
      std::string_view pjson = el->properties();
      if (!pjson.empty()) {
            try {
                  json decl = json::parse(pjson);
                  // Walk the "rows" array looking for a cell matching propertyName.
                  if (decl.contains("rows") && decl["rows"].is_array()) {
                        auto findCell = [&](const json& c) -> bool {
                              return c.contains("name") && c["name"].is_string() &&
                                     c["name"].get<std::string>() == propertyName;
                              };
                        for (const auto& row : decl["rows"]) {
                              if (!row.contains("cells"))
                                    continue;
                              for (const auto& cell : row["cells"]) {
                                    bool found = findCell(cell);
                                    if (!found && cell.contains("cells") && cell["cells"].is_array()) {
                                          for (const auto& sub : cell["cells"]) {
                                                if (findCell(sub)) {
                                                      found = true;
                                                      break;
                                                      }
                                                }
                                          }
                                    if (found) {
                                          if (cell.contains("min"))
                                                out["min"] = cell["min"];
                                          if (cell.contains("max"))
                                                out["max"] = cell["max"];
                                          if (cell.contains("default"))
                                                out["default"] = cell["default"];
                                          if (cell.contains("unit"))
                                                out["unit"] = cell["unit"];
                                          if (cell.contains("precision"))
                                                out["precision"] = cell["precision"];
                                          if (cell.contains("scriptable"))
                                                out["scriptable"] = cell["scriptable"];
                                          break;
                                          }
                                    }
                              }
                        }
                  }
            catch (...) {
                  // properties() is not valid JSON — ignore.
                  }
            }
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolInvokeMethod
//--------------------------------------------------------------------

std::string AIAgent::toolInvokeMethod(const json& args) {
      std::string elementName = args.value("element", "");
      std::string methodName  = args.value("method", "");
      std::string argsJson    = args.value("arguments", "[]");
      Element* el             = resolveElement(QString::fromStdString(elementName));
      if (!el)
            return errorResponse("element not found: " + elementName);
      const QMetaObject* mo = el->metaObject();
      int methodCount       = mo->methodCount();
      int idx               = -1;
      for (int i = 0; i < methodCount; ++i) {
            QMetaMethod m = mo->method(i);
            if (QString::fromUtf8(m.name()) == methodName && m.methodType() == QMetaMethod::Method) {
                  idx = i;
                  break;
                  }
            }
      if (idx < 0)
            return errorResponse("method not found: " + methodName);
      QMetaMethod m  = mo->method(idx);
      int paramCount = m.parameterCount();
      // Parse arguments JSON
      json argArray = json::array();
      try {
            if (!argsJson.empty()) {
                  json parsed = json::parse(argsJson);
                  if (parsed.is_array())
                        argArray = parsed;
                  else if (parsed.is_object())
                        for (auto it = parsed.begin(); it != parsed.end(); ++it)
                              argArray.push_back(it.value());
                  }
            }
      catch (...) {
            return errorResponse("invalid arguments JSON: " + argsJson);
            }
      if (paramCount != argArray.size())
            return errorResponse(std::format(
                "method {} expects {} arguments, got {}", methodName, paramCount, argArray.size()));
      // Build the QGenericArgument list.
      // We need to keep the actual argument values alive until after
      // invoke() returns.  QVariant stores its data internally in a way
      // that may not align with the raw type expected by Qt's meta-Call
      // (e.g. QVector3D inside a QVariant is not at the same offset as
      // a standalone QVector3D).  So we extract the concrete values
      // into dedicated storage and pass pointers to those.
      // We need to keep the actual argument values alive until after
      // invoke() returns.  QVariant stores its data internally in a way
      // that may not align with the raw type expected by Qt's meta-Call
      // (e.g. QVector3D inside a QVariant is not at the same offset as
      // a standalone QVector3D).  So we extract the concrete values
      // into dedicated storage and build QGenericArgument objects that
      // point into that storage.  Fixed-size arrays avoid dangling
      // pointers from vector reallocation.
      QVariant argVariants[10];
      int argInts[10];
      double argDoubles[10];
      float argFloats[10];
      bool argBools[10];
      QString argStrings[10];
      QVector3D argVec3ds[10];
      QVector2D argVec2ds[10];
      QGenericArgument genericArgs[10] = {
         QGenericArgument(), QGenericArgument(), QGenericArgument(), QGenericArgument(), QGenericArgument(),
         QGenericArgument(), QGenericArgument(), QGenericArgument(), QGenericArgument(), QGenericArgument()};

      for (int i = 0; i < paramCount; ++i) {
            int ptype  = m.parameterType(i);
            QVariant v = jsonToVariant(argArray[i], ptype);
            if (!v.isValid()) {
                  return errorResponse(
                      std::format("cannot convert argument {} to {}", i, QMetaType(ptype).name()));
                  }
            argVariants[i] = v;
            switch (ptype) {
                  case QMetaType::Int:
                  case QMetaType::Short:
                  case QMetaType::UShort:
                        argInts[i]     = v.toInt();
                        genericArgs[i] = QGenericArgument("int", &argInts[i]);
                        break;
                  case QMetaType::Double:
                        argDoubles[i]  = v.toDouble();
                        genericArgs[i] = QGenericArgument("double", &argDoubles[i]);
                        break;
                  case QMetaType::Float:
                        argFloats[i]   = static_cast<float>(v.toDouble());
                        genericArgs[i] = QGenericArgument("float", &argFloats[i]);
                        break;
                  case QMetaType::Bool:
                        argBools[i]    = v.toBool();
                        genericArgs[i] = QGenericArgument("bool", &argBools[i]);
                        break;
                  case QMetaType::QString:
                        argStrings[i]  = v.toString();
                        genericArgs[i] = QGenericArgument("QString", &argStrings[i]);
                        break;
                  case QMetaType::QVector3D:
                        argVec3ds[i]   = v.value<QVector3D>();
                        genericArgs[i] = QGenericArgument("QVector3D", &argVec3ds[i]);
                        break;
                  case QMetaType::QVector2D:
                        argVec2ds[i]   = v.value<QVector2D>();
                        genericArgs[i] = QGenericArgument("QVector2D", &argVec2ds[i]);
                        break;
                  default:
                        // For types we don't handle explicitly, fall back
                        // to passing through the QVariant's data.  This may
                        // not work for all types but covers simple cases.
                        genericArgs[i] = QGenericArgument(v.metaType().name(), v.constData());
                        break;
                  }
            }
      // Determine the return type to capture the return value.
      int retType = m.returnType();
      QVariant retVal;
      QGenericReturnArgument retArg;
      if (retType != QMetaType::Void && retType != QMetaType::UnknownType) {
            retVal = QVariant(QMetaType(retType));
            retArg = QGenericReturnArgument(QMetaType(retType).name(), retVal.data());
            }
      // Use QMetaMethod::invoke with the generic argument list.
      // The variadic version requires a fixed number of arguments at
      // compile time; we use the (QObject*, Qt::ConnectionType,
      // QGenericReturnArgument, QGenericArgument...) overload via
      // QMetaMethod::invoke's generic form which takes the
      // QGenericArgument array through a helper.
      bool ok = false;
      if (retType == QMetaType::Void || retType == QMetaType::UnknownType) {
            switch (paramCount) {
                  case 0: ok = m.invoke(el, Qt::DirectConnection); break;
                  case 1: ok = m.invoke(el, Qt::DirectConnection, genericArgs[0]); break;
                  case 2: ok = m.invoke(el, Qt::DirectConnection, genericArgs[0], genericArgs[1]); break;
                  case 3:
                        ok = m.invoke(
                            el, Qt::DirectConnection, genericArgs[0], genericArgs[1], genericArgs[2]);
                        break;
                  case 4:
                        ok = m.invoke(el, Qt::DirectConnection, genericArgs[0], genericArgs[1],
                            genericArgs[2], genericArgs[3]);
                        break;
                  default:
                        return errorResponse(
                            std::format("method {} takes too many arguments ({})", methodName, paramCount));
                  }
            }
      else {
            switch (paramCount) {
                  case 0: ok = m.invoke(el, Qt::DirectConnection, retArg); break;
                  case 1: ok = m.invoke(el, Qt::DirectConnection, retArg, genericArgs[0]); break;
                  case 2:
                        ok = m.invoke(el, Qt::DirectConnection, retArg, genericArgs[0], genericArgs[1]);
                        break;
                  case 3:
                        ok = m.invoke(
                            el, Qt::DirectConnection, retArg, genericArgs[0], genericArgs[1], genericArgs[2]);
                        break;
                  case 4:
                        ok = m.invoke(el, Qt::DirectConnection, retArg, genericArgs[0], genericArgs[1],
                            genericArgs[2], genericArgs[3]);
                        break;
                  default:
                        return errorResponse(
                            std::format("method {} takes too many arguments ({})", methodName, paramCount));
                  }
            }
      json out;
      out["ok"]      = ok;
      out["element"] = el->name().toStdString();
      out["method"]  = methodName;
      if (ok && retType != QMetaType::Void && retType != QMetaType::UnknownType)
            out["returnValue"] = variantToJson(retVal);
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolListMethods
//--------------------------------------------------------------------

std::string AIAgent::toolListMethods(const json& args) {
      std::string elementName = args.value("element", "");
      Element* el             = resolveElement(QString::fromStdString(elementName));
      if (!el)
            return errorResponse("element not found: " + elementName);
      json methods          = json::array();
      const QMetaObject* mo = el->metaObject();
      int count             = mo->methodCount();
      for (int i = 0; i < count; ++i) {
            QMetaMethod m = mo->method(i);
            if (m.methodType() != QMetaMethod::Method)
                  continue;
            json me;
            me["name"]     = QString::fromUtf8(m.name()).toStdString();
            int paramCount = m.parameterCount();
            json params    = json::array();
            for (int p = 0; p < paramCount; ++p) {
                  json pm;
                  pm["type"] = QMetaType(m.parameterType(p)).name();
                  params.push_back(pm);
                  }
            me["params"] = params;
            methods.push_back(me);
            }
      json out;
      out["ok"]      = true;
      out["element"] = el->name().toStdString();
      out["methods"] = methods;
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolListElements
//--------------------------------------------------------------------

std::string AIAgent::toolListElements(const json& args) {
      (void)args;
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      json elements                               = json::array();
      std::function<void(Element*, int, int)> add = [this, &elements, &add](
                                                        Element* e, int depth, int maxDepth) {
            if (depth > maxDepth)
                  return;
            json el;
            el["name"]  = e->name().toStdString();
            el["type"]  = e->typeName().toStdString();
            el["depth"] = depth;
            elements.push_back(el);
            for (Element* child : e->children())
                  add(child, depth + 1, maxDepth);
            };
      Element* root = _zc->project();
      int maxDepth =
          args.contains("max_depth") && args["max_depth"].is_number() ? args["max_depth"].get<int>() : 99;
      add(root, 0, maxDepth);
      json out;
      out["ok"]       = true;
      out["elements"] = elements;
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolScreenshot
//    Capture the on-screen image of the 3-D canvas.  The result is
//    returned as:
//      - "file"    : absolute path of the full-resolution PNG saved
//                    under ~/ZCam/screenshots
//      - "width" / "height" : pixel dimensions of the saved file
//      - "image"   : raw base64-encoded PNG (no data-URL prefix) of a
//                    down-scaled version (default 1280 px wide) that a
//                    multimodal LLM can ingest directly — the AI chat
//                    loop embeds this into the tool result message via
//                    Ollama's `images` field, so the model actually sees
//                    the canvas.
//      - "dataUrl" : (only when "inline" is true) a base64 data-URL for
//                    generic consumers of the tool result, e.g. the
//                    stdin remote-control interface.
//    Parameters:
//      - "inline"    : include a base64 'dataUrl' field (default true)
//      - "max_width" : max pixel width of the embedded image (default 1280)
//      - "with_image": include the base64 'image' payload at all (default
//                      true).  When false, only the saved file path and
//                      dimensions are returned — used by the stdin
//                      remote-control so stdout stays small.
//--------------------------------------------------------------------

std::string AIAgent::toolScreenshot(const json& args) {
      if (!_zc)
            return errorResponse("ZCam not ready");

      bool inlineDataUrl = args.contains("inline") ? args["inline"].get<bool>() : true;
      bool withImage     = args.contains("with_image") ? args["with_image"].get<bool>() : true;
      int maxWidth =
          args.contains("max_width") && args["max_width"].is_number() ? args["max_width"].get<int>() : 1280;

      // Full-resolution capture, saved to disk.
      const QString path = _zc->saveCanvasScreenshot();
      if (path.isEmpty())
            return errorResponse("screenshot capture failed (no canvas/window?)");

      QImage full(path);
      if (full.isNull())
            return errorResponse("screenshot file could not be read back: " + path.toStdString());

      json out;
      out["ok"]     = true;
      out["file"]   = path.toStdString();
      out["width"]  = full.width();
      out["height"] = full.height();
      out["msg"]    = "Screenshot of the 3-D canvas saved.  Full-resolution copy: " + path.toStdString();

      if (withImage) {
            // Down-scaled version for the LLM context.
            QImage small = _zc->grabCanvas(maxWidth);
            if (small.isNull())
                  small = full;
            QByteArray pngData;
            QBuffer buf(&pngData);
            buf.open(QIODevice::WriteOnly);
            small.save(&buf, "PNG");
            const QByteArray b64 = pngData.toBase64();
            out["image"]         = b64.toStdString();
            if (inlineDataUrl)
                  out["dataUrl"] =
                      (QStringLiteral("data:image/png;base64,") + QString::fromLatin1(b64)).toStdString();
            }
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolGetCurrentElement
//    Return the name, type and hierarchical path of the element that is
//    currently selected (ZCam::currentElement).  When nothing is
//    selected, the "current" field is null.
//--------------------------------------------------------------------

std::string AIAgent::toolGetCurrentElement(const json& args) const {
      (void)args;
      if (!_zc)
            return errorResponse("ZCam not ready");
      Element3d* el = _zc->currentElement();
      json out;
      out["ok"] = true;
      if (el) {
            json cur;
            cur["name"] = el->name().toStdString();
            cur["type"] = el->typeName().toStdString();
            // Build the hierarchical path by walking parents.
            std::vector<std::string> path;
            for (Element* p = el; p; p = p->parent())
                  path.push_back(p->name().toStdString());
            std::reverse(path.begin(), path.end());
            json pathJson = json::array();
            for (const auto& n : path)
                  pathJson.push_back(n);
            cur["path"]    = pathJson;
            out["current"] = cur;
            }
      else {
            out["current"] = nullptr;
            }
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolSelectElement
//    Set ZCam::currentElement to the named element.  An empty or absent
//    name clears the selection (deselects everything).
//--------------------------------------------------------------------

std::string AIAgent::toolSelectElement(const json& args) {
      if (!_zc)
            return errorResponse("ZCam not ready");
      if (!_zc->project())
            return errorResponse("no project");
      std::string nameStr;
      if (args.contains("name") && !args["name"].is_null())
            nameStr = args["name"].get<std::string>();
      if (nameStr.empty()) {
            _zc->clearSelection();
            json out;
            out["ok"]      = true;
            out["current"] = nullptr;
            out["msg"]     = "selection cleared";
            return out.dump();
            }
      Element* el = resolveElement(QString::fromStdString(nameStr));
      if (!el)
            return errorResponse("element not found: " + nameStr);
      auto* e3d = qobject_cast<Element3d*>(el);
      if (!e3d)
            return errorResponse("element is not a 3D element: " + nameStr);
      _zc->clearSelectionList();
      _zc->setCurrentElement(e3d);
      json out;
      out["ok"]   = true;
      out["name"] = e3d->name().toStdString();
      out["type"] = e3d->typeName().toStdString();
      out["msg"]  = "current element set";
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolGetSelectedElements
//    Return the list of all elements in ZCam::selectedElements (the
//    lasso/multi-selection).  Each entry has name, type and depth.
//--------------------------------------------------------------------

std::string AIAgent::toolGetSelectedElements(const json& args) const {
      (void)args;
      if (!_zc)
            return errorResponse("ZCam not ready");
      json elements = json::array();
      for (Element3d* el : _zc->selectedElements()) {
            json e;
            e["name"] = el->name().toStdString();
            e["type"] = el->typeName().toStdString();
            // Compute depth by walking parents.
            int depth = 0;
            for (Element* p = el->parent(); p; p = p->parent())
                  ++depth;
            e["depth"] = depth;
            elements.push_back(e);
            }
      json out;
      out["ok"]       = true;
      out["elements"] = elements;
      if (_zc->currentElement())
            out["current"] = _zc->currentElement()->name().toStdString();
      else
            out["current"] = nullptr;
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolSelectElements
//    Replace the multi-selection with the given list of named elements.
//    The first element becomes the current (primary) element.
//--------------------------------------------------------------------

std::string AIAgent::toolSelectElements(const json& args) {
      if (!_zc)
            return errorResponse("ZCam not ready");
      if (!_zc->project())
            return errorResponse("no project");
      if (!args.contains("names") || !args["names"].is_array())
            return errorResponse("'names' must be a JSON array of strings");
      // Clear current selection first.
      _zc->clearSelection();
      const auto& names = args["names"];
      if (names.empty()) {
            json out;
            out["ok"]       = true;
            out["msg"]      = "selection cleared (empty list)";
            out["elements"] = json::array();
            return out.dump();
            }
      std::vector<Element3d*> resolved;
      std::vector<std::string> notFound;
      for (const auto& n : names) {
            if (!n.is_string()) {
                  notFound.push_back(n.dump());
                  continue;
                  }
            std::string nameStr = n.get<std::string>();
            Element* el         = resolveElement(QString::fromStdString(nameStr));
            if (!el) {
                  notFound.push_back(nameStr);
                  continue;
                  }
            auto* e3d = qobject_cast<Element3d*>(el);
            if (!e3d) {
                  notFound.push_back(nameStr + " (not a 3D element)");
                  continue;
                  }
            resolved.push_back(e3d);
            }
      if (!notFound.empty()) {
            std::string msg = "element(s) not found: ";
            for (std::size_t i = 0; i < notFound.size(); ++i) {
                  if (i > 0)
                        msg += ", ";
                  msg += notFound[i];
                  }
            return errorResponse(msg);
            }
      // Build up the selection via addToSelection so signals fire correctly.
      for (auto* e3d : resolved)
            _zc->addToSelection(e3d);
      // Ensure the first element is the current (primary) element.
      if (!resolved.empty())
            _zc->setCurrentElement(resolved[0]);
      json elements = json::array();
      for (auto* e3d : resolved) {
            json e;
            e["name"] = e3d->name().toStdString();
            e["type"] = e3d->typeName().toStdString();
            elements.push_back(e);
            }
      json out;
      out["ok"]       = true;
      out["elements"] = elements;
      if (!resolved.empty())
            out["current"] = resolved[0]->name().toStdString();
      else
            out["current"] = nullptr;
      out["msg"] = "multi-selection set";
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolClearSelection
//    Clear both the current element and the multi-selection list.
//--------------------------------------------------------------------

std::string AIAgent::toolClearSelection(const json& args) {
      (void)args;
      if (!_zc)
            return errorResponse("ZCam not ready");
      _zc->clearSelection();
      json out;
      out["ok"]  = true;
      out["msg"] = "selection cleared";
      return out.dump();
      }

//--------------------------------------------------------------------
//     elementToWorldPaths
//    Convert an element's *fill* geometry (the unmodified outline before
//    strokeAndFill inflation) to world-space Clipper2 paths, closing any
//    open subpaths so Clipper2 can treat them as closed polygons.
//
//    This is the correct geometry source for containment tests:
//    _pathList may have been replaced by inflated stroke outlines when
//    lineWidth > 0, which would give wrong results for area operations.
//--------------------------------------------------------------------

static Clipper2Lib::PathsD elementToWorldPaths(const Element3d* element) {
      Clipper2Lib::PathsD result;
      const PathList& pl = element->fillPathList();
      if (pl.empty())
            return result;
      QMatrix4x4 matrix = element->globalMatrix();
      for (const auto& path : pl) {
            if (path.size() < 3)
                  continue;
            Clipper2Lib::PathD cp;
            cp.reserve(path.size() + 1);
            for (const auto& pt : path) {
                  auto r = matrix.map(QVector3D(float(pt.x()), float(pt.y()), 0.0f));
                  cp.push_back({double(r.x()), double(r.y())});
                  }
            // Close the path explicitly: Clipper2 requires closed
            // contours for Difference / Union / Intersection operations.
            if (cp.front() != cp.back())
                  cp.push_back(cp.front());
            result.push_back(std::move(cp));
            }
      return result;
      }

//--------------------------------------------------------------------
//     toolIsElementInside
//    Test whether one element's closed 2D geometry is fully contained
//    inside another element's closed 2D geometry, using the Clipper2
//    polygon-difference algorithm.
//
//    Algorithm:
//      1. Convert each element's *fill* geometry (the unmodified outline
//         before strokeAndFill inflation) to world-space paths via
//         elementToWorldPaths(), which applies the full transform chain
//         (pos, rot, scale, parent transforms) and closes open subpaths.
//         Using the fill geometry (not _pathList) is critical: when
//         lineWidth > 0, strokeAndFill() replaces _pathList with inflated
//         stroke outlines that are larger than the actual fill outline.
//      2. Compute the Clipper2 Difference (inner − outer) with
//         FillRule::NonZero.  If the result is empty, every point of
//         the inner element lies inside (or on the boundary of) the
//         outer element — i.e. the inner element is fully contained.
//      3. As a fast pre-check, verify that the inner element's bounding
//         box is inside the outer element's bounding box.  If not, we
//         can short-circuit with inside=false without running Clipper2.
//
//    The method works for any elements that produce closed 2D outlines
//    (Polygon, Rectangle, Ellipse, Text, Group, …).  Elements with no
//    path data (empty pathList) are treated as having no geometry.
//
//    Parameters:
//      - "inner": name of the element to test for containment
//      - "outer": name of the containing element
//
//    Returns:
//      { "ok": true, "inside": bool, "inner": name, "outer": name,
//        "method": "clipper2-difference" }
//--------------------------------------------------------------------

std::string AIAgent::toolIsElementInside(const json& args) {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      std::string innerName = args.value("inner", "");
      std::string outerName = args.value("outer", "");
      if (innerName.empty())
            return errorResponse("'inner' parameter is required");
      if (outerName.empty())
            return errorResponse("'outer' parameter is required");
      if (innerName == outerName)
            return errorResponse("'inner' and 'outer' must be different elements");
      Element* innerEl = resolveElement(QString::fromStdString(innerName));
      if (!innerEl)
            return errorResponse("element not found: " + innerName);
      Element* outerEl = resolveElement(QString::fromStdString(outerName));
      if (!outerEl)
            return errorResponse("element not found: " + outerName);
      auto* inner3d = qobject_cast<Element3d*>(innerEl);
      if (!inner3d)
            return errorResponse("inner element is not a 3D element: " + innerName);
      auto* outer3d = qobject_cast<Element3d*>(outerEl);
      if (!outer3d)
            return errorResponse("outer element is not a 3D element: " + outerName);

      // Get world-space fill geometry for both elements.
      // Use fillPathList (not _pathList) because strokeAndFill() may
      // have replaced _pathList with inflated stroke outlines.
      Clipper2Lib::PathsD innerPaths = elementToWorldPaths(inner3d);
      Clipper2Lib::PathsD outerPaths = elementToWorldPaths(outer3d);

      if (innerPaths.empty())
            return errorResponse("inner element has no geometry: " + innerName);
      if (outerPaths.empty())
            return errorResponse("outer element has no geometry: " + outerName);

      // Fast bounding-box pre-check: if the inner element's world
      // bounding box is not fully inside the outer element's world
      // bounding box, the inner cannot be inside the outer.
      QRectF innerBBox = inner3d->worldBoundingBox();
      QRectF outerBBox = outer3d->worldBoundingBox();
      if (innerBBox.isNull() || innerBBox.isEmpty() || outerBBox.isNull() || outerBBox.isEmpty())
            return errorResponse("one or both elements have an empty bounding box");
      if (innerBBox.left() < outerBBox.left() || innerBBox.right() > outerBBox.right() ||
          innerBBox.top() < outerBBox.top() || innerBBox.bottom() > outerBBox.bottom()) {
            json out;
            out["ok"]     = true;
            out["inside"] = false;
            out["inner"]  = innerName;
            out["outer"]  = outerName;
            out["method"] = "bounding-box-reject";
            out["msg"]    = "inner bounding box is not inside outer bounding box";
            return out.dump();
            }

      // Clipper2 Difference: inner − outer.
      // If the result is empty, the inner geometry is fully contained
      // within the outer geometry (no part of inner lies outside outer).
      Clipper2Lib::ClipperD clipper(4);
      clipper.AddSubject(innerPaths);
      clipper.AddClip(outerPaths);
      Clipper2Lib::PathsD diff;
      clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, diff);

      bool inside = diff.empty();
      json out;
      out["ok"]     = true;
      out["inside"] = inside;
      out["inner"]  = innerName;
      out["outer"]  = outerName;
      out["method"] = "clipper2-difference";
      out["msg"]    = inside ? innerName + " is fully contained inside " + outerName
                             : innerName + " is not fully contained inside " + outerName;
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolSetMops
//    Assign a named laser MOP (laser layer) to an element by setting
//    its 'mop' property.  Pass an empty mops_name to clear the
//    assignment (element inherits from parent).  The change is
//    routed through Project::changeProperty() so it is undoable.
//--------------------------------------------------------------------

std::string AIAgent::toolSetMops(const json& args) {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      std::string elementName = args.value("element", "");
      std::string mopsName    = args.value("mops_name", "");
      if (elementName.empty())
            return errorResponse("'element' parameter is required");
      Element* el = resolveElement(QString::fromStdString(elementName));
      if (!el)
            return errorResponse("element not found: " + elementName);
      auto* el3d = qobject_cast<Element3d*>(el);
      if (!el3d)
            return errorResponse("element is not a 3D element: " + elementName);

      // Read the old MOP name (if any) for the response.
      QVariant oldMopVar      = el3d->property("mop");
      Mop* oldMop             = oldMopVar.value<Mop*>();
      std::string oldMopsName = oldMop ? oldMop->name().toStdString() : "";

      QVariant value;
      if (!mopsName.empty()) {
            Mop* mop = _zc->mopPtr(QString::fromStdString(mopsName));
            if (!mop)
                  return errorResponse("laser MOP not found: " + mopsName);
            value = QVariant::fromValue(mop);
            }
      else {
            // Clear: write a typed null for the Mop* property.
            int propIdx = el3d->metaObject()->indexOfProperty("mop");
            if (propIdx < 0)
                  return errorResponse("element has no 'mop' property: " + elementName);
            QMetaProperty mp = el3d->metaObject()->property(propIdx);
            value            = QVariant(mp.metaType(), nullptr);
            }

      _zc->project()->changeProperty(el3d, QStringLiteral("mop"), value);

      // Read the new MOP name (if any) for the response.
      QVariant newMopVar      = el3d->property("mop");
      Mop* newMop             = newMopVar.value<Mop*>();
      std::string newMopsName = newMop ? newMop->name().toStdString() : "";

      json out;
      out["ok"]      = true;
      out["element"] = el->name().toStdString();
      out["mops"]    = mopsName;
      out["oldMops"] = oldMopsName;
      out["newMops"] = newMopsName;
      out["msg"]     = el->name().toStdString() + ".mop = " + (mopsName.empty() ? "(cleared)" : mopsName);
      return out.dump();
      }

//--------------------------------------------------------------------
//     toolRunScript
//    Execute a JavaScript snippet in the ZCam scripting engine.
//    The script has access to `zcam` (imperative API), `geom`
//    (geometry helpers), `project` (element namespace) and `config`.
//    Returns {ok, result} or {ok:false, error}.
//--------------------------------------------------------------------

std::string AIAgent::toolRunScript(const json& args) {
      if (!_zc || !_zc->project())
            return errorResponse("no project");
      std::string script = args.value("script", "");
      int timeoutMs      = args.value("timeout_ms", 10000);
      if (script.empty())
            return errorResponse("'script' parameter is required");

      auto* se = ScriptEngine::instance();
      if (!se)
            return errorResponse("script engine not available");

      auto r = se->evalImperative(QString::fromStdString(script), timeoutMs);
      if (!r.error.isEmpty()) {
            json out;
            out["ok"]    = false;
            out["error"] = r.error.toStdString();
            return out.dump();
            }

      json out;
      out["ok"]     = true;
      out["result"] = variantToJson(r.value);
      return out.dump();
      }

//--------------------------------------------------------------------
//     truncateToolResult
//    Cap a tool result so it fits inside the context window.  Ollama
//    front-truncates (drops oldest messages first) when the prompt
//    exceeds num_ctx.  The Qwen3.8 renderer's validateMessages() then
//    fails with "no user query found in messages" because the original
//    user message was the one dropped.  Keeping tool results under ~
//    50 % of the context window in tokens leaves room for the system
//    prompt, user query, assistant message and the model's reply.
//
//    8000 chars ≈ 2 300 tokens  →  fits in a 4096-token window with
//    ~1 700 tokens of headroom.
//--------------------------------------------------------------------

static constexpr size_t TOOL_RESULT_MAX_CHARS = 8000;
static std::string truncateToolResult(const std::string& result) {
      if (result.size() <= TOOL_RESULT_MAX_CHARS)
            return result;
      const size_t half      = TOOL_RESULT_MAX_CHARS / 2;
      std::string truncated  = result.substr(0, half);
      truncated             += "\n... [truncated " + std::to_string(result.size() - TOOL_RESULT_MAX_CHARS) +
                               " chars — use list_elements with max_depth or a more specific query] ...\n";
      truncated             += result.substr(result.size() - half);
      return truncated;
      }

//--------------------------------------------------------------------
//     executeTool
//    Dispatch to the named tool implementation.
//--------------------------------------------------------------------

std::string AIAgent::executeTool(const std::string& functionName, const json& arguments) {
      Debug("AI tool call: <{}> {}", functionName, arguments.dump(2));
      try {
            if (functionName == "new_project")
                  return toolNewProject();
            else if (functionName == "save_project")
                  return toolSaveProject();
            else if (functionName == "start_session")
                  return toolStartSession();
            else if (functionName == "end_session")
                  return toolEndSession();
            else if (functionName == "undo")
                  return toolUndo();
            else if (functionName == "redo")
                  return toolRedo();
            else if (functionName == "create_element")
                  return toolCreateElement(arguments);
            else if (functionName == "delete_element")
                  return toolDeleteElement(arguments);
            else if (functionName == "rename_element")
                  return toolRenameElement(arguments);
            else if (functionName == "move_element")
                  return toolMoveElement(arguments);
            else if (functionName == "read_property")
                  return toolReadProperty(arguments);
            else if (functionName == "write_property")
                  return toolWriteProperty(arguments);
            else if (functionName == "list_properties")
                  return toolListProperties(arguments);
            else if (functionName == "describe_property")
                  return toolDescribeProperty(arguments);
            else if (functionName == "invoke_method")
                  return toolInvokeMethod(arguments);
            else if (functionName == "list_methods")
                  return toolListMethods(arguments);
            else if (functionName == "list_elements")
                  return toolListElements(arguments);
            else if (functionName == "screenshot")
                  return toolScreenshot(arguments);
            else if (functionName == "get_current_element")
                  return toolGetCurrentElement(arguments);
            else if (functionName == "select_element")
                  return toolSelectElement(arguments);
            else if (functionName == "get_selected_elements")
                  return toolGetSelectedElements(arguments);
            else if (functionName == "select_elements")
                  return toolSelectElements(arguments);
            else if (functionName == "clear_selection")
                  return toolClearSelection(arguments);
            else if (functionName == "is_element_inside")
                  return toolIsElementInside(arguments);
            else if (functionName == "set_mops")
                  return toolSetMops(arguments);
            else if (functionName == "run_script")
                  return toolRunScript(arguments);
            }
      catch (const std::exception& e) {
            return errorResponse(std::string("tool execution error: ") + e.what());
            }
      catch (...) {
            return errorResponse("tool execution error: unknown exception");
            }
      return errorResponse("unknown tool: " + functionName);
      }

//--------------------------------------------------------------------
//     trimHistory
//    Keep the conversation history within the context window so Ollama
//    does not front-truncate away the original user message.  Two
//    strategies are combined:
//
//    1. Strip the base64 "images" array from all tool results except the
//       most recent one.  Screenshot images are by far the largest single
//       payload (each ~50 000–300 000 chars of base64) and a model rarely
//       needs to *see* an old screenshot once newer context is available.
//       The textual "content" summary is kept so the model still knows a
//       screenshot was taken.
//
//    2. If the estimated token count (chars/4 heuristic) still exceeds a
//       fraction of num_ctx, drop the oldest messages from the *middle*
//       of the history — keeping the system prompt, the most recent user
//       message, and the last few tool exchanges — so the user query is
//       always present and the total fits comfortably.
//
//    The function is called before every POST to /api/chat.
//--------------------------------------------------------------------

void AIAgent::trimHistory() {
      if (_history.empty())
            return;

      // ── 1. Strip old screenshot images ────────────────────────────────
      // Find the index of the last message that carries an "images" array.
      int lastImageIdx = -1;
      for (int i = static_cast<int>(_history.size()) - 1; i >= 0; --i) {
            if (_history[i].contains("images")) {
                  lastImageIdx = i;
                  break;
                  }
            }
      // Remove "images" from every earlier message.
      for (int i = 0; i < lastImageIdx; ++i)
            if (_history[i].contains("images"))
                  _history[i].erase("images");

      // ── 2. Enforce a token budget ─────────────────────────────────────
      // Estimate tokens as chars/4 (rough heuristic for mixed text/code).
      // Target at most 60 % of num_ctx for the history so the model has
      // room for its reply.
      const int numCtx         = (_contextSize > 0) ? _contextSize : 4096;
      const size_t tokenBudget = static_cast<size_t>(numCtx * 0.60);
      const size_t charBudget  = tokenBudget * 4;

      // Compute total character size of the history.
      auto historyChars = [this]() {
            size_t total = 0;
            for (const auto& m : _history)
                  total += m.dump().size();
            return total;
            };

      if (historyChars() <= charBudget)
            return;

      // Drop oldest messages from the middle, keeping:
      //   - index 0 is never in _history (that's the system prompt added
      //     in buildRequestJson), so _history[0] is the first user message.
      //   - Always keep the first user message (so the model has a query).
      //   - Always keep the last N messages (recent context).
      const size_t keepRecent = 10; // keep last 10 messages
      while (_history.size() > keepRecent + 1 && historyChars() > charBudget) {
            // Remove the second message (index 1) — the oldest non-essential one.
            // We never remove index 0 (the original user query).
            if (_history.size() <= 2)
                  break;
            _history.erase(_history.begin() + 1);
            }
      }

//--------------------------------------------------------------------
//     buildRequestJson
//    Build the Ollama /api/chat request body, including the
//    tools schema and the current session history.
//--------------------------------------------------------------------

json AIAgent::buildRequestJson() const {
      json requestJson;
      requestJson["model"]      = _ollamaModel.toStdString();
      requestJson["stream"]     = true;
      requestJson["keep_alive"] = 60; // keep model loaded for 60s

      json options;
      if (_temperature >= 0.0)
            options["temperature"] = _temperature;
      if (_contextSize > 0)
            options["num_ctx"] = _contextSize;
      if (!options.empty())
            requestJson["options"] = options;

      // Convert MCP-style tools to Ollama's {type, function{...}} format.
      json ollamaTools = json::array();
      for (const auto& tool : _tools) {
            json t;
            t["type"]     = "function";
            t["function"] = {
                     {       "name",        tool["name"]},
                     {"description", tool["description"]},
                     { "parameters", tool["inputSchema"]}
                  };
            ollamaTools.push_back(t);
            }
      if (!ollamaTools.empty())
            requestJson["tools"] = ollamaTools;

      // Build the messages array.
      json messages = json::array();
      // System prompt
      json sys;
      sys["role"]    = "system";
      sys["content"] = "You are the AI assistant for ZCam, a manufacturing tool for G-code CNC machines and "
                       "fiber laser engraving.  You control the application through a set of JSON tools.  "
                       "When the user asks you to create / modify / delete elements or change properties, "
                       "invoke the appropriate tool.  Always use list_elements and list_properties to "
                       "understand the current project before making changes.  When you are done, "
                       "summarise what you did in plain text.  "
                       "IMPORTANT: Only write properties that the user explicitly mentions.  Do not set "
                       "any other properties to their default values — leave them untouched.  "
                       "Tools available: new_project, save_project, start_session, end_session, undo, "
                       "redo, create_element, delete_element, rename_element, move_element, read_property, "
                       "write_property, list_properties, describe_property, invoke_method, "
                       "list_methods, list_elements, screenshot, is_element_inside, set_mops.  "
                       "The screenshot tool lets you SEE the current 3-D canvas (a multimodal "
                       "image is attached to the tool result) — use it to answer visual questions "
                       "or to verify the result of edits before you finish.  "
                       "The is_element_inside tool tests whether one element's geometry is fully "
                       "contained inside another (e.g. polygon inside polygon), using the Clipper2 "
                       "polygon-difference algorithm in world coordinates.";
      messages.push_back(sys);
      for (const auto& msg : _history) {
            json m;
            m["role"] = msg.value("role", "user");
            if (msg.contains("content"))
                  m["content"] = msg["content"];
            if (msg.contains("tool_calls"))
                  m["tool_calls"] = msg["tool_calls"];
            if (msg.contains("name"))
                  m["name"] = msg["name"];
            if (msg.contains("tool_call_id"))
                  m["tool_call_id"] = msg["tool_call_id"];
            if (msg.contains("images"))
                  m["images"] = msg["images"];
            messages.push_back(m);
            }
      requestJson["messages"] = messages;
      return requestJson;
      }

//--------------------------------------------------------------------
//     sendMessage
//--------------------------------------------------------------------

void AIAgent::sendMessage(const QString& text) {
      if (_busy) {
            Warning("AI agent is busy, dropping message");
            return;
            }
      if (text.trimmed().isEmpty())
            return;
      // Append user message to history
      json user;
      user["role"]    = "user";
      user["content"] = text.toStdString();
      _history.push_back(user);

      _busy          = true;
      _stopRequested = false;
      _streamBuffer.clear();
      _currentMessage.clear();
      emit busyChanged();
      emit currentMessageChanged();
      emit chunkReceived(QString(), QString());

      QNetworkRequest request;
      request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
      request.setUrl(QUrl(_ollamaBaseUrl));
      request.setTransferTimeout(10 * 60 * 1000);

      trimHistory();
      json body = buildRequestJson();
      QByteArray payload =
          QString::fromStdString(body.dump(-1, ' ', false, json::error_handler_t::replace)).toUtf8();

      if (QNetworkReply* old = _reply) {
            _reply = nullptr;
            old->disconnect();
            old->abort();
            old->deleteLater();
            }
      _reply = _network.post(request, payload);
      connect(_reply, &QNetworkReply::readyRead, this, &AIAgent::onReadyRead);
      connect(_reply, &QNetworkReply::finished, this, &AIAgent::onFinished);
      }

//--------------------------------------------------------------------
//     onReadyRead
//--------------------------------------------------------------------

void AIAgent::onReadyRead() {
      if (!_reply)
            return;
      _streamBuffer.append(_reply->readAll());
      }

//--------------------------------------------------------------------
//     onFinished
//--------------------------------------------------------------------

void AIAgent::onFinished() {
      QNetworkReply* reply = _reply;
      _reply               = nullptr;
      if (!reply)
            return;
      reply->deleteLater();

      if (reply->error() != QNetworkReply::NoError) {
            // If the user requested a stop, this abort is expected — don't
            // log it as an error or emit agentError.
            if (_stopRequested) {
                  _busy = false;
                  emit busyChanged();
                  return;
                  }
            // Log the error (it used to be silently emitted to the UI only,
            // so e.g. an Ollama "internal server error" was invisible in
            // zcam.log).  Include the HTTP status code and the first
            // bytes of the response body — the server often sends a
            // useful error description there.
            int httpStatus = -1;
            if (QVariant attr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute); attr.isValid())
                  httpStatus = attr.toInt();
            QByteArray body = reply->readAll().left(1024);
            Warning("AI agent: request to {} failed: {} (HTTP status {}), body: {}",
                _ollamaBaseUrl.toStdString(), reply->errorString().toStdString(), httpStatus,
                body.toStdString());
            emit agentError(reply->errorString());
            _busy = false;
            emit busyChanged();
            return;
            }

      // Parse the streamed response.  Ollama's /api/chat streams
      // a sequence of JSON lines, each with a "message" object.
      // Each chunk may contain content (string) or tool_calls (array).
      QString all;
      json allToolCalls = json::array();
      for (const auto& line : _streamBuffer.split('\n', Qt::SkipEmptyParts)) {
            if (line.trimmed().isEmpty())
                  continue;
            try {
                  json item = json::parse(line.toStdString());
                  if (item.contains("error")) {
                        std::string err = item["error"].is_string() ? item["error"].get<std::string>()
                                                                    : item["error"].dump();
                        emit agentError(QString::fromStdString(err));
                        continue;
                        }
                  if (item.contains("message") && item["message"].is_object()) {
                        const auto& msg = item["message"];
                        if (msg.contains("content") && msg["content"].is_string()) {
                              std::string s = msg["content"].get<std::string>();
                              if (!s.empty()) {
                                    all += QString::fromStdString(s);
                                    emit chunkReceived(QString(), QString::fromStdString(s));
                                    }
                              }
                        if (msg.contains("tool_calls") && msg["tool_calls"].is_array())
                              for (const auto& tc : msg["tool_calls"])
                                    allToolCalls.push_back(tc);
                        }
                  }
            catch (const json::parse_error& e) {
                  Warning("AI agent JSON parse error: {}", e.what());
                  }
            }
      _streamBuffer.clear();

      if (!allToolCalls.empty()) {
            // ── Execute the tool calls and append to history ──────────
            // First add the assistant message with tool_calls
            json assistantMsg;
            assistantMsg["role"]       = "assistant";
            assistantMsg["content"]    = all.toStdString();
            assistantMsg["tool_calls"] = allToolCalls;
            _history.push_back(assistantMsg);

            for (const auto& tc : allToolCalls) {
                  std::string name;
                  json args = json::object();
                  if (tc.contains("function")) {
                        if (tc["function"].contains("name"))
                              name = tc["function"]["name"].get<std::string>();
                        if (tc["function"].contains("arguments")) {
                              if (tc["function"]["arguments"].is_string()) {
                                    try {
                                          args = json::parse(tc["function"]["arguments"].get<std::string>());
                                          }
                                    catch (...) {
                                          args = json::object();
                                          }
                                    }
                              else if (tc["function"]["arguments"].is_object()) {
                                    args = tc["function"]["arguments"];
                                    }
                              }
                        }
                  else if (tc.contains("name")) {
                        name = tc["name"].get<std::string>();
                        if (tc.contains("arguments") && tc["arguments"].is_object())
                              args = tc["arguments"];
                        }
                  if (name.empty())
                        continue;
                  std::string result;
                  try {
                        result = executeTool(name, args);
                        }
                  catch (const std::exception& e) {
                        result = errorResponse(std::string("tool error: ") + e.what());
                        }
                  // Emit a UI notification
                  json resJson;
                  try {
                        resJson = json::parse(result);
                        }
                  catch (...) {
                        resJson = json(result);
                        }
                  QString resultText;
                  if (resJson.is_object() && resJson.contains("ok")) {
                        resultText = resJson.value("ok", false)
                                         ? QString("[%1] OK — %2")
                                               .arg(QString::fromStdString(name))
                                               .arg(QString::fromStdString(
                                                   resJson.contains("msg") && resJson["msg"].is_string()
                                                       ? resJson["msg"].get<std::string>()
                                                       : std::string()))
                                         : QString("[%1] FAIL — %2")
                                               .arg(QString::fromStdString(name))
                                               .arg(QString::fromStdString(
                                                   resJson.contains("error") && resJson["error"].is_string()
                                                       ? resJson["error"].get<std::string>()
                                                       : std::string()));
                        }
                  else {
                        resultText = QString("[%1] %2")
                                         .arg(QString::fromStdString(name))
                                         .arg(QString::fromStdString(result));
                        }
                  emit toolCallResult(QString::fromStdString(name), resultText);
                  emit chunkReceived(QString(), "\n" + resultText + "\n");
                  // Append tool result to history.  If the tool produced
                  // an embedded image (e.g. the `screenshot` tool), attach
                  // it via Ollama's `images` field so a multimodal model
                  // can actually see the canvas — the base64 payload stays
                  // out of the textual content.
                  json toolResult;
                  toolResult["role"] = "tool";
                  toolResult["name"] = name;
                  if (resJson.is_object() && resJson.contains("image") && resJson["image"].is_string()) {
                        // Plain-text summary for the model; the image itself
                        // is carried in the `images` array below.
                        std::string contentText = resJson.value(
                            "msg", std::string("Screenshot of the 3-D canvas (see attached image)."));
                        if (resJson.contains("file") && resJson["file"].is_string())
                              contentText += "  Full-resolution copy: " + resJson["file"].get<std::string>();
                        toolResult["content"] = contentText;
                        json imgArray         = json::array();
                        imgArray.push_back(resJson["image"].get<std::string>());
                        toolResult["images"] = imgArray;
                        }
                  else {
                        // Truncate oversized results (e.g. list_elements on a
                        // large project) so the prompt fits in num_ctx — see
                        // truncateToolResult().  Without this the Qwen3.8
                        // renderer rejects the request with "no user query
                        // found in messages" after Ollama front-truncation
                        // drops the original user message.
                        toolResult["content"] = truncateToolResult(result);
                        }
                  if (tc.contains("id"))
                        toolResult["tool_call_id"] = tc["id"];
                  _history.push_back(toolResult);
                  }

            // Continue the loop: ask the model to continue / summarise.
            if (_stopRequested) {
                  _busy = false;
                  emit busyChanged();
                  saveCurrentSession();
                  return;
                  }
            // Re-send the conversation (without stream buffering — just loop).
            // We do this by re-invoking sendMessage internally.  To avoid
            // a visible "user: " message we construct the request directly.
            QNetworkRequest request;
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            request.setUrl(QUrl(_ollamaBaseUrl));
            request.setTransferTimeout(10 * 60 * 1000);
            trimHistory();
            json body = buildRequestJson();
            QByteArray payload =
                QString::fromStdString(body.dump(-1, ' ', false, json::error_handler_t::replace)).toUtf8();
            _reply = _network.post(request, payload);
            connect(_reply, &QNetworkReply::readyRead, this, &AIAgent::onReadyRead);
            connect(_reply, &QNetworkReply::finished, this, &AIAgent::onFinished);
            return;
            }

      // ── Final answer (no tool calls) ────────────────────────────────
      json finalMsg;
      finalMsg["role"]    = "assistant";
      finalMsg["content"] = all.toStdString();
      _history.push_back(finalMsg);

      _currentMessage = all;
      emit currentMessageChanged();
      emit finished(all);

      _busy = false;
      emit busyChanged();
      saveCurrentSession();
      }

//--------------------------------------------------------------------
//     stop
//--------------------------------------------------------------------

void AIAgent::stop() {
      _stopRequested = true;
      // Capture the reply pointer and null the member *before* calling
      // abort().  abort() may synchronously emit finished(), which runs
      // onFinished() and sets _reply to nullptr.  Without this guard we
      // would dereference a dangling _reply below (segfault).
      if (QNetworkReply* reply = _reply) {
            _reply = nullptr;
            reply->abort();
            reply->deleteLater();
            }
      _busy = false;
      emit busyChanged();
      saveCurrentSession();
      }

//--------------------------------------------------------------------
//     Session helpers
//--------------------------------------------------------------------

QString AIAgent::sessionDirectory() const {
      QString home = QDir::homePath();
      return home + "/ZCam/ai_sessions";
      }

QString AIAgent::nextSessionPath() const {
      QDir dir(sessionDirectory());
      if (!dir.exists())
            dir.mkpath(".");
      QDate today = QDate::currentDate();
      int n       = 1;
      QString path;
      do {
            path = dir.absoluteFilePath(QString("Session-%1-%2.json").arg(today.toString("yy-MM-dd")).arg(n));
            ++n;
            } while (QFile::exists(path) && n < 10000);
      return path;
      }

void AIAgent::refreshSessionList() {
      QString dirPath = sessionDirectory();
      QDir dir(dirPath);
      QStringList files = dir.entryList({"Session-*.json"}, QDir::Files, QDir::Name);
      _sessionList.clear();
      for (const auto& f : files) {
            // Strip "Session-" prefix and ".json" suffix for compact display
            QString display = f;
            if (display.startsWith("Session-"))
                  display = display.mid(8);
            if (display.endsWith(".json"))
                  display.chop(5);
            _sessionList.append(display);
            }
      emit sessionListChanged();
      }

void AIAgent::saveCurrentSession() {
      if (_history.empty())
            return;
      QString path;
      if (_sessionName.isEmpty()) {
            path = nextSessionPath();
            }
      else {
            // _sessionName may be a file name or full path
            if (QFileInfo(_sessionName).isAbsolute())
                  path = _sessionName;
            else
                  path = sessionDirectory() + "/" + _sessionName;
            }
      QDir().mkpath(QFileInfo(path).absolutePath());
      QFile f(path);
      if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            json header;
            header["model"]       = _ollamaModel.toStdString();
            header["temperature"] = _temperature;
            header["created"]     = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
            f.write((QString::fromStdString(header.dump()) + "\n").toUtf8());
            for (const auto& m : _history)
                  f.write((QString::fromStdString(m.dump()) + "\n").toUtf8());
            f.close();
            _sessionName = QFileInfo(path).fileName();
            emit sessionNameChanged();
            refreshSessionList();
            }
      }

//--------------------------------------------------------------------
//     sessionFileName
//--------------------------------------------------------------------
//   Reconstructs the full session filename ("Session-yy-MM-dd-n.json")
//   from a compact display name stored in _sessionList ("yy-MM-dd-n").
QString AIAgent::sessionFileName(int index) const {
      QString name = _sessionList.at(index);
      if (!name.startsWith("Session-"))
            name = "Session-" + name;
      if (!name.endsWith(".json"))
            name += ".json";
      return name;
      }

void AIAgent::loadSession(int index) {
      if (index < 0 || index >= _sessionList.size()) {
            _history.clear();
            _sessionName.clear();
            emit sessionNameChanged();
            emit sessionLoaded();
            return;
            }
      QString path = sessionDirectory() + "/" + sessionFileName(index);
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly)) {
            emit agentError("cannot open session: " + path);
            return;
            }
      _history.clear();
      while (!f.atEnd()) {
            QByteArray line = f.readLine().trimmed();
            if (line.isEmpty())
                  continue;
            try {
                  json j = json::parse(line.toStdString());
                  if (j.contains("role") || j.contains("parts") || j.contains("content"))
                        _history.push_back(j);
                  }
            catch (...) {
                  // skip malformed line
                  }
            }
      f.close();
      _sessionName = QFileInfo(path).fileName();
      emit sessionNameChanged();
      _currentSessionIndex = index;
      emit currentSessionChanged();
      emit sessionLoaded();
      }

void AIAgent::newSession() {
      if (_busy)
            return;
      _history.clear();
      _sessionName.clear();
      _currentMessage.clear();

      // Create the session file on disk immediately so the new session
      // appears in the session list (and thus in the QML ComboBox) right
      // away — the user sees the freshly generated name as soon as the
      // "+" button is clicked, without having to send a message first.
      QString path = nextSessionPath();
      QDir().mkpath(QFileInfo(path).absolutePath());
      QFile f(path);
      if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            json header;
            header["model"]       = _ollamaModel.toStdString();
            header["temperature"] = _temperature;
            header["created"]     = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
            f.write((QString::fromStdString(header.dump()) + "\n").toUtf8());
            f.close();
            _sessionName = QFileInfo(path).fileName();
            }

      // Refresh the session list so the new file is visible and
      // select it as the current session.
      refreshSessionList();
      int newIdx = _sessionList.indexOf(QFileInfo(path).baseName().mid(8));
      if (newIdx < 0)
            newIdx = _sessionList.size() - 1; // fallback: last entry
      _currentSessionIndex = newIdx;

      emit sessionNameChanged();
      emit currentMessageChanged();
      emit currentSessionChanged();
      emit sessionLoaded();
      }

void AIAgent::selectSession(int index) {
      if (_busy)
            return;
      loadSession(index);
      }

void AIAgent::deleteSession(int index) {
      if (index < 0 || index >= _sessionList.size())
            return;
      QString path = sessionDirectory() + "/" + sessionFileName(index);
      QFile::remove(path);
      if (_currentSessionIndex == index)
            newSession();
      refreshSessionList();
      }

void AIAgent::setCurrentSession(int index) {
      if (index == _currentSessionIndex)
            return;
      _currentSessionIndex = index;
      emit currentSessionChanged();
      }

//--------------------------------------------------------------------
//     sessionConversation
//--------------------------------------------------------------------
//   Returns the conversation history as a QVariantList of objects with
//   "role" ("user"/"assistant"/"tool") and "text" (displayable content).
//   Used by QML to rebuild the conversation view after loading a session.
//   The first JSON line in a session file is a header (model, temperature,
//   created) without a "role" field — it is skipped.  Tool-call entries
//   (assistant messages with tool_calls but no content) are skipped so
//   only visible text is shown.
QVariantList AIAgent::sessionConversation() const {
      QVariantList result;
      for (const auto& msg : _history) {
            std::string role = msg.value("role", std::string());
            if (role == "user") {
                  std::string content = msg.value("content", std::string());
                  if (!content.empty())
                        result.append(QVariantMap {
                                 {"role",                          "user"},
                                 {"text", QString::fromStdString(content)}
                              });
                  }
            else if (role == "assistant") {
                  std::string content = msg.value("content", std::string());
                  if (!content.empty())
                        result.append(QVariantMap {
                                 {"role",                     "assistant"},
                                 {"text", QString::fromStdString(content)}
                              });
                  }
            else if (role == "tool") {
                  std::string content = msg.value("content", std::string());
                  if (!content.empty())
                        result.append(QVariantMap {
                                 {"role",                          "tool"},
                                 {"text", QString::fromStdString(content)}
                              });
                  }
            }
      return result;
      }
