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
#include "framing.h"
#include "grid.h"
#include "layer.h"
#include "laserlayer.h"
#include "text.h"
#include "rectangle.h"
#include "polygon.h"
#include "ellipse.h"
#include "materialtest.h"
#include "treemodel.h"
#include "zcam.h"

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
      }

void Element::clearProject() {
      names.clear();
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
                              element = new Layer(zcam, this);
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
                        else if (key == "laserLayer") {
                              element = new LaserLayer(zcam, this);
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
      QString n  = v == "" ? typeName() : v;
      int i      = 1;
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

      // notify the TreeModel so the TreeView updates its display
      if (zcam) {
            TreeModel* tm = zcam->treeModel();
            if (tm)
                  tm->notifyElementRenamed(this);
            }
      }
