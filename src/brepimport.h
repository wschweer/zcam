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

class QString;
class QRectF;

class ZCam;

//---------------------------------------------------------
//   BrepElementInterface
//    Public entry points for OpenCASCADE BRep handling in
//    ZCam: import of ".brep" files, creation of a Brep
//    element inside the project tree and bounding box
//    queries used by the GUI.
//---------------------------------------------------------

namespace BrepElementInterface {

//---------------------------------------------------------
//   import
//    Reads the given OpenCASCADE ".brep" file and creates
//    a Brep element in the project tree. Returns true on
//    success.
//---------------------------------------------------------
bool import(ZCam* zcam, const QString& path);

//---------------------------------------------------------
//   boundingBox
//    Computes the axis-aligned world bounding box of the
//    given ".brep" file without modifying the project.
//    Returns an empty QRectF on failure.
//---------------------------------------------------------
QRectF boundingBox(ZCam* zcam, const QString& path);

} // namespace BrepElementInterface
