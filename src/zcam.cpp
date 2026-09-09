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

#include "zcam.h"
#include <functional>
#include <QtQuick3D/private/qquick3dcamera_p.h>
#include <QtQuick3D/private/qquick3dnode_p.h>
#include <QtQuick3D/private/qquick3dviewport_p.h>
#include "galvocalibration.h"
#include "project.h"
#include "cad.h"
// #include "cameraelement.h"
#include "scriptengine.h"
#include "text.h"
#include "group.h"
#include "recipe.h"
#include "rectangle.h"
#include "polygon.h"
#include "ellipse.h"
#include "element3d.h"
#include "mop.h"
#include "laser_mop.h"
#include "nest.h"
#include "treemodel.h"
#include "machine.h"
#include "machines.h"
#include "laser.h"
#include "recipe.h"
#include "element.h"
#include "undo.h"
// #include "propertyjson.h"
#include "geometryworker.h"
#include "grid.h"
#include "fixture.h"
#include "framing.h"
#include "stock.h"
#include "dxfimport.h"
#include "brepimport.h"
#include "imageimport.h"
#include "importipc2581.h"
#include "config.h"
#include "ai_agent.h"

#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QTextStream>
#include <QStandardPaths>
#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <tuple>
#include <QQuaternion>
#include <QMatrix4x4>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

//---------------------------------------------------------
//   ZCam
//---------------------------------------------------------

ZCam* ZCam::_instance {nullptr};
QString ZCam::_startupFilePath;
ZCam::ZCam(QObject* parent) : QObject(parent) {
      _instance = this;
      _config   = new Config(this);

      // maintain two tree structures:
      //    - QObject tree
      //    - Element tree
      // Reason: we cannot easily manage the object order in QObject tree

      _machines         = new Machines(this);
      _recipes          = new Recipe(this);
      _galvoCalibration = new GalvoCalibration(this, this);

      // Script engine for property bindings (TODO.md “Scripting”).
      // The ScriptEngine is a child of ZCam so its lifetime spans all
      // projects.  The global ScriptEngine::instance() pointer is set
      // here so elements can register/remove bindings without having
      // access to the ZCam singleton.
      _scriptEngine = new ScriptEngine(this);
      _scriptEngine->setZcam(this);
      ScriptEngine::setInstance(_scriptEngine);

      loadAssets();
      setupFileDialogFavorites();

      // Create the AI agent as a child of ZCam and wire it after
      // loadAssets() so syncConfig() picks up the persisted AI
      // config values (ollamaModel, ollamaBaseUrl, etc.) from assets.json.
      _aiAgent = new AIAgent(this);
      _aiAgent->setZCam(this);

      _treeModel = new TreeModel(this);

      // Create an initial empty project as a valid default state.
      // The QML layer calls restoreLastProject() from Component.onCompleted
      // (after all signal handlers are connected) to replace this with the
      // previously-opened project, if any.
      //
      // IMPORTANT: newProject() must NOT clear the persisted lastPath here,
      // otherwise restoreLastProject() has nothing to read.
      newProject(false);

      // When the machines list changes (e.g. machines added/removed/renamed),
      // re-resolve the Project's Machine* from the stored machine name.
      connect(_machines, &Machines::machinesModelChanged, this, [this]() {
            if (_project)
                  _project->resolveMachine();
            });

      // When the active machine changes, update the recipe machine-type filter
      // so only recipes for the current machine type are shown.
      connect(this, &ZCam::projectChanged, this, [this]() {
            if (_project) {
                  // Disconnect any previous machineChanged handler from the
                  // old project, then connect to the new one.
                  disconnect(_project, &Project::machineChanged, this, nullptr);
                  connect(_project, &Project::machineChanged, this, [this]() {
                        if (_project && _project->machine())
                              _recipes->set_machineType(_project->machine()->type());
                        });
                  }
            // Also apply immediately for the current project
            if (_project && _project->machine())
                  _recipes->set_machineType(_project->machine()->type());
            });

      // Reload machines and recipes when their configured directory changes
      // at runtime (e.g. via the Config Panel).
      connect(_config, &Config::machinesDirectoryChanged, this,
          [this]() { _machines->loadFromDirectory(machinesDirectory()); });
      connect(_config, &Config::recipesDirectoryChanged, this,
          [this]() { _recipes->loadFromDirectory(recipesDirectory(), _recipes->machineType()); });

      // When a Mop colour in Config changes, emit curColorChanged on
      // every visible Element3d in the project tree so the 3D canvas
      // updates the displayed colours immediately.
      for (int i = 0; i < 32; ++i) {
            QByteArray sigName = "mopColor" + QByteArray::number(i) + "Changed";
            int sigIdx         = Config::staticMetaObject.indexOfSignal(sigName.constData());
            int slotIdx        = metaObject()->indexOfSlot("onMopColorChanged()");
            if (sigIdx >= 0 && slotIdx >= 0)
                  QMetaObject::connect(_config, sigIdx, this, slotIdx, Qt::AutoConnection);
            }

      // When a recipe's content changes (e.g. laser parameters edited,
      // layer added/removed), check whether any LaserMop element
      // in the current project references that recipe.  If so, mark the
      // CAM data as dirty so the user knows a refresh is needed.

      connect(_recipes, &Recipe::recipeChanged, this, [this](int idx) {
            if (!_project || !_rootElement)
                  return;
            LaserRecipe* changedRecipe = _recipes->recipePtr(idx);
            if (!changedRecipe)
                  return;

            // Traverse the project tree looking for LaserMop
            // elements whose recipe pointer matches the changed recipe.

            bool found                         = false;
            std::function<void(Element*)> walk = [&](Element* e) {
                  if (found)
                        return;
                  auto* ll = qobject_cast<LaserMop*>(e);
                  if (ll && ll->recipe() == changedRecipe) {
                        found = true;
                        return;
                        }
                  for (auto* c : e->children())
                        walk(c);
                  };
            walk(_rootElement);
            if (found)
                  setCamDirty(true);
            });

      // When the recipe model structure changes (recipes added, removed,
      // or reloaded from disk), recipe pointers held by Recipe elements
      // may become invalid or point to different recipes.  Mark CAM dirty
      // so the user knows a refresh is needed.
      connect(_recipes, &Recipe::recipeModelChanged, this, [this]() { setCamDirty(true); });

      // Automatically save assets (machines, recipes) and stop the laser
      // when the application is about to quit so changes are not lost and
      // background threads are cleanly joined before the event loop stops.
      //
      // GeometryWorker is NOT shut down here because aboutToQuit fires
      // before the QML engine is destroyed.  QML destructors may enqueue
      // new geometry tasks whose callbacks would target dead objects.
      // The GeometryWorker singleton destructor handles final cleanup
      // with a bounded timeout.
      QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
            GeometryWorker::instance().shutdown();
            saveAssets();
            });
      }

//---------------------------------------------------------
//   setCurrentElement
//    Custom setter for currentElement.  Emits curColorChanged on
//    the old and new element so the 3D view updates highlight
//    colors regardless of whether the selection originated from
//    QML (TreeView click) or C++ (3D canvas pick).
//---------------------------------------------------------

void ZCam::setCurrentElement(Element3d* el) {
      Element3d* oldElement = _currentElement;
      // Early return only if both are the same AND no multi-selection
      // needs to be cleared.  When el is null and _selectedElements
      // is non-empty, we must still proceed to clear the selection.
      if (el == oldElement && (el || _selectedElements.isEmpty()))
            return;
      // Clear segment selection on the old element when switching away.
      if (oldElement) {
            auto* oldPoly = qobject_cast<Polygon*>(oldElement);
            if (oldPoly && oldPoly->selectedSegment() >= 0)
                  oldPoly->clearSegmentSelection();
            }
      _currentElement = el;
      // When selecting a single element (not via lasso) or when
      // deselecting (el == nullptr, e.g. clicking on empty canvas),
      // clear the lasso multi-selection so the two selection modes
      // don't overlap.  When el is in _selectedElements (e.g. lassoSelect
      // sets the first element as currentElement), keep the selection.
      if (!_selectedElements.isEmpty() && (!el || !_selectedElements.contains(el))) {
            auto old = _selectedElements;
            _selectedElements.clear();
            for (auto* e : old)
                  emit e->curColorChanged();
            emit selectedElementsChanged();
            }
      emit currentElementChanged();
      // Signal color changes so the 3D view updates highlight colors.
      if (oldElement)
            emit oldElement->curColorChanged();
      if (el)
            emit el->curColorChanged();
      // When the new selection is a Group, refresh it now so its
      // selection bounding box is up-to-date when the bbox overlay
      // becomes visible.
      if (auto* group = qobject_cast<Group*>(el))
            group->update();
      }

//---------------------------------------------------------
//   forgetElement
//    Clear all tracking pointers that reference the given element.
//    Called from the Element3d destructor to prevent dangling-pointer
//    dereferences when elements are deleted while still being tracked
//    as hoverElement, currentElement, or in _selectedElements.
//    Emits changed signals on ZCam (not on the dying element) so QML
//    stays in sync, but never emits curColorChanged on the deleted
//    element — that would activate connections on a dying object.
//---------------------------------------------------------

void ZCam::forgetElement(Element3d* el) {
      if (!el)
            return;
      if (_hoverElement == el)
            set_hoverElement(nullptr);
      if (_currentElement == el) {
            _currentElement = nullptr;
            emit currentElementChanged();
            }
      if (!_selectedElements.isEmpty() && _selectedElements.removeAll(el))
            emit selectedElementsChanged();
      }

//---------------------------------------------------------
//   applyFontToCurrentText
//    Apply a font family to the currently selected Text
//    element. The change goes through the Project undo system so it
//    is undoable and marks the project dirty.
//---------------------------------------------------------

void ZCam::applyFontToCurrentText(const QString& family) {
      if (!_currentElement)
            return;
      auto* text = qobject_cast<Text*>(_currentElement);
      if (!text)
            return;
      if (!text->fontFamily().isEmpty() && text->fontFamily() == family)
            return;
      _project->changeProperty(text, "fontFamily", family);
      }

//---------------------------------------------------------

//--------------------------------------------------------------------
//     onMopColorChanged
//    Slot connected to all 32 Config::mopColor<N>Changed signals.
//    Emits curColorChanged on every Element3d in the project tree
//    so the 3D canvas updates displayed colours immediately.
//--------------------------------------------------------------------

void ZCam::onMopColorChanged() {
      if (!_rootElement)
            return;
      std::function<void(Element*)> walk = [&](Element* e) {
            if (!e)
                  return;
            if (auto* e3d = qobject_cast<Element3d*>(e))
                  emit e3d->curColorChanged();
            for (auto* c : e->children())
                  walk(c);
            };
      walk(_rootElement);
      }

//---------------------------------------------------------
//   setCamDirty
//    Mark the cam data as out-of-date.  The QML "Cam" refresh
//    button becomes enabled when this is true.
//---------------------------------------------------------

void ZCam::setCamDirty(bool v) {
      if (v == _camDirty)
            return;
      _camDirty = v;
      emit camDirtyChanged();
      }

//---------------------------------------------------------
//   centerOnWorkspace
//    Center the given element on the workspace midpoint.
//    The workspace size is determined by the current machine's
//    maxTravel (X and Y).  Only Text, Polygon, Ellipse and
//    Rectangle elements are accepted; Z is always set to zero.
//    The operation is routed through the undo stack.
//---------------------------------------------------------

void ZCam::centerOnWorkspace(Element3d* element) {
      if (!element || !element->draggable())
            return;

      // Only accept Text, Polygon, Ellipse and Rectangle elements.
      // All four override draggable() to return true, so the draggable()
      // check above already filters non-shape elements.  But we also
      // check the concrete type to be explicit and safe.
      if (!isType<Text>(element) && !isType<Polygon>(element) && !isType<Ellipse>(element) &&
          !isType<Rectangle>(element))
            return;

      // Determine the workspace center from the current machine.
      double centerX = 0.0;
      double centerY = 0.0;
      if (_project && _project->machine()) {
            QVector3D travel = _project->machine()->maxTravel();
            centerX          = travel.x() / 2.0;
            centerY          = travel.y() / 2.0;
            }

      // Compute the element's bounding box center in world coordinates.
      // The bounding box is in local coordinates, so we transform it
      // through the element's globalMatrix() to get world coordinates.
      QRectF bbox = element->boundingBox();
      if (bbox.isNull() || bbox.isEmpty())
            return;

      // The center of the bbox in local coordinates.
      QPointF localCenter = bbox.center();

      // Transform the local center to world coordinates.
      QVector3D worldCenter = element->globalMatrix().map(
          QVector3D(static_cast<float>(localCenter.x()), static_cast<float>(localCenter.y()), 0.0f));

      // The new position must move the bbox center to the workspace center.
      // Since pos is in the parent's local coordinate system, we need
      // to convert the world-space displacement to parent-local space.
      QVector3D desiredWorldCenter(static_cast<float>(centerX), static_cast<float>(centerY), 0.0f);
      QVector3D worldDelta = desiredWorldCenter - worldCenter;

      // Convert world delta to parent-local delta.
      QVector3D localDelta = worldDelta;
      if (auto* p = qobject_cast<Element3d*>(element->parent())) {
            QMatrix4x4 parentGlobal = p->globalMatrix();
            bool ok                 = false;
            QMatrix4x4 inv          = parentGlobal.inverted(&ok);
            if (ok)
                  localDelta = inv.mapVector(worldDelta);
            }

      QVector3D oldPos = element->pos();
      QVector3D newPos = QVector3D(oldPos.x() + localDelta.x(), oldPos.y() + localDelta.y(), 0.0f);

      if ((newPos - oldPos).length() < 0.001)
            return;

      if (_project) {
            _project->undo()->beginMacro();
            auto cmd = new PropertyChangeCommand(
                this, element, "pos", QVariant::fromValue(oldPos), QVariant::fromValue(newPos));
            _project->undo()->push(cmd);
            _project->undo()->endMacro();
            }
      }

//---------------------------------------------------------
//   refreshCam
//    Recalculate cam data and clear the dirty flag.
//---------------------------------------------------------

void ZCam::refreshCam() {
      if (!_project)
            return;
      Cam* cam = _project->cam();
      if (!cam)
            return;
      cam->updateCam();
      setCamDirty(false);
      }

//---------------------------------------------------------
//   updateViewCamera
//    Called from QML (3D panel) whenever the canvas view changes
//    (pan / zoom / rotate).  Stores the camera's perpendicular foot
//    point on z=0 (cx, cy) and its height above z=0, both in
//    root-local millimetres, so Cam::grabCameraView() can align the
//    laser projection with what is shown on the canvas.
//---------------------------------------------------------

void ZCam::updateViewCamera(double cx, double cy, double height) {
      QVector2D c(cx, cy);
      if (c != _viewCameraCenter || height != _viewCameraHeight) {
            _viewCameraCenter = c;
            _viewCameraHeight = height;
            emit viewCameraChanged();
            }
      }

//--------------------------------------------------------------------
//     setCanvasItem
//    Register the main 3-D canvas View3D item (from View3DPanel.qml) so
//    the screenshot code can grab its on-screen region.
//--------------------------------------------------------------------

void ZCam::setCanvasItem(QObject* item) {
      if (item != _canvasItem) {
            _canvasItem = item;
            emit canvasItemChanged();
            }
      }

//--------------------------------------------------------------------
//     screenshotDirectory
//--------------------------------------------------------------------

QString ZCam::screenshotDirectory() {
      QString dir = QDir::homePath() + "/ZCam/screenshots";
      QDir().mkpath(dir);
      return dir;
      }

