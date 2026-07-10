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
#include "cam.h"
#include "text.h"
#include "treemodel.h"
#include "laserlayer.h"

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
            return;
            }
      auto cmd = std::make_unique<PropertyChangeCommand>(element, pn, oldValue, newValue);
      cmd->redo(); // apply the new value immediately
      pushCommand(std::move(cmd));
      setDirty(true);
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
      return openProject(path);
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

bool ProjectManager::newProject(bool clearPersistedPath) {
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
      zcam->set_currentElement(nullptr);
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
      zcam->set_topLevel(nullptr);

      setProjectPath(QString());
      setDirty(false);
      if (clearPersistedPath)
            saveLastProjectPath(QString());

      auto root = new RootElement(zcam, nullptr);
      zcam->set_rootElement(root);
      auto top = new Project(zcam, root);
      zcam->set_topLevel(top);
      auto cad     = new Cad(zcam, top);
      auto cam     = new Cam(zcam, top);
      auto fixture = new Fixture(zcam, cam);
      auto framing = new Framing(zcam, fixture);
      auto ll      = new LaserLayer(zcam, fixture);
      auto stock   = new Stock(zcam, cam);

      auto layer = new Layer(zcam, cad);
      auto text  = new Text(zcam, layer);
      text->setColor("yellow");

      text->set_text("ZCam00");
      ll->setName(QString("LL-%1").arg(layer->name()));
      ll->set_baseElement(layer);

      // build project tree
      cad->addChild(layer);
      layer->addChild(text);
      top->addChild(cad);
      top->addChild(cam);
      cam->addChild(stock);
      fixture->addChild(framing);
      fixture->addChild(ll);
      cam->addChild(fixture);
      root->addChild(top);
      update();

      emit projectCreated();
      return true;
      }

//---------------------------------------------------------
//   update
//    updates the tree view and triggers update of 3DCanvas
//---------------------------------------------------------

void ProjectManager::update() {
      zcam->treeModel()->setRoot(zcam->rootElement()); // update project tree view
      zcam->set_rootElement(zcam->topLevel());         // build and show the scene
      }

//---------------------------------------------------------
//   openProject
//---------------------------------------------------------

bool ProjectManager::openProject(const QString& path) {
      if (path.isEmpty()) {
            Warning("ProjectManager::openProject: empty path");
            return false;
            }
      if (!readProjectFile(path.toStdString())) {
            Warning("ProjectManager::openProject: failed to read", path);
            return false;
            }
      setProjectPath(path);
      clearUndoStack();
      setDirty(false);
      saveLastProjectPath(path);
      emit projectLoaded(path);
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
      // TODO: dispatch to format-specific importers
      Debug("ProjectManager::importFile stub {}", path);
      markDirty();
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
      }

//---------------------------------------------------------
//   writeProjectFile  (stub)
//---------------------------------------------------------

bool ProjectManager::writeProjectFile(const std::string& path) {
      Project* tl = zcam->topLevel();
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

bool ProjectManager::readProjectFile(const std::string& path) {
      std::ifstream f(path);
      if (!f.is_open()) {
            Warning("ProjectManager: cannot open for reading:", path);
            return false;
            }

      json jdata;
      try {
            f >> jdata;
            std::string version = jdata.value("version", "unknown");
            Debug("ProjectManager: loaded project v", version);
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
            zcam->set_currentElement(nullptr);
            zcam->set_hoverElement(nullptr);

            zcam->treeModel()->setRoot(nullptr);
            zcam->set_rootElement(nullptr); // detach scene
            zcam->set_topLevel(nullptr);

            // Process pending deleteLater() calls so the old tree is
            // truly gone before we build the new one.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

            auto root = new RootElement(zcam, nullptr);
            auto top  = new Project(zcam, root);
            root->addChild(top);
            zcam->set_topLevel(top);
            top->fromJson(jdata.at("toplevel"));
            zcam->treeModel()->setRoot(root);
            Debug("toplevel {}", (void*)(zcam->topLevel()));
            zcam->set_rootElement(zcam->topLevel()); // build and show the scene
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("JSON parse error:", err.what());
            return false;
            }

      return true;
      }
