//=============================================================================
//  wcam
//    CAM tool for gcode and galvo laser machines.
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#pragma once

#include <array>
#include <string>
#include <opencv4/opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include "types.h"

class ZCam;

//-----------------------------------------------------------------------------
//   CalOffset
//    This offset is added to the galvo position to correct lens distortions.
//-----------------------------------------------------------------------------

struct CalOffset {
      int x;
      int y;
      double error() const { return double(x) * double(x) + double(y) * double(y); }
      };

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CalOffset, x, y)

struct Trans;

//---------------------------------------------------------
//   CalData
//    Calibration Data is an array of ROWS * COLUMNS
//    CalOffset() values.
//    The offset is the difference between expected and real
//    position of the laser beam. These values
//    are used in the laser controller to correct distortions
//    of the optical system.
//
//    For calibration purposes we create a machine with
//    the traveling size of 150mmx150mm and scale 1.0.
//---------------------------------------------------------

class CalData
      {
      ZCam* zcam;
      static constexpr int ROWS        = 65;
      static constexpr int COLUMNS     = 65;
      static constexpr int XOFFSET     = COLUMNS / 2;
      static constexpr int YOFFSET     = ROWS / 2;
      static constexpr int SIZE        = ROWS * COLUMNS;
      static constexpr double GRIDSIZE = 150.0; // mm
      static constexpr double raster   = GRIDSIZE / (COLUMNS - 1);

      std::array<CalOffset, SIZE> data;
      Vec2d offsetPos(int x, int y) const {
            CalOffset o = value(x, y);
            return Vec2d(unmap(o.x), unmap(o.y));
            }
      Vec2d rasterPos(int x, int y) const { return Vec2d(x * raster, y * raster); }

    public:
      CalData(ZCam* wc) {
            zcam = wc;
            clear();
            }
      void clear() {
            for (int i = 0; i < SIZE; ++i)
                  data[i] = {0, 0};
            };
      void analyzeImage(cv::Mat& img);
      double error(const Trans&, int x, int y) const;
      double error(const Trans& t) const;
      bool read(std::string fileName);
      bool write(std::string fileName) const;
      void scanImage(std::string fileName);
      auto begin() { return data.begin(); }
      auto end() { return data.end(); }
      // convert mm to galvo units:
      int map(double v) const { return int((v * 65536.0) / GRIDSIZE); }
      // convert galvo units to mm:
      double unmap(int v) const { return (v * GRIDSIZE) / 65536.0; }
      //---------------------------------------------------------
      //   raster operations
      //    x >= -(COLUMNS/2)  <= (COLUMNS/2)
      //    y >= -(ROWS/2)     <= (ROWS/2)
      //---------------------------------------------------------

      CalOffset value(int x, int y) const { return data[x + XOFFSET + (y + YOFFSET) * COLUMNS]; }
      void setValue(int x, int y, CalOffset value) { data[x + XOFFSET + (y + YOFFSET) * COLUMNS] = value; }
      Vec2d pos(int x, int y) const { return rasterPos(x, y) + offsetPos(x, y); }
      };
