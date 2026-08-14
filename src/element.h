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
#include <QtQml/qqmlregistration.h>
#include <QHash>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "logger.h"
// #include "macros.h"

class ZCam;
class ScriptEngine;

//---------------------------------------------------------
//   Element
//    base class for Element3d, CamElement
//    for use in ProjectTreeView
//---------------------------------------------------------

class Element : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      Q_PROPERTY(QList<Element*> children READ children)
      Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
      Q_PROPERTY(bool expanded READ expanded WRITE setExpanded NOTIFY expandedChanged)

      // ── Scripting ──────────────────────────────────────────────────
      //   A property whose value is computed by a JavaScript expression
      //   is shown in the inspector with an f(x) button.  The scripts
      //   are stored per element and persisted in the project file.
      //   The Q_PROPERTY declarations are kept for compatibility with
      //   the meta-object system (e.g. QML scriptChanged signal), but
      //   the actual storage is in the _scripts map (protected).

      QList<Element*> _children;
      Element* _parent {nullptr};
      QString _name;
      bool _expanded = false; ///< persistent expand state in TreeView

      // this is a global list of all elements, accessible by name
      static QHash<QString, Element*> names;

      friend class ScriptEngine;
      friend class PropertyBinding;

    protected:
      ZCam* zcam;

      // Scripting state (see ScriptEngine).  Each entry in
      // _scripts maps a property name to its script text and
      // active flag.  _scriptComp holds the optional component
      // bindings for vector2d/vector3d properties: index 0..2
      // maps to x/y/z.  These are protected so derived classes
      // (e.g. Config) can serialise/deserialise them in toJson/
      // fromJson.
      struct ScriptEntry {
            QString script;
            bool active {true};
            };
      QHash<QString, ScriptEntry> _scripts;
      QStringList _scriptComp       = {QString(), QString(), QString()};
      QStringList _scriptCompProp   = {QString(), QString(), QString()};
      QList<bool> _scriptCompActive = {true, true, true}; ///< component binding active states

    signals:
      void nameChanged();
      void expandedChanged();
      void childAdded(Element*);
      void childRemoved(Element*);
      void scriptChanged();
      void scriptPropChanged();

    public:
      bool _saveChildren {true};
      Element(ZCam* zcam, Element* parent = nullptr);
      virtual ~Element();
      Q_INVOKABLE virtual QString typeName() = 0;
      virtual const std::string_view properties() const { return ""; }
      const QList<Element*>& children() const { return _children; }
      QList<Element*>& children() { return _children; }
      Q_INVOKABLE Element* parent() const { return _parent; }
      virtual json toJson() const;
      virtual void fromJson(const json&);
      void addChild(Element* e);
      void removeChild(Element* e) {
            _children.removeAll(e);
            if (e->_parent == this)
                  e->_parent = nullptr;
            emit childRemoved(e);
            }
      static void clearProject();
      static Element* byName(const QString& name) { return names.value(name); }
      /// The global element name registry (used by the script
      /// engine's dependency scan).  Names are unique and sanitized
      /// to valid JS identifiers by setName().
      static const QHash<QString, Element*>& namesMap() { return names; }
      void setName(QString v);
      QString name() const { return _name; }
      Q_INVOKABLE virtual bool nameEditable() const { return false; }
      bool expanded() const { return _expanded; }
      void setExpanded(bool v) {
            if (v != _expanded) {
                  _expanded = v;
                  emit expandedChanged();
                  }
            }
      ZCam* zcamInstance() const { return zcam; }
      virtual bool saveChildren() const { return _saveChildren; }
      virtual void fixup() {}
      // ── Scripting accessors ────────────────────────────────────────
      /// Returns the script text for the given property, or empty.
      QString script(const QString& prop = QString()) const {
            if (prop.isEmpty()) {
                  for (auto it = _scripts.constBegin(); it != _scripts.constEnd(); ++it)
                        if (!it.value().script.isEmpty())
                              return it.value().script;
                  return {};
                  }
            return _scripts.value(prop).script;
            }
      /// Set the script text for the given property.
      void setScript(const QString& prop, const QString& s) {
            if (s.isEmpty()) {
                  _scripts.remove(prop);
                  emit scriptChanged();
                  }
            else {
                  auto& entry = _scripts[prop];
                  if (s != entry.script) {
                        entry.script = s;
                        emit scriptChanged();
                        }
                  }
            }
      /// True when this element has at least one scalar script.
      bool hasScript() const { return !_scripts.isEmpty(); }
      /// True when this element has a script for *prop*.
      bool hasScriptFor(const QString& prop) const {
            return _scripts.contains(prop) && !_scripts.value(prop).script.isEmpty();
            }
      QString scriptComp(int comp) const {
            return (comp >= 0 && comp < _scriptComp.size()) ? _scriptComp[comp] : QString();
            }
      void setScriptComp(int comp, const QString& s) {
            if (comp >= 0 && comp < _scriptComp.size() && s != _scriptComp[comp])
                  _scriptComp[comp] = s;
            }
      QString scriptCompProp(int comp) const {
            return (comp >= 0 && comp < _scriptCompProp.size()) ? _scriptCompProp[comp] : QString();
            }
      void setScriptCompProp(int comp, const QString& p) {
            if (comp >= 0 && comp < _scriptCompProp.size() && p != _scriptCompProp[comp])
                  _scriptCompProp[comp] = p;
            }
      bool hasScriptComp(int comp) const {
            return comp >= 0 && comp < _scriptComp.size() && !_scriptComp[comp].isEmpty() &&
                   !_scriptCompProp[comp].isEmpty();
            }
      /// Clear all stored scripts (called when a binding is removed).
      void clearScripts() {
            _scripts.clear();
            for (int i = 0; i < _scriptComp.size(); ++i) {
                  _scriptComp[i].clear();
                  _scriptCompProp[i].clear();
                  _scriptCompActive[i] = true;
                  }
            }
      /// Clear only the stored scripts for the given base property
      /// (scalar binding and vector-component bindings).
      void clearScriptsFor(const QString& prop) {
            _scripts.remove(prop);
            for (int i = 0; i < _scriptComp.size(); ++i) {
                  if (_scriptCompProp[i] == prop) {
                        _scriptComp[i].clear();
                        _scriptCompProp[i].clear();
                        }
                  }
            }
      };

//---------------------------------------------------------
//   isType
//---------------------------------------------------------

template <typename T> inline bool isType(QObject* o) {
      return qobject_cast<T*>(o) != nullptr;
      }

template <typename T> inline bool isType(const QObject* o) {
      return qobject_cast<const T*>(o) != nullptr;
      }

//---------------------------------------------------------
//   toType
//---------------------------------------------------------

#ifndef NDEBUG
template <typename T> static inline T* toType(QObject* e) {
      if (isType<T>(e))
            return static_cast<T*>(e);
      Fatal("bad type");
      }

#else
template <typename T> static inline T* toType(QObject* e) {
      return static_cast<T*>(e);
      }

#endif
