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
#include <QJSEngine>
#include <QJSValue>
#include <QMetaMethod>
#include <QPointer>
#include <QVariant>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include "logger.h"

#include "geometryapi.h"
#include "scriptapi.h"

class ZCam;
class Element;
class PropertyBinding;

//---------------------------------------------------------
//   ScriptEngine
//    Singleton JavaScript engine for property scripting.
//    All named project-tree elements are registered in a
//    namespace tree mirroring the project tree, e.g.
//
//      project.cad.layer1.rectangle2.width
//
//    so scripts can read properties of other elements.  When
//    a dependency property changes, all dependent properties
//    are re-evaluated and updated.
//---------------------------------------------------------

class ScriptEngine : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_SINGLETON

      // ── Named-script session state (mirrors AIAgent sessions) ──────
      Q_PROPERTY(QStringList scriptList READ scriptList NOTIFY scriptListChanged)
      Q_PROPERTY(int currentScript READ currentScript WRITE setCurrentScript NOTIFY currentScriptChanged)
      Q_PROPERTY(QString currentScriptName READ currentScriptName NOTIFY currentScriptNameChanged)

      QJSEngine _engine;
      ZCam* _zcam {nullptr};
      ScriptApi* _scriptApi {nullptr};
      GeometryApi* _geometryApi {nullptr};

    public:
      void setZcam(ZCam* zc) {
            _zcam = zc;
            if (_scriptApi)
                  _scriptApi->setZcam(zc);
            _scriptApi->setEngine(&_engine);
            }
      struct EvalResult {
            QVariant value; ///< result value (empty QVariant on error)
            QString error;  ///< error message (empty on success)
            void setError(const QString& e) { error = e; }
            };
      /// Registry of active property bindings.  Every binding of
      /// (element, property) appears exactly once (either as an
      /// exact scalar binding or as a component binding whose
      /// propName has the form "prop.x").
      QVector<PropertyBinding*> _bindings;

      /// Currently evaluating binding (guards against self-updates
      /// triggered by the element's NOTIFY signals during apply).
      PropertyBinding* _evaluatingBinding {nullptr};

      /// Elements currently being destroyed — bindings referencing
      /// them are dropped on the fly.
      friend class Element;
      friend class PropertyBinding;
      friend struct ScriptEngineDependencyTracker;

      /// True while rebuildRegistry() is in progress.  Used by
      /// Element::addChild() to skip applyDefaultScripts() during
      /// project loading (rebuildRegistry handles it centrally).
      bool _rebuilding {false};

      void registerElement(Element*, QJSValue ns);
      void addElementToTree(Element*);
      void refreshVectorSnapshots(Element* depElement);
      void buildAndSetSnapshot(QJSValue& cur, Element* depElement);

      /// Evaluate a script expression and return its result.
      EvalResult eval(const QString& script);

      /// Evaluate an imperative multi-statement script (loops,
      /// conditions, function defs).  The script has access to
      /// the global objects: `zcam` (imperative API), `geom`
      /// (geometry helpers), `project` (element namespace tree),
      /// `config`.  No `with()` scope chain is applied — the
      /// script must use fully qualified names like
      /// `zcam.createElement(...)` or `project.cad.layer1.rect1`.
      /// *timeoutMs* limits the execution time (QJSEngine::setInterrupted).
      EvalResult evalImperative(const QString& script, int timeoutMs = 10000);

    public:
      ScriptEngine(QObject* parent = nullptr);
      ~ScriptEngine();

      /// Rebuild the registered element namespace tree from the
      /// current project tree.  Called whenever the project is
      /// (re)created or loaded.  When keepBindings is true, all
      /// existing bindings are re-evaluated afterwards (used for
      /// undo/redo where elements survive); otherwise the bindings
      /// are rebuilt from the elements' stored scripts.
      void rebuildRegistry(bool keepBindings);

      /// Apply default scripts declared in the element's properties()
      /// JSON to properties that have no manually-set script.  Each
      /// property with a "script" metadata gets an active binding.
      /// Called for newly created elements and during project loading
      /// when no stored script exists for that property.
      void applyDefaultScripts(Element*);

      /// Register an element and all its descendants in the JS
      /// namespace tree and apply default scripts.  Used after
      /// batch operations (e.g. DXF import) that bypass the
      /// per-element addChild/setName hooks.
      void registerSubtree(Element* root);

      /// Re-evaluate this binding and write the result to the
      /// target property (unless the value did not change).
      void refreshBinding(PropertyBinding*);

      /// Bind the scalar property prop of element to the script.
      /// An existing binding for the same (element, prop) is
      /// replaced.  The property is re-evaluated immediately.
      void createBinding(Element*, QString prop, QString script);
      /// Bind component comp (0=x, 1=y, 2=z) of a vector property.
      void createBinding(Element*, QString prop, int comp, QString script);
      /// Remove the binding for (element, prop) — scalar or any of
      /// its components — and return true if one existed.
      bool removeBinding(Element*, const QString& prop);
      /// Remove every binding of the given element (called from
      /// ~Element before destruction completes).
      bool removeBindingsFor(Element*);
      /// Return the binding for (element, prop) — scalar or any of
      /// its components — or nullptr.
      PropertyBinding* bindingFor(Element*, const QString& prop) const;
      /// Pause or resume a binding without deleting it.  When
      /// deactivated, the script text is kept on the element and
      /// the binding object stays alive, but evaluate() is a
      /// no-op.  Returns true if a binding was found.
      bool setBindingActive(Element*, const QString& prop, bool active);
      /// Check whether the binding for (element, prop) is active.
      bool isBindingActive(Element*, const QString& prop) const;

      /// Test an expression without creating a binding.  Returns
      /// QJSValue::Error on evaluation failure (checkable in QML
      /// with isError()), otherwise the converted result value.
      Q_INVOKABLE QVariant testScript(const QString&);

      /// Evaluate an imperative script from QML.  Returns a map:
      ///   { ok: true, value: ... }  or  { ok: false, error: "..." }
      Q_INVOKABLE QVariantMap evalImperativeQml(const QString& script, int timeoutMs = 10000);

      //--------------------------------------------------------------------
      //     print()
      //    JavaScript global function `print(msg1, msg2, ...)` that
      //    forwards its stringified arguments to the Script Console
      //    (QML) via the scriptPrinted() signal and to the app log.
      //    The JS wrapper (registered by ensurePrintGlobal()) handles
      //    argument conversion (JSON.stringify for objects, String()
      //    for primitives) before calling this C++ method.
      //--------------------------------------------------------------------

      /// Print a message to the script console and the app log.
      Q_INVOKABLE void print(const QString& msg);
      /// Register the `print` global JS function on the engine's
      /// global object.  Idempotent — safe to call multiple times.
      void ensurePrintGlobal();

      //--------------------------------------------------------------------
      //     Named scripts
      //    Managed like the AI sessions: each script is one file in
      //    ~/ZCam/scripts/Script-yy-MM-dd-n.js (mirroring the AI
      //    sessions in ~/ZCam/ai_sessions/Session-yy-MM-dd-n.json).
      //    The Script Console (QML) lists them in a ComboBox bound to
      //    the scriptList property, creates new ones (+) via newScript(),
      //    deletes the current one (-) via deleteScriptByIndex() and
      //    renames them by editing the name via renameScript().
      //    Script names are auto-generated like sessions (date + counter);
      //    the "Script-" prefix distinguishes them from AI sessions.
      //--------------------------------------------------------------------
      /// Full path of the scripts directory (created on demand).
      Q_INVOKABLE QString scriptsDirectory() const;
      /// Sorted list of existing script display names (without the
      /// "Script-" prefix and ".js" suffix).  Drives the QML ComboBox
      /// via the scriptList property.
      Q_INVOKABLE QStringList scriptNames();
      /// Q_PROPERTY accessor for the script list (same as scriptNames()).
      QStringList scriptList() const { return _scriptList; }
      /// Q_PROPERTY accessor: index of the currently selected script.
      int currentScript() const { return _currentScriptIndex; }
      /// Q_PROPERTY setter: select the script at *index* (loads it
      /// and updates currentScriptName).
      void setCurrentScript(int index);
      /// Q_PROPERTY accessor: file name of the currently selected
      /// script ("" when none).
      QString currentScriptName() const { return _currentScriptName; }
      /// Select the script at the given index (loads it from disk).
      Q_INVOKABLE void selectScript(int index);

      /// Source text of the named script ("" if it does not exist).
      /// *name* is a display name (without "Script-" prefix).
      Q_INVOKABLE QString scriptText(const QString& name);
      /// Create or overwrite the script <name>.js with <content>.
      Q_INVOKABLE bool saveScript(const QString& name, const QString& content);
      /// Create a brand-new empty script with an auto-generated name
      /// like "Script-yy-MM-dd-n".  When *name* is non-empty it is used
      /// as a base (sanitised and de-duplicated); when empty a name is
      /// auto-generated like the AI sessions.  Returns the display name
      /// (without "Script-" prefix) that was actually created.
      Q_INVOKABLE QString newScript(const QString& name = QString());
      /// Delete the script at the given index.  Returns true on success.
      Q_INVOKABLE bool deleteScriptByIndex(int index);
      /// Delete the named script file.  Returns true if it existed.
      Q_INVOKABLE bool deleteScript(const QString& name);
      /// Rename an existing script.  *newName* is sanitised and
      /// de-duplicated (ignoring <oldName> itself).  Returns the new
      /// name that was actually used ("" on failure).
      Q_INVOKABLE QString renameScript(const QString& oldName, const QString& newName);
      /// Turn a free-form user-typed name into a safe, readable
      /// file-system name (no path separators, no spaces).  Static so
      /// it can be unit-tested.
      static QString sanitizeScriptName(const QString& name);
      /// Append "-1", "-2", ... if <base> is already in <taken>.
      static QString uniqueScriptName(const QString& base, const QStringList& taken);

      /// Evaluate a script expression with *element* as the local
      /// context.  The script is wrapped in nested ``with`` statements
      /// that put the element and each of its ancestors into the JS
      /// scope chain (innermost = element, outermost = topmost ancestor).
      /// This lets the user write bare property names (``width``) and
      /// bare element names (``cad``) which JS resolves via the scope
      /// chain — no text substitution needed.
      EvalResult evalWithContext(const QString& script, Element* element);

      /// Test an expression with element context: the script is
      /// evaluated via evalWithContext() so short names are resolved
      /// through the JS scope chain.  Returns the result value or an
      /// error string (same convention as testScript).
      Q_INVOKABLE QVariant testScriptWithContext(const QString& script, QObject* element);

      /// True when *element* is currently being written by a component
      /// binding for a property whose base name matches *propBase*.
      /// Used by custom setters (e.g. Rectangle::set_size) to skip
      /// lock enforcement when a script binding controls one component.
      bool isWritingComponentBinding(Element* element, const QString& propBase) const;

      /// Create a binding from QML (scalar or component when comp >= 0).
      Q_INVOKABLE void createBindingQml(QObject* element, QString prop, int comp, QString script);
      /// Remove all bindings for (element, prop).  Safe to call when
      /// none exist.
      Q_INVOKABLE void removeBindingQml(QObject* element, QString prop);
      /// Returns the stored script text for (element, prop) — scalar
      /// or, when comp >= 0, for the vector component — or "".
      Q_INVOKABLE QString scriptForQml(QObject* element, const QString& prop, int comp) const;
      /// Returns the current evaluation error for (element, prop),
      /// or "" if there is no binding or the binding evaluates fine.
      Q_INVOKABLE QString scriptErrorQml(QObject* element, const QString& prop, int comp) const;
      /// Returns a comma-separated list of component indices that
      /// have active bindings, e.g. "" or "0,2" or "all".
      Q_INVOKABLE QString boundComponentsQml(QObject* element, const QString& prop) const;

      static QJSValue toJs(QJSEngine&, const QVariant&, QMetaType mt);
      static QVariant fromJs(const QJSValue&, QMetaType mt);
      /// Singleton instance created in main.cpp.  May be nullptr in
      /// early bootstrap; callers must check.
      static ScriptEngine* instance() { return _instance; }
      static void setInstance(ScriptEngine* se) { _instance = se; }

      // ── Named-script session state ────────────────────────────────
      QStringList _scriptList;           ///< sorted display names (no prefix/suffix)
      int _currentScriptIndex {-1};       ///< index into _scriptList, -1 = none
      QString _currentScriptName;        ///< file name of the current script

      /// Generate the next available script file path:
      /// ~/ZCam/scripts/Script-yy-MM-dd-n.js  (mirrors AI sessions which
      /// use Session-yy-MM-dd-n.json).
      QString nextScriptPath() const;
      /// Re-read the scripts directory and rebuild _scriptList.
      void refreshScriptList();
      /// Reconstruct the full file name ("Script-…-n.js") from a
      /// compact display name stored in _scriptList.
      QString scriptFileName(int index) const;

    signals:
      /// Emitted when a script calls print(msg).  The QML ScriptPanel
      /// connects to this signal to display the output in the console.
      void scriptPrinted(const QString& msg);
      void scriptListChanged();
      void currentScriptChanged();
      void currentScriptNameChanged();

    private:
      static inline ScriptEngine* _instance = nullptr;
      };

