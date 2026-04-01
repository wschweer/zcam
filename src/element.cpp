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
#include "element.h"

//---------------------------------------------------------
//   Element
//---------------------------------------------------------

Element::Element(ZCam* zc, QObject* parent) : QObject(parent) {
      zcam = zc;
      QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership);
      }
