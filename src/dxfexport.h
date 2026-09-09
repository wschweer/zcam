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
//   DxfExport
//    Public entry point: writes a DXF file from the current
//    project's CAD tree.  Walks the CAD hierarchy and writes
//    LWPOLYLINE entities for Polygons (with bulge values for
//    circular arc segments and SPLINE entities for cubic bezier
//    segments), CIRCLE/ELLIPSE entities for Ellipses, and
//    LWPOLYLINE entities for Rectangles.  Colours are derived
//    from the effective LaserMop colour of each element, mapped
//    to the nearest ACI colour index.
//---------------------------------------------------------

namespace DxfExport {
bool exportDxf(ZCam* zcam, const QString& path);
      } // namespace DxfExport