//---------------------------------------------------------
//   PropertyBinding
//    One active script binding: evaluates a script and writes
//    the result to (element, prop) or to one component of a
//    vector property.
//---------------------------------------------------------

class PropertyBinding : public QObject
      {
      Q_OBJECT
      struct DepInfo {
            QPointer<Element> element;
            QMetaMethod signal;
            int signalIndex {-1};
            QMetaObject::Connection connection;
            };
      ScriptEngine* _engine;
      QPointer<Element> _targetElement;
      QString _targetProp;
      int _targetComp {-1}; ///< -1 = scalar, else 0/1/2 for x/y/z
      QString _script;
      QList<DepInfo> _deps;
      bool _deleted {false}; ///< evaluation failed → do not retry automatically
      bool _active {true};   ///< if false, binding is paused (script kept, not evaluated)

      void bind();
      void disconnectDeps();

      friend class ScriptEngine;
      friend struct ScriptEngineDependencyTracker;

    private Q_SLOTS:
      void dependencyTriggered();

    public:
      PropertyBinding(ScriptEngine*, Element* target, QString prop, int comp, QString script);
      ~PropertyBinding();
      Element* element() const { return _targetElement; }
      /// Property name including component suffix for component
      /// bindings (e.g. "pos" for scalar, "pos.x" for component 0).
      QString property() const {
            return _targetComp < 0 ? _targetProp : QStringLiteral("%1.%2").arg(_targetProp).arg(_targetComp);
            }
      QString script() const { return _script; }
      void setScript(const QString& s) {
            _script  = s;
            _deleted = false;
            }
      bool isActive() const { return _active; }
      void setActive(bool a) { _active = a; }
      void evaluate();
      /// Returns the current evaluation error, or an empty string.
      QString lastError() const { return _deleted ? _error : QString(); }
      QString _error;
      };
