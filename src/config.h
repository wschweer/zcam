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

#include <QObject>
#include <QQmlEngine>
#include <QColor>
#include <nlohmann/json.hpp>

#include "element.h"
#include "macros.h"

class ZCam;

//---------------------------------------------------------
//   Config
//    Derives from Element so its properties can be made
//    scriptable via the ScriptEngine.  Config is registered
//    in the JS namespace as "config" (a sibling of "project")
//    so scripts can reference config properties like
//    "config.machinesDirectory".
//---------------------------------------------------------

class Config : public Element
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      PROPV(int, iconSize, 32)
      PROPV(int, navCubeSize, 200)
      PROPV(double, handleSize, 0.02)
      PROPV(double, dragThreshold, 0.5)
      PROPV(QString, font, QStringLiteral("NotoSans"))
      PROPV(int, fontSize, 12)
      PROPV(QColor, panelBG, QColor("darkGray"))
      PROPV(QColor, canvasBG, QColor("#37474F"))
      PROPV(QColor, accentColor, QColor("teal"))
      PROPV(QColor, gridColor, QColor("#808080"))
      PROPV(QColor, markColor, QColor("#000000"))
      PROPV(QColor, moveColor, QColor("#0000ff"))
      PROPV(QColor, framingColor, QColor("#00ff00"))
      // 32 configurable Mop colours (indices 0..31).
      // Defaults match Mop::mopColorTable().
      PROPV(QColor, mopColor0,  QColor(0x80, 0x80, 0x80))
      PROPV(QColor, mopColor1,  QColor(0xFF, 0x00, 0x00))
      PROPV(QColor, mopColor2,  QColor(0x00, 0xFF, 0x00))
      PROPV(QColor, mopColor3,  QColor(0x00, 0x00, 0xFF))
      PROPV(QColor, mopColor4,  QColor(0xFF, 0xFF, 0x00))
      PROPV(QColor, mopColor5,  QColor(0xFF, 0x00, 0xFF))
      PROPV(QColor, mopColor6,  QColor(0x00, 0xFF, 0xFF))
      PROPV(QColor, mopColor7,  QColor(0xFF, 0x80, 0x00))
      PROPV(QColor, mopColor8,  QColor(0x80, 0x00, 0xFF))
      PROPV(QColor, mopColor9,  QColor(0x00, 0x80, 0xFF))
      PROPV(QColor, mopColor10, QColor(0x80, 0xFF, 0x00))
      PROPV(QColor, mopColor11, QColor(0xFF, 0x00, 0x80))
      PROPV(QColor, mopColor12, QColor(0x00, 0xFF, 0x80))
      PROPV(QColor, mopColor13, QColor(0x80, 0x80, 0x00))
      PROPV(QColor, mopColor14, QColor(0x00, 0x80, 0x80))
      PROPV(QColor, mopColor15, QColor(0x80, 0x00, 0x80))
      PROPV(QColor, mopColor16, QColor(0xFF, 0x80, 0x80))
      PROPV(QColor, mopColor17, QColor(0x80, 0xFF, 0x80))
      PROPV(QColor, mopColor18, QColor(0x80, 0x80, 0xFF))
      PROPV(QColor, mopColor19, QColor(0xFF, 0xFF, 0x80))
      PROPV(QColor, mopColor20, QColor(0xFF, 0x80, 0xFF))
      PROPV(QColor, mopColor21, QColor(0x80, 0xFF, 0xFF))
      PROPV(QColor, mopColor22, QColor(0xC0, 0xC0, 0xC0))
      PROPV(QColor, mopColor23, QColor(0xA0, 0x40, 0x40))
      PROPV(QColor, mopColor24, QColor(0x40, 0xA0, 0x40))
      PROPV(QColor, mopColor25, QColor(0x40, 0x40, 0xA0))
      PROPV(QColor, mopColor26, QColor(0xA0, 0xA0, 0x40))
      PROPV(QColor, mopColor27, QColor(0xA0, 0x40, 0xA0))
      PROPV(QColor, mopColor28, QColor(0x40, 0xA0, 0xA0))
      PROPV(QColor, mopColor29, QColor(0x40, 0x40, 0x40))
      PROPV(QColor, mopColor30, QColor(0xE0, 0xE0, 0xE0))
      PROPV(QColor, mopColor31, QColor(0x20, 0x20, 0x20))
      PROPV(bool, showGrid, true)
      PROPV(double, gridSpacing, 10.0)
      PROPV(double, smPanX, 4.0)
      PROPV(double, smPanY, 4.0)
      PROPV(double, smZoom, 12.0)
      PROPV(double, smPitch, 1.0)
      PROPV(double, smYaw, 1.0)
      PROPV(double, smRoll, 1.0)
      PROPV(QString, defaultMachine, QString())
      PROPV(QString, artworkDirectory, QString())
      PROPV(QString, iconDirectory, QString("/usr/share/icons"))
      PROPV(QString, machinesDirectory, QString("~/ZCam/machines"))
      PROPV(QString, recipesDirectory, QString("~/ZCam/recipes"))
      PROPV(QString, zcamDirectory, QString("~/ZCam"))
      PROPV(QString, projectsDirectory, QString("~/ZCam/projects"))

      PROPV(double, dxfScale, 72.0)
      PROPV(int, dxfCircleResolution, 360)
      PROPV(int, dxfCurveResolution, 100)

      static const std::string_view _properties;

    public:
      explicit Config(ZCam* zc);
      QString typeName() override { return QStringLiteral("config"); }
      const std::string_view properties() const override { return _properties; }
      nlohmann::json toJson() const override;
      void fromJson(const nlohmann::json&) override;
      };