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
//   ImportIpc2581
//    Imports an IPC-2581 (revision C) "digital twin" XML
//    description of a printed circuit board.  Each layer
//    becomes a Group with Polygon children below a new
//    import layer; negative polarity features and cutouts
//    are imported as filled red polygons.
//---------------------------------------------------------

namespace ImportIpc2581 {
/// Import the IPC-2581 file at *path* into the current project.
bool import(ZCam* zcam, const QString& path);
/// Sniff-test for the IPC-2581 root element.
bool isIpc2581File(const QString& path);
      } // namespace ImportIpc2581
