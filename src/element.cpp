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
#include "toplevel.h"
#include "cad.h"
#include "text.h"
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
      Debug("kill <{}> found: {} size {}", name(), rv, names.size());
      }

//---------------------------------------------------------
//   toJson
//---------------------------------------------------------

json Element::toJson() const {
      json childList = json::array();
      for (const auto& child : children()) {
            json c;
            c[child->typeName()] = child->toJson();
            childList.push_back(c);
            }
      nlohmann::json data;
      data["name"]     = name().toStdString();
      data["children"] = childList;
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void Element::fromJson(const json& data) {
      if (data.contains("name"))
            setName(QString::fromStdString(data["name"]));
      if (data.contains("children")) {
            json children = data["children"];
            for (const auto child : children) {
                  for (const auto& [key, value] : child.items()) {
                        Element3d* element = nullptr;
                        if (key == "toplevel") {
                              element = new TopLevel(zcam, this);
                              zcam->set_topLevel(static_cast<TopLevel*>(element));
                              element->fromJson(value);
                              }
                        else if (key == "cad") {
                              element = new Cad(zcam, this);
                              element->fromJson(value);
                              if (zcam->topLevel())
                                    zcam->topLevel()->set_cad(static_cast<Cad*>(element));
                              else
                                    Critical("no toplevel");
                              }
                        else if (key == "text") {
                              element = new Text(zcam, this);
                              element->fromJson(value);
                              }
                        if (!element)
                              Critical("no element");
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
      names.remove(name());   // in case setName is called twice
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
      _name = nn;
      emit nameChanged();
      }
