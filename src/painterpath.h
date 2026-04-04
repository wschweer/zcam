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

#include "logger.h"
#include "types.h"

class QFont;
// class XmlWriter;
// class XmlReader;

enum class PPType : char { MoveTo, LineTo, CurveTo, CurveToData1, CurveToData2 };

//---------------------------------------------------------
//   PPElement
//---------------------------------------------------------

class PPElement
      {
    public:
      PPType type;
      Vec2d pos;
      double x() const { return pos.x(); }
      double y() const { return pos.y(); }
      };

//---------------------------------------------------------
//   PainterPath
//---------------------------------------------------------

class PainterPath : public std::vector<PPElement>
      {
    public:
      PainterPath() {}
      PathList toPathList() const;
      PPElement& operator()(int idx) {
            if (idx >= size())
                  Fatal("bad index {} in vector of size {}", idx, size());
            return at(idx);
            }
      const PPElement& operator()(int idx) const {
            if (idx >= size())
                  Fatal("bad index {} in vector of size {}", idx, size());
            return at(idx);
            }
      void addText(const QPointF&, const QFont&, const QString&);
      void addEllipse(const QPointF& center, double rx, double ry) { addEllipse(QRectF(center.x() - rx, center.y() - ry, 2 * rx, 2 * ry)); }
      void addEllipse(const QRectF& boundingRect);
      void lineTo(const Vec2d& p) { push_back({PPType::LineTo, p}); }
      void arcTo(const QRectF&, double startAngle, double sweepLength);
      void moveTo(const Vec2d& p) { push_back({PPType::MoveTo, p}); }
      void cubicTo(const Vec2d& p1, const Vec2d& p2, const Vec2d& p3) {
            push_back({PPType::CurveTo, p1});
            push_back({PPType::CurveToData1, p2});
            push_back({PPType::CurveToData2, p3});
            }
      void quadTo(const Vec2d& p1, const Vec2d& p2);
      void setElementPosition(int idx, const Vec2d& p) { (*this)[idx].pos = p; }
      bool isClosed() const;
      void closeSubpath();

      //      void write(XmlWriter& w) const;
      //      void read(XmlReader& r);
      void eraseElement(int idx);
      void insertElement(int idx, const PPElement& e);
      void makeSpline(int idx);
      };
