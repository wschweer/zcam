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

#include "zcam.h"
#include "cam.h"
#include "project.h"
#include "fixture.h"

//---------------------------------------------------------
//   machineTime
//---------------------------------------------------------

double machineTime(Vec3d& p1, const CamPath& camPath)
      {
      double sec = 0;
      double feed = 1000.0;         // mm/min

      bool first = true;
      for (auto p2 : camPath) {
            double length = (p2 - p1).length();
            p1 = p2;
            if (first) {
                  sec += (length * 60.0) / feed;
                  first = false;
                  feed = camPath.feed == 0.0 ? 1000.0 : camPath.feed;
                  }
            else {
                  sec += (length * 60.0) / feed;
                  }
            }
      return sec;
      }

double machineTime(const CamPathList& pl)
      {
      double sec = 0;

      Vec3d p1 { 0.0, 0.0, 0.0 };

      for (auto camPath : pl) {
            sec += machineTime(p1, camPath);
            }
      return sec;
      }

Cam::Cam(ZCam* zcam, Element* parent)
   : Element3d(zcam, parent)
      {
      setName("cam");
      zcam->topLevel()->set_cam(this);

//      connect(panelRowsProperty,        &Property::valueChanged, [this]() { updateCam(ROWS_COLUMNS);});
//      connect(panelColumnsProperty,     &Property::valueChanged, [this]() { updateCam(ROWS_COLUMNS);});
//      connect(panelHDistanceProperty,   &Property::valueChanged, [this]() { updateCam(DISTANCE);});
//      connect(panelVDistanceProperty,   &Property::valueChanged, [this]() { updateCam(DISTANCE);});
      };

//---------------------------------------------------------
//   updateCam
//---------------------------------------------------------

void Cam::updateCam(int flags)
      {
      Debug("===");
      double w, h;
      Fixture* fixture = zcam->topLevel()->fixture();
      if (!fixture) {
            Critical("no fixture");
            return;
            }
      fixture->size(w, h);
      fixture->update();
#if 0
      if (flags & UpdateFlags::ROWS_COLUMNS) {
            for (int row = 0; row < panelRows(); ++row) {
                  for (int column = 0; column < panelColumns(); ++column) {
                        Node* n = new Node(fixture);
                        n->setRow(row);
                        n->setColumn(column);
                        n->setName(format("panel:{}:{}", row, column));
                        node()->addChild(n);
                        n->addChild(fixture->node());
                        }
                  }
            auto stock = zcam->topLevel()->stock();
            if (stock)
                  node()->addChild(stock->node());
            flags |= UpdateFlags::DISTANCE;
            }
      if (flags & UpdateFlags::DISTANCE) {
            node()->setActive(false);
            for (int i = 0; i < node()->getNumChildren(); ++i) {
//                  Node* n = dynamic_cast<Node*>(node()->getChild(i));
                  if (n && n->element() && isFixture(n->element())) {
                        double xo = (panelHDistance()+w) * n->column();
                        double yo = (panelVDistance()+h) * n->row();
            Debug("update distance {} {}", xo, yo);
//                        osg::Matrix m;
//                        m.setTrans(osg::Vec3d(xo, yo, 0.0));
//                        n->setMatrix(m);
                        }
                  }
            node()->setActive(true);
            }
#endif
      }
