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

#include "element.h"
#include "cad.h"

//---------------------------------------------------------
//   TopLevel
//---------------------------------------------------------

class TopLevel : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no no")

      Q_PROPERTY(Cad* cad READ cad WRITE setCad NOTIFY cadChanged)

      Cad* _cad{nullptr};

    signals:
      void cadChanged();

    public:
      TopLevel(ZCam*, QObject* parent = nullptr);
      Cad* cad() const { return _cad; }
      void setCad(Cad* v) {
            if (v != _cad) {
                  _cad = v;
                  emit cadChanged();
                  }
            }
      };
