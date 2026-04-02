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
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QIODevice>
#include "logger.h"

//---------------------------------------------------------
//   ProjectManager
//---------------------------------------------------------

ProjectManager::ProjectManager(QObject* parent) : QObject(parent)
      {
      }

//---------------------------------------------------------
//   create  (QML singleton factory)
//---------------------------------------------------------

ProjectManager* ProjectManager::create(QQmlEngine*, QJSEngine*)
      {
      return new ProjectManager();
      }

//---------------------------------------------------------
//   projectName
//---------------------------------------------------------

QString ProjectManager::projectName() const
      {
      if (_projectPath.isEmpty())
            return QStringLiteral("Untitled");
      return QFileInfo(_projectPath).baseName();
      }

//---------------------------------------------------------
//   setDirty
//---------------------------------------------------------

void ProjectManager::setDirty(bool v)
      {
      if (v == _dirty)
            return;
      _dirty = v;
      emit dirtyChanged();
      }

//---------------------------------------------------------
//   setProjectPath
//---------------------------------------------------------

void ProjectManager::setProjectPath(const QString& v)
      {
      if (v == _projectPath)
            return;
      _projectPath = v;
      emit projectPathChanged();
      }

//---------------------------------------------------------
//   markDirty
//---------------------------------------------------------

void ProjectManager::markDirty()
      {
      setDirty(true);
      }

//---------------------------------------------------------
//   clearUndoStack
//---------------------------------------------------------

void ProjectManager::clearUndoStack()
      {
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

bool ProjectManager::checkUnsavedChanges()
      {
      // The actual "Save changes?" dialog is driven from QML.
      // This method is kept as a synchronous fast-path for cases where the
      // project is already clean.
      return !_dirty;
      }

//---------------------------------------------------------
//   newProject
//---------------------------------------------------------

bool ProjectManager::newProject()
      {
      // Caller is responsible for checking unsaved changes via QML dialog
      // before invoking this method.
      clearUndoStack();
      setProjectPath(QString());
      setDirty(false);
      emit projectCreated();
      return true;
      }

//---------------------------------------------------------
//   openProject
//---------------------------------------------------------

bool ProjectManager::openProject(const QString& path)
      {
      if (path.isEmpty()) {
            Warning("ProjectManager::openProject: empty path");
            return false;
            }
      if (!readProjectFile(path)) {
            Warning("ProjectManager::openProject: failed to read", path);
            return false;
            }
      setProjectPath(path);
      clearUndoStack();
      setDirty(false);
      emit projectLoaded(path);
      return true;
      }

//---------------------------------------------------------
//   save
//---------------------------------------------------------

bool ProjectManager::save()
      {
      if (_projectPath.isEmpty())
            return false;   // QML should call saveAs with a chosen path
      if (!writeProjectFile(_projectPath))
            return false;
      setDirty(false);
      emit projectSaved(_projectPath);
      return true;
      }

//---------------------------------------------------------
//   saveAs
//---------------------------------------------------------

bool ProjectManager::saveAs(const QString& path)
      {
      if (path.isEmpty())
            return false;
      if (!writeProjectFile(path))
            return false;
      setProjectPath(path);
      setDirty(false);
      emit projectSaved(path);
      return true;
      }

//---------------------------------------------------------
//   importFile
//---------------------------------------------------------

bool ProjectManager::importFile(const QString& path)
      {
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

void ProjectManager::undo()
      {
      if (!canUndo())
            return;
      --_undoIndex;
      // TODO: deserialise _undoStack[_undoIndex] and apply to scene
      emit undoStateChanged();
      markDirty();
      }

//---------------------------------------------------------
//   redo
//---------------------------------------------------------

void ProjectManager::redo()
      {
      if (!canRedo())
            return;
      // TODO: deserialise _undoStack[_undoIndex] and apply to scene
      ++_undoIndex;
      emit undoStateChanged();
      markDirty();
      }

//---------------------------------------------------------
//   writeProjectFile  (stub)
//---------------------------------------------------------

bool ProjectManager::writeProjectFile(const QString& path)
      {
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            Warning("ProjectManager: cannot open for writing:", path);
            return false;
            }
      // Minimal JSON skeleton – replace with full scene serialisation
      QJsonObject root;
      root["version"]     = QStringLiteral("1.0");
      root["application"] = QStringLiteral("zcam");
      // TODO: root["scene"] = scene->toJson();

      QJsonDocument doc(root);
      f.write(doc.toJson(QJsonDocument::Indented));
      return true;
      }

//---------------------------------------------------------
//   readProjectFile  (stub)
//---------------------------------------------------------

bool ProjectManager::readProjectFile(const QString& path)
      {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            Warning("ProjectManager: cannot open for reading:", path);
            return false;
            }
      QJsonParseError err;
      QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
      if (err.error != QJsonParseError::NoError) {
            Warning("ProjectManager: JSON parse error:", err.errorString());
            return false;
            }
      QJsonObject root = doc.object();
      // TODO: scene->fromJson(root["scene"].toObject());
      Debug("ProjectManager: loaded project v", root["version"].toString());
      return true;
      }
