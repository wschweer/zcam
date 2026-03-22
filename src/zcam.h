//=============================================================================
//  ZCam - manufactoring tool for G-code machines and Fiber Laser
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
#include <QJSEngine>
#include <QtQml/qqmlregistration.h>

//---------------------------------------------------------
//   Config
//---------------------------------------------------------

class Style :public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY iconSizeChanged)

      int _iconSize { 48 };

   signals:
      void iconSizeChanged();

   public:
      explicit Style(QObject* parent = nullptr) : QObject(parent) {}
      int iconSize() const { return _iconSize; }
      void setIconSize(int v) { if (v != _iconSize) {_iconSize = v; emit iconSizeChanged(); } }
      };

//---------------------------------------------------------
//   ZCam
//---------------------------------------------------------

class ZCam : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_SINGLETON
      Q_PROPERTY(Style* style READ style NOTIFY styleChanged)

      Style* _style { nullptr };

   signals:
      void styleChanged();

    public:
      explicit ZCam(QObject* parent = nullptr);

      Style* style() const { return _style; }

      static ZCam* create(QQmlEngine*, QJSEngine*);
      };
