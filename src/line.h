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

#include "element3d.h"

enum class PPType : char;

//---------------------------------------------------------
//   Line
//---------------------------------------------------------

class Line : public Element3d
      {
      Q_OBJECT

      int currentVertex = -1;

    protected:
      QRectF _bbox;
      virtual PathList createPath() { return painterPath.toPathList(); }
      void setHover(bool);

    public:
      Line(ZCam*, Element3d* parent = nullptr);
      ~Line();

      void update(int flags = ~0) override;

      virtual int makeSpline(int idx);
      void clear() { painterPath.clear(); }
      void addVertex(const Vec2d& p);
      void drag(int idx, const QVector3D& delta);

      void lineTo(const Vec2d& p) { painterPath.lineTo(p); }
      void moveTo(const Vec2d& p) { painterPath.moveTo(p); }
      bool canClose(const Vec2d& p) const;
      void closePath();
      const Vec2d& startPos() const { return painterPath[0].pos; }
      int vertices() const { return painterPath.size(); }
      };
