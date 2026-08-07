//=============================================================================
//  ZCam - manufactoring tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "brepimport.h"

#include <QFileInfo>
#include <QFileInfo>
#include <QVector3D>

#include <TopoDS_Shape.hxx>

#include "brepelement.h"
#include "brepgeometry.h"
#include "logger.h"
#include "project.h"
#include "treemodel.h"
#include "undo.h"
#include "zcam.h"

namespace {

//---------------------------------------------------------
//   loadBrepShape
//---------------------------------------------------------

TopoDS_Shape loadBrepShape(const QString& path) {
      TopoDS_Shape shape;
      if (!path.isEmpty())
            BrepElement::loadShapeFromFile(path, shape);
      return shape;
      }

//---------------------------------------------------------
//   centerBottomLeft
//---------------------------------------------------------

QVector3D centerBottomLeft(const QRectF& bbox) {
      return QVector3D(float(bbox.center().x()), float(bbox.top()), 0.0f);
      }

} // namespace

//---------------------------------------------------------
//---------------------------------------------------------
//   BrepElementInterface::import
//---------------------------------------------------------
//---------------------------------------------------------

bool BrepElementInterface::import(ZCam* zcam, const QString& path) {
      Debug("import BREP: {}", path.toUtf8().data());

      if (!zcam->project() || !zcam->project()->cad()) {
            Critical("BrepElementInterface::import: no project or CAD element");
            return false;
            }

      // fail fast on invalid file
      TopoDS_Shape shape;
      if (!BrepElement::loadShapeFromFile(path, shape)) {
            Warning("BrepElementInterface::import: failed to read file: {}", path.toUtf8().constData());
            return false;
            }

      Cad* cad = zcam->project()->cad();

      auto* element = new BrepElement(zcam, cad);
      element->setName(QFileInfo(path).baseName());
      element->setExpanded(true);
      if (!element->loadFile(path)) {
            Warning("BrepElementInterface::import: loadFile failed for {}", path.toUtf8().constData());
            delete element;
            return false;
            }

      // Centre the imported object around the origin (bottom-left
      // of its bounding box goes to the scene origin, matching the
      // behaviour of other file importers).
      const QRectF bbox  = element->boundingBox();
      const QVector3D off = centerBottomLeft(bbox);
      element->set_pos(-off);

      // Undoable insert
      zcam->project()->undo()->beginMacro();
      auto cmd = new InsertElementCommand(zcam, cad, element, -1);
      zcam->project()->undo()->push(cmd);
      zcam->project()->undo()->endMacro();

      zcam->setCamDirty(true);
      return true;
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   BrepElementInterface::boundingBox
//---------------------------------------------------------
//---------------------------------------------------------

QRectF BrepElementInterface::boundingBox(ZCam* zcam, const QString& path) {
      Q_UNUSED(zcam);
      TopoDS_Shape shape = loadBrepShape(path);
      if (shape.IsNull())
            return {};

      QRectF bbox;
      PathList pathList;
      if (!BrepElement::buildPolylineFromShape(shape, 0.1, pathList, bbox))
            return {};
      return bbox;
      }