//--------------------------------------------------------------------
//     grabCanvas
//    Capture the on-screen image of the 3-D canvas.  We resolve the
//    canvas item's window and its global scene geometry, then crop that
//    rectangle out of a full window grab.  Using grabWindow() (instead
//    of item->grabToImage()) keeps the real, GPU-rendered pixels —
//    which is exactly what the user sees, including the transparent
//    environment of the Offscreen render mode — and works even when the
//    canvas uses Qt Quick 3D's offscreen compositor.
//
//    *maxWidth* (in pixels, 0 = no limit) optionally downscales the
//    result while preserving the aspect ratio.  Used by the AI tool to
//    keep the base64 payload that is embedded into the LLM context
//    reasonably small.
//--------------------------------------------------------------------

QImage ZCam::grabCanvas(int maxWidth) {
      if (!_canvasItem)
            return QImage();

      QQuickItem* item = qobject_cast<QQuickItem*>(_canvasItem);
      if (!item)
            return QImage();

      QQuickWindow* window = item->window();
      if (!window)
            return QImage();

      // Canvas geometry in the window's scene (logical) pixels.
      const QPointF scenePos = item->mapToScene(QPointF(0, 0));
      const qreal w          = item->width();
      const qreal h          = item->height();
      if (w <= 0.0 || h <= 0.0)
            return QImage();

      const qreal dpr = window->devicePixelRatio();

      // grabWindow() renders the full window into a QImage at the window's
      // native (physical) resolution.  We crop the canvas rectangle out of
      // it, converting the logical-pixel crop into physical pixels with the
      // window's devicePixelRatio.  Using grabWindow() (rather than
      // item->grabToImage()) keeps the real GPU-rendered pixels the user
      // sees and also works for Qt Quick 3D's Offscreen render mode.
      QImage full = window->grabWindow();
      if (full.isNull())
            return QImage();

      QRectF cropF(scenePos.x() * dpr, scenePos.y() * dpr, w * dpr, h * dpr);
      QRect crop = cropF.toAlignedRect();

      // Clamp to the captured image bounds.
      const QRect bounds(full.rect());
      if (!bounds.intersects(crop))
            return QImage();
      crop = crop.intersected(bounds);

      QImage cropped = full.copy(crop);
      if (cropped.isNull())
            return QImage();

      // Compress a fully-transparent (alpha) canvas to a solid background so
      // the image is useful even when the transparent Offscreen canvas is
      // captured over a transparent surface.
      if (cropped.hasAlphaChannel()) {
            QImage flat(cropped.size(), QImage::Format_ARGB32_Premultiplied);
            flat.fill(QColor(28, 28, 30)); // ZCam Material dark base
            QPainter p(&flat);
            p.drawImage(0, 0, cropped);
            p.end();
            cropped = flat;
            }

      // Optional downscale (aspect ratio preserved).
      if (maxWidth > 0 && cropped.width() > maxWidth) {
            int h   = int(qreal(cropped.height()) * maxWidth / cropped.width());
            cropped = cropped.scaled(maxWidth, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
      return cropped;
      }

//--------------------------------------------------------------------
//     saveCanvasScreenshot
//--------------------------------------------------------------------

QString ZCam::saveCanvasScreenshot() {
      QImage img = grabCanvas();
      if (img.isNull()) {
            Warning("saveCanvasScreenshot: capture failed (no canvas/window?)");
            return QString();
            }
      const QString path =
          screenshotDirectory() + "/" +
          QStringLiteral("zcam-%1.png").arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss"));
      if (!img.save(path, "PNG")) {
            Warning("saveCanvasScreenshot: failed to write {}", path.toStdString());
            return QString();
            }
      Debug("Canvas screenshot saved to {} ({}x{})", path.toStdString(), img.width(), img.height());
      return path;
      }

//---------------------------------------------------------
//   create
//---------------------------------------------------------

ZCam* ZCam::create(QQmlEngine*, QJSEngine*) {
      return new ZCam();
      }

//---------------------------------------------------------
//   defaultMachinesDirectory
//---------------------------------------------------------

QString ZCam::defaultMachinesDirectory() {
      return QStringLiteral("~/ZCam/machines");
      }

//---------------------------------------------------------
//   defaultRecipesDirectory
//---------------------------------------------------------

QString ZCam::defaultRecipesDirectory() {
      return QStringLiteral("~/ZCam/recipes");
      }

//---------------------------------------------------------
//   defaultArtworkDirectory
//---------------------------------------------------------

QString ZCam::defaultArtworkDirectory() {
      return QStringLiteral("~/ZCam/artwork");
      }

//---------------------------------------------------------
//   defaultIconDirectory
//---------------------------------------------------------

QString ZCam::defaultIconDirectory() {
      return QStringLiteral("~/ZCam/icons");
      }

//---------------------------------------------------------
//   expandPath
//    Expand a leading '~' to the user's home directory.
//    Returns the path unchanged if it does not start with '~'.
//---------------------------------------------------------

QString ZCam::expandPath(const QString& path) {
      if (path.startsWith('~'))
            return QDir::homePath() + path.mid(1);
      return path;
      }

//---------------------------------------------------------
//   machinesDirectory
//    Return the configured machines directory, or the default.
//    A leading '~' is expanded to the user's home directory.
//---------------------------------------------------------

QString ZCam::machinesDirectory() const {
      if (!_config->machinesDirectory().isEmpty())
            return expandPath(_config->machinesDirectory());
      return expandPath(defaultMachinesDirectory());
      }

//---------------------------------------------------------
//   recipesDirectory
//    Return the configured recipes directory, or the default.
//---------------------------------------------------------

QString ZCam::recipesDirectory() const {
      if (!_config->recipesDirectory().isEmpty())
            return expandPath(_config->recipesDirectory());
      return expandPath(defaultRecipesDirectory());
      }

//---------------------------------------------------------
//   setupFileDialogFavorites
//    Add the configured projectsDirectory and the current project's
//    directory as favorites to the Qt Quick FileDialog sidebar.
//
//    Qt's QQuickSideBarPrivate::readSettings() reads favorites
//    from QSettings("QtProject", "qquickfiledialog") using
//    beginReadArray("favorites") with key "favorite" (a URL).
//    We write the projectsDirectory here so it appears as a
//    sidebar entry in all file dialogs.  When a project is currently
//    open, its parent directory is also added as a "Current" favorite.
//---------------------------------------------------------

void ZCam::setupFileDialogFavorites() {
      QSettings settings(QStringLiteral("QtProject"), QStringLiteral("qquickfiledialog"));

      // Build the URL for the configured projects directory
      QString projectsPath = expandPath(_config->projectsDirectory());
      QUrl projectsUrl     = QUrl::fromLocalFile(projectsPath);

      // Build the URL for the current project's directory (if any)
      QUrl currentProjectUrl;
      if (_project && !_project->projectPath().isEmpty()) {
            QString currentDir = QFileInfo(_project->projectPath()).absolutePath();
            currentProjectUrl  = QUrl::fromLocalFile(currentDir);
            }

      // Read existing favorites
      QList<QUrl> favorites;
      int count = settings.beginReadArray(QStringLiteral("favorites"));
      for (int i = 0; i < count; ++i) {
            settings.setArrayIndex(i);
            QUrl url = settings.value(QStringLiteral("favorite")).toUrl();
            if (url.isValid())
                  favorites.append(url);
            }
      settings.endArray();

      // Remove any existing entry for the same path (avoid duplicates)
      favorites.removeIf([&](const QUrl& u) { return u.toLocalFile() == projectsUrl.toLocalFile(); });
      if (currentProjectUrl.isValid())
            favorites.removeIf(
                [&](const QUrl& u) { return u.toLocalFile() == currentProjectUrl.toLocalFile(); });

      // Prepend the current project directory (if any) so it appears at the top
      if (currentProjectUrl.isValid())
            favorites.prepend(currentProjectUrl);

      // Prepend the projects directory so it appears at the top
      favorites.prepend(projectsUrl);

      // Write back
      settings.beginWriteArray(QStringLiteral("favorites"));
      for (int i = 0; i < favorites.size(); ++i) {
            settings.setArrayIndex(i);
            settings.setValue(QStringLiteral("favorite"), favorites[i]);
            }
      settings.endArray();
      settings.sync();
      }

//---------------------------------------------------------
//   loadAssets
//    Load config from assets.json in the AppDataLocation,
//    then load machines and recipes from their individual
//    directories.
//---------------------------------------------------------

void ZCam::loadAssets() {
      // Load config from assets.json (still a single file)
      QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      json legacyAssets;
      if (!dataDir.isEmpty()) {
            QString filePath = QDir(dataDir).filePath("assets.json");
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                  QByteArray data = file.readAll();
                  file.close();
                  try {
                        legacyAssets = json::parse(data.toStdString());
                        if (legacyAssets.contains("config"))
                              _config->fromJson(legacyAssets.at("config"));
                        }
                  catch (json::parse_error& e) {
                        qWarning() << "Failed to parse assets.json:" << e.what();
                        }
                  }
            }

      // If the config did not specify explicit directories, populate
      // the Config properties with the default values so they are visible
      // in the Config Panel and persisted on save.
      if (_config->machinesDirectory().isEmpty())
            _config->set_machinesDirectory(defaultMachinesDirectory());
      if (_config->recipesDirectory().isEmpty())
            _config->set_recipesDirectory(defaultRecipesDirectory());
      if (_config->artworkDirectory().isEmpty())
            _config->set_artworkDirectory(defaultArtworkDirectory());
      if (_config->iconDirectory().isEmpty())
            _config->set_iconDirectory(defaultIconDirectory());

      // Load machines from individual files in the machines directory
      auto md = machinesDirectory();
      _machines->loadFromDirectory(md);

      // Migrate: if the machines directory was empty but the legacy
      // assets.json contained machines, import them and save to the
      // directory so the migration is permanent.
      if (_machines->machinesModel().isEmpty() && legacyAssets.contains("machines")) {
            _machines->fromJson(legacyAssets.at("machines"));
            _machines->saveToDirectory(machinesDirectory());
            }

      // Load recipes from individual files in the recipes directory
      _recipes->loadFromDirectory(recipesDirectory());

      // Migrate: if the recipes directory was empty but the legacy
      // assets.json contained recipes, import them and save to the
      // directory so the migration is permanent.
      if (_recipes->recipeCount() == 0 && legacyAssets.contains("recipes")) {
            _recipes->fromJson(legacyAssets.at("recipes"));
            // Save migrated recipes under the root (no machineType filter)
            // so they are available before a machine is selected.
            _recipes->saveToDirectory(recipesDirectory());
            }
      }

//---------------------------------------------------------
//   dragged
//    Called from QML when an element is dragged in the 3D viewport.
//    Updates the element's position property directly (live update).
//    The undo record is created once at endElementDrag().
//---------------------------------------------------------
//---------------------------------------------------------
//   worldToParentLocal
//    Convert a world (root) space position into the local
//    coordinate system of the element's parent, so it can be
//    assigned to element->pos().
//---------------------------------------------------------

static QVector3D worldToParentLocal(Element3d* element, const QVector3D& worldPos) {
      QVector3D local = worldPos;
      if (auto* p = qobject_cast<Element3d*>(element->parent())) {
            bool ok              = false;
            QMatrix4x4 parentInv = p->globalMatrix().inverted(&ok);
            if (ok)
                  local = parentInv.map(worldPos);
            }
      return local;
      }

void ZCam::logPosition(const char* caller) {
      Element3d* el = _elementDragElement;
      if (!el)
            return;
      QVector3D origin = el->globalMatrix().map(QVector3D(0, 0, 0));
      Debug("{}: element '{}' pos=({:.3f},{:.3f},{:.3f}) worldOrigin=({:.3f},{:.3f},{:.3f}) "
            "snapRef=({:.3f},{:.3f},{:.3f}) snapCursor=({:.3f},{:.3f},{:.3f})",
          caller, el->name(), el->pos().x(), el->pos().y(), el->pos().z(), origin.x(), origin.y(), origin.z(),
          _snapState.refPos.x(), _snapState.refPos.y(), _snapState.refPos.z(), _snapState.cursorPos.x(),
          _snapState.cursorPos.y(), _snapState.cursorPos.z());
      }

void ZCam::dragged(Element3d* element, const QVector3D& delta, int modifiers) {
      if (!element || !element->draggable())
            return;
      // If startElementDrag() was not called before the first drag
      // event (e.g. because a segment was selected on the second
      // click, which skipped startElementDrag in QML), lazily
      // initialize the drag state here.
      if (!_elementDragElement) {
            startElementDrag(element);
            // A drag is now in progress — discard any pending segment
            // selection so the bounding box remains visible throughout
            // the drag and after release.
            _pendingSegmentElement = nullptr;
            // Clear any pre-existing segment selection so the bounding
            // box reappears during the drag.
            auto* poly = qobject_cast<Polygon*>(element);
            if (poly && poly->selectedSegment() >= 0)
                  poly->clearSegmentSelection();
            }
      // The delta from QML is in world (root) space coordinates, but
      // element->pos() is in the parent's local coordinate system.
      // If the parent (e.g. a Layer) has a non-identity scale, the
      // world-space delta must be converted to local space before
      // being applied.  Without this, an element under a layer with
      // scale 0.5 would move at half speed, because the parent's
      // scale also transforms the child's local translation.
      //
      // We use the inverse of the parent's globalMatrix() and
      // mapVector() (which applies only rotation+scale, not
      // translation) to convert the direction delta correctly.
      QVector3D localDelta = delta;
      if (auto* p = qobject_cast<Element3d*>(element->parent())) {
            QMatrix4x4 parentGlobal = p->globalMatrix();
            bool ok                 = false;
            QMatrix4x4 inv          = parentGlobal.inverted(&ok);
            if (ok)
                  localDelta = inv.mapVector(delta);
            }

      // Magnetic grid snap: when the project's Grid has snap enabled,
      // grid lines act magnetically.  The element's reference point is
      // (0,0) in local coords; in world (root) space this is the
      // translation part of the element's globalMatrix().
      //
      // The QML panel delivers the drag as a stream of world-space
      // deltas (differences of consecutive screenToScene() results).
      // We accumulate them into _snapState.cursorPos — the world
      // position the cursor currently points at.  The element then
      // snaps per axis (independently) to the grid line nearest to
      // the cursor:  as long as the snapped line stays within half
      // the minor spacing of the cursor, the element sticks to it;
      // beyond that it snaps to the new nearest line.  This makes
      // the element follow the cursor with at most half a grid cell
      // of lag and with no hysteresis effects (no "skipping every
      // second line", no "running away" from the cursor, and
      // direction reversals work instantly).
      Grid* grid = nullptr;
      if (_project)
            grid = qobject_cast<Grid*>(_project->gridElement());

      // Advance the virtual cursor position that owns the snap.
      // (Seeded with the element origin in startElementDrag(); kept
      // in sync with the cursor by QML via updateDragAnchor() — e.g.
      // after camera pans / rotations mid-drag.)
      //
      // IMPORTANT: detect a modifier change mid-drag (Shift pressed
      // or released) and re-anchor the cursor to the current element
      // position so the element does not jump when the snap mode
      // toggles.  Without this, cursorPos (which has been
      // accumulating deltas all along) would be at a completely
      // different position than the snapped element origin, causing
      // a visible jump.
      bool snapActive = grid && grid->snap();
      if (modifiers & Qt::ShiftModifier)
            snapActive = !snapActive;

      bool curSnap = (modifiers & Qt::ShiftModifier) != 0;
      if (_snapState.lastSnapModifier != curSnap) {
            // Modifier changed — re-anchor cursor to the element's
            // current world position and reset the snap seed.
            _snapState.cursorPos        = element->globalMatrix().map(QVector3D(0, 0, 0));
            _snapState.hasCursorPos     = false;
            _snapState.lastSnapModifier = curSnap;
            }
      _snapState.cursorPos  += delta;
      QVector3D newWorldRef  = _snapState.cursorPos;

      if (snapActive && grid) {
            double spacing = grid->minorSpacing();
            if (spacing > 0.0) {
                  double halfSpacing = spacing / 2.0;

                  // Snap per axis:  candidate line nearest to the
                  // cursor.  On the first snap frame the element
                  // snaps directly to the nearest line; afterwards
                  // it sticks to the previously snapped line until
                  // the cursor is closer to another one (distance
                  // from the current line > half spacing).
                  if (!_snapState.hasCursorPos) {
                        _snapState.lastSnappedX = std::lround(newWorldRef.x() / spacing) * spacing;
                        _snapState.lastSnappedY = std::lround(newWorldRef.y() / spacing) * spacing;
                        _snapState.hasCursorPos = true;
                        }
                  else {
                        if (std::abs(newWorldRef.x() - _snapState.lastSnappedX) > halfSpacing)
                              _snapState.lastSnappedX = std::lround(newWorldRef.x() / spacing) * spacing;
                        if (std::abs(newWorldRef.y() - _snapState.lastSnappedY) > halfSpacing)
                              _snapState.lastSnappedY = std::lround(newWorldRef.y() / spacing) * spacing;
                        }

                  newWorldRef.setX(_snapState.lastSnappedX);
                  newWorldRef.setY(_snapState.lastSnappedY);
                  }
            }

      QVector3D newLocalPos = worldToParentLocal(element, newWorldRef);

      _snapState.refPos = newWorldRef;
      emit snapRefPosChanged();
      element->set_pos(newLocalPos);
      }

//---------------------------------------------------------
//   updateDragAnchor
//    Re-anchor the grid-snap reference to the current cursor
//    position.  Called from QML when the canvas camera pans or
//    rotates while a drag is in progress:  the delta stream from
//    screenToScene() discontinues at a camera jump, so the virtual
//    cursor position must be re-seeded; otherwise the snap logic
//    (and the element) would drift away from the cursor.
//---------------------------------------------------------

void ZCam::updateDragAnchor(Element3d* element, const QVector3D& cursorPos) {
      if (!_snapDragActive || element != _elementDragElement || !element)
            return;

      // Preserve the current offset between the element's (possibly
      // snapped) reference point and the cursor so the element does
      // not jump when the anchor is corrected.
      if (_snapState.hasCursorPos) {
            QVector3D offset     = _snapState.refPos - _snapState.cursorPos;
            _snapState.cursorPos = cursorPos;
            _snapState.refPos    = cursorPos + offset;
            }
      else {
            // No drag frame processed yet — anchor at the element.
            _snapState.cursorPos = element->globalMatrix().map(QVector3D(0, 0, 0));
            _snapState.refPos    = _snapState.cursorPos;
            }
      }

//---------------------------------------------------------
//   rotated
//    Called from QML when an element is rotated in the 3D viewport.
//    Updates the element's rotation property directly (live update).
//    The undo record is created once at endElementDrag().
//---------------------------------------------------------

void ZCam::rotated(Element3d* element, const QVector3D& deltaRotation, int modifiers) {
      if (!element || !element->draggable())
            return;
      // Lazily initialize drag state if startElementDrag() was skipped.
      if (!_elementDragElement)
            startElementDrag(element);
      QVector3D newRot = element->rot() + deltaRotation;
      element->set_rot(newRot);
      }

//---------------------------------------------------------
//   scaled
//    Called from QML when an element is scaled in the 3D viewport.
//    Updates the element's scale property directly (live update).
//    The undo record is created once at endElementDrag().
//---------------------------------------------------------

void ZCam::scaled(Element3d* element, const QVector3D& scaleFactor, int modifiers, const QVector3D& pivot) {
      if (!element || !element->draggable())
            return;
      // Lazily initialize drag state if startElementDrag() was skipped.
      if (!_elementDragElement)
            startElementDrag(element);

      // Compute the element's world-space origin BEFORE the scale
      // change.  The origin (0,0,0) in local coords maps to the
      // translation part of globalMatrix(); it does not depend on
      // the current scale value.
      QVector3D worldOrigin = element->globalMatrix().map(QVector3D(0.0f, 0.0f, 0.0f));

      // Use set_scaleAR() instead of set_scale() so that the lockScale
      // enforcement (Off / Lock / Square) is applied.  set_scale() bypasses
      // the lock constraints, which would allow non-uniform scaling even
      // when lockScale is set to Square or Lock.
      QVector3D cur = element->scale();
      QVector3D newScale(cur.x() * scaleFactor.x(), cur.y() * scaleFactor.y(), cur.z() * scaleFactor.z());
      element->set_scaleAR(newScale);

      // Determine the effective uniform scale factor.  set_scaleAR()
      // may adjust the requested scale when lockScale is Square or
      // Lock, so we compute the actual ratio from the resulting
      // scale values.
      QVector3D actualNew = element->scale();
      float sd            = (cur.x() != 0.0f) ? actualNew.x() / cur.x() : 1.0f;

      // To scale around the pivot point (in world coords), the
      // element's world-space origin must move so that the pivot
      // stays fixed.  The required world-space displacement is:
      //    worldDelta = (1 - sd) * (pivot - worldOrigin)
      // This is analogous to how the canvas zoom keeps the cursor
      // position fixed: root.position += cursorScenePos - root.position) * (1 - sd).
      QVector3D worldDelta = (1.0f - sd) * (pivot - worldOrigin);

      // Convert the world-space delta to the parent's local
      // coordinate system, analogous to dragged() and
      // centerOnWorkspace().  mapVector() applies only
      // rotation+scale, not translation, which is correct for
      // direction vectors.
      QVector3D localDelta = worldDelta;
      if (auto* p = qobject_cast<Element3d*>(element->parent())) {
            QMatrix4x4 parentGlobal = p->globalMatrix();
            bool ok                 = false;
            QMatrix4x4 inv          = parentGlobal.inverted(&ok);
            if (ok)
                  localDelta = inv.mapVector(worldDelta);
            }

      element->beginBatchUpdate();
      element->set_pos(element->pos() + localDelta);
      element->endBatchUpdate();
      // The pivot-scale changes the element's world origin, so update
      // the snap reference-point marker as well.  The virtual cursor
      // position follows the element so a subsequent dragged() call
      // does not continue from the stale pre-scale location.
      if (_snapDragActive) {
            _snapState.refPos    = element->globalMatrix().map(QVector3D(0, 0, 0));
            _snapState.cursorPos = _snapState.refPos;
            emit snapRefPosChanged();
            }
      }

//---------------------------------------------------------
//   startElementDrag
//    Called from QML when the user starts dragging/rotating/scaling
//    an element in the 3D viewport.  Records the original transform
//    values (pos, rot, scale) so that endElementDrag() can create
//    a single undo command for the entire drag operation.
//---------------------------------------------------------

void ZCam::startElementDrag(Element3d* element) {
      if (!element || !element->draggable())
            return;
      _elementDragElement   = element;
      _elementDragOrigPos   = element->pos();
      _elementDragOrigRot   = element->rot();
      _elementDragOrigScale = element->scale();
      // Reset snap state from any previous drag so the magnetic-snap
      // logic starts clean.  Seed the virtual cursor position with
      // the live world position of the element origin so the element
      // does not jump on the first drag delta (cursorPos must start
      // AT the element, not at the world origin).
      _snapState           = {};
      _snapState.cursorPos = element->globalMatrix().map(QVector3D(0, 0, 0));
      _snapState.refPos    = _snapState.cursorPos;
      _snapDragActive      = true;
      emit snapDragActiveChanged();
      emit snapRefPosChanged();
      logPosition("startElementDrag");
      _project->undo()->beginMacro();
      }

//---------------------------------------------------------
//   endElementDrag
//    Called from QML when the user finishes dragging/rotating/scaling
//    an element.  Creates and pushes a single undo command that
//    captures all three transform properties (pos, rot, scale) in
//    one atomic operation.
//---------------------------------------------------------

void ZCam::endElementDrag() {
      // Helper lambda: apply the pending segment selection if any.
      // Called when no actual drag movement occurred (pure click).
      auto applyPendingSegment = [this]() {
            if (!_pendingSegmentElement)
                  return;
            auto* poly = qobject_cast<Polygon*>(_pendingSegmentElement);
            if (poly) {
                  if (_pendingSegmentToggleOff)
                        poly->clearSegmentSelection();
                  else {
                        int nearest = poly->findNearestSegment(_pendingSegmentClickPos);
                        if (nearest >= 0)
                              poly->setSelectedSegment(nearest);
                        }
                  }
            _pendingSegmentElement = nullptr;
            };

      if (!_elementDragElement) {
            // startElementDrag() was never called — apply pending
            // segment selection if any (e.g. click on already-selected
            // polygon without any movement).
            applyPendingSegment();
            return;
            }

      Element3d* el      = _elementDragElement;
      QVector3D newPos   = el->pos();
      QVector3D newRot   = el->rot();
      QVector3D newScale = el->scale();

      // Only create an undo command if something actually changed
      bool changed = (newPos - _elementDragOrigPos).length() > 0.001 ||
                     (newRot - _elementDragOrigRot).length() > 0.001 ||
                     (newScale - _elementDragOrigScale).length() > 0.001;

      if (!changed) {
            // No movement — this is a pure click.  Apply pending
            // segment selection (e.g. clicking on an already-selected
            // polygon to select a segment).
            //
            // CRITICAL: endMacro() must be called to balance the
            // beginMacro() from startElementDrag().  Without this,
            // curCmd remains active and the next beginMacro() fails
            // with Fatal("already active"), corrupting all subsequent
            // undo operations.
            applyPendingSegment();
            _elementDragElement = nullptr;
            _snapDragActive     = false;
            emit snapDragActiveChanged();
            // Roll back the empty macro (0 children → endMacro will
            // delete it instead of pushing it onto the list).
            _project->undo()->endMacro(true);
            return;
            }

      // A drag occurred — discard any pending segment selection so the
      // bounding box stays visible after the drag ends.
      _pendingSegmentElement = nullptr;

      // Use a single PropertyChangeCommand for pos; the undo command
      // records the old/new pos.  Rot and scale are captured in
      // additional commands pushed together.
      //
      // We push up to three commands atomically.  They are undone
      // and redone in reverse/forward order as a group.

      UndoCommand* cmd;
      cmd = new PropertyChangeCommand(
          this, el, "pos", QVariant::fromValue(_elementDragOrigPos), QVariant::fromValue(newPos));
      _project->undo()->push(cmd);
      cmd = new PropertyChangeCommand(
          this, el, "rot", QVariant::fromValue(_elementDragOrigRot), QVariant::fromValue(newRot));
      _project->undo()->push(cmd);
      cmd = new PropertyChangeCommand(
          this, el, "scale", QVariant::fromValue(_elementDragOrigScale), QVariant::fromValue(newScale));
      _project->undo()->push(cmd);

      _elementDragElement = nullptr;
      _snapDragActive     = false;
      emit snapDragActiveChanged();

      _project->undo()->endMacro();
      emit elementDragEnded();
      }

//---------------------------------------------------------
//   snapRefPos
//    World position used to place the snap reference-point cross.
//    _snapState.refPos is kept in sync with the element origin in
//    every drag frame (both snap and non-snap paths in dragged()),
//    so the QML marker binding stays up to date.
//---------------------------------------------------------

QVector3D ZCam::snapRefPos() const {
      return _snapState.refPos;
      }

//---------------------------------------------------------
//   saveAssets
//    Save config to assets.json and machines/recipes to their
//    individual directories.
//---------------------------------------------------------

void ZCam::saveAssets() {
      // Save config to assets.json (still a single file)
      QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      if (!dataDir.isEmpty()) {
            QDir dir(dataDir);
            if (!dir.exists())
                  dir.mkpath(".");

            QString filePath = dir.filePath("assets.json");
            QFile file(filePath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                  json j;
                  j["config"] = _config->toJson();
                  QTextStream out(&file);
                  out << QString::fromStdString(j.dump(4));
                  file.close();
                  }
            }

      // Save machines to individual files
      _machines->saveToDirectory(machinesDirectory());

      // Save recipes to individual files
      _recipes->saveToDirectory(recipesDirectory(), _recipes->machineType());

      emit assetsSaved();
      }

//---------------------------------------------------------
//   hover
//---------------------------------------------------------

void ZCam::hover(Element3d* element) {
      Element3d* oldElement = hoverElement();
      set_hoverElement(element);
      // if an element changes its hover status, signal
      // a color change
      if (oldElement != element) {
            if (element)
                  emit element->curColorChanged();
            if (oldElement)
                  emit oldElement->curColorChanged();
            }
      }

//---------------------------------------------------------
//   invokeElementMethod
//    Invoke a Q_INVOKABLE method on an Element3d through its
//    DYNAMIC meta-object.  QML sees Element3d pointers with their
//    STATIC type only, so methods declared in derived classes
//    (Nest::nest, Polygon::optimize, ...) come through as
//    `undefined` and a guarded call like  if (el && el.nest)
//    el.nest();  silently does nothing.  This invoker dispatches by
//    method name on the runtime type (metaObject() is virtual) and
//    passes the arguments from left to right.
//    Returns false when the element or the method does not exist.
//---------------------------------------------------------

bool ZCam::invokeElementMethod(Element3d* element, const QString& method, const QVariantList& args) {
      if (!element)
            return false;
      const QMetaObject* mo = element->metaObject();
      // indexOfMethod() requires the NORMALIZED signature, i.e. the
      // parameter list in parentheses ("nest()", not "nest").
      QByteArray sig = method.toUtf8();
      if (!sig.contains('('))
            sig += "()";
      int idx = mo->indexOfMethod(sig.constData());
      if (idx < 0) {
            Warning("invokeElementMethod: {} has no method {}", element->name().toUtf8().constData(),
                method.toUtf8().constData());
            return false;
            }
      QMetaMethod m = mo->method(idx);

      const int paramCount = m.parameterCount();
      if (args.size() < paramCount) {
            Warning("invokeElementMethod: {} expects {} argument(s), {} given", method.toUtf8().constData(),
                paramCount, args.size());
            return false;
            }

      // Convert every argument to the parameter's meta type.  The
      // converted variants must outlive the invoke() call because
      // QGenericArgument holds only a pointer to the data.
      std::vector<QVariant> converted;
      converted.reserve(static_cast<size_t>(paramCount));
      for (int i = 0; i < paramCount; ++i) {
            QVariant v = args[static_cast<int>(i)];
            v.convert(m.parameterMetaType(i));
            converted.push_back(std::move(v));
            }
      auto argAt = [&](int i) -> QGenericArgument {
            // Already converted above; the data pointer stays valid
            // because `converted` outlives the invoke call.
            const void* data = converted[static_cast<size_t>(i)].constData();
            return QGenericArgument(converted[static_cast<size_t>(i)].typeName(), data);
            };

      bool result = false;
      bool ok     = false;
      // QMetaMethod::invoke supports up to 10 QGenericArgument slots.
      // Only the first paramCount are filled; methods taking a bool
      // return value pass it through (Nest::nest returns bool).
      switch (paramCount) {
            case 0:
                  if (m.returnMetaType() == QMetaType::fromType<bool>())
                        ok = m.invoke(element, Q_RETURN_ARG(bool, result));
                  else
                        ok = m.invoke(element);
                  break;
            case 1: ok = m.invoke(element, argAt(0)); break;
            case 2: ok = m.invoke(element, argAt(0), argAt(1)); break;
            case 3: ok = m.invoke(element, argAt(0), argAt(1), argAt(2)); break;
            default:
                  Warning("invokeElementMethod: {} — more than 3 arguments not supported",
                      method.toUtf8().constData());
                  return false;
            }
      if (!ok) {
            Warning("invokeElementMethod: invoking {} on {} failed", method.toUtf8().constData(),
                element->name().toUtf8().constData());
            return false;
            }
      return true;
      }

//---------------------------------------------------------
//   mopColor
//    Return the Mop colour for the given element when it is a
//    Mop (LaserMop/NopMop), otherwise the transparent colour.
//    QML sees Element pointers with their STATIC type, so the
//    Mop-specific mopColor() method is invisible on the JS
//    wrapper.  The Project Tree delegate uses this helper to
//    draw the small coloured circle in front of a LaserMop's
//    name.  NopMop is excluded — it is the default no-op Mop
//    on Cad and is not shown in the tree.
//---------------------------------------------------------

QColor ZCam::mopColor(const Element* el) {
      const auto* mop = dynamic_cast<const Mop*>(el);
      if (mop && const_cast<Mop*>(mop)->typeName() == QStringLiteral("laserMop"))
            return mop->mopColor();
      return QColor(Qt::transparent);
      }

//---------------------------------------------------------
//   collectPickCandidates
//    Depth-first traversal collecting all Element3d whose world
//    bounding box contains the point (x, y).  A candidate must be
//    visible on the 3D canvas: show, ancestorsShow, selectable and
//    visible() (has an on-canvas representation).  Non-rendered
//    containers (Project, Fixture, ...) are excluded.  Each
//    candidate stores its tree depth so that, when several
//    elements have the same bounding-box area (a container whose
//    bounding box is derived from its single child), the deepest
//    element — the actual shape — wins over its ancestors.
//---------------------------------------------------------

static void collectPickCandidates(Element* root, double x, double y, int depth,
    std::vector<std::tuple<double, int, Element3d*>>& candidates) {
      if (!root)
            return;
      auto* e3d = qobject_cast<Element3d*>(root);
      if (e3d && e3d->show() && e3d->ancestorsShow() && e3d->selectable() && e3d->visible()) {
            QRectF wb = e3d->worldBoundingBox();
            if (!wb.isNull() && !wb.isEmpty()) {
                  if (x >= wb.left() && x <= wb.right() && y >= wb.top() && y <= wb.bottom()) {
                        double area = wb.width() * wb.height();
                        candidates.emplace_back(area, depth, e3d);
                        }
                  else {
                        // Element not hit. If this element is a pure
                        // container (no own path data, bounding box is
                        // derived from children) and its box doesn't
                        // contain the point, none of its children can
                        // contain it either — skip the entire subtree.
                        if (e3d->isPureContainer())
                              return;
                        }
                  }
            }
      else if (e3d && (!e3d->show() || !e3d->ancestorsShow())) {
            // Hidden element: skip entire subtree (children are also hidden).
            return;
            }
      for (auto* child : root->children())
            collectPickCandidates(child, x, y, depth + 1, candidates);
      }

//---------------------------------------------------------
//   pickElement
//    Return the innermost candidate (smallest bounding-box area).
//    On equal areas the deepest tree node wins, then selection
//    cycling: clicking again on the already-selected element
//    cycles to the next-larger candidate (usually its parent
//    group), wrapping around at the outermost one.
//---------------------------------------------------------

Element3d* ZCam::pickElement(double x, double y) {
      std::vector<std::tuple<double, int, Element3d*>> candidates;
      collectPickCandidates(_rootElement, x, y, 0, candidates);
      if (candidates.empty())
            return nullptr;
      // Sort by area ascending (innermost first).  On equal areas
      // the deeper tree node (larger depth) comes first so the
      // innermost shape wins over its ancestor containers.
      std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            if (std::get<0>(a) != std::get<0>(b))
                  return std::get<0>(a) < std::get<0>(b);
            return std::get<1>(a) > std::get<1>(b);
            });
      if (_currentElement) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                  if (std::get<2>(candidates[i]) == _currentElement) {
                        if (i + 1 < candidates.size())
                              return std::get<2>(candidates[i + 1]);
                        return std::get<2>(candidates[0]);
                        }
                  }
            }
      return std::get<2>(candidates[0]);
      }

