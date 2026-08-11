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
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include "logger.h"

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

      QJSEngine _engine;
      ZCam* _zcam {nullptr};

    public:
      void setZcam(ZCam* zc) { _zcam = zc; }
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

      /// Evaluate a script expression and return its result.
      EvalResult eval(const QString& script);

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
