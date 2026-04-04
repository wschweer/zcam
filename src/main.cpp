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

//---------------------------------------------------------
//   main
//---------------------------------------------------------

int main(int argc, char* argv[]) {
      QCoreApplication::setOrganizationName("zcam");
      QCoreApplication::setOrganizationDomain("zcam.org");
      QCoreApplication::setApplicationName("ZCam");
      QCoreApplication::setApplicationVersion("0.0.1");

      QGuiApplication app(argc, argv);

      QQuickStyle::setStyle("Material");

      QQmlApplicationEngine engine;
      engine.loadFromModule("ZCam", "Main");

      return app.exec();
      }
