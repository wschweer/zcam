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

//---------------------------------------------------------
//   ZCam
//---------------------------------------------------------

ZCam::ZCam(QObject* parent) : QObject(parent) {
      _style = new Style(this);

      // maintain two tree structures:
      //    - QObject tree
      //    - Element tree
      // Reason: we cannot easily manage the object order in QObject tree

      _treeModel      = new TreeModel(this);
      _projectManager = new ProjectManager(this, this);
      auto r          = new RootElement(this, nullptr);
      auto top        = new TopLevel(this, r);
      set_topLevel(top);
      auto cad        = new Cad(this, top);
      auto text       = new Text(this, cad);
      text->set_text("ZCam");

      cad->addChild(text);
      top->addChild(cad);
      r->addChild(top);

      _treeModel->setRoot(r);       // update project tree view
      set_rootElement(top);         // build and show the scene
      }

//---------------------------------------------------------
//   create
//---------------------------------------------------------

ZCam* ZCam::create(QQmlEngine*, QJSEngine*) {
      return new ZCam();
      }
