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
#include "cam.h"
#include "project.h"
#include "fixture.h"

//---------------------------------------------------------
//   Framing
//---------------------------------------------------------

Framing::Framing(ZCam* w, Element* parent) : Element3d(w, parent) {
      setName("framing");
      _geometry = new TessGeometry(this);
      setColor(w->config()->framingColor());
      // React to config framingColor changes so the 3D canvas
      // updates immediately when the user picks a new color.
      connect(w->config(), &Config::framingColorChanged, this,
              [this, w]() { setColor(w->config()->framingColor()); });
      connect(w->project(), &Project::updateFraming, [this] { update(); });
      // Rebuild the framing contour when the framing type changes.
      connect(this, &Framing::framingTypeChanged, this, [this] { update(); });
      }

//---------------------------------------------------------
//   update
//    Build the framing contour from the convex hull of all
//    burn LaserLayer geometry, including panel-grid offsets.
//    The convex hull is computed by Cam and already includes
//    the full panel layout.
//---------------------------------------------------------

void Framing::update(int flags) {
      Cam* cam = zcam->project() ? zcam->project()->cam() : nullptr;
      if (!cam) {
            _pathList.clear();
            _geometry->setPolygons(_pathList);
            return;
            }
      _pathList.clear();
      switch (static_cast<FramingType>(framingType())) {
            case FramingType::BoundingBox: {
                  Clipper2Lib::RectD bbox = cam->boundingBox();
                  if (bbox.IsEmpty())
                        break;
                  _pathList.push_back(bbox.AsPath());
                  closePath(_pathList);
                  break;
                  }
            case FramingType::ConvexHull: {
                  Clipper2Lib::PathD polygon = cam->convexHull();
                  _pathList.push_back(polygon);
                  break;
                  }
            }
      _geometry->setPolygons(_pathList);
      }
