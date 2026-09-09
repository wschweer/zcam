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
#include <QtWebEngineQuick>
#include "zcam.h"
#include "project.h"
#include "scriptengine.h"
#include "inspector_model.h"
#include "rectangle.h"
#include "group.h"
#include "element3d.h"
#include "sidebarcolorfixer.h"
#include "remotecontrol.h"
#include <QTimer>

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

      Debug("First install: creating {}", zcamDir);
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
                  Debug("  copied {}", destRelative);
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
      // Use the Qt Quick implementation of FileDialog / MessageDialog etc.
      // instead of native dialogs, so the Material Dark palette is applied
      // consistently and the sidebar text stays light-on-dark.
      // On KDE Plasma the "native" dialog is provided via the
      // xdg-desktop-portal / KDEPlasmaPlatformTheme and shows unreadable
      // black-on-dark sidebar text;  setting AA_DontUseNativeDialogs
      // forces Qt Quick Dialogs' quickimpl everywhere.
      qputenv("QT_QUICK_DIALOGS_USE_QT_QUICK_IMPL", "1");
      QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

      // Initialize WebEngine before creating the application object
      QtWebEngineQuick::initialize();

      QCoreApplication::setOrganizationName("zcam");
      QCoreApplication::setOrganizationDomain("zcam.org");
      QCoreApplication::setApplicationName("ZCam");
      QCoreApplication::setApplicationVersion("0.0.1");

      QGuiApplication app(argc, argv);

      // ── Fix FileDialog sidebar text color (Qt 6.12 regression) ───────
      //    Install an event filter that sets IconLabel colors to white
      //    after a file dialog window is exposed.
      SideBarColorFixer sidebarColorFixer;

      const QStringList args = app.arguments();

      // ── Command-line file argument ────────────────────────────────
      //    If a file path is given as the first non-option argument,
      //    pass it to ZCam for handling at startup (.zcam → open,
      //    importable → new project + import).
      for (int i = 1; i < args.size(); ++i) {
            const QString& arg = args[i];
            if (arg.startsWith('-'))
                  continue; // skip options like -style, -platform, ...
            QFileInfo fi(arg);
            if (fi.exists() && fi.isFile()) {
                  ZCam::setStartupFilePath(fi.absoluteFilePath());
                  break;
                  }
            }

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

      // Force the application-wide color scheme to dark so that native and
      // Qt Quick Dialogs also use a dark palette, even when the desktop is
      // configured for a light theme.  This prevents black-on-dark text in
      // file dialogs when the app runs with Material.Dark.
      app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);

      QQuickStyle::setStyle("Material");
      firstInstall();

      QQmlApplicationEngine engine;
      //      engine.addImportPath(QCoreApplication::applicationDirPath());

      engine.loadFromModule("ZCam", "Main");

      // ── Stdin remote-control interface (test / automation) ────────────
      //    Attach a line-based stdin reader that maps each command line to
      //    an AIAgent tool call and returns one line of JSON on stdout.  See
      //    NPED.md → "Stdin Remote Control" for the full protocol.  This is
      //    safe even when stdin is a terminal: the QSocketNotifier only
      //    fires when there is actually data to read.
      //
      //    The ZCam singleton is created lazily during QML loading (Main.qml)
      //    above, so we attach the reader one event-loop tick later to be
      //    sure ZCam::instance() and its AIAgent exist.
      RemoteControl* remoteControl = new RemoteControl(&app);
      QTimer::singleShot(0, remoteControl, [remoteControl]() { remoteControl->start(); });

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