//---------------------------------------------------------
//   collectRayPickCandidates
//    Depth-first traversal collecting every visible, selectable
//    Element3d whose world 3D bounding box is intersected by the
//    pick ray, using the slab method.  The hit parameter t
//    (distance along the ray) is stored with each candidate.
//---------------------------------------------------------

static void collectRayPickCandidates(Element* root, const QVector3D& origin, const QVector3D& dir, int depth,
    std::vector<std::tuple<float, int, Element3d*>>& candidates) {
      if (!root)
            return;
      auto* e3d = qobject_cast<Element3d*>(root);
      if (e3d && e3d->show() && e3d->ancestorsShow() && e3d->selectable() && e3d->visible()) {
            QVector3D bMin, bMax;
            e3d->worldBoundingBox3D(bMin, bMax);
            if (bMin.x() <= bMax.x()) {
                  // Slab test: intersect the ray with the three axis
                  // planes pairs; the box is hit iff the largest entry
                  // parameter does not exceed the smallest exit one.
                  float tNear = std::numeric_limits<float>::lowest();
                  float tFar  = std::numeric_limits<float>::max();
                  bool hit    = true;
                  for (int axis = 0; axis < 3; ++axis) {
                        float o = origin[axis], d = dir[axis];
                        float lo = bMin[axis], hi = bMax[axis];
                        if (qFuzzyIsNull(d)) {
                              if (o < lo || o > hi) {
                                    hit = false;
                                    break;
                                    }
                              continue;
                              }
                        float t0 = (lo - o) / d;
                        float t1 = (hi - o) / d;
                        if (t0 > t1)
                              std::swap(t0, t1);
                        tNear = std::max(tNear, t0);
                        tFar  = std::min(tFar, t1);
                        if (tNear > tFar) {
                              hit = false;
                              break;
                              }
                        }
                  if (hit && tFar >= 0.0f)
                        candidates.emplace_back(std::max(tNear, 0.0f), depth, e3d);
                  else if (e3d->isPureContainer()) {
                        // Container not hit by ray — skip entire subtree.
                        return;
                        }
                  }
            }
      else if (e3d && (!e3d->show() || !e3d->ancestorsShow())) {
            // Hidden element: skip entire subtree.
            return;
            }
      for (auto* child : root->children())
            collectRayPickCandidates(child, origin, dir, depth + 1, candidates);
      }

