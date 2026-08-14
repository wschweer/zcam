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

#include <QJSEngine>
#include "logger.h"
#include "element.h"
#include "element3d.h"
#include "project.h"
#include "cad.h"
#include "cam.h"
#include "cameraelement.h"
#include "framing.h"
#include "grid.h"
#include "group.h"
#include "recipe.h"
#include "text.h"
#include "rectangle.h"
#include "polygon.h"
#include "ellipse.h"
#include "materialtest.h"
#include "brepelement.h"
#include "imageelement.h"
#include "treemodel.h"
#include "zcam.h"
#include "scriptengine.h"
#include <QRegularExpression>

QHash<QString, Element*> Element::names;

//---------------------------------------------------------
//   Element
//---------------------------------------------------------

Element::Element(ZCam* zc, Element* parent) : QObject(parent) {
      zcam = zc;
      QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership);
      }

Element::~Element() {
      bool rv = names.remove(name());
      if (auto* se = ScriptEngine::instance())
            se->removeBindingsFor(this);
      }

void Element::clearProject() {
      names.clear();
      }

//---------------------------------------------------------
//   addChild
//    Add a child element and apply default scripts from the
//    properties() JSON when the element is newly created (not
//    loaded from a project file).  During project loading the
//    ScriptEngine sets _rebuilding and handles default scripts
//    centrally in rebuildRegistry().
//---------------------------------------------------------

void Element::addChild(Element* e) {
      _children.push_back(e);
      e->_parent = this;
      e->setParent(this);
      // Invalidate the new child's cached global matrix and world
      // bounding boxes since the parent chain changed.
      if (auto* e3d = qobject_cast<Element3d*>(e))
            e3d->invalidateGlobalMatrix();
      emit childAdded(e);
      if (auto* se = ScriptEngine::instance()) {
            if (!se->_rebuilding)
                  se->applyDefaultScripts(e);
            }
      }

//---------------------------------------------------------
//   toJson
//---------------------------------------------------------

