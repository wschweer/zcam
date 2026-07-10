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

#include "stock.h"
#include "zcam.h"

//---------------------------------------------------------
//   Stock
//---------------------------------------------------------

Stock::Stock(ZCam* w, Element* parent)
   : Element3d(w, parent)
      {
      setName("stock");
#if 0
      auto colors = new osg::Vec4Array;
      colors->push_back(osgColor(zcam->colorStock()));

      auto linewidth = new osg::LineWidth();
      linewidth->setWidth(4);

      auto polymode = new osg::PolygonMode;
      polymode->setMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE);
      auto polyOffset = new osg::PolygonOffset( -5.0f, -1.0f);

      geometry = new osg::Geometry;
      geometry->setName("Lines");
      geometry->setColorArray(colors, osg::Array::BIND_OVERALL);
      geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINE_STRIP, 0, 5));

      geode = new osg::Geode;
      auto state = geode->getOrCreateStateSet();
      state->setAttributeAndModes(linewidth, osg::StateAttribute::ON);
      state->setAttributeAndModes(polymode, osg::StateAttribute::ON);
      state->setAttributeAndModes(polyOffset, osg::StateAttribute::ON);
      state->setMode(GL_LINE_SMOOTH, osg::StateAttribute::ON);
      geode->addDrawable(geometry);
      _node->addChild(geode);

      connect(this, &Element::elementChanged, [this]() { update(); });
#endif
      }

//---------------------------------------------------------
//   update
//---------------------------------------------------------

void Stock::update(int)
      {
#if 0
      float _x = x();
      float _y = y();
      float _w = width();
      float _d = depth();
      auto vertices = initVertices(geometry);
      vertices->push_back({ _x, _y, 0.0 });
      vertices->push_back({ _x + _w, _y, 0.0 });
      vertices->push_back({ _x + _w, _y + _d, 0.0});
      vertices->push_back({ _x, _y + _d, 0.0});
      vertices->push_back({ _x, _y, 0.0 });
      updateGeometry(geometry);
#endif
      }