//---------------------------------------------------------
//   debugRayPick
//    Diagnosis helper: log all elements along the pick ray —
//    which are skipped by the visibility gate, which boxes are
//    hit and at what ray parameter.
//---------------------------------------------------------

void ZCam::debugRayPick(const QVector3D& origin, const QVector3D& dir) {
      std::function<void(Element*, int)> walk = [&](Element* e, int depth) {
            if (!e)
                  return;
            if (auto* e3d = qobject_cast<Element3d*>(e)) {
                  QVector3D bMin, bMax;
                  e3d->worldBoundingBox3D(bMin, bMax);
                  Debug(
                      "  [{}] vis={}/{} sel={} show={}/{} box=({:.1f},{:.1f},{:.1f})..({:.1f},{:.1f},{:.1f})",
                      depth, e3d->visible(), e3d->draggable(), e3d->selectable(), e3d->show(),
                      e3d->ancestorsShow(), bMin.x(), bMin.y(), bMin.z(), bMax.x(), bMax.y(), bMax.z());
                  }
            for (auto* c : e->children())
                  walk(c, depth + 1);
            };
      Debug("debugRayPick: origin=({:.1f},{:.1f},{:.1f}) dir=({:.3f},{:.3f},{:.3f})", origin.x(), origin.y(),
          origin.z(), dir.x(), dir.y(), dir.z());
      walk(_rootElement, 0);
      std::vector<std::tuple<float, int, Element3d*>> candidates;
      collectRayPickCandidates(_rootElement, origin, dir.normalized(), 0, candidates);
      std::sort(candidates.begin(), candidates.end(),
          [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
      Debug("  {} candidate(s):", candidates.size());
      for (const auto& [t, depth, el] : candidates)
            Debug("    t={:.1f} depth={} name={}", t, depth, el->name().toUtf8().constData());
      }

//---------------------------------------------------------
//   pickAt
//    Convenience helper for QML: unproject the viewport point
//    (x, y in pixels) through the view's camera and root node
//    and pick with the resulting ray.  The unprojection uses
//    the official QQuick3D C++ API so the pick ray is the same
//    ray the internal picker would use — including the y-flip
//    between View3D viewport coordinates and scene coordinates.
//---------------------------------------------------------

Element3d* ZCam::pickAt(QObject* view3d, QObject* rootNode, double x, double y) {
      auto* view = qobject_cast<QQuick3DViewport*>(view3d);
      auto* root = qobject_cast<QQuick3DNode*>(rootNode);
      if (!view || !root)
            return nullptr;
      auto* cam = view->camera();
      if (!cam || view->width() <= 0.0 || view->height() <= 0.0)
            return nullptr;
      const float nx          = float(x / view->width());
      const float ny          = float(y / view->height());
      const QVector3D nearPos = root->mapPositionFromScene(cam->mapFromViewport(QVector3D(nx, ny, 0.0f)));
      const QVector3D farPos  = root->mapPositionFromScene(cam->mapFromViewport(QVector3D(nx, ny, 1.0f)));
      QVector3D dir           = farPos - nearPos;
      if (dir.isNull())
            return nullptr;
      dir.normalize();

      // Pick in the QML root node's local coordinate space: the ray
      // (nearPos/dir) already lives there.  The element C++ hierarchy
      // starts at Cad, so globalMatrix() misses the QML root node's
      // own transform (scale / rotation / position) — collect the
      // candidates' globalMatrix boxes and pre-multiply the QML root
      // scene transform so both sides share the same coordinate frame.
      const QMatrix4x4 qmlRoot = root->sceneTransform();

      std::vector<std::tuple<float, int, Element3d*>> candidates;
      std::function<void(Element*, int)> walk = [&](Element* e, int depth) {
            if (!e)
                  return;
            if (auto* e3d = qobject_cast<Element3d*>(e)) {
                  if (e3d->show() && e3d->ancestorsShow() && e3d->selectable() && e3d->visible()) {
                        QVector3D lMin, lMax;
                        e3d->boundingBox3D(lMin, lMax);
                        if (lMin.x() <= lMax.x()) {
                              // 8 local corners through (qmlRoot * globalMatrix).
                              QMatrix4x4 m = qmlRoot * e3d->globalMatrix();
                              QVector3D bMin(std::numeric_limits<float>::max(),
                                  std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
                              QVector3D bMax(std::numeric_limits<float>::lowest(),
                                  std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
                              for (int i = 0; i < 8; ++i) {
                                    QVector3D c = m.map(QVector3D((i & 1) ? lMax.x() : lMin.x(),
                                        (i & 2) ? lMax.y() : lMin.y(), (i & 4) ? lMax.z() : lMin.z()));
                                    bMin.setX(std::min(bMin.x(), c.x()));
                                    bMin.setY(std::min(bMin.y(), c.y()));
                                    bMin.setZ(std::min(bMin.z(), c.z()));
                                    bMax.setX(std::max(bMax.x(), c.x()));
                                    bMax.setY(std::max(bMax.y(), c.y()));
                                    bMax.setZ(std::max(bMax.z(), c.z()));
                                    }
                              // Slab test.
                              float tNear = std::numeric_limits<float>::lowest();
                              float tFar  = std::numeric_limits<float>::max();
                              bool hit    = true;
                              for (int axis = 0; axis < 3; ++axis) {
                                    float o = nearPos[axis], d = dir[axis];
                                    float lo = bMin[axis], hi = bMax[axis];
                                    if (qFuzzyIsNull(d)) {
                                          if (o < lo || o > hi) {
                                                hit = false;
                                                break;
                                                }
                                          continue;
                                          }
                                    float t0 = (lo - o) / d;
                                    float t1 = (hi - o) / d;
                                    if (t0 > t1)
                                          std::swap(t0, t1);
                                    tNear = std::max(tNear, t0);
                                    tFar  = std::min(tFar, t1);
                                    if (tNear > tFar) {
                                          hit = false;
                                          break;
                                          }
                                    }
                              if (hit && tFar >= 0.0f)
                                    candidates.emplace_back(std::max(tNear, 0.0f), depth, e3d);
                              else if (e3d->isPureContainer())
                                    return; // skip subtree
                              }
                        }
                  }
            else if (e3d && (!e3d->show() || !e3d->ancestorsShow()))
                  return; // hidden subtree
            for (auto* c : e->children())
                  walk(c, depth + 1);
            };
      walk(_rootElement, 0);
      if (candidates.empty())
            return nullptr;
      std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            if (std::get<0>(a) != std::get<0>(b))
                  return std::get<0>(a) < std::get<0>(b);
            return std::get<1>(a) > std::get<1>(b);
            });
      if (_currentElement) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                  if (std::get<2>(candidates[i]) == _currentElement) {
                        if (i + 1 < candidates.size())
                              return std::get<2>(candidates[i + 1]);
                        return std::get<2>(candidates[0]);
                        }
                  }
            }
      return std::get<2>(candidates[0]);
      }

//---------------------------------------------------------
//   pickElementAtRay
//    Return the candidate hit closest to the ray origin; on equal
//    distances the deepest tree node wins so innermost shapes are
//    preferred over their ancestor containers.  Selection cycling
//    behaves like in pickElement(): clicking again on the already
//    selected element returns the next-farther candidate.
//---------------------------------------------------------

Element3d* ZCam::pickElementAtRay(const QVector3D& origin, const QVector3D& dir) {
      if (dir.isNull())
            return nullptr;
      std::vector<std::tuple<float, int, Element3d*>> candidates;
      collectRayPickCandidates(_rootElement, origin, dir.normalized(), 0, candidates);
      if (candidates.empty())
            return nullptr;
      std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            if (std::get<0>(a) != std::get<0>(b))
                  return std::get<0>(a) < std::get<0>(b);
            return std::get<1>(a) > std::get<1>(b);
            });
      if (_currentElement) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                  if (std::get<2>(candidates[i]) == _currentElement) {
                        if (i + 1 < candidates.size())
                              return std::get<2>(candidates[i + 1]);
                        return std::get<2>(candidates[0]);
                        }
                  }
            }
      return std::get<2>(candidates[0]);
      }

//---------------------------------------------------------
//   pickDragTarget
//    Picking helper used when the user starts a left-button drag.
//
//    Priority order:
//      [1] Lasso multi-selection: if the click falls inside any
//          selected element's bounding box, return the smallest
//          such element directly so it can be dragged without
//          pickElement() cycling to the parent (which would
//          deselect the lasso selection via setCurrentElement).
//
//      [2] Container (has children) inside its bbox → drag the
//          whole container.  Essential for Groups: a Group has
//          no own pickable geometry, so the ray-based pick
//          would return a child (deselecting the Group) or
//          nothing (empty space → deselect the Group).
//
//      [3] Ray-based element pick (pickElement) — the normal
//          selection path for plain shapes and overlapping
//          geometry.
//
//      [4] Bbox fallback: if the ray-based pick returned nothing
//          but the click point lies inside the current element's
//          world bounding box, the bounding box acts as a drag
//          handle.  This makes it possible to drag a selected
//          plain shape (Rectangle, Ellipse, Polygon, …) by
//          clicking anywhere inside the yellow selection box,
//          even if no pickable geometry was hit exactly.
//---------------------------------------------------------

