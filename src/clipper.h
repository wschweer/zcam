//=============================================================================
//  wcam
//    CAM tool for gcode and fiber laser machines.
//
//  Copyright (C) 2025 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#pragma once

#include "clipper2/clipper.h"

// collection of Clipper2Lib based functions

using PathD    = Clipper2Lib::PathD;
using PathsD   = Clipper2Lib::PathsD;
using RectD    = Clipper2Lib::RectD;
using PointD   = Clipper2Lib::PointD;
using ClipType = Clipper2Lib::ClipType;
using FillRule = Clipper2Lib::FillRule;
using Point64  = Clipper2Lib::Point64;

//---------------------------------------------------------
//   Clipper
//---------------------------------------------------------

class Clipper : public Clipper2Lib::ClipperD
      {
    public:
      Clipper(int precision = 4) : Clipper2Lib::ClipperD(precision) {}
      static void invertPolygons(PathsD& polygons, RectD bounds = RectD());
      static PathsD createHatch(const RectD& bounds, double interval, double degrees);
      PathsD hatch(const PathsD& input, const RectD& bounds, double angle, double interval);
      };

//---------------------------------------------------------
//   formatter Element
//---------------------------------------------------------

template <> struct std::formatter<RectD> {
      constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
      auto format(RectD r, auto& ctx) const { return std::format_to(ctx.out(), "<l{}t{}r{}b{}>", r.left, r.top, r.right, r.bottom); }
      };
