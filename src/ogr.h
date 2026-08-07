//=============================================================================
//  wcam
//    Process CAD files for production on CNC and laser machines.
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#pragma once

#include <QImage>
#include <QRect>
#include <opencv2/opencv.hpp>
#include <logger.h>

//---------------------------------------------------------
//   Ogr
//    optical grid recognition
//---------------------------------------------------------

class Ogr
      {
      cv::Mat* img;
      void radonTransform(unsigned long* projection, int w, int n, const QRect& r);

    public:
      double skew(cv::Mat& image);
      };