Element3d* ZCam::pickDragTarget(double x, double y) {
      // [1] Lasso multi-selection: return the smallest selected
      //     element whose bbox contains the click point.
      if (!_selectedElements.isEmpty()) {
            Element3d* best = nullptr;
            double bestArea = 0.0;
            for (auto* el : _selectedElements) {
                  if (!el || !el->draggable() || !el->show() || !el->ancestorsShow())
                        continue;
                  QRectF wb = el->worldBoundingBox();
                  if (!wb.isNull() && !wb.isEmpty() && x >= wb.left() && x <= wb.right() && y >= wb.top() &&
                      y <= wb.bottom()) {
                        double area = wb.width() * wb.height();
                        if (!best || area < bestArea) {
                              bestArea = area;
                              best     = el;
                              }
                        }
                  }
            if (best)
                  return best;
            }
      // [2] Container with children: bbox is a drag handle.
      if (_currentElement && _currentElement->draggable() && _currentElement->show() &&
          _currentElement->ancestorsShow()) {
            bool hasChildren = false;
            for (auto* c : _currentElement->children()) {
                  if (qobject_cast<Element3d*>(c)) {
                        hasChildren = true;
                        break;
                        }
                  }
            if (hasChildren && _currentElement->containsWorldPoint(x, y))
                  return _currentElement;
            }
      // [3] Normal ray-based element pick (also the correct path
      //     for plain shapes and lasso multi-selection).
      Element3d* el = pickElement(x, y);
      if (el)
            return el;
      // [4] Bbox fallback: the click missed all pickable geometry
      //     but falls inside the current element's world bounding
      //     box — the bounding box acts as a drag handle.
      if (_currentElement && _currentElement->draggable() && _currentElement->show() &&
          _currentElement->ancestorsShow() && _currentElement->containsWorldPoint(x, y))
            return _currentElement;
      return nullptr;
      }

//---------------------------------------------------------
//   pointInPolygon
//    Ray-casting point-in-polygon test.  Returns true if the
//    point (x, y) lies inside the polygon defined by the given
//    list of world-space vertices.
//---------------------------------------------------------

static bool pointInPolygon(double x, double y, const QList<QVector3D>& polygon) {
      int n = polygon.size();
      if (n < 3)
            return false;
      bool inside = false;
      for (int i = 0, j = n - 1; i < n; j = i++) {
            double xi = polygon[i].x(), yi = polygon[i].y();
            double xj = polygon[j].x(), yj = polygon[j].y();
            if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi))
                  inside = !inside;
            }
      return inside;
      }

//---------------------------------------------------------
//   collectLassoCandidates
//    Depth-first traversal collecting all Element3d whose world
//    bounding-box center lies inside the given polygon.  Only
//    visible, selectable elements with a non-empty bounding box
//    are considered.  Groups are included as individual candidates
//    so a lasso can select a whole group at once.
//---------------------------------------------------------

static void collectLassoCandidates(
    Element* root, const QList<QVector3D>& polygon, QList<Element3d*>& candidates) {
      if (!root)
            return;
      auto* e3d = qobject_cast<Element3d*>(root);
      if (e3d && e3d->show() && e3d->ancestorsShow() && e3d->selectable() && e3d->visible()) {
            QRectF wb = e3d->worldBoundingBox();
            if (!wb.isNull() && !wb.isEmpty()) {
                  double cx   = wb.center().x();
                  double cy   = wb.center().y();
                  bool inside = pointInPolygon(cx, cy, polygon);
                  if (inside)
                        candidates.append(e3d);
                  }
            }
      for (auto* child : root->children())
            collectLassoCandidates(child, polygon, candidates);
      }

//---------------------------------------------------------
//   collectLassoSegments
//    Depth-first traversal that applies lasso segment selection to
//    every visible, selectable Polygon that was NOT already selected as
//    a whole element (i.e. not in `elementSelected`).  For each such
//    polygon, all segments whose world-space midpoint lies inside the
//    lasso polygon are selected and the polygon is appended to `result`.
//    Polygons selected as whole elements have their segment selection
//    cleared so the bounding box (drag mode) is shown, not segments.
//    Polygons with no midpoint inside the lasso are cleared, so a lasso
//    drag fully replaces the previous segment-selection state.
//---------------------------------------------------------

static void collectLassoSegments(Element* root, const QList<QVector3D>& lasso,
    const QList<Element3d*>& elementSelected, QList<Element3d*>& result) {
      if (!root)
            return;
      auto* e3d = qobject_cast<Element3d*>(root);
      if (e3d && e3d->show() && e3d->ancestorsShow() && e3d->selectable() && e3d->visible()) {
            auto* polyObj = qobject_cast<Polygon*>(e3d);
            if (polyObj && !polyObj->isDrawing()) {
                  if (elementSelected.contains(e3d)) {
                        // Selected as a whole element — clear any segment
                        // selection so the bounding box / drag mode applies.
                        polyObj->clearSegmentSelection();
                        }
                  else {
                        int n = polyObj->segmentCount();
                        QList<int> inside;
                        for (int i = 0; i < n; ++i) {
                              QVector3D mid = polyObj->segmentMidpoint(i);
                              if (pointInPolygon(mid.x(), mid.y(), lasso))
                                    inside.append(i);
                              }
                        polyObj->setLassoSelectedSegments(inside);
                        if (!inside.isEmpty() && !result.contains(e3d))
                              result.append(e3d);
                        }
                  }
            }
      for (auto* child : root->children())
            collectLassoSegments(child, lasso, elementSelected, result);
      }

//---------------------------------------------------------
//   lassoSelect
//    Select all visible, selectable elements whose world
//    bounding-box center lies inside the given polygon (in
//    world/root coordinates).  The first element becomes the
//    currentElement.  Clears any previous lasso selection.
//---------------------------------------------------------

void ZCam::lassoSelect(const QList<QVector3D>& polygon) {
      QList<Element3d*> old = _selectedElements;
      _selectedElements.clear();

      if (polygon.size() < 3) {
            if (!old.isEmpty()) {
                  for (auto* e : old)
                        emit e->curColorChanged();
                  emit selectedElementsChanged();
                  }
            return;
            }

      collectLassoCandidates(_rootElement, polygon, _selectedElements);

      // Remove ancestor elements whose children are already in the
      // selection: when a Group's bounding-box center lies inside the
      // lasso polygon alongside some of its children, the Group itself
      // should not be selected — the user wants the individual children,
      // not the container.
      for (int i = _selectedElements.size() - 1; i >= 0; --i) {
            Element3d* el   = _selectedElements[i];
            bool isAncestor = false;
            for (Element3d* other : _selectedElements) {
                  if (other == el)
                        continue;
                  // Walk up other's parent chain; if we encounter el,
                  // then el is an ancestor of other and should be removed.
                  Element* p = other->parent();
                  while (p) {
                        if (p == el) {
                              isAncestor = true;
                              break;
                              }
                        p = p->parent();
                        }
                  if (isAncestor)
                        break;
                  }
            if (isAncestor)
                  _selectedElements.removeAt(i);
            }

      // Polygon segment selection: walk the whole element tree and, for
      // every visible, selectable Polygon, select all segments whose
      // world-space midpoint lies inside the lasso polygon.  This lets a
      // lasso drag pick individual line/bezier edges of a polygon — even
      // when the polygon's bounding-box center (used for element selection)
      // is outside the lasso.  Any polygon that ends up with one or more
      // selected segments is also added to the multi-selection so it
      // becomes the current element.
      QList<Element3d*> segmentPols;
      collectLassoSegments(_rootElement, polygon, _selectedElements, segmentPols);

      // Ensure every polygon with lasso-selected segments is part of the
      // multi-selection so the first one becomes the current element and
      // the segment handles are shown.
      for (Element3d* el : segmentPols)
            if (!_selectedElements.contains(el))
                  _selectedElements.append(el);

      // Emit curColorChanged for all elements that changed selection state.
      for (auto* e : old)
            if (!_selectedElements.contains(e))
                  emit e->curColorChanged();
      for (auto* e : _selectedElements)
            if (!old.contains(e))
                  emit e->curColorChanged();

      if (!_selectedElements.isEmpty())
            setCurrentElement(_selectedElements.first());
      else
            setCurrentElement(nullptr);

      emit selectedElementsChanged();
      }

//---------------------------------------------------------
//   clearSelection
//    Clear the multi-selection list and reset currentElement.
//    This is called from QML (e.g. on Escape) to deselect
//    everything — both the lasso multi-selection and the
//    current (primary) element.
//---------------------------------------------------------

void ZCam::clearSelection() {
      QList<Element3d*> old = _selectedElements;
      _selectedElements.clear();
      for (auto* e : old)
            emit e->curColorChanged();
      if (!old.isEmpty())
            emit selectedElementsChanged();
      // Clear any lasso-selected segments on the previously selected
      // polygons (not just the current one) so segment editing state does
      // not persist after Escape.
      for (auto* e : old)
            if (auto* poly = qobject_cast<Polygon*>(e))
                  poly->clearSegmentSelection();
      // Also clear the primary current element so Escape truly
      // deselects everything when a multi-selection is active.
      setCurrentElement(nullptr);
      }

//--------------------------------------------------------------------
//   clearSelectionList
//    Clear only the multi-selection list without changing currentElement.
//    Used when switching to a new single selection (e.g. plain click in
//    the TreeView) to avoid an intermediate null state that would cause
//    the InspectorModel to clear its data while delegates are still
//    being torn down, leading to null-access runtime errors.
//--------------------------------------------------------------------

void ZCam::clearSelectionList() {
      QList<Element3d*> old = _selectedElements;
      _selectedElements.clear();
      for (auto* e : old)
            emit e->curColorChanged();
      if (!old.isEmpty())
            emit selectedElementsChanged();
      }

//---------------------------------------------------------
//   isSelected
//    Returns true if the given element is in the lasso selection.
//---------------------------------------------------------

bool ZCam::isSelected(const Element3d* el) const {
      for (auto* e : _selectedElements)
            if (e == el)
                  return true;
      return false;
      }

//--------------------------------------------------------------------
//   addToSelection
//    Add an element to the multi-selection list.  The element
//    becomes the current (primary) element shown in the inspector.
//    If the element is already in the list, it just becomes current.
//    Emits selectedElementsChanged and curColorChanged as needed.
//--------------------------------------------------------------------

void ZCam::addToSelection(Element3d* el) {
      if (!el)
            return;
      bool wasInList = _selectedElements.contains(el);
      if (!wasInList) {
            _selectedElements.append(el);
            emit el->curColorChanged();
            emit selectedElementsChanged();
            }
      // Set as current element without clearing the multi-selection.
      // We bypass setCurrentElement() because it clears _selectedElements
      // when the new element is not already in the list (but we just
      // added it, so it is).  Still, calling setCurrentElement() here
      // is safe because el IS in _selectedElements now.
      setCurrentElement(el);
      }

//--------------------------------------------------------------------
//   removeFromSelection
//    Remove an element from the multi-selection list.  If it was
//    the current element, the next remaining element (or nullptr)
//    becomes current.
//--------------------------------------------------------------------

void ZCam::removeFromSelection(Element3d* el) {
      if (!el)
            return;
      if (!_selectedElements.contains(el))
            return;
      _selectedElements.removeOne(el);
      emit el->curColorChanged();
      emit selectedElementsChanged();
      // If the removed element was the current element, pick a new one.
      if (_currentElement == el) {
            if (!_selectedElements.isEmpty())
                  setCurrentElement(_selectedElements.last());
            else
                  setCurrentElement(nullptr);
            }
      }

//--------------------------------------------------------------------
//   toggleSelection
//    Toggle the selection state of an element.  If the element is
//    not yet selected, it is added and becomes current.  If it is
//    already selected, it is removed; if it was current, the next
//    remaining element (or nullptr) becomes current.
//--------------------------------------------------------------------

void ZCam::toggleSelection(Element3d* el) {
      if (!el)
            return;
      if (_selectedElements.contains(el))
            removeFromSelection(el);
      else
            addToSelection(el);
      }

//---------------------------------------------------------
//   mousePress
//---------------------------------------------------------

void ZCam::mousePress(Element3d* element, int buttons, int modifiers, double x, double y) {
      Debug("{} x: {} y: {}", element ? element->name() : "--", x, y);
      // If the same polygon is already selected, defer segment selection
      // to endElementDrag().  This allows click+drag to move the polygon
      // (with bounding box visible) while a pure click (no drag) selects
      // the nearest segment.  Previously, segment selection happened
      // immediately here, which replaced the bounding box with a segment
      // line before the drag started, making the bounding box disappear.
      if (element && element == _currentElement) {
            auto* poly = qobject_cast<Polygon*>(element);
            if (poly) {
                  QVector3D worldPos(x, y, 0.0);
                  int nearest = poly->findNearestSegment(worldPos);
                  if (nearest < 0)
                        return;
                  // Remember the click for later.  If the same segment
                  // is already selected, we will toggle it off.
                  _pendingSegmentElement   = element;
                  _pendingSegmentClickPos  = worldPos;
                  _pendingSegmentToggleOff = (nearest == poly->selectedSegment());
                  // Do NOT select the segment here — let the drag proceed
                  // with the bounding box visible.  The segment will be
                  // selected in endElementDrag() if no drag occurred.
                  return;
                  }
            }
      // Ctrl-click on the 3D canvas toggles multi-selection.
      if (element && (modifiers & Qt::ControlModifier)) {
            toggleSelection(element);
            return;
            }
      setCurrentElement(element);
      }

//---------------------------------------------------------
//   startVertexDrag
//    Called from QML when the user starts dragging a handle.
//    Records the original handle position so that endVertexDrag()
//    can create an undo command with old and new positions.
//---------------------------------------------------------

void ZCam::startVertexDrag(Element3d* element, int vertexIndex) {
      if (!element || vertexIndex < 0 || vertexIndex >= element->vertexCount())
            return;
      _vertexDragElement = element;
      _vertexDragIndex   = vertexIndex;
      // Store original position in LOCAL coordinates for the undo command
      _vertexDragOrigPos = element->vertexPos(vertexIndex);
      // Nest bin-corner handles resize the bin (binSize + pos), so also
      // snapshot the bin state.  endVertexDrag() uses _vertexDragIsNest
      // to pick the matching undo command.
      _vertexDragIsNest = isType<Nest>(element);
      if (_vertexDragIsNest) {
            _vertexDragOrigBinSize = element->property("binSize").value<QVector2D>();
            _vertexDragOrigNestPos = element->pos();
            }
      }

//---------------------------------------------------------
//   dragVertexTo
//    Called from QML during dragging a handle.
//    Sets the handle to the given WORLD position by converting
//    it back to local coordinates via the inverse global matrix.
//---------------------------------------------------------

void ZCam::dragVertexTo(Element3d* element, int vertexIndex, const QVector3D& worldPos) {
      if (!element || vertexIndex < 0 || vertexIndex >= element->vertexCount())
            return;
      // Convert world position to local position
      QMatrix4x4 inv = element->globalMatrix();
      bool ok;
      inv = inv.inverted(&ok);
      if (!ok)
            return;
      QVector3D localPos = inv.map(worldPos);
      element->setVertexPos(vertexIndex, localPos);
      }

//---------------------------------------------------------
//   endVertexDrag
//    Called from QML when the user finishes dragging a handle.
//    Creates and pushes an undo command with the original and final
//    handle positions.
//---------------------------------------------------------

void ZCam::endVertexDrag(Element3d* element, int vertexIndex) {
      if (!element || vertexIndex < 0 || vertexIndex >= element->vertexCount())
            return;
      // Nest bin-corner handles: record a NestBinCommand that restores
      // the complete binSize/pos pair captured at drag start (resizing
      // the bin changes BOTH properties, so a positional HandleDragCommand
      // is not sufficient).
      if (_vertexDragIsNest && isType<Nest>(element)) {
            QVector2D newBinSize = element->property("binSize").value<QVector2D>();
            QVector3D newPos     = element->pos();
            _vertexDragElement   = nullptr;
            _vertexDragIsNest    = false;
            if (qFuzzyCompare(newBinSize.x(), _vertexDragOrigBinSize.x()) &&
                qFuzzyCompare(newBinSize.y(), _vertexDragOrigBinSize.y()) &&
                (newPos - _vertexDragOrigNestPos).length() < 0.001)
                  return; // nothing changed — no undo entry
            if (_project) {
                  _project->undo()->beginMacro();
                  auto cmd = new NestBinCommand(
                      this, element, _vertexDragOrigBinSize, newBinSize, _vertexDragOrigNestPos, newPos);
                  _project->undo()->push(cmd);
                  _project->undo()->endMacro();
                  }
            return;
            }
      QVector3D newPos = element->vertexPos(vertexIndex);
      // Only create an undo command if the handle actually moved
      if (std::abs(newPos.x() - _vertexDragOrigPos.x()) < 0.001 &&
          std::abs(newPos.y() - _vertexDragOrigPos.y()) < 0.001) {
            _vertexDragElement = nullptr;
            return;
            }
      if (_project) {
            _project->undo()->beginMacro();
            auto cmd = new HandleDragCommand(this, element, vertexIndex, _vertexDragOrigPos, newPos);
            _project->undo()->push(cmd);
            _project->undo()->endMacro();
            }
      _vertexDragElement = nullptr;
      }

