//=============================================================================
//  wcam
//    CAM tool for gcode and fiber laser machines.
//
//  Copyright (C) 2025 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "clipper2/clipper.h"
#include "zcam.h"
#include "framing.h"
#include "fixture.h"

//---------------------------------------------------------
//   Framing
//---------------------------------------------------------

Framing::Framing(ZCam* w, Element* parent)
   : Element3d(w, parent)
      {
      setName("framing");
      _geometry = new TessGeometry(this);
//      setColor(QColor(0, 255, 0));
      }

//---------------------------------------------------------
//   update
//---------------------------------------------------------

void Framing::update(int flags)
      {
      Fixture* fixture = zcam->topLevel()->fixture();
      if (!fixture) {
            Critical("no fixture");
            return;
            }
      _pathList.clear();
      Clipper2Lib::PathD polygon;
#if 0
      double w, h;
      Clipper2Lib::RectD r = fixture->size(w, h);
      polygon.push_back({r.left, r.top});
      polygon.push_back({r.right, r.top});
      polygon.push_back({r.right, r.bottom});
      polygon.push_back({r.left, r.bottom});
      polygon.push_back({r.left, r.top});
#else
      polygon = fixture->convexHull();
#endif

      _pathList.push_back(polygon);
      _geometry->setPolygons(_pathList);
      }
