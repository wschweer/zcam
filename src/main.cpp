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
#include <QElapsedTimer>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cstdlib>
#include <functional>
#include "zcam.h"

//---------------------------------------------------------
//   firstInstall
//    Check whether ~/ZCam exists.  If not, create the directory
//    tree and copy all bundled resources from the ":/ZCam/" prefix
//    (embedded from data/ZCam/) to ~/ZCam.
//---------------------------------------------------------

static void firstInstall() {
      QString home    = QDir::homePath();
      QString zcamDir = home + "/ZCam";

      if (QDir(zcamDir).exists())
            return;

      qDebug() << "First install: creating" << zcamDir;
      QDir().mkpath(zcamDir);

      // Recursively copy all files from the embedded resource tree
      // ":/ZCam/" → ~/ZCam/
      std::function<void(const QString&)> copyResourceDir = [&](const QString& resourcePath) {
            QDir dir(resourcePath);
            for (const auto& entry : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
                  // Build the relative path from the resource root ":/ZCam/"
                  QString resourceRel  = entry.absoluteFilePath().mid(2); // strip ":/"
                  QString destRelative = resourceRel.mid(QStringLiteral("ZCam/").length());
                  QString dest         = zcamDir + "/" + destRelative;

                  // Ensure parent directory exists
                  QFileInfo di(dest);
                  QDir().mkpath(di.absolutePath());

                  QFile src(entry.absoluteFilePath());
                  if (src.open(QIODevice::ReadOnly)) {
                        QFile dst(dest);
                        if (dst.open(QIODevice::WriteOnly)) {
                              dst.write(src.readAll());
                              dst.close();
                              }
                        src.close();
                        }
                  qDebug() << "  copied" << destRelative;
                  }
            for (const auto& entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                  copyResourceDir(entry.absoluteFilePath());
            };

      copyResourceDir(":/ZCam");
      }

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

      firstInstall();

      QQmlApplicationEngine engine;
      //      engine.addImportPath(QCoreApplication::applicationDirPath());

      engine.loadFromModule("ZCam", "Main");

      int ret = app.exec();

      // All critical cleanup (assets save, laser shutdown, geometry worker
      // shutdown) has already been done in the aboutToQuit handler.
      //
      // The ~QQmlApplicationEngine destructor (stack unwinding) and the
      // static singleton destructors (~GeometryWorker, ~ZCam, etc.) are
      // very slow because they tear down hundreds of QML objects.
      // We skip them entirely by calling _exit(), which terminates the
      // process immediately without running destructors.
      std::_Exit(ret);
      }