//---------------------------------------------------------
//   selectSegment
//    Select a segment of a Polygon element by segment index.
//    The segment is highlighted in the 3D viewport and only its
//    endpoint vertices show handles.  Pass -1 to clear.
//---------------------------------------------------------

void ZCam::selectSegment(Element3d* element, int segmentIndex) {
      if (!element)
            return;
      auto* poly = qobject_cast<Polygon*>(element);
      if (!poly)
            return;
      poly->setSelectedSegment(segmentIndex);
      }

//---------------------------------------------------------
//   clearSegmentSelection
//---------------------------------------------------------

void ZCam::clearSegmentSelection(Element3d* element) {
      if (!element)
            return;
      auto* poly = qobject_cast<Polygon*>(element);
      if (!poly)
            return;
      poly->clearSegmentSelection();
      }

//---------------------------------------------------------
//   selectNearestSegment
//    Find and select the segment closest to the given world position.
//    Returns the selected segment index, or -1 on failure.
//---------------------------------------------------------

int ZCam::selectNearestSegment(Element3d* element, const QVector3D& worldPos) {
      if (!element)
            return -1;
      auto* poly = qobject_cast<Polygon*>(element);
      if (!poly)
            return -1;
      int idx = poly->findNearestSegment(worldPos);
      if (idx >= 0)
            poly->setSelectedSegment(idx);
      return idx;
      }

//---------------------------------------------------------
//   collectLayers (static helper)
//    Recursively traverse the element tree and collect all
//    Layer element names.
//---------------------------------------------------------

static void collectLayers(Element* root, QStringList& names) {
      if (!root)
            return;
      if (isType<Group>(root))
            names.append(root->name());
      for (Element* child : root->children())
            collectLayers(child, names);
      }

//---------------------------------------------------------
//   layerNames
//    Collect all Layer element names by traversing the project tree.
//---------------------------------------------------------

QStringList ZCam::layerNames() const {
      QStringList names;
      collectLayers(rootElement(), names);
      return names;
      }

//---------------------------------------------------------
//   layerPtr
//    Return the Layer* for a given name, or nullptr.
//---------------------------------------------------------

Group* ZCam::layerPtr(const QString& name) const {
      Element* e = Element::byName(name);
      if (!e)
            return nullptr;
      return qobject_cast<Group*>(e);
      }

//---------------------------------------------------------
//   collectMops
//    Collect all Mop element names by traversing the project tree.
//---------------------------------------------------------

static void collectMops(Element* root, QStringList& names) {
      if (!root)
            return;
      if (isType<Mop>(root))
            names.append(root->name());
      for (Element* child : root->children())
            collectMops(child, names);
      }

QStringList ZCam::mopNames() const {
      QStringList names;
      collectMops(rootElement(), names);
      return names;
      }

//---------------------------------------------------------
//   mopPtr
//    Return the LaserMop* for a given name, or nullptr.
//---------------------------------------------------------

Mop* ZCam::mopPtr(const QString& name) const {
      Element* e = Element::byName(name);
      if (!e)
            return nullptr;
      return qobject_cast<Mop*>(e);
      }

//---------------------------------------------------------
//   recipeNames
//    Return all recipe names from ZCam::recipes.
//---------------------------------------------------------

QStringList ZCam::recipeNames() const {
      return _recipes->recipeModel();
      }

//---------------------------------------------------------
//   recipePtr
//    Return a pointer to the Recipe with the given name.
//    NOTE: Recipes stores std::vector<Recipe> by value, so we
//    return a pointer into that vector.  The pointer is valid
//    until recipeModelChanged is emitted.
//---------------------------------------------------------

LaserRecipe* ZCam::recipePtr(const QString& name) const {
      for (int i = 0; i < _recipes->recipeCount(); ++i) {
            const LaserRecipe* r = _recipes->recipePtr(i);
            if (r->name() == name) {
                  LaserRecipe* p = _recipes->recipePtr(i);
                  if (p)
                        QQmlEngine::setObjectOwnership(p, QQmlEngine::CppOwnership);
                  return p;
                  }
            }
      return nullptr;
      }

//---------------------------------------------------------
//   findFirstVisibleLayer
//    Recursively traverse the element tree to find the first
//    Layer that is currently visible (show == true).
//---------------------------------------------------------

Group* ZCam::findFirstVisibleLayer(Element* root) const {
      if (!root)
            return nullptr;
      if (auto* layer = qobject_cast<Group*>(root)) {
            if (layer->show() && layer->ancestorsShow())
                  return layer;
            }
      for (Element* child : root->children()) {
            auto* found = findFirstVisibleLayer(child);
            if (found)
                  return found;
            }
      return project()->cad();
      }

//---------------------------------------------------------
//   findCurrentLayer
//    Find the Layer that is the current element itself, or the
//    nearest Layer ancestor of the current element, walking up
//    the parent chain until Cad is reached.
//
//    The search starts at _currentElement. If _currentElement is
//    itself a Layer, it is returned (provided it is visible).
//    Otherwise we walk up through parent() until we either find
//    a Layer or reach the Cad element (which is the container of
//    all layers and therefore the stop marker).
//
//    Returns nullptr if:
//      - there is no current element
//      - no Layer is found in the parent chain before reaching Cad
//      - the found Layer is not visible (show == false or an
//        ancestor has show == false)
//---------------------------------------------------------

Group* ZCam::findCurrentLayer() const {
      if (!_currentElement)
            return nullptr;

      // Walk up the parent chain from the current element.
      // Stop when we reach a Cad element (the container of layers)
      // or when there are no more parents.
      for (Element* e = _currentElement; e; e = e->parent()) {
            // If we reach Cad, the layers are direct children of Cad,
            // so there is no Layer in this chain.
            if (isType<Cad>(e))
                  return nullptr;

            if (auto* layer = qobject_cast<Group*>(e)) {
                  if (layer->show() && layer->ancestorsShow())
                        return layer;
                  return nullptr;
                  }
            }
      return nullptr;
      }

//---------------------------------------------------------
//   createRectangle
//    Create a new Rectangle element with size (0, 0) at the
//    given world position and add it to the current Layer (the
//    Layer of the selected element) or the first visible Layer
//    as fallback.  The new rectangle is set as the current element
//    so that vertex handles are displayed.  Returns the new
//    Rectangle or nullptr if no suitable layer was found.
//    The operation is routed through the undo stack so it can
//    be undone/redone.
//---------------------------------------------------------

Element3d* ZCam::createRectangle(double x, double y) {
      if (!_project || !_project->cad())
            return nullptr;

      // Find the layer to host the new rectangle: prefer the layer
      // of the currently selected element, fall back to the first
      // visible layer in the tree.
      Group* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd        = new AddRectangleCommand(this, layer, x, y);
      Element3d* rect = cmd->rectangle();
      _project->undo()->beginMacro();
      _project->undo()->push(cmd);
      _project->undo()->endMacro();

      // Select the new rectangle so vertex handles appear.
      setCurrentElement(rect);

      return rect;
      }

//---------------------------------------------------------
//   createPolygon
//    Create a new Polygon element at the given world position
//    and add it to the current Layer (the Layer of the selected
//    element) or the first visible Layer as fallback.  The new
//    polygon is set as the current element so that vertex handles
//    are displayed.  Returns the new Polygon or nullptr if no
//    suitable layer was found.  The operation is routed through
//    the undo stack so it can be undone/redone.
//---------------------------------------------------------

Element3d* ZCam::createPolygon(double x, double y) {
      if (!_project || !_project->cad())
            return nullptr;

      Group* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd        = new AddPolygonCommand(this, layer, x, y);
      Element3d* poly = cmd->polygon();
      _project->undo()->beginMacro();
      _project->undo()->push(cmd);
      _project->undo()->endMacro();

      setCurrentElement(poly);

      return poly;
      }

//---------------------------------------------------------
//   createEllipse
//    Create a new Ellipse element with size (0, 0) at the
//    given world position and add it to the current Layer (the
//    Layer of the selected element) or the first visible Layer
//    as fallback.  The new ellipse is set as the current element
//    so that vertex handles are displayed.  Returns the new Ellipse
//    or nullptr if no suitable layer was found.
//    The operation is routed through the undo stack so it can
//    be undone/redone.
//---------------------------------------------------------

Element3d* ZCam::createEllipse(double x, double y) {
      if (!_project || !_project->cad())
            return nullptr;

      Group* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd       = new AddEllipseCommand(this, layer, x, y);
      Element3d* ell = cmd->ellipse();
      _project->undo()->beginMacro();
      _project->undo()->push(cmd);
      _project->undo()->endMacro();

      setCurrentElement(ell);

      return ell;
      }

//---------------------------------------------------------
//   createText
//    Create a new Text element at the given world position
//    and add it to the current Layer (the Layer of the selected
//    element) or the first visible Layer as fallback.
//
//    If the currently selected element is itself a Text, its
//    font and appearance properties (fontFamily, pointSize, weight,
//    stretch, letterSpacing, wordSpacing, lineSpacing, align, bold,
//    italic, underline, fill, color, burn, show, mirrorX, mirrorY,
//    lockScale, lineWidth, endType, joinType, scale, rot) are
//    copied to the new Text so that the user gets visual continuity.
//    The "text" string itself is not copied – the new Text starts
//    empty.
//
//    The new text is set as the current element.  Returns the new
//    Text or nullptr if no suitable layer was found.
//    The operation is routed through the undo stack so it can
//    be undone/redone.
//---------------------------------------------------------

Element3d* ZCam::createText(double x, double y) {
      if (!_project || !_project->cad())
            return nullptr;

      Group* layer = findCurrentLayer();
      if (!layer)
            layer = findFirstVisibleLayer(_project->cad());
      if (!layer) {
            Debug("no layer");
            return nullptr;
            }

      auto cmd        = new AddTextCommand(this, layer, x, y);
      Element3d* text = cmd->text();

      // If the currently selected element is a Text, copy its font
      // and appearance properties to the new Text.
      if (_currentElement && isType<Text>(_currentElement)) {
            auto* src = qobject_cast<Text*>(_currentElement);
            auto* dst = qobject_cast<Text*>(text);
            if (src && dst) {
                  dst->set_fontFamily(src->fontFamily());
                  dst->set_pointSize(src->pointSize());
                  dst->set_weight(src->weight());
                  dst->set_stretch(src->stretch());
                  dst->set_letterSpacing(src->letterSpacing());
                  dst->set_wordSpacing(src->wordSpacing());
                  dst->set_lineSpacing(src->lineSpacing());
                  dst->set_align(src->align());
                  dst->set_bold(src->bold());
                  dst->set_italic(src->italic());
                  dst->set_underline(src->underline());
                  dst->set_fill(src->fill());
                  dst->setColor(src->color());
                  dst->set_burn(src->burn());
                  dst->set_show(src->show());
                  dst->set_mirrorX(src->mirrorX());
                  dst->set_mirrorY(src->mirrorY());
                  dst->set_lockScale(src->lockScale());
                  dst->set_lineWidth(src->lineWidth());
                  dst->set_endType(src->endType());
                  dst->set_joinType(src->joinType());
                  dst->set_scale(src->scale());
                  dst->set_rot(src->rot());
                  dst->update();
                  }
            }

      _project->undo()->beginMacro();
      _project->undo()->push(cmd);
      _project->undo()->endMacro();

      setCurrentElement(text);

      return text;
      }

//---------------------------------------------------------
//   reparentElement
//    Re-parent an element to a new parent Element3d.  The element's
//    local pos/rot/scale are adjusted so that its world-space
//    transform stays the same (the visual position doesn't jump).
//    This is the core of the drag-&-drop grouping mechanism:
//    when the user drops one draggable element onto another, the
//    dropped element becomes a child of the target element.
//
//    The world-space-preserving coordinate transformation is handled
//    entirely by MoveElementCommand, which computes the new local
//    transform from the old and new parent global matrices.
//---------------------------------------------------------

void ZCam::reparentElement(Element3d* element, Element3d* newParent) {
      if (!element || !newParent || element == newParent)
            return;
      // Prevent re-parenting into one's own descendant
      Element* p = newParent;
      while (p) {
            if (p == element)
                  return;
            p = p->parent();
            }

      Element* oldParent = element->parent();
      if (!oldParent)
            return;

      // Reset the drag state since we bypassed endElementDrag().
      _elementDragElement = nullptr;

      // moveElement creates a MoveElementCommand which handles the
      // world-space-preserving transform adjustment internally.
      if (_project) {
            Debug("reparentElement: calling moveElement: element={} oldParent={} newParent={}",
                element->name(), oldParent ? oldParent->name() : "null", newParent->name());
            _project->moveElement(element, newParent, -1);
            // Refresh the new and old parent's selection geometry so
            // the Group bounding box updates to include/exclude the
            // moved element.
            if (auto* np = qobject_cast<Element3d*>(newParent))
                  np->update();
            if (auto* op = qobject_cast<Element3d*>(oldParent))
                  op->update();
            }
      else {
            Debug("reparentElement: no project");
            }
      }

//--------------------------------------------------------------------
//     groupSelectedElements
//--------------------------------------------------------------------
//   Group the currently selected elements (lasso multi-selection or
//   single current element) into a new Group element.
//
//   The new Group is created as a child of the first selected element's
//   parent Layer (or the Cad root if the parent is not a Layer).  All
//   selected elements are re-parented into the new Group, preserving
//   their world-space transforms so nothing visually jumps.
//
//   The operation is wrapped in a single undo macro that contains:
//     1. AddGroupCommand  — creates and inserts the new Group
//     2. MoveElementCommand (one per element) — moves each element into
//        the new Group, with preceding PropertyChangeCommands for
//        pos/rot/scale adjustments (handled by reparentElement logic
//        inlined here).
//
//   After grouping, the new Group becomes the current element and the
//   lasso selection is cleared.
//---------------------------------------------------------

