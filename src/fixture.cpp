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
#include "project.h"
#include "cad.h"

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

      for (auto e : children()) {
            if (!isType<Recipe>(e))
                  continue;
            auto layer = toType<Recipe>(e);
            if (!layer->burn())
                  continue;
            auto elements = layer->collectElements();
            for (const auto* ce : elements) {
                  // Single-tile geometry in project-root space.
                  QMatrix4x4 m = ce->globalMatrix();

                  for (auto p : ce->pathList()) {
                        Clipper2Lib::PathD path;
                        for (auto pp : p) {
                              auto pt = m.map(QVector3D(pp.x(), pp.y(), 0.0));
                              path.emplace_back(pt.x(), pt.y());
                              }
                        pl.push_back(path);
                        }
                  }
            }

      Clipper2Lib::RectD r = GetBounds(pl);
      width                = std::abs(r.right - r.left);
      height               = std::abs(r.top - r.bottom);
      return r;
      }