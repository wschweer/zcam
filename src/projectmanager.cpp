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

#include "projectmanager.h"

#include <QFileInfo>
#include <QSettings>
#include <QCoreApplication>
#include <fstream>
#include <utility>
#include "logger.h"
#include "project.h"
#include "zcam.h"
#include "cad.h"
#include "fixture.h"
#include "layer.h"
#include "laserlayer.h"
#include "dxfimport.h"
#include "cam.h"
#include "text.h"
#include "polygon.h"
#include "treemodel.h"
#include "grid.h"
#include "rectangle.h"
#include "ellipse.h"

//---------------------------------------------------------
//   ProjectManager
//---------------------------------------------------------

ProjectManager::ProjectManager(ZCam* zc, QObject* parent) : QObject(parent) {
      zcam = zc;
      }

//---------------------------------------------------------
//   projectName
//---------------------------------------------------------

QString ProjectManager::projectName() const {
      if (_projectPath.isEmpty())
            return QStringLiteral("Untitled");
      return QFileInfo(_projectPath).baseName();
      }

//---------------------------------------------------------
//   setDirty
//---------------------------------------------------------

void ProjectManager::setDirty(bool v) {
      if (v == _dirty)
            return;
      _dirty = v;
      emit dirtyChanged();
      }

#include "cad.h"

//---------------------------------------------------------
//   setProjectPath
//---------------------------------------------------------

void ProjectManager::setProjectPath(const QString& v) {
      if (v == _projectPath)
            return;
      _projectPath = v;
      emit projectPathChanged();
      }

//---------------------------------------------------------
//   markDirty
//---------------------------------------------------------

void ProjectManager::markDirty() {
      setDirty(true);
      }

//---------------------------------------------------------
//   pushCommand
//    Push a new undo command onto the stack, discarding any
//    redo entries that come after the current index.
//---------------------------------------------------------

void ProjectManager::pushCommand(std::unique_ptr<UndoCommand> cmd) {
      // Discard any redo entries after the current index
      while (static_cast<int>(_undoStack.size()) > _undoIndex)
            _undoStack.pop_back();
      _undoStack.push_back(std::move(cmd));
      _undoIndex = static_cast<int>(_undoStack.size());
      emit undoStateChanged();
      }

//---------------------------------------------------------
//   changeProperty
//    Public API used by the Inspector to route a property change
//    through the undo system and set the dirty flag.
//---------------------------------------------------------

void ProjectManager::changeProperty(QObject* element, const QString& propName, const QVariant& newValue) {
      if (!element)
            return;
      QByteArray pn     = propName.toUtf8();
      QVariant oldValue = element->property(pn.constData());
      if (oldValue == newValue)
            return;
      // For the "name" property, use RenameElementCommand which stores
      // the actual de-duplicated name that Element::setName() assigns,
      // not the user-typed text.  This ensures undo/redo restores the
      // exact same name even when de-duplication occurred.
      if (pn == "name") {
            auto el = qobject_cast<Element*>(element);
            if (!el)
                  return;
            QString oldName = el->name();
            // Apply the new name first to capture the de-duplicated result
            el->setName(newValue.toString());
            QString actualName = el->name();
            auto cmd           = std::make_unique<RenameElementCommand>(el, oldName, actualName);
            pushCommand(std::move(cmd));
            setDirty(true);
            // mark cam data as stale so the Cam refresh button enables
            zcam->setCamDirty(true);
            return;
            }
      auto cmd = std::make_unique<PropertyChangeCommand>(element, pn, oldValue, newValue);
      cmd->redo(); // apply the new value immediately
      pushCommand(std::move(cmd));
      setDirty(true);
      // mark cam data as stale so the Cam refresh button enables
      zcam->setCamDirty(true);

      // Property changes do NOT automatically refresh the Cam display.
      // Instead, the camDirty flag is set so the user can trigger a
      // manual refresh via the refresh button when ready.
      // (setCamDirty(true) was already called above)
      }

//---------------------------------------------------------
//   saveLastProjectPath / lastProjectPath
//    Persist the current project path in QSettings so it can be
//    restored on the next application start.
//---------------------------------------------------------

void ProjectManager::saveLastProjectPath(const QString& path) {
      QSettings settings;
      settings.setValue("project/lastPath", path);
      }

