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

#include "imageimport.h"

#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QStringList>

#include "imageelement.h"
#include "logger.h"
#include "project.h"
#include "treemodel.h"
#include "undo.h"
#include "zcam.h"
namespace {

//---------------------------------------------------------
//   imageSuffixes
//    Set of supported image file suffixes (lower-case).
//---------------------------------------------------------

const QStringList& imageSuffixes() {
      static const QStringList suffixes = {"png", "jpg", "jpeg", "bmp", "gif", "tiff", "tif", "webp"};
      return suffixes;
      }

      } // namespace

//---------------------------------------------------------
//   ImageImport::isImageFile
//---------------------------------------------------------

bool ImageImport::isImageFile(const QString& path) {
      QString suffix = QFileInfo(path).suffix().toLower();
      return imageSuffixes().contains(suffix);
      }

//---------------------------------------------------------
//   ImageImport::import
//    Load the image file and create an ImageElement in the
//    project's CAD tree.
//---------------------------------------------------------

bool ImageImport::import(ZCam* zcam, const QString& path) {
      Debug("import Image: {}", path.toUtf8().constData());

      if (!zcam->project() || !zcam->project()->cad()) {
            Critical("ImageImport::import: no project or CAD element");
            return false;
            }

      QImage img(path);
      if (img.isNull()) {
            Warning("ImageImport::import: failed to load image: {}", path.toUtf8().constData());
            return false;
            }

      Cad* cad = zcam->project()->cad();

      auto* element = new ImageElement(zcam, cad);
      element->setName(QFileInfo(path).baseName());
      element->setExpanded(true);
      if (!element->loadFile(path)) {
            Warning("ImageImport::import: loadFile failed for {}", path.toUtf8().constData());
            delete element;
            return false;
            }

      // Center the image around the origin: the bottom-left of the
      // bounding box goes to the scene origin (matching the behaviour
      // of other file importers).
      const QRectF bbox = element->boundingBox();
      element->set_pos(QVector3D(float(-bbox.left()), float(-bbox.top()), 0.0f));

      // Undoable insert
      zcam->project()->undo()->beginMacro();
      auto cmd = new InsertElementCommand(zcam, cad, element, -1);
      zcam->project()->undo()->push(cmd);
      zcam->project()->undo()->endMacro();

      zcam->setCamDirty(true);
      return true;
      }

//---------------------------------------------------------
//   ImageImport::importAt
//    Import an image file and position it so the bounding
//    box's bottom-left corner is at (x, y) in scene coords.
//---------------------------------------------------------

bool ImageImport::importAt(ZCam* zcam, const QString& path, double x, double y) {
      Debug("importAt Image: {} at ({}, {})", path.toUtf8().constData(), x, y);

      if (!zcam->project() || !zcam->project()->cad()) {
            Critical("ImageImport::importAt: no project or CAD element");
            return false;
            }

      QImage img(path);
      if (img.isNull()) {
            Warning("ImageImport::importAt: failed to load image: {}", path.toUtf8().constData());
            return false;
            }

      Cad* cad = zcam->project()->cad();

      auto* element = new ImageElement(zcam, cad);
      element->setName(QFileInfo(path).baseName());
      element->setExpanded(true);
      if (!element->loadFile(path)) {
            Warning("ImageImport::importAt: loadFile failed for {}", path.toUtf8().constData());
            delete element;
            return false;
            }

      // Position so the bounding box bottom-left is at (x, y).
      // The content bounding box is [-0.5, -0.5, 0.5, 0.5] and
      // scale = [sx, sy, 1], so bottom-left in local space is
      // (-0.5 * sx, -0.5 * sy).  We want:
      //   pos + (-0.5*sx, -0.5*sy) = (x, y)
      //   pos = (x + 0.5*sx, y + 0.5*sy)
      QVector3D s = element->scale();
      element->set_pos(QVector3D(float(x + 0.5 * s.x()), float(y + 0.5 * s.y()), 0.0f));

      // Undoable insert
      zcam->project()->undo()->beginMacro();
      auto cmd = new InsertElementCommand(zcam, cad, element, -1);
      zcam->project()->undo()->push(cmd);
      zcam->project()->undo()->endMacro();

      zcam->setCamDirty(true);
      return true;
      }

//---------------------------------------------------------
//   ImageImport::boundingBox
//    Compute the bounding box (in mm) of the image at the
//    given path, assuming the default scale (larger axis =
//    100 mm, aspect-ratio preserved).
//---------------------------------------------------------

QRectF ImageImport::boundingBox(ZCam* zcam, const QString& path) {
      Q_UNUSED(zcam);
      QImage img(path);
      if (img.isNull())
            return {};
      int w = img.width();
      int h = img.height();
      if (w <= 0 || h <= 0)
            return {};
      double aspect = double(w) / double(h);
      double sx, sy;
      if (aspect >= 1.0) {
            sx = 100.0;
            sy = 100.0 / aspect;
            }
      else {
            sx = 100.0 * aspect;
            sy = 100.0;
            }
      // The bounding box is centered at origin: [-sx/2, -sy/2, sx/2, sy/2]
      return QRectF(-sx / 2.0, -sy / 2.0, sx, sy);
      }