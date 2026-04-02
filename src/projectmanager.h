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

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QJSEngine>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

//---------------------------------------------------------
//   ProjectManager
//
//   Singleton that owns the lifecycle of a ZCam project
//   file: new / open / save / save-as / import, plus an
//   undo/redo command stack (stubs ready for expansion).
//---------------------------------------------------------

class ProjectManager : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_SINGLETON

      Q_PROPERTY(bool   dirty       READ dirty       NOTIFY dirtyChanged)
      Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectPathChanged)
      Q_PROPERTY(QString projectName READ projectName NOTIFY projectPathChanged)
      Q_PROPERTY(bool   canUndo     READ canUndo     NOTIFY undoStateChanged)
      Q_PROPERTY(bool   canRedo     READ canRedo     NOTIFY undoStateChanged)

    signals:
      void dirtyChanged();
      void projectPathChanged();
      void undoStateChanged();

      /// Emitted when a brand-new empty project was created.
      void projectCreated();
      /// Emitted after a project file was successfully loaded.
      void projectLoaded(const QString& path);
      /// Emitted after the project was saved.
      void projectSaved(const QString& path);

    public:
      explicit ProjectManager(QObject* parent = nullptr);

      // ── Properties ────────────────────────────────────────────────────────
      bool    dirty()       const { return _dirty; }
      QString projectPath() const { return _projectPath; }
      QString projectName() const;
      bool    canUndo()     const { return _undoIndex > 0; }
      bool    canRedo()     const { return _undoIndex < static_cast<int>(_undoStack.size()); }

      // ── QML-callable API ──────────────────────────────────────────────────

      /// Start a fresh, unnamed project.  Returns false if user cancelled.
      Q_INVOKABLE bool newProject();

      /// Open a project from *path*.  Pass an empty string to trigger the
      /// file-dialog logic from C++ (alternatively drive the dialog from QML).
      Q_INVOKABLE bool openProject(const QString& path);

      /// Save to the current path; falls through to saveAs if none is set.
      Q_INVOKABLE bool save();

      /// Save under a new path.
      Q_INVOKABLE bool saveAs(const QString& path);

      /// Import an external file into the current project.
      Q_INVOKABLE bool importFile(const QString& path);

      /// Undo the last command.
      Q_INVOKABLE void undo();

      /// Redo the previously undone command.
      Q_INVOKABLE void redo();

      /// Mark the project as modified (called by edit operations).
      Q_INVOKABLE void markDirty();

      /// Check for unsaved changes; returns true when it is safe to proceed.
      /// If dirty, this pops the "Save changes?" dialog via a signal so that
      /// QML can display it asynchronously.
      Q_INVOKABLE bool checkUnsavedChanges();

      // ── Singleton factory ─────────────────────────────────────────────────
      static ProjectManager* create(QQmlEngine*, QJSEngine*);

    private:
      // ── Internal helpers ──────────────────────────────────────────────────
      bool writeProjectFile(const QString& path);
      bool readProjectFile(const QString& path);
      void clearUndoStack();
      void setDirty(bool v);
      void setProjectPath(const QString& v);

      // ── State ─────────────────────────────────────────────────────────────
      bool    _dirty        { false };
      QString _projectPath  {};              ///< empty = no file yet

      /// Minimal undo stack: each entry is an opaque serialised snapshot.
      /// Replace with a proper Command pattern as the app grows.
      QList<QByteArray> _undoStack {};
      int               _undoIndex { 0 };
      };