QString ProjectManager::lastProjectPath() const {
      QSettings settings;
      return settings.value("project/lastPath").toString();
      }

//---------------------------------------------------------
//   restoreLastProject
//    Called at startup to re-open the project that was open when
//    the application was last closed.
//---------------------------------------------------------

bool ProjectManager::restoreLastProject() {
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
//   clearUndoStack
//---------------------------------------------------------

void ProjectManager::clearUndoStack() {
      _undoStack.clear();
      _undoIndex = 0;
      emit undoStateChanged();
      }

//---------------------------------------------------------
//   checkUnsavedChanges
//   Returns true if it is safe to proceed (no pending changes, or user
//   chose to discard).  When dirty the caller should show a dialog; this
//   stub always returns true so QML-level dialogs can take over.
//---------------------------------------------------------

bool ProjectManager::checkUnsavedChanges() {
      // The actual "Save changes?" dialog is driven from QML.
      // This method is kept as a synchronous fast-path for cases where the
      // project is already clean.
      return !_dirty;
      }

//---------------------------------------------------------
//   newProject
//---------------------------------------------------------

void ProjectManager::newProject(bool clearPersistedPath) {
      startNewProject(clearPersistedPath);

      auto project = zcam->project();
      auto fixture = project->fixture();
      auto cam     = project->cam();
      auto cad     = project->cad();
      auto ll      = new LaserLayer(zcam, fixture);
      auto recipes = zcam->recipes();
      if (recipes && recipes->recipeCount() > 0)
            ll->set_recipe(recipes->recipePtr(0));
      auto stock = new Stock(zcam, cam);
      auto layer = new Layer(zcam, cad);
      auto text  = new Text(zcam, layer);
      text->setColor("yellow");
      text->set_text("ZCam");

      auto rectangle = new Rectangle(zcam, layer);
      rectangle->set_size(QVector2D(40.0, 30.0));
      rectangle->set_pos(QVector3D(50.0, 50.0, 0.0));
      rectangle->setColor(QColor("blue"));
      rectangle->set_corner(5.0);
      rectangle->set_lineWidth(1.0);
      rectangle->set_fill(false);

      auto poly = new Polygon(zcam, layer);
      poly->set_pos(QVector3D(10.0, 25.0, 0.0));
      poly->setColor(QColor("green"));
      poly->set_lineWidth(1.0);
      poly->moveTo({0.0, 0.0});
      poly->lineTo({20.0, 20.0});
      poly->lineTo({10.0, 5.0});
      poly->set_fill(true);

      auto ell = new Ellipse(zcam, layer);
      ell->set_size(QVector2D(25.0, 25.0));
      ell->set_pos(QVector3D(-30.0, 40.0, 0.0));
      ell->setColor(QColor("magenta"));
      ell->set_lineWidth(1.0);
      ell->set_fill(false);

      ll->setName(QString("LL-%1").arg(layer->name()));
      ll->set_baseElement(layer);

      auto grid = new Grid(zcam, project);

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

void ProjectManager::startNewProject(bool clearPersistedPath) {
      // Caller is responsible for checking unsaved changes via QML dialog
      // before invoking this method.
      clearUndoStack();
      Element::clearProject(); // clear global name hash

      // Clear the current/hover element pointers BEFORE destroying the old
      // tree.  If these are left pointing at soon-to-be-deleted Element3d
      // objects, any subsequent QML access (e.g. the InspectorPanel binding
      //   element: ZCam.currentElement
      // or the Shape.qml binding
      //   visible: element && ZCam.currentElement === element
      // ) will dereference a dangling pointer and crash in
      // QQmlData::wasDeleted() inside QObjectWrapper::wrap().
      zcam->setCurrentElement(nullptr);
      zcam->set_hoverElement(nullptr);

      // Synchronously destroy the old element tree before creating new
      // elements.  TreeModel::setRoot(nullptr) schedules deleteLater() on
      // the old root; we must flush those deferred deletes NOW, otherwise
      // the old elements' destructors would run later (after new elements
      // with the same names have been created) and accidentally remove
      // the new elements from the Element::names hash.
      zcam->treeModel()->setRoot(nullptr);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      zcam->set_rootElement(nullptr);
      zcam->set_project(nullptr);
      //
      // construct a demo project
      //
      setProjectPath(QString());
      setDirty(false);
      if (clearPersistedPath)
            saveLastProjectPath(QString());

      auto root = new RootElement(zcam, nullptr);
      zcam->set_rootElement(root);
      auto top = new Project(zcam, root);
      zcam->set_project(top);

      // Set the default machine from the Config, if configured.
      if (zcam->config() && !zcam->config()->defaultMachine().isEmpty() && zcam->machines()) {
            QStringList model = zcam->machines()->machinesModel();
            int idx = model.indexOf(zcam->config()->defaultMachine());
            if (idx >= 0)
                  top->set_machine(zcam->machines()->machine(idx));
            }

      auto cad     = new Cad(zcam, top);
      auto cam     = new Cam(zcam, top);
      auto fixture = new Fixture(zcam, cam);
      auto framing = new Framing(zcam, cam);
      top->addChild(cad);
      top->addChild(cam);
      cam->addChild(fixture);
      cam->addChild(framing);
      connect(top, &Project::updateFraming, framing, &Framing::update);
      }

//---------------------------------------------------------
//   endNewProject
//---------------------------------------------------------

void ProjectManager::endNewProject() {
      zcam->rootElement()->addChild(zcam->project());
      update();

      // Notify QML that the grid element may have changed so the
      // background View3D can re-evaluate its GridShape binding.
      if (zcam->project())
            emit zcam->project()->gridElementChanged();

      // Re-resolve the Project's Machine* after the project is replaced.
      if (zcam && zcam->project())
            zcam->project()->resolveMachine();

      emit projectCreated();
      // Update CAD layer visibility based on the active fixture.
      // The CAM data is NOT refreshed automatically here because the
      // processTileLines() call inside Cam::updateCam() may need to
      // run createFill() for filled elements (e.g. Text), which can
      // be extremely expensive on the main thread when the recipe
      // has many passes or a small interval.  Instead, the camDirty
      // flag is set so the user can trigger a refresh manually via
      // the Cam refresh button when ready.
      if (zcam->project())
            zcam->project()->updateCadLayerVisibility();
      zcam->setCamDirty(true);
      }

//---------------------------------------------------------
//   update
//    updates the tree view and triggers update of 3DCanvas
//---------------------------------------------------------

void ProjectManager::update() {
      zcam->treeModel()->setRoot(zcam->rootElement()); // update project tree view
      zcam->set_rootElement(zcam->project());          // build and show the scene
      }

//---------------------------------------------------------
//   openProject
//---------------------------------------------------------

bool ProjectManager::openProject(const QString& path, bool skipCamUpdate) {
      if (path.isEmpty()) {
            Warning("ProjectManager::openProject: empty path");
            return false;
            }
      if (!readProjectFile(path.toStdString(), skipCamUpdate)) {
            Warning("ProjectManager::openProject: failed to read", path);
            return false;
            }
      setProjectPath(path);
      clearUndoStack();
      setDirty(false);
      saveLastProjectPath(path);
      emit projectLoaded(path);
      // cam data is fresh after loading a project, unless the CAM update
      // was skipped (e.g. at startup) — in that case, mark it as dirty
      // so the refresh button is enabled and the user can update manually.
      zcam->setCamDirty(skipCamUpdate);
      return true;
      }

//---------------------------------------------------------
//   save
//---------------------------------------------------------

bool ProjectManager::save() {
      if (_projectPath.isEmpty())
            return false; // QML should call saveAs with a chosen path
      if (!writeProjectFile(_projectPath.toStdString()))
            return false;
      setDirty(false);
      saveLastProjectPath(_projectPath);
      emit projectSaved(_projectPath);
      return true;
      }

//---------------------------------------------------------
//   saveAs
//---------------------------------------------------------

bool ProjectManager::saveAs(const QString& path) {
      if (path.isEmpty())
            return false;
      if (!writeProjectFile(path.toStdString()))
            return false;
      setProjectPath(path);
      setDirty(false);
      saveLastProjectPath(path);
      emit projectSaved(path);
      return true;
      }

//---------------------------------------------------------
//   importFile
//---------------------------------------------------------

bool ProjectManager::importFile(const QString& path) {
      if (path.isEmpty())
            return false;
      QFileInfo fi(path);
      QString suffix = fi.suffix().toLower();
      if (suffix == QStringLiteral("svg"))
            zcam->importSvg(path);
      else if (suffix == QStringLiteral("dxf") || suffix == QStringLiteral("dwg"))
            return DxfImport::import(zcam, path);
      else {
            Warning("ProjectManager::importFile: unsupported file type: {}", suffix);
            return false;
            }
      markDirty();
      zcam->setCamDirty(true);
      return true;
      }

//---------------------------------------------------------
//   undo
//---------------------------------------------------------

void ProjectManager::undo() {
      if (!canUndo())
            return;
      --_undoIndex;
      _undoStack[_undoIndex]->undo();
      emit undoStateChanged();
      setDirty(true);
      zcam->setCamDirty(true);
      // Cam display is NOT refreshed automatically after undo.
      // The user triggers it manually via the refresh button.
      if (zcam->project())
            zcam->project()->updateCadLayerVisibility();
      }

//---------------------------------------------------------
//   redo
//---------------------------------------------------------

void ProjectManager::redo() {
      if (!canRedo())
            return;
      _undoStack[_undoIndex]->redo();
      ++_undoIndex;
      emit undoStateChanged();
      setDirty(true);
      zcam->setCamDirty(true);
      // Cam display is NOT refreshed automatically after redo.
      // The user triggers it manually via the refresh button.
      if (zcam->project())
            zcam->project()->updateCadLayerVisibility();
      }

//---------------------------------------------------------
//   writeProjectFile  (stub)
//---------------------------------------------------------

bool ProjectManager::writeProjectFile(const std::string& path) {
      Project* tl = zcam->project();
      if (!tl) {
            Warning("no toplevel");
            return false;
            }
      std::ofstream f(path);
      if (!f.is_open()) {
            Warning("ProjectManager: cannot open for writing:", path);
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
//   readProjectFile  (stub)
//---------------------------------------------------------

bool ProjectManager::readProjectFile(const std::string& path, bool skipCamUpdate) {
      std::ifstream f(path);
      if (!f.is_open()) {
            Warning("ProjectManager: cannot open for reading:", path);
            return false;
            }

      json jdata;
      try {
            f >> jdata;
            std::string version = jdata.value("version", "unknown");
            Debug("ProjectManager: loaded project version <{}>", version);
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

            // Clear the current/hover element pointers BEFORE destroying
            // the old tree to avoid dangling-pointer dereferences in QML.
            // See newProject() for a detailed explanation.
            zcam->setCurrentElement(nullptr);
            zcam->set_hoverElement(nullptr);

            zcam->treeModel()->setRoot(nullptr);
            zcam->set_rootElement(nullptr); // detach scene
            zcam->set_project(nullptr);

            // Process pending deleteLater() calls so the old tree is
            // truly gone before we build the new one.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

            auto root = new RootElement(zcam, nullptr);
            auto top  = new Project(zcam, root);
            root->addChild(top);
            zcam->set_project(top);
            top->fromJson(jdata.at("toplevel"));
            zcam->treeModel()->setRoot(root);
            zcam->set_rootElement(zcam->project()); // build and show the scene

            // Notify QML that the grid element may have changed.
            emit top->gridElementChanged();

            // Re-resolve the Project's Machine* after loading.
            if (zcam->project())
                  zcam->project()->resolveMachine();

            // Initial CAD layer visibility update and CAM refresh so
            // display geometry is populated after load.
            // When skipCamUpdate is true (e.g. at startup when restoring
            // the last project), the expensive Cam::updateCam() call is
            // skipped — the user can trigger it manually via the Cam
            // refresh button.
            if (zcam->project()) {
                  zcam->project()->updateCadLayerVisibility();
                  if (!skipCamUpdate && zcam->project()->cam())
                        zcam->project()->cam()->updateCam();
                  }
            auto func = [] (this auto& self, Element* e) -> void {
                  for (auto c : e->children()) {
                        c->fixup();
                        self(c);
                        }
                  };
            func(zcam->project());
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("JSON parse error:", err.what());
            return false;
            }

      return true;
      }
