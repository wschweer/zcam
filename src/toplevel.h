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

#pragma once

#include "element.h"
#include "cad.h"

//---------------------------------------------------------
//   TopLevel
//---------------------------------------------------------

class TopLevel : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no no")

      PROPV(Cad*, cad, nullptr)

    public:
      TopLevel(ZCam*, Element* parent = nullptr);
      virtual const char* typeName() override { return "toplevel"; }
      };