void ZCam::groupSelectedElements() {
      if (!_project || !_project->cad())
            return;

      // Build the list of elements to group.
      QList<Element3d*> toGroup;
      if (!_selectedElements.isEmpty())
            toGroup = _selectedElements;
      else if (_currentElement)
            toGroup.append(_currentElement);

      // Need at least two elements to form a group.
      if (toGroup.size() < 2)
            return;

      // Filter: keep only draggable elements that have a parent.
      // Skip elements that are already inside one of the other
      // selected elements (descendant) — they will be moved together
      // with their ancestor.
      QList<Element3d*> filtered;
      for (auto* el : toGroup) {
            if (!el || !el->draggable() || !el->parent())
                  continue;
            bool isDescendant = false;
            for (auto* other : toGroup) {
                  if (other == el)
                        continue;
                  // Walk up el's parent chain; if we encounter other,
                  // then el is a descendant of other.
                  Element* p = el->parent();
                  while (p) {
                        if (p == other) {
                              isDescendant = true;
                              break;
                              }
                        p = p->parent();
                        }
                  if (isDescendant)
                        break;
                  }
            if (!isDescendant)
                  filtered.append(el);
            }

      if (filtered.size() < 2)
            return;

      // Determine the common parent: use the parent of the first
      // filtered element.  All filtered elements should share the
      // same parent (they come from a lasso selection at the same
      // tree level), but if they don't we use the first element's
      // parent as the Group's parent.
      Element* groupParent = filtered.first()->parent();
      if (!groupParent)
            return;

      // Find the Layer ancestor for the new Group.  If groupParent
      // is itself a Group/Layer, add the new Group as its child.
      // Otherwise add it to the Cad root.
      Group* targetLayer = nullptr;
      if (auto* gp = qobject_cast<Group*>(groupParent))
            targetLayer = gp;
      else {
            // Walk up to find the nearest Group ancestor.
            for (Element* e = groupParent; e; e = e->parent()) {
                  if (auto* g = qobject_cast<Group*>(e)) {
                        targetLayer = g;
                        break;
                        }
                  if (isType<Cad>(e))
                        break;
                  }
            }
      if (!targetLayer)
            targetLayer = findFirstVisibleLayer(_project->cad());
      if (!targetLayer) {
            Debug("groupSelectedElements: no target layer");
            return;
            }

      // Create the new Group element.
      auto* newGroup = new Group(this, nullptr);
      newGroup->setName("group");

      // Compute the world-space bounding box of all filtered elements
      // to position the new Group at the center of the bounding box.
      // This keeps the Group's local origin near the visual center of
      // its children, making subsequent transforms intuitive.
      QRectF bbox = filtered.first()->worldBoundingBox();
      for (int i = 1; i < filtered.size(); ++i) {
            QRectF wb = filtered[i]->worldBoundingBox();
            bbox      = bbox.united(wb);
            }
      QVector3D groupPos;
      if (!bbox.isNull() && !bbox.isEmpty()) {
            // The Group is placed in the target Layer's coordinate space.
            // Convert the world-space center back to the target Layer's
            // local space.
            QMatrix4x4 layerGlobal = targetLayer->globalMatrix();
            bool ok                = false;
            QMatrix4x4 layerInv    = layerGlobal.inverted(&ok);
            if (ok) {
                  QVector3D worldCenter(bbox.center().x(), bbox.center().y(), 0.0);
                  QVector3D localCenter = layerInv.map(worldCenter);
                  groupPos              = localCenter;
                  }
            }
      newGroup->set_pos(groupPos);

      // Begin the undo macro.
      _project->undo()->beginMacro();

      // Insert the new Group into the target Layer.
            {
            int row = targetLayer->children().size();
            if (treeModel())
                  treeModel()->beginInsertChild(targetLayer, row);
            targetLayer->addChild(newGroup);
            if (treeModel())
                  treeModel()->endInsertChild();
            emit add3dElement(newGroup);
            newGroup->update();
            }

      // Re-parent each filtered element into the new Group.
      // MoveElementCommand handles the world-space-preserving
      // transform adjustment internally.
      for (auto* el : filtered) {
            Element* oldParent = el->parent();
            if (!oldParent)
                  continue;

            int oldRow = 0;
            for (const auto c : oldParent->children()) {
                  if (c == el)
                        break;
                  ++oldRow;
                  }
            auto moveCmd = new MoveElementCommand(this, el, oldParent, oldRow, newGroup, -1);
            _project->undo()->push(moveCmd);
            }

      _project->undo()->endMacro();

      // Update the new Group and old parents.
      newGroup->update();
      if (auto* tp = qobject_cast<Element3d*>(targetLayer))
            tp->update();

      // Select the new Group and clear the lasso selection.
      _selectedElements.clear();
      emit selectedElementsChanged();
      setCurrentElement(newGroup);

      setCamDirty(true);
      }

//--------------------------------------------------------------------
//     combineSelectedPolygons
//--------------------------------------------------------------------
//   Combine all selected Polygon elements that share the same parent
//   (same tree level) into a single new Polygon.
//
//   For each selected Polygon, its path data (PainterPath) is converted
//   to a PathList, each path is transformed from the polygon's local
//   coordinate space to the common parent's local coordinate space
//   using the polygon's globalMatrix and the parent's inverse global
//   matrix, and all transformed paths are unioned via Clipper2.
//
//   The union result is converted back to a PainterPath (as a series
//   of MoveTo / LineTo elements) and assigned to a new Polygon that
//   is inserted as a child of the common parent.  All original selected
//   polygons are then deleted.
//
//   The operation is wrapped in a single undo macro containing:
//     1. AddPolygonCommand — creates and inserts the new combined Polygon
//     2. RemoveElementCommand (one per original polygon) — deletes each
//
//   After combining, the new Polygon becomes the current element and
//   the lasso selection is cleared.
//---------------------------------------------------------

void ZCam::combineSelectedPolygons() {
      if (!_project || !_project->cad())
            return;

      // Build the list of selected elements.
      QList<Element3d*> toCombine;
      if (!_selectedElements.isEmpty())
            toCombine = _selectedElements;
      else if (_currentElement)
            toCombine.append(_currentElement);

      // Filter: keep only Polygon elements that have a parent.
      QList<Polygon*> polygons;
      for (auto* el : toCombine) {
            auto* poly = qobject_cast<Polygon*>(el);
            if (poly && poly->parent())
                  polygons.append(poly);
            }

      // Need at least two polygons to combine.
      if (polygons.size() < 2)
            return;

      // Verify all polygons share the same parent (same tree level).
      Element* commonParent = polygons.first()->parent();
      for (int i = 1; i < polygons.size(); ++i) {
            if (polygons[i]->parent() != commonParent) {
                  Debug("combineSelectedPolygons: selected polygons are not on the same tree level");
                  return;
                  }
            }

      auto* parent3d = qobject_cast<Element3d*>(commonParent);
      if (!parent3d)
            return;

      // Collect all paths in the common parent's local coordinate space.
      // For each polygon, transform its pathList from its local space
      // to the parent's local space via:
      //   parentLocal = parentGlobalInv * polyGlobal * polyLocal
      // We use the polygon's globalMatrix() to map local points to
      // world (root) space, then the parent's inverse globalMatrix to
      // map back to parent-local space.
      QMatrix4x4 parentGlobal    = parent3d->globalMatrix();
      bool ok                    = false;
      QMatrix4x4 parentGlobalInv = parentGlobal.inverted(&ok);
      if (!ok)
            return;

      Clipper2Lib::PathsD allPaths;
      for (auto* poly : polygons) {
            // Ensure the polygon's pathList is up to date.
            // toPathList() converts the PainterPath (with bezier
            // flattening) to a list of 2D point paths.
            PathList pl           = poly->painterPathData().toPathList();
            QMatrix4x4 polyGlobal = poly->globalMatrix();
            for (const auto& path : pl) {
                  Clipper2Lib::PathD clipperPath;
                  for (const auto& pt : path) {
                        QVector3D worldPt  = polyGlobal.map(QVector3D(float(pt.x()), float(pt.y()), 0.0f));
                        QVector3D parentPt = parentGlobalInv.map(worldPt);
                        clipperPath.push_back({parentPt.x(), parentPt.y()});
                        }
                  if (clipperPath.size() >= 3)
                        allPaths.push_back(clipperPath);
                  }
            }

      if (allPaths.empty())
            return;

      // Determine nesting depth for each path.
      // A path at odd nesting depth (inside one other path) must be
      // reversed so that it becomes a hole in the NonZero union.
      // Without this, two same-orientation paths where one is inside
      // the other would simply merge into the outer contour — the
      // inner path would be absorbed and no hole would appear.
      for (int i = 0; i < static_cast<int>(allPaths.size()); ++i) {
            int depth = 0;
            // Use the first point of path i as a representative point.
            Clipper2Lib::PointD testPt = allPaths[i][0];
            for (int j = 0; j < static_cast<int>(allPaths.size()); ++j) {
                  if (i == j)
                        continue;
                  if (Clipper2Lib::PointInPolygon(testPt, allPaths[j]) ==
                      Clipper2Lib::PointInPolygonResult::IsInside)
                        ++depth;
                  }
            if (depth % 2 == 1)
                  std::reverse(allPaths[i].begin(), allPaths[i].end());
            }

      // Union all paths via Clipper2.
      // NonZero fill rule with correct orientations (outer contours CCW,
      // holes CW) produces a result where holes are preserved as separate
      // paths with opposite orientation.
      Clipper2Lib::ClipperD clipper(4);
      clipper.AddSubject(allPaths);
      Clipper2Lib::PathsD unioned;
      clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, unioned);

      if (unioned.empty())
            return;

      // Build a PainterPath from the union result.
      // Each Clipper2 path becomes a subpath: MoveTo to the first point,
      // then LineTo for each subsequent point.  The subpath is closed
      // by adding a final LineTo back to the first point (unless the
      // path is already closed).
      //
      // We do NOT use PainterPath::closeSubpath() because that method
      // always closes to front() of the entire PainterPath, which is
      // wrong when multiple subpaths exist.
      PainterPath combinedPath;
      for (const auto& path : unioned) {
            if (path.empty())
                  continue;
            Vec2d firstPt(path[0].x, path[0].y);
            combinedPath.moveTo(firstPt);
            for (size_t i = 1; i < path.size(); ++i)
                  combinedPath.lineTo(Vec2d(path[i].x, path[i].y));
            // Close the subpath explicitly.
            Vec2d lastPt(path.back().x, path.back().y);
            if (std::abs(firstPt.x() - lastPt.x()) > 0.0001 || std::abs(firstPt.y() - lastPt.y()) > 0.0001)
                  combinedPath.lineTo(firstPt);
            }

      // Find the target Layer for the new Polygon.
      Group* targetLayer = nullptr;
      if (auto* gp = qobject_cast<Group*>(commonParent))
            targetLayer = gp;
      else {
            for (Element* e = commonParent; e; e = e->parent()) {
                  if (auto* g = qobject_cast<Group*>(e)) {
                        targetLayer = g;
                        break;
                        }
                  if (isType<Cad>(e))
                        break;
                  }
            }
      if (!targetLayer)
            targetLayer = findFirstVisibleLayer(_project->cad());
      if (!targetLayer) {
            Debug("combineSelectedPolygons: no target layer");
            return;
            }

      // Create the new combined Polygon.
      auto* newPoly = new Polygon(this, nullptr);
      newPoly->setName("");
      newPoly->set_pos(QVector3D(0.0, 0.0, 0.0));
      // Copy visual properties from the first polygon.
      newPoly->setColor(polygons.first()->color());
      newPoly->set_lineWidth(polygons.first()->lineWidth());
      newPoly->set_fill(polygons.first()->fill());
      newPoly->set_endType(polygons.first()->endType());
      newPoly->set_joinType(polygons.first()->joinType());
      // Set the combined painter path.
      newPoly->setPainterPath(combinedPath);
      newPoly->update();

      // Begin the undo macro.
      _project->undo()->beginMacro();

      // Insert the new Polygon into the target Layer.
            {
            int row = targetLayer->children().size();
            if (treeModel())
                  treeModel()->beginInsertChild(targetLayer, row);
            targetLayer->addChild(newPoly);
            if (treeModel())
                  treeModel()->endInsertChild();
            emit add3dElement(newPoly);
            }

      // Remove all original polygons.
      for (auto* poly : polygons) {
            Element* p = poly->parent();
            if (!p)
                  continue;
            int row = 0;
            for (const auto c : p->children()) {
                  if (c == poly)
                        break;
                  ++row;
                  }
            auto cmd = new RemoveElementCommand(this, p, poly, row);
            _project->undo()->push(cmd);
            }

      _project->undo()->endMacro();

      // Update the parent's selection geometry.
      parent3d->update();

      // Select the new Polygon and clear the lasso selection.
      _selectedElements.clear();
      emit selectedElementsChanged();
      setCurrentElement(newPoly);

      setCamDirty(true);
      }

//---------------------------------------------------------
//   deleteCurrentElement
//    Delete the current element and/or all lasso-selected
//    elements.  When _selectedElements is non-empty, all
//    selected elements are deleted in a single undo macro.
//    Hierarchical de-duplication: if an element is a
//    descendant of another selected element, the descendant
//    is skipped (it will be removed together with its
//    ancestor).  The current element is included in the
//    deletion set even if it is not in _selectedElements
//    (e.g. when the user clicked a single element without
//    lasso).  Non-deletable elements are silently skipped.
//    The operation is routed through the undo stack via
//    Project::removeElement() so it can be undone/redone.
//---------------------------------------------------------

void ZCam::deleteCurrentElement() {
      if (!_project)
            return;

      // Build the set of elements to delete.
      // Start with _selectedElements (lasso multi-selection).
      // If _selectedElements is empty, fall back to just
      // _currentElement (single selection mode).
      QList<Element3d*> toDelete;
      if (!_selectedElements.isEmpty())
            toDelete = _selectedElements;
      else if (_currentElement)
            toDelete.append(_currentElement);

      if (toDelete.isEmpty())
            return;

      // Filter: keep only deletable elements.
      // Also remove elements that are descendants of other
      // elements in the deletion set — deleting the ancestor
      // will already remove the descendant from the tree.
      QList<Element3d*> filtered;
      for (auto* el : toDelete) {
            if (!el || !el->deletable())
                  continue;
            // Walk up the parent chain; if any ancestor is
            // also in the deletion set, skip this element.
            bool isDescendant = false;
            for (Element* p = el->parent(); p; p = p->parent()) {
                  auto* p3d = qobject_cast<Element3d*>(p);
                  if (!p3d)
                        continue;
                  if (toDelete.contains(p3d)) {
                        isDescendant = true;
                        break;
                        }
                  }
            if (!isDescendant)
                  filtered.append(el);
            }

      if (filtered.isEmpty())
            return;

      // Clear selection BEFORE deleting so QML bindings don't
      // dereference dangling pointers.
      auto oldSelected = _selectedElements;
      _selectedElements.clear();
      for (auto* e : oldSelected)
            emit e->curColorChanged();
      if (!oldSelected.isEmpty())
            emit selectedElementsChanged();
      setCurrentElement(nullptr);
      // Pre-compute the parent and row for each element BEFORE
      // any deletion happens.  Since push() executes redo()
      // immediately, deleting an element shifts the children list
      // of its parent.  To avoid stale indices, we sort the
      // deletion list by (parent, descending row) so that within
      // the same parent we delete from highest to lowest index,
      // preserving the validity of lower indices.
      struct DelInfo {
            Element3d* element;
            Element* parent;
            int row;
            };
      QList<DelInfo> delList;
      for (auto* el : filtered) {
            Element* parent = el->parent();
            if (!parent)
                  continue;
            int row = 0;
            for (const auto c : parent->children()) {
                  if (c == el)
                        break;
                  ++row;
                  }
            delList.append({el, parent, row});
            }
      std::sort(delList.begin(), delList.end(), [](const DelInfo& a, const DelInfo& b) {
            if (a.parent != b.parent)
                  return a.parent < b.parent;
            return a.row > b.row; // descending row within same parent
            });

      // Delete all filtered elements in a single undo macro.
      // We create RemoveElementCommand objects directly instead of
      // calling Project::removeElement() because removeElement()
      // creates its own beginMacro/endMacro pair, and macro nesting
      // is not supported by the undo stack.
      _project->undo()->beginMacro();
      for (const auto& info : delList) {
            auto cmd = new RemoveElementCommand(this, info.parent, info.element, info.row);
            _project->undo()->push(cmd);
            }
      _project->undo()->endMacro();
      // Update CAD layer visibility since removing a Layer or
      // LaserMop may change which layers are referenced by the
      // active fixture.
      _project->updateCadLayerVisibility();
      }

//=========================================================
//  Project lifecycle methods (moved from ProjectManager)
//=========================================================
//---------------------------------------------------------
//   saveLastProjectPath / lastProjectPath
//    Persist the current project path in QSettings so it can be
//    restored on the next application start.
//---------------------------------------------------------

