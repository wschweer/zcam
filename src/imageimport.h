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

#pragma once

class QString;
class QRectF;
class ZCam;

//---------------------------------------------------------
//   ImageImport
//    Public entry points for pixel-image file handling in
//    ZCam: import of PNG, JPEG, BMP, GIF, TIFF, WEBP files,
//    creation of an ImageElement inside the project tree,
//    and bounding-box queries used by the GUI for drag
//    previews.
//---------------------------------------------------------

namespace ImageImport {

//---------------------------------------------------------
//   import
//    Loads the given image file and creates an ImageElement
//    in the project's CAD tree.  Returns true on success.
//---------------------------------------------------------

bool import(ZCam* zcam, const QString& path);

//---------------------------------------------------------
//   importAt
//    Like import() but positions the element so the
//    bounding box's bottom-left corner is at (x, y) in
//    scene coordinates.
//---------------------------------------------------------

bool importAt(ZCam* zcam, const QString& path, double x, double y);

//---------------------------------------------------------
//   boundingBox
//    Returns the bounding box (in mm) of the image at the
//    given path, assuming a default size of 100 mm on the
//    larger axis.  Returns an empty QRectF on failure.
//---------------------------------------------------------

QRectF boundingBox(ZCam* zcam, const QString& path);

//---------------------------------------------------------
//   isImageFile
//    Returns true if the file suffix is a supported image
//    format (png, jpg, jpeg, bmp, gif, tiff, webp).
//---------------------------------------------------------

bool isImageFile(const QString& path);

      } // namespace ImageImport