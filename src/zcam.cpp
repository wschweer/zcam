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

#include "zcam.h"
#include "toplevel.h"
#include "cad.h"
#include "text.h"
#include "treemodel.h"
#include "machine.h"
#include "laser.h"
#include "recipe.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

//---------------------------------------------------------
//   ZCam
//---------------------------------------------------------

ZCam::ZCam(QObject* parent) : QObject(parent) {
      _style = new Style(this);

      // maintain two tree structures:
      //    - QObject tree
      //    - Element tree
      // Reason: we cannot easily manage the object order in QObject tree

      _machine = new Machine(); // dummy
      _machines = new Machines(this);
      _laser   = new Laser(this, this);
      _recipes = new Recipes(this);

      loadAssets();

      _treeModel      = new TreeModel(this);
      _projectManager = new ProjectManager(this, this);
      auto r          = new RootElement(this, nullptr);
      auto top        = new TopLevel(this, r);
      set_topLevel(top);
      auto cad  = new Cad(this, top);
      auto text = new Text(this, cad);
      text->set_text("ZCam");

      cad->addChild(text);
      top->addChild(cad);
      r->addChild(top);

      _treeModel->setRoot(r); // update project tree view
      set_rootElement(top);   // build and show the scene
      }

//---------------------------------------------------------
//   create
//---------------------------------------------------------

ZCam* ZCam::create(QQmlEngine*, QJSEngine*) {
      return new ZCam();
      }

//---------------------------------------------------------
//   initAssets
//    initialize assets with dummies so the app will not
//    crash
//---------------------------------------------------------

void ZCam::initAssets() {
      // create recipes default
      if (_recipes) {
            json defaultRecipes = json::array();
            _recipes->fromJson(defaultRecipes);
            }
      if (_machines) {
            json defaultMachines = json::array();
            _machines->fromJson(defaultMachines);
            }
      }

//---------------------------------------------------------
//   loadAssets
//    load assets on application startup
//---------------------------------------------------------

void ZCam::loadAssets() {
      initAssets();

      QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      if (dataDir.isEmpty())
            return;

      QString filePath = QDir(dataDir).filePath("assets.json");
      QFile file(filePath);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;

      QByteArray data = file.readAll();
      file.close();

      try {
            json j = json::parse(data.toStdString());
            if (j.contains("recipes")) {
                  if (_recipes)
                        _recipes->fromJson(j["recipes"]);
                  }
            if (j.contains("machines")) {
                  if (_machines)
                        _machines->fromJson(j["machines"]);
                  }
            }
      catch (json::parse_error& e) {
            qWarning() << "Failed to parse assets.json:" << e.what();
            }
      }

//---------------------------------------------------------
//   saveAssets
//    assets are written in json format
//---------------------------------------------------------

void ZCam::saveAssets() {
      QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      if (dataDir.isEmpty())
            return;

      QDir dir(dataDir);
      if (!dir.exists())
            dir.mkpath(".");

      QString filePath = dir.filePath("assets.json");
      QFile file(filePath);
      if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "Failed to open assets.json for writing";
            return;
            }

      json j;
      if (_recipes)
            j["recipes"] = _recipes->toJson();
      if (_machines)
            j["machines"] = _machines->toJson();

      QTextStream out(&file);
      out << QString::fromStdString(j.dump(4));
      file.close();
      }