static void saveLastProjectPath(const QString& path) {
      QSettings settings;
      settings.setValue("project/lastPath", path);
      }

static QString lastProjectPath() {
      QSettings settings;
      return settings.value("project/lastPath").toString();
      }

//---------------------------------------------------------
//   newProject
//---------------------------------------------------------

void ZCam::newProject(bool clearPersistedPath) {
      startNewProject(clearPersistedPath);
      endNewProject();
      }

//---------------------------------------------------------
//   createTestProject
//    Create a project pre-populated with test geometry (text,
//    rectangle, polygon, ellipse) for quick experimentation.
//---------------------------------------------------------

void ZCam::createTestProject() {
      startNewProject();

      auto project = this->project();
      auto fixture = project->fixture();
      auto cam     = project->cam();
      auto cad     = project->cad();
      auto ll      = new LaserMop(this, fixture);
      auto recipes = this->recipes();
      if (recipes && recipes->recipeCount() > 0)
            ll->set_recipe(recipes->recipePtr(0));
      auto stock = new Stock(this, cam);
      auto layer = new Group(this, cad);
      // Set the LaserMop on the Layer so all children inherit it.
      layer->set_mop(ll);
      auto text = new Text(this, layer);
      text->set_text("ZCam");

      auto rectangle = new Rectangle(this, layer);
      rectangle->set_size(QVector2D(40.0, 30.0));
      rectangle->set_pos(QVector3D(50.0, 50.0, 0.0));
      rectangle->set_corner(5.0);
      rectangle->set_lineWidth(1.0);
      rectangle->set_fill(false);

      auto poly = new Polygon(this, layer);
      poly->set_pos(QVector3D(10.0, 25.0, 0.0));
      poly->set_lineWidth(1.0);
      poly->moveTo({0.0, 0.0});
      poly->lineTo({20.0, 20.0});
      poly->lineTo({10.0, 5.0});
      poly->set_fill(true);

      auto ell = new Ellipse(this, layer);
      ell->set_size(QVector2D(25.0, 25.0));
      ell->set_pos(QVector3D(-30.0, 40.0, 0.0));
      ell->set_lineWidth(1.0);
      ell->set_fill(false);

      ll->setName(QString("LL-%1").arg(layer->name()));

      auto grid = new Grid(this, project);

      // build project tree
      cad->addChild(layer);
      layer->addChild(text);
      layer->addChild(rectangle);
      layer->addChild(poly);
      layer->addChild(ell);
      project->addChild(grid);
      cam->addChild(stock);
      fixture->addChild(ll);

      endNewProject();
      }

//---------------------------------------------------------
//   startNewProject
//---------------------------------------------------------

void ZCam::startNewProject(bool clearPersistedPath) {
      // Caller is responsible for checking unsaved changes via QML dialog
      // before invoking this method.
      if (_project)
            _project->clearUndoStack();
      Element::clearProject(); // clear global name hash

      // Re-register Config in the global name hash after clearProject()
      // removed all entries.  Config is not part of the project tree and
      // its name must persist across project changes so scripts can
      // reference it as "config".
      _config->setName(QStringLiteral("config"));

      // Clear the current/hover element pointers BEFORE destroying the old
      // tree.  If these are left pointing at soon-to-be-deleted Element3d
      // objects, any subsequent QML access (e.g. the InspectorPanel binding
      //   element: ZCam.currentElement
      // or the Shape.qml binding
      //   visible: element && ZCam.currentElement === element
      // ) will dereference a dangling pointer and crash in
      // QQmlData::wasDeleted() inside QObjectWrapper::wrap().
      setCurrentElement(nullptr);
      set_hoverElement(nullptr);

      // Synchronously destroy the old element tree before creating new
      // elements.  TreeModel::setRoot(nullptr) schedules deleteLater() on
      // the old root; we must flush those deferred deletes NOW, otherwise
      // the old elements' destructors would run later (after new elements
      // with the same names have been created) and accidentally remove
      // the new elements from the Element::names hash.
      _treeModel->setRoot(nullptr);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      set_rootElement(nullptr);
      set_project(nullptr);
      //
      // construct a demo project
      //
      if (clearPersistedPath)
            saveLastProjectPath(QString());

      auto root = new RootElement(this, nullptr);
      set_rootElement(root);
      auto top = new Project(this, root);
      set_project(top);

      top->setProjectPath(QString());

      // Set the default machine from the Config, if configured.
      if (!_config->defaultMachine().isEmpty()) {
            QStringList model = _machines->machinesModel();
            int idx           = model.indexOf(_config->defaultMachine());
            if (idx >= 0)
                  top->set_machine(_machines->machine(idx));
            }

      auto cad     = new Cad(this, top);
      auto cam     = new Cam(this, top);
      auto fixture = new Fixture(this, cam);
      auto framing = new Framing(this, cam);
      top->addChild(cad);
      top->addChild(cam);
      cam->addChild(fixture);
      cam->addChild(framing);
      connect(top, &Project::updateFraming, framing, &Framing::update);

      // Register all named elements in the script engine namespace
      // (project.cad ...) so scripts can reference them.  Bindings
      // from the previous project are dropped.
      if (_scriptEngine)
            _scriptEngine->rebuildRegistry(false);
      }

//---------------------------------------------------------
//   endNewProject
//---------------------------------------------------------

void ZCam::endNewProject() {
      // Create a CameraElement for new projects before the scene is built.
      if (project())
            project()->ensureCameraElement();

      rootElement()->addChild(project());
      update();

      // Notify QML that the grid element may have changed so the
      // background View3D can re-evaluate its GridShape binding.
      if (project())
            emit project() -> gridElementChanged();

      // Re-resolve the Project's Machine* after the project is replaced.
      if (project())
            project()->resolveMachine();

      emit projectCreated();
      // Update CAD layer visibility based on the active fixture.
      // The CAM data is NOT refreshed automatically here because the
      // processTileLines() call inside Cam::updateCam() may need to
      // run createFill() for filled elements (e.g. Text), which can
      // be extremely expensive on the main thread when the recipe
      // has many passes or a small interval.  Instead, the camDirty
      // flag is set so the user can trigger a refresh manually via
      // the Cam refresh button when ready.
      if (project())
            project()->updateCadLayerVisibility();
      setupFileDialogFavorites();
      setCamDirty(true);
      }

//---------------------------------------------------------
//   update
//    updates the tree view and triggers update of 3DCanvas
//---------------------------------------------------------

void ZCam::update() {
      _treeModel->setRoot(rootElement()); // update project tree view
      set_rootElement(project());         // build and show the scene
      }

//---------------------------------------------------------
//   openProject
//---------------------------------------------------------

bool ZCam::openProject(const QString& path, bool skipCamUpdate) {
      if (path.isEmpty()) {
            Warning("ZCam::openProject: empty path");
            return false;
            }
      if (!readProjectFile(path.toStdString(), skipCamUpdate)) {
            Warning("ZCam::openProject: failed to read", path);
            return false;
            }
      _project->setProjectPath(path);
      _project->clearUndoStack();
      saveLastProjectPath(path);
      setupFileDialogFavorites();
      emit projectLoaded(path);
      // cam data is fresh after loading a project, unless the CAM update
      // was skipped (e.g. at startup) — in that case, mark it as dirty
      // so the refresh button is enabled and the user can update manually.
      setCamDirty(skipCamUpdate);
      return true;
      }

//---------------------------------------------------------
//   save
//---------------------------------------------------------

bool ZCam::save() {
      if (!_project || _project->projectPath().isEmpty())
            return false; // QML should call saveAs with a chosen path
      if (!writeProjectFile(_project->projectPath().toStdString()))
            return false;
      _project->undo()->setClean();
      saveLastProjectPath(_project->projectPath());
      emit projectSaved(_project->projectPath());
      return true;
      }

//---------------------------------------------------------
//   saveAs
//---------------------------------------------------------

bool ZCam::saveAs(const QString& path) {
      if (path.isEmpty() || !_project)
            return false;
      if (!writeProjectFile(path.toStdString()))
            return false;
      _project->setProjectPath(path);
      _project->undo()->setClean();
      saveLastProjectPath(path);
      setupFileDialogFavorites();
      emit projectSaved(path);
      return true;
      }

//---------------------------------------------------------
//   importFile
//---------------------------------------------------------

bool ZCam::importFile(const QString& path) {
      if (path.isEmpty())
            return false;
      QFileInfo fi(path);
      QString suffix = fi.suffix().toLower();
      if (suffix == QStringLiteral("svg"))
            importSvg(path);
      else if (suffix == QStringLiteral("dxf") || suffix == QStringLiteral("dwg"))
            return DxfImport::import(this, path);
      else if (ImportIpc2581::isIpc2581File(path))
            return ImportIpc2581::import(this, path);
      else if (suffix == QStringLiteral("brep"))
            return BrepElementInterface::import(this, path);
      else if (ImageImport::isImageFile(path))
            return ImageImport::import(this, path);
      else {
            Warning("ZCam::importFile: unsupported file type: {}", suffix);
            return false;
            }
      setCamDirty(true);
      return true;
      }

//---------------------------------------------------------
//   dxfBoundingBox
//    Compute the bounding box of a DXF/DWG file in millimetres.
//---------------------------------------------------------

QRectF ZCam::dxfBoundingBox(const QString& path) {
      return DxfImport::boundingBox(this, path);
      }

//---------------------------------------------------------
//   importDxfAt
//    Import a DXF/DWG file and position it so the bounding
//    box's bottom-left corner is at (x, y) in scene coordinates.
//---------------------------------------------------------

bool ZCam::importDxfAt(const QString& path, double x, double y) {
      return DxfImport::importAt(this, path, x, y);
      }

//---------------------------------------------------------
//   importImageAt
//    Import an image file (PNG, JPEG, ...) and position it so
//    the bounding box's bottom-left corner is at (x, y) in
//    scene coordinates.
//---------------------------------------------------------

bool ZCam::importImageAt(const QString& path, double x, double y) {
      return ImageImport::importAt(this, path, x, y);
      }

//---------------------------------------------------------
//   imageBoundingBox
//    Compute the bounding box (in mm) of an image file.
//---------------------------------------------------------

QRectF ZCam::imageBoundingBox(const QString& path) {
      return ImageImport::boundingBox(this, path);
      }

//---------------------------------------------------------
//   restoreLastProject
//    Called at startup to re-open the project that was open when
//    the application was last closed.
//---------------------------------------------------------

bool ZCam::restoreLastProject() {
      QString path = lastProjectPath();
      if (path.isEmpty())
            return false;
      QFileInfo fi(path);
      if (!fi.exists() || !fi.isFile())
            return false;
      // Skip CAM data update at startup — the user can trigger a
      // refresh manually via the Cam button when needed.
      return openProject(path, /*skipCamUpdate=*/true);
      }

//---------------------------------------------------------
//   handleStartupFile
//    Handle the file path passed on the command line.  Called from
//    the QML startup timer (replaces restoreLastProject()).
//      - .zcam file         → openProject(path)
//      - importable file     → importFile(path) into the fresh project
//      - empty / unset       → restoreLastProject()
//---------------------------------------------------------

bool ZCam::handleStartupFile() {
      if (_startupFilePath.isEmpty())
            return restoreLastProject();

      QFileInfo fi(_startupFilePath);
      if (!fi.exists() || !fi.isFile()) {
            Warning("ZCam::handleStartupFile: file not found: {}", _startupFilePath);
            _startupFilePath.clear();
            return restoreLastProject();
            }

      QString suffix = fi.suffix().toLower();

      // Case 1: .zcam project file → open directly
      if (suffix == QStringLiteral("zcam")) {
            QString path = _startupFilePath;
            _startupFilePath.clear();
            return openProject(path, /*skipCamUpdate=*/true);
            }

      // Case 2: importable file → import into the fresh (empty) project
      bool isImportable =
          (suffix == "svg" || suffix == "dxf" || suffix == "dwg" || suffix == "brep" ||
              ImageImport::isImageFile(_startupFilePath) || ImportIpc2581::isIpc2581File(_startupFilePath));

      if (isImportable) {
            QString path = _startupFilePath;
            _startupFilePath.clear();
            if (!importFile(path)) {
                  Warning("ZCam::handleStartupFile: import failed: {}", path);
                  return false;
                  }
            return true;
            }

      // Unknown file type → fall back to normal startup
      Warning("ZCam::handleStartupFile: unrecognized file type: {}", suffix);
      _startupFilePath.clear();
      return restoreLastProject();
      }

//---------------------------------------------------------
//   writeProjectFile
//---------------------------------------------------------

bool ZCam::writeProjectFile(const std::string& path) {
      Project* tl = _project;
      if (!tl) {
            Warning("no toplevel");
            return false;
            }
      std::ofstream f(path);
      if (!f.is_open()) {
            Warning("ZCam: cannot open for writing:", path);
            return false;
            }
      // Minimal JSON skeleton – replace with full scene serialisation
      nlohmann::ordered_json root;
      root["version"]     = "1.0";
      root["application"] = "zcam";
      root["toplevel"]    = tl->toJson();
      f << root.dump(4);
      return true;
      }

//---------------------------------------------------------
//   readProjectFile
//---------------------------------------------------------

bool ZCam::readProjectFile(const std::string& path, bool skipCamUpdate) {
      std::ifstream f(path);
      if (!f.is_open()) {
            Warning("ZCam: cannot open for reading:", path);
            return false;
            }

      json jdata;
      try {
            f >> jdata;
            std::string version = jdata.value("version", "unknown");
            Debug("ZCam: loaded project version <{}>", version);
            //
            //  destroy old project
            //
            //  Clear the global name registry first, then synchronously
            //  delete the old element tree.  We cannot rely on
            //  deleteLater() here because it is asynchronous: the old
            //  elements would still be alive (and registered in the
            //  Element::names hash) when the new tree is built below,
            //  causing spurious name collisions.
            //
            Element::clearProject();

            // Re-register Config after clearProject() (see startNewProject).
            _config->setName(QStringLiteral("config"));

            // Clear the current/hover element pointers BEFORE destroying
            // the old tree to avoid dangling-pointer dereferences in QML.
            // See newProject() for a detailed explanation.
            setCurrentElement(nullptr);
            set_hoverElement(nullptr);

            _treeModel->setRoot(nullptr);
            set_rootElement(nullptr); // detach scene
            set_project(nullptr);

            // Process pending deleteLater() calls so the old tree is
            // truly gone before we build the new one.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

            auto root = new RootElement(this, nullptr);
            auto top  = new Project(this, root);
            root->addChild(top);
            set_project(top);
            top->fromJson(jdata.at("toplevel"));

            // Migration: create a CameraElement if the loaded project
            // doesn't have one (projects saved before the camera element
            // was introduced).
            if (project())
                  project()->ensureCameraElement();

            _treeModel->setRoot(root);
            set_rootElement(project()); // build and show the scene

            // Notify QML that the grid element may have changed.
            emit top->gridElementChanged();

            // Re-resolve the Project's Machine* after loading.
            if (project())
                  project()->resolveMachine();

            // Initial CAD layer visibility update and CAM refresh so
            // display geometry is populated after load.
            // When skipCamUpdate is true (e.g. at startup when restoring
            // the last project), the expensive Cam::updateCam() call is
            // skipped — the user can trigger it manually via the Cam
            // refresh button.
            if (project()) {
                  project()->updateCadLayerVisibility();
                  if (!skipCamUpdate && project()->cam())
                        project()->cam()->updateCam();
                  }
            auto func = [](this auto& self, Element* e) -> void {
                  for (auto c : e->children()) {
                        c->fixup();
                        self(c);
                        }
                  };
            func(project());

            // Register all loaded elements in the script engine and
            // re-create script bindings from the elements' stored
            // script JSON.
            if (_scriptEngine)
                  _scriptEngine->rebuildRegistry(false);
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("JSON parse error:", err.what());
            return false;
            }

      return true;
      }