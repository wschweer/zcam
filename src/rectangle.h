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

#include "element3d.h"

enum class PPType : char;

//---------------------------------------------------------
//   Rectangle
//---------------------------------------------------------

class Rectangle : public Element3d
      {
      Q_OBJECT

      PROP(QVector2D, size)
      PROPV(double, corner, 0.0)
      PROPV(bool, fill, false)

    public:
      Rectangle(ZCam*, Element3d* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("Rectangle"); }
      void update(int flags = ~0) override;

      virtual PathList createPath();
      bool isClosed() const;
      QRectF rectangle() const;
      };
