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

#include "cad.h"
#include "project.h"
#include "zcam.h"
#include "text.h"

//---------------------------------------------------------
//   Element
//---------------------------------------------------------

Cad::Cad(ZCam* zcam, Element* parent) : Element3d(zcam, parent) {
      setName("");
      zcam->project()->set_cad(this);
      }
