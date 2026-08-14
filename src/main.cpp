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



      // ── Scripting self-test (--script-test) ─────────────────────────
      //    Loads /tmp/script-test.zcam and verifies:
      //      1. the persisted script for rectangle1.pos.x is recreated
      //         and evaluated to rectangle2.size.x + 5
      //      2. changing rectangle2.size.x re-evaluates the binding
      //      3. the binding is serialized when the project is saved
      //    A scalar binding on r2.pos.x is created at runtime to test
      //    the createBindingQml path used by the inspector popup.
      const QStringList args = app.arguments();
      if (args.contains(QStringLiteral("--script-test"))) {
            auto* zcam = new ZCam();
            bool ok    = zcam->openProject(QStringLiteral("/home/ws/zcam/script-test.zcam"), true);
            Q_ASSERT(ok);
            auto* cad = zcam->project()->cad();
            Q_ASSERT(cad && !cad->children().isEmpty());
            auto* layer = cad->children().first();
            auto* r1    = qobject_cast<Rectangle*>(layer->children().at(0));
            auto* r2    = qobject_cast<Rectangle*>(layer->children().at(1));
            Q_ASSERT(r1 && r2);
            auto* se = zcam->scriptEngine();
            Q_ASSERT(se);

            // 1) persisted component binding was restored & evaluated
            qInfo() << "1) r1 pos.x =" << r1->pos().x() << " expected" << r2->size().x() + 5.0;
            Q_ASSERT(qAbs(r1->pos().x() - (r2->size().x() + 5.0)) < 1e-6);

            // 2) dependency change re-evaluates
            //    (lockSize may be Off (old files) or Square (new default)
            //    depending on the stored project; compute expected size)
            r2->set_size(QVector2D(100.0, r2->size().y()));
            qInfo() << "2) after r2.size.x = 100: r1 pos.x =" << r1->pos().x() << " size " << r2->size();
            Q_ASSERT(qAbs(r1->pos().x() - (r2->size().x() + 5.0)) < 1e-6);

            // 3) runtime scalar binding on r2.rot.z
            se->createBindingQml(
                r2, QStringLiteral("corner"), -1, QStringLiteral("project.cad.layer1.rectangle1.pos.y * 2"));
            qInfo() << "3) r2 corner =" << r2->corner() << " expected" << r1->pos().y() * 2;
            Q_ASSERT(qAbs(r2->corner() - r1->pos().y() * 2) < 1e-6);
            r1->set_pos(QVector3D(r1->pos().x(), 7.5, 0.0));
            Q_ASSERT(qAbs(r2->corner() - 15.0) < 1e-6);

            // 4) serialization round-trip
            ok = zcam->saveAs(QStringLiteral("/tmp/script-test-saved.zcam"));
            Q_ASSERT(ok);
            ok = zcam->openProject(QStringLiteral("/tmp/script-test-saved.zcam"), true);
            Q_ASSERT(ok);
            cad   = zcam->project()->cad();
            layer = cad->children().first();
            r1    = qobject_cast<Rectangle*>(layer->children().at(0));
            r2    = qobject_cast<Rectangle*>(layer->children().at(1));
            Q_ASSERT(r1 && r2);
            qInfo() << "4) after save+load: r1 pos.x =" << r1->pos().x() << " expected"
                    << r2->size().x() + 5.0 << " r2 corner =" << r2->corner();
            Q_ASSERT(qAbs(r1->pos().x() - (r2->size().x() + 5.0)) < 1e-6);
            Q_ASSERT(qAbs(r2->corner() - 15.0) < 1e-6);
            Q_ASSERT(r1->scriptCompProp(0) == QStringLiteral("pos"));
            Q_ASSERT(r2->hasScriptFor(QStringLiteral("corner")));

            // 5) removing the binding keeps the last value
            se->removeBindingQml(r2, QStringLiteral("corner"));
            r1->set_pos(QVector3D(r1->pos().x(), 42.0, 0.0));
            Q_ASSERT(qAbs(r2->corner() - 15.0) < 1e-6);
            qInfo() << "5) after remove: r2 corner stays" << r2->corner();

            // 6) self-referential vector component binding:
            //    rectangle2.size.x = rectangle2.size.y * 0.5
            //    changing size.y must re-evaluate size.x
            //    (use lockSize=Off so the preset sizes are exact)
            se->removeBindingsFor(r2); // clear the corner binding so it does not interfere
            r2->set_lockSize(static_cast<int>(LockScaleMode::Off));
            r2->set_size(QVector2D(10.0, 20.0));
            r2->setScriptCompProp(0, QString());
            se->createBinding(r2, QStringLiteral("size"), 0, QStringLiteral("size.y * 0.5"));
            qInfo() << "6) r2 size =" << r2->size() << "expected (10, 20)";
            Q_ASSERT(qAbs(r2->size().x() - 10.0) < 1e-6);
            Q_ASSERT(qAbs(r2->size().y() - 20.0) < 1e-6);

            r2->set_size(QVector2D(r2->size().x(), 60.0));
            qInfo() << "6b) after r2.size.y = 60: r2 size =" << r2->size() << "expected (30, 60)";
            Q_ASSERT(qAbs(r2->size().x() - 30.0) < 1e-6);
            Q_ASSERT(qAbs(r2->size().y() - 60.0) < 1e-6);

            // 7) inspector path: when size.x is bound, changing size.y through
            //    the inspector must re-evaluate size.x and show the new value.
            r2->set_size(QVector2D(10.0, 20.0));
            se->createBinding(r2, QStringLiteral("size"), 0, QStringLiteral("size.y * 0.5"));
            Q_ASSERT(qAbs(r2->size().x() - 10.0) < 1e-6);

            // Simulate what InspectorModel::setData does for a partial vector binding:
            // read current size, preserve bound component, write merged value.
            QVector2D curSize = r2->size();
            QVector2D inspectorValue(curSize.x(), 80.0); // user changed y to 80, x left untouched
            se->createBinding(r2, QStringLiteral("size"), 0, QStringLiteral("size.y * 0.5"));
            r2->set_size(inspectorValue);
            qInfo() << "7) inspector path: r2 size =" << r2->size() << "expected (40, 80)";
            Q_ASSERT(qAbs(r2->size().x() - 40.0) < 1e-6);
            Q_ASSERT(qAbs(r2->size().y() - 80.0) < 1e-6);

            // 8) default lockSize mode (Square): with size.x bound, the
            //    raw user edit of size.y must NOT be squared away, and
            //    the binding re-evaluation must propagate (this was the
            //    reported GUI failure: nothing happened on change).
            se->removeBindingsFor(r2);
            r2->set_lockSize(static_cast<int>(LockScaleMode::Square));
            r2->set_size(QVector2D(10.0, 10.0)); // square, enforcement active
            Q_ASSERT(qAbs(r2->size().x() - 10.0) < 1e-6);
            Q_ASSERT(qAbs(r2->size().y() - 10.0) < 1e-6);
            se->createBinding(r2, QStringLiteral("size"), 0, QStringLiteral("size.y * 0.5"));
            Q_ASSERT(qAbs(r2->size().x() - 5.0) < 1e-6);
            Q_ASSERT(qAbs(r2->size().y() - 10.0) < 1e-6);
            // User edits height (y) to 40 while width (x) is bound.
            // Without the lock-skip fix, lockSize=Square would square
            // (5,40) to (10,10) → sizeChanged swallowed → nothing updates.
            r2->set_size(QVector2D(r2->size().x(), 40.0));
            qInfo() << "8) lockSize=Square, after size.y = 40: r2 size =" << r2->size()
                    << "expected (20, 40)";
            Q_ASSERT(qAbs(r2->size().x() - 20.0) < 1e-6);
            Q_ASSERT(qAbs(r2->size().y() - 40.0) < 1e-6);

            // 9) inspector row path: the inspector's "size" cell is a row
            //    entry, so GUI edits go through InspectorModel::setSubProperty,
            //    which must merge the free component with the bound one.
            //    (lockSize is still Square from step 8 — like the GUI default)
            InspectorModel im;
            im.setElement(r2);
            int sizeRow = -1;
            for (int i = 0; i < im.rowCount(); ++i) {
                  const QVariantList subs = im.data(im.index(i, 0), InspectorModel::SubPropsRole).toList();
                  for (const QVariant& s : subs)
                        if (s.toString() == QStringLiteral("size"))
                              sizeRow = i;
                  }
            Q_ASSERT(sizeRow >= 0);
            // size.x is bound → the merged write keeps the script value of x;
            // setSubProperty must still accept the free y component.
            bool accepted = im.setSubProperty(sizeRow, QStringLiteral("size"), QVector2D(999.0, 80.0));
            qInfo() << "9) setSubProperty accepted =" << accepted << "r2 size =" << r2->size()
                    << "expected (40, 80)";
            Q_ASSERT(accepted);
            Q_ASSERT(qAbs(r2->size().x() - 40.0) < 1e-6);
            Q_ASSERT(qAbs(r2->size().y() - 80.0) < 1e-6);
            // A scalar binding ("all") must still block the write.
            se->removeBindingsFor(r2);
            se->createBinding(r2, QStringLiteral("size"), QStringLiteral("[7, 14]"));
            qInfo() << "9b) scalar binding: r2 size =" << r2->size() << "expected (7, 14)";
            Q_ASSERT(qAbs(r2->size().x() - 7.0) < 1e-6);
            Q_ASSERT(qAbs(r2->size().y() - 14.0) < 1e-6);
            accepted = im.setSubProperty(sizeRow, QStringLiteral("size"), QVector2D(1.0, 2.0));
            Q_ASSERT(!accepted);
            Q_ASSERT(qAbs(r2->size().x() - 7.0) < 1e-6);
            se->removeBindingsFor(r2);

            qInfo() << "SCRIPT TEST PASSED";
            return 0;
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