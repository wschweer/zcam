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

#include <QProperty>

#include "line.h"

enum CircleSubelement { CenterHandle=0, SizeHandle };

//---------------------------------------------------------
//   Circle
//---------------------------------------------------------

class Circle : public ElementVisible {
      Q_OBJECT
//JSON { "name":"size", "type":"QVector2D", "default":"QVector2D(1.0, 1.0)", "min":0.0, "max":50000.0, "unit":"\\\"mm\\\"" }
//##<
      Q_PROPERTY(QVector2D size READ size WRITE setSize NOTIFY sizeChanged)

   protected:
      Property* sizeProperty = PropertyMap::push_back("size", new Property(this, "size", [this] { sizeChanged(); }, QVector2D(1.0, 1.0), "{ \"min\":0.0, \"max\":50000.0 }"));

   signals:
      void sizeChanged();

   public:
      void setSize(QVector2D v) { sizeProperty->setValue(QVariant::fromValue<QVector2D>(v)); }
      QVector2D size() const { return get<QVector2D>(sizeProperty->value()); }

   public:
//##>

    public:
      Circle(Wcam*, Element* parent = nullptr);
      constexpr ElementType type() const override { return ElementType::Circle; }
      bool write(XmlWriter&) const override;
      void update(int flags = ~0) override;
      bool setEditMode(bool val) override;
      };
