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

//---------------------------------------------------------
//   ZCam
//---------------------------------------------------------

ZCam::ZCam(QObject* parent) : QObject(parent) {
      _style = new Style(this);
      setTopLevel(new TopLevel(this, nullptr));

      emit rootElementChanged(topLevel());
      }

//---------------------------------------------------------
//   create
//---------------------------------------------------------

ZCam* ZCam::create(QQmlEngine*, QJSEngine*) {
      return new ZCam();
      }
