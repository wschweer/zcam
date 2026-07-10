//=============================================================================
//  wcam
//  G-Code generator
//
//  Copyright (C) 2023 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "xmlreader.h"
#include "xmlwriter.h"
#include "zcam.h"
#include "tessgeometry.h"
#include "types.h"
#include "rectangle.h"

//---------------------------------------------------------
//   Rectangle
//---------------------------------------------------------

Rectangle::Rectangle(ZCam* w, Element3d* parent)
  : Element3d(w, parent) {
      set_geometry(new TessGeometry(this));
      QJSEngine::setObjectOwnership(_geometry, QJSEngine::CppOwnership);
      }

//---------------------------------------------------------
//   isClosed
//---------------------------------------------------------

bool Rectangle::isClosed() const {
      return painterPath.isClosed();
      }

//---------------------------------------------------------
//   createPath
//---------------------------------------------------------

PathList Rectangle::createPath() {
      auto rect = rectangle();

      double r = corner();
      painterPath.clear();
      if (r == 0.0) {
            painterPath.moveTo(rect.topLeft());
            painterPath.lineTo(rect.topRight());
            painterPath.lineTo(rect.bottomRight());
            painterPath.lineTo(rect.bottomLeft());
            painterPath.lineTo(rect.topLeft());
            }
      else {
            double d = 2 * r;
            QSizeF sz(d, d);
            painterPath.moveTo({rect.left() + r, rect.bottom()});

            QRectF re({rect.left(), rect.bottom() - d}, sz);
            painterPath.arcTo(re, -90, -90);

            re = QRectF({rect.left(), rect.top()}, sz);
            painterPath.arcTo(re, -180, -90);

            re = QRectF({rect.right() - d, rect.top()}, sz);
            painterPath.arcTo(re, -270, -90);

            re = QRectF({rect.right() - d, rect.bottom() - d}, sz);
            painterPath.arcTo(re, 0, -90);

            painterPath.lineTo({rect.left() + r, rect.bottom()});
            }
      auto pl = painterPath.toPathList();
      pl.setFill(fill());
      return pl;
      }

//---------------------------------------------------------
//   update
//---------------------------------------------------------

void Rectangle::update(int flags) {
      _pathList = createPath();
//      strokeAndFill();
      }

//---------------------------------------------------------
//   rectangle
//---------------------------------------------------------

QRectF Rectangle::rectangle() const {
      QRectF r; // (-size().x() * .5, -size().y() * .5, size().x(), size().y());
      return r;
      }
