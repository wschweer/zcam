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

#include <QString>

class ZCam;

//---------------------------------------------------------
//   DxfImport
//    Public entry point: reads a DXF file using libdxfrw and
//    creates project elements (Layers, Polygons, Ellipses)
//    in the current ZCam project.
//---------------------------------------------------------

namespace DxfImport {
bool import(ZCam* zcam, const QString& path);
      }