json Element::toJson() const {
      json childList = json::array();
      if (saveChildren()) {
            for (const auto& child : children()) {
                  json c;
                  c[child->typeName().toStdString()] = child->toJson();
                  childList.push_back(c);
                  }
            }
      nlohmann::json data;
      data["name"]     = name().toStdString();
      data["expanded"] = _expanded;
      data["children"] = childList;

      // ── Scripting ────────────────────────────────────────────────
      if (hasScript()) {
            json scripts = json::array();
            for (auto it = _scripts.constBegin(); it != _scripts.constEnd(); ++it) {
                  json s;
                  s["prop"]   = it.key().toStdString();
                  s["script"] = it.value().script.toStdString();
                  s["active"] = it.value().active;
                  scripts.push_back(s);
                  }
            data["scripts"] = scripts;
            }
      json comps = json::array();
      for (int i = 0; i < 3; ++i) {
            if (hasScriptComp(i)) {
                  json s;
                  s["prop"]   = _scriptCompProp[i].toStdString();
                  s["comp"]   = i;
                  s["script"] = _scriptComp[i].toStdString();
                  s["active"] = _scriptCompActive[i];
                  comps.push_back(s);
                  }
            }
      if (!comps.empty())
            data["scriptComp"] = comps;
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void Element::fromJson(const json& data) {
      if (data.contains("name"))
            setName(QString::fromStdString(data.at("name").get<std::string>()));
      if (data.contains("expanded"))
            _expanded = data.at("expanded").get<bool>();

      // ── Scripting ────────────────────────────────────────────────
      // New format: "scripts" is an array of {prop, script, active}.
      if (data.contains("scripts") && data.at("scripts").is_array()) {
            for (const auto& s : data.at("scripts")) {
                  if (!s.contains("prop") || !s.contains("script"))
                        continue;
                  QString prop = QString::fromStdString(s.at("prop").get<std::string>());
                  ScriptEntry entry;
                  entry.script = QString::fromStdString(s.at("script").get<std::string>());
                  entry.active = s.value("active", true);
                  _scripts.insert(prop, entry);
                  }
            }
      // Legacy format: single "script" object with {prop, script, active}.
      else if (data.contains("script")) {
            const json& s = data.at("script");
            if (s.contains("prop") && s.contains("script")) {
                  QString prop = QString::fromStdString(s.at("prop").get<std::string>());
                  ScriptEntry entry;
                  entry.script = QString::fromStdString(s.at("script").get<std::string>());
                  entry.active = s.value("active", true);
                  _scripts.insert(prop, entry);
                  }
            }
      if (data.contains("scriptComp")) {
            for (const auto& s : data.at("scriptComp")) {
                  if (!s.contains("prop") || !s.contains("script") || !s.contains("comp"))
                        continue;
                  int comp = s.at("comp").get<int>();
                  if (comp >= 0 && comp < 3) {
                        _scriptCompProp[comp]   = QString::fromStdString(s.at("prop").get<std::string>());
                        _scriptComp[comp]       = QString::fromStdString(s.at("script").get<std::string>());
                        _scriptCompActive[comp] = s.value("active", true);
                        }
                  }
            }
      if (data.contains("children")) {
            const json& children = data.at("children");
            for (const auto& child : children) {
                  for (const auto& [key, value] : child.items()) {
                        Element3d* element = nullptr;
                        if (key == "toplevel") {
                              element = new Project(zcam, this);
                              zcam->set_project(static_cast<Project*>(element));
                              element->fromJson(value);
                              }
                        else if (key == "cad") {
                              element = new Cad(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "cam") {
                              element = new Cam(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "text") {
                              element = new Text(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "layer" || key == "group") {
                              element = new Group(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "stock") {
                              element = new Stock(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "fixture") {
                              element = new Fixture(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "framing") {
                              element = new Framing(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "grid") {
                              element = new Grid(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "cameraElement") {
                              element = new CameraElement(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "laserLayer" || key == "recipe" || key == "laserMop") {
                              element = new LaserMop(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "rectangle") {
                              element = new Rectangle(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "polygon") {
                              element = new Polygon(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "ellipse") {
                              element = new Ellipse(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "materialtest") {
                              element = new MaterialTest(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "brep") {
                              element = new BrepElement(zcam, this);
                              element->fromJson(value);
                              }
                        else if (key == "image") {
                              element = new ImageElement(zcam, this);
                              element->fromJson(value);
                              }
                        if (!element) {
                              Critical("no element for key: {}", key);
                              continue; // skip unknown element types instead of crashing
                              }
                        addChild(element);
                        }
                  }
            }
      }

//---------------------------------------------------------
//   set_name
//    set an unique name, starting with "v".
//---------------------------------------------------------

void Element::setName(QString v) {
      names.remove(name()); // in case setName is called twice
      QString n = v == "" ? typeName() : v;
      int i     = 1;
      // Sanitize the name so it is always a valid JavaScript identifier:
      // scripts reference elements by name via project.<path>.<name>.
      n.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_$]")), QStringLiteral("_"));
      if (n.isEmpty() || !(n[0].isLetter() || n[0] == u'_'))
            n.prepend(u'_');
      QString nn = n;
      while (names.contains(nn)) {
            nn = QString("%1-%2").arg(n).arg(i);
            ++i;
            if (i >= 100000) {
                  Critical("cannot create unique name: {}", i);
                  break;
                  }
            }
      names[nn] = this;
      _name     = nn;
      emit nameChanged();

      // Register in the script namespace tree so scripts can refer to
      // this element by name (project.cad.myLayer.myElement ...).
      ScriptEngine* se = ScriptEngine::instance();
      if (se) {
            if (!se->_rebuilding)
                  se->addElementToTree(this);
            }

      // notify the TreeModel so the TreeView updates its display.
      // Skip during batch operations (e.g. DXF import) — the
      // TreeModel is reset once at the end.
      if (zcam) {
            TreeModel* tm = zcam->treeModel();
            if (tm && !(se && se->_rebuilding))
                  tm->notifyElementRenamed(this);
            }
      }
