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

#include <ranges>
#include <QMatrix4x4>

#include "zcam.h"
#include "fixture.h"
#include "group.h"
#include "recipe.h"
#include "cam.h"
#include "project.h"
#include "cad.h"
#include "element3d.h"

//---------------------------------------------------------
//   Fixture
//---------------------------------------------------------

Fixture::Fixture(ZCam* w, Element* parent) : Element3d(w, parent) {
      setName("fixture");
      if (w->project())
            w->project()->addFixture(this);
      }

//---------------------------------------------------------
//   size
//    Compute the bounding box of all burn elements in the
//    fixture's LaserLayers.  The geometry is in project-root
//    coordinate space (ce->globalMatrix()), NOT in machine
//    space, because the QML scene graph applies the cam and
//    fixture transforms through the parent-child hierarchy.
//    Only single-tile geometry is considered (no panel offsets);
//    the panel layout is handled by Cam.
//---------------------------------------------------------

Clipper2Lib::RectD Fixture::size(double& width, double& height) const {
      Clipper2Lib::PathsD pl;

      // Projection settings of the active Cam (perspective vs. orthographic).
      bool persp = false;
      double h   = 0.0;
      QPointF vc;
      if (Cam* cam = zcam->project() ? zcam->project()->cam() : nullptr) {
            persp = cam->perspective();
            h     = cam->projectionHeight();
            vc    = QPointF(cam->viewCenter().x(), cam->viewCenter().y());
            }

      for (auto e : children()) {
            if (!isType<LaserMop>(e))
                  continue;
            auto layer = toType<LaserMop>(e);
            if (!layer->burn())
                  continue;
            auto elements = layer->collectElements();
            for (const auto* ce : elements) {
                  // Project the 3D path data onto the z=0 plane
                  // (top-down view, orthographic or perspective
                  // depending on the Cam settings).
                  Clipper2Lib::PathsD paths = projectPathListToXY(ce, persp, h, vc);
                  pl.append_range(paths);
                  }
            }

      Clipper2Lib::RectD r = GetBounds(pl);
      width                = std::abs(r.right - r.left);
      height               = std::abs(r.top - r.bottom);
      return r;
      }

//---------------------------------------------------------
//   toJson
//    Serialize the Fixture including the hidden jobDuration
//    property which is not part of the properties() JSON and
//    therefore not handled by the base class Element3d::toJson().
//---------------------------------------------------------

json Fixture::toJson() const {
      json data                    = Element3d::toJson();
      data["jobDuration"]          = _jobDuration;
      data["jobDurationEstimated"] = _jobDurationEstimated;
      return data;
      }

//---------------------------------------------------------
//   fromJson
//    Deserialize the Fixture including the hidden jobDuration
//    property.
//---------------------------------------------------------

void Fixture::fromJson(const json& data) {
      Element3d::fromJson(data);
      if (data.contains("jobDuration"))
            _jobDuration = data.at("jobDuration").get<double>();
      if (data.contains("jobDurationEstimated"))
            _jobDurationEstimated = data.at("jobDurationEstimated").get<bool>();
      }
