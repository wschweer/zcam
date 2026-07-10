//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QPalette>
#include "zcam.h"

//---------------------------------------------------------
//   main
//---------------------------------------------------------

int main(int argc, char* argv[]) {
      QCoreApplication::setOrganizationName("zcam");
      QCoreApplication::setOrganizationDomain("zcam.org");
      QCoreApplication::setApplicationName("ZCam");
      QCoreApplication::setApplicationVersion("0.0.1");

      QGuiApplication app(argc, argv);

      QPalette darkPalette;
      darkPalette.setColor(QPalette::Window, QColor(48, 48, 48));
      darkPalette.setColor(QPalette::WindowText, Qt::white);
      darkPalette.setColor(QPalette::Base, QColor(32, 32, 32));
      darkPalette.setColor(QPalette::AlternateBase, QColor(48, 48, 48));
      darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
      darkPalette.setColor(QPalette::ToolTipText, Qt::white);
      darkPalette.setColor(QPalette::Text, Qt::white);
      darkPalette.setColor(QPalette::Button, QColor(48, 48, 48));
      darkPalette.setColor(QPalette::ButtonText, Qt::white);
      darkPalette.setColor(QPalette::BrightText, Qt::red);
      darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
      darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
      darkPalette.setColor(QPalette::HighlightedText, Qt::black);
      app.setPalette(darkPalette);

      QQuickStyle::setStyle("Material");

      QQmlApplicationEngine engine;
//      engine.addImportPath(QCoreApplication::applicationDirPath());

      engine.loadFromModule("ZCam", "Main");

      return app.exec();
      }
