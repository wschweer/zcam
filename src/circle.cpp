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

#include "circle.h"
#include "xmlwriter.h"
#include "zcam.h"
#include "handle.h"

//---------------------------------------------------------
//   write
//---------------------------------------------------------

bool Circle::write(XmlWriter& w) const
      {
      return Element3d::write(w);
      }

//---------------------------------------------------------
//   Circle
//---------------------------------------------------------

Circle::Circle(Wcam* w, Element* parent) : ElementVisible(w, parent)
      {
      _geometry = new TessGeometry(this);
      QJSEngine::setObjectOwnership(_geometry, QJSEngine::CppOwnership);

      QSignalBlocker b1(sizeProperty);
      setSize(QVector2D(1,1));

      connect(sizeProperty,  &Property::valueChanged, [this]()   { update();});
      setEditMode(false);

      connect(scaleProperty, &Property::valueChanged,     [this]() { update(); });
      connect(rotProperty, &Property::valueChanged,       [this]() { update(); });
      connect(lineWidthProperty, &Property::valueChanged, [this]() { update(); });
      connect(fillProperty, &Property::valueChanged, [this]() { update(); });

      update();
      }

//---------------------------------------------------------
//   upate
//---------------------------------------------------------

void Circle::update(int flags)
      {
      painterPath.clear();
      painterPath.addEllipse(QPointF(), size().x(), size().y());
      if (!_subElements.empty()) {
            _subElements[CircleSubelement::CenterHandle]->setPos(QVector3D());
            QVector3D p(size().x(), size().y(), 0.0);
            _subElements[CircleSubelement::SizeHandle]->setPos(p);
            }
      _pathList = painterPath.toPathList();
      strokeAndFill();
      }

//---------------------------------------------------------
//   setEditMode
//---------------------------------------------------------

bool Circle::setEditMode(bool val) {
      if (val) {
            //    create a handle for every vertex
            //    minus one if path is closed
            //
            for (int idx = 0; idx < 2; ++idx) {
                  auto handle = new Handle(zcam, this, idx, nullptr);
                  handle->setActive(true);
                  _subElements.push_back(handle);
                  emit zcam->addSubElement(handle, this);
                  }
            _subElements[CircleSubelement::CenterHandle]->setPos(QVector3D());
            QVector3D p(size().x(), size().y(), 0.0);
            _subElements[CircleSubelement::SizeHandle]->setPos(p);
            }
      else
            clearSubelements();
      return true;
      }
