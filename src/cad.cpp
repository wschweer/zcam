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
#include "mop.h"
#include "text.h"

//---------------------------------------------------------
//   Cad
//    The constructor creates a NopMop as the default Mop for
//    this Cad element.  All child elements inherit this Mop via
//    effectiveMop() unless they have their own laserLayer set.
//    The NopMop is a QObject child of this Cad (for lifetime
//    management) but is NOT added to the element tree (addChild)
//    so it does not appear in the tree view and is not serialized.
//    On project load the constructor recreates a fresh NopMop;
//    the serialized laserLayer reference resolves to it via the
//    global name registry.
//---------------------------------------------------------

Cad::Cad(ZCam* zcam, Element* parent) : Group(zcam, parent) {
      setName("");
      zcam->project()->set_cad(this);

      auto* nopMop = new NopMop(zcam, this);
      set_mop(nopMop);
      }
