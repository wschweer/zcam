//=============================================================================
//  ZCam - manufactoring tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "scriptengine.h"
#include "element.h"
#include "element3d.h"
#include "zcam.h"
#include "project.h"
#include "config.h"
#include "logger.h"
#include "propertyjson.h"
#include "scriptapi.h"

#include <QColor>
#include <functional>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QVector2D>
#include <QVector3D>

//---------------------------------------------------------
//   dependencySlotIndex
//    Meta-object method index of PropertyBinding::dependencyTriggered(),
//    used to wire dependency NOTIFY signals to the binding via
//    QMetaObject::connect(sender, signalIdx, receiver, slotIdx).
//---------------------------------------------------------

static int dependencySlotIndex() {
      static int idx = PropertyBinding::staticMetaObject.indexOfMethod("dependencyTriggered()");
      return idx;
      }

//---------------------------------------------------------
//   DependencyScan
//    Static analysis of a script to find direct dependencies
//    on element properties.  The script is matched against the
//    project's element names; for each match, the trailing
//    ".property" segments are checked against the element's
//    meta-object.
//
//    Dependency rules:
//      • If the script uses the element as a scalar (name only)
//        or with an unknown property, we bind to ALL notify signals.
//      • If the script references "element.prop", we bind only to
//        the notify signal of "prop".
//
//    A small verification pass runs right after binding: any
//    signal emitted by the dependency elements while the script
//    is being evaluated is added as an additional dependency.
//    This allows scripts like "Math.max(width, height)" to bind
//    to width AND height even when the static pass could only
//    find one of them.
//---------------------------------------------------------

namespace {
static bool isValidIdentifier(const QString& s) {
      if (s.isEmpty() || !(s[0].isLetter() || s[0] == u'_'))
            return false;
      for (int i = 1; i < s.size(); ++i)
            if (!(s[i].isLetterOrNumber() || s[i] == u'_'))
                  return false;
      return true;
      }

static bool hasProperty(QObject* obj, const QString& propName) {
      if (!obj)
            return false;
      const QMetaObject* meta = obj->metaObject();
      return meta->indexOfProperty(propName.toUtf8().constData()) >= 0;
      }

struct DependencyScanResult {
      QList<Element*> elements;                   ///< elements the script may depend on
      QHash<Element*, QSet<QString>> directProps; ///< valid direct property references per element
      QSet<QString> scalarElements;               ///< names of elements used as scalar
      };

static DependencyScanResult scanDependencies(const QString& script, const QHash<QString, Element*>& names,
    const QSet<QString>& ownName, Element* contextElement = nullptr) {
      DependencyScanResult out;

      // Words that are definitely not element names.
      static const QSet<QString> keywords {
         QStringLiteral("project"),
         QStringLiteral("Math"),
         QStringLiteral("Number"),
         QStringLiteral("String"),
         QStringLiteral("Boolean"),
         QStringLiteral("Array"),
         QStringLiteral("Object"),
         QStringLiteral("Date"),
         QStringLiteral("JSON"),
         QStringLiteral("undefined"),
         QStringLiteral("null"),
         QStringLiteral("true"),
         QStringLiteral("false"),
         QStringLiteral("NaN"),
         QStringLiteral("Infinity"),
         QStringLiteral("isNaN"),
         QStringLiteral("isFinite"),
         QStringLiteral("parseInt"),
         QStringLiteral("parseFloat"),
         QStringLiteral("console"),
         QStringLiteral("Qt"),
         QStringLiteral("function"),
         QStringLiteral("var"),
         QStringLiteral("let"),
         QStringLiteral("const"),
         QStringLiteral("return"),
         QStringLiteral("if"),
         QStringLiteral("else"),
         QStringLiteral("for"),
         QStringLiteral("while"),
         QStringLiteral("do"),
         QStringLiteral("switch"),
         QStringLiteral("case"),
         QStringLiteral("default"),
         QStringLiteral("break"),
         QStringLiteral("continue"),
         QStringLiteral("new"),
         QStringLiteral("this"),
         QStringLiteral("typeof"),
         QStringLiteral("instanceof"),
         QStringLiteral("in"),
         QStringLiteral("of"),
         QStringLiteral("delete"),
         QStringLiteral("void"),
         QStringLiteral("throw"),
         QStringLiteral("try"),
         QStringLiteral("catch"),
         QStringLiteral("finally"),
            };

      // Match single identifiers; the property access (if any) is
      // extracted manually from the text following the match, so
      // chained expressions like "layer1.rectangle2.size.x" still
      // find EVERY identifier (regex globalMatch cannot overlap).
      QRegularExpression identifierRe {QStringLiteral(R"([A-Za-z_$][A-Za-z0-9_$]*)")};
      QRegularExpression propRe {QStringLiteral(R"(^\.([A-Za-z_$][A-Za-z0-9_$]*))")};
      auto it = identifierRe.globalMatch(script);
      while (it.hasNext()) {
            auto match   = it.next();
            QString name = match.captured(0);
            // property directly following this identifier ("element.prop")
            QString prop;
            auto pm = propRe.match(script.sliced(match.capturedEnd()));
            if (pm.hasMatch())
                  prop = pm.captured(1);
            if (keywords.contains(name))
                  continue;

            // Check if this is a property access after a dot.
            bool isPropertyAccess = false;
            if (match.capturedStart() > 0 && script.at(match.capturedStart() - 1) == u'.')
                  isPropertyAccess = true;

            // We simply check every identifier against the element name map.
            Element* el = names.value(name);
            if (!el) {
                  // If we have a context element, try to resolve the bare
                  // identifier as a property of the context element or any
                  // of its ancestors.
                  if (contextElement && !isPropertyAccess && !prop.isEmpty()) {
                        Element* e = contextElement;
                        while (e) {
                              if (hasProperty(e, name)) {
                                    el = e;
                                    break;
                                    }
                              e = e->parent();
                              }
                        }
                  if (!el)
                        continue;
                  }
            else if (isPropertyAccess) {
                  // Identifier after '.' followed by a property name.
                  // If it matches a known element name, it's a path
                  // component (e.g. "rectangle2" in
                  // "project.cad.layer1.rectangle2.size.x") — fall
                  // through and track it as a dependency element.
                  // Otherwise it's a property access on an unknown
                  // object — skip.
                  }
            // Skip self-reference: a binding must never depend on its
            // own target element (would create an infinite loop).
            if (ownName.contains(name))
                  continue;
            if (!out.elements.contains(el)) {
                  out.elements.append(el);
                  out.directProps.insert(el, {});
                  }
            if (prop.isEmpty()) {
                  // Using the element as a scalar: depend on everything.
                  out.scalarElements.insert(el->name());
                  continue;
                  }
            // Validate that "prop" is a real property of the element.
            // Non-property references (e.g. child-element names such as
            // "layer1.rectangle2") are simply ignored — they do not
            // create a change-dependency on this element.
            //
            // Special case: if *name* was resolved via the context
            // element (i.e. it is a Q_PROPERTY like "size" or "pos"),
            // and *prop* is x/y/z (a vector component), then track the
            // *property* (e.g. "size") rather than the component name.
            // This allows scripts like "size.y * 0.5" to correctly
            // bind to the sizeChanged notify signal.
            const QMetaObject* meta = el->metaObject();
            QByteArray propBytes    = prop.toUtf8();
            bool found              = meta->indexOfProperty(propBytes.constData()) >= 0;
            if (found) {
                  out.directProps[el].insert(prop);
                  }
            else if ((prop == u"x" || prop == u"y" || prop == u"z")) {
                  // Check if *name* is a Q_PROPERTY of type QVector2D
                  // or QVector3D — if so, track it as the dependency.
                  int pi = meta->indexOfProperty(name.toUtf8().constData());
                  if (pi >= 0) {
                        QMetaProperty pmp = meta->property(pi);
                        int typeId        = pmp.metaType().id();
                        if (typeId == QMetaType::QVector2D || typeId == QMetaType::QVector3D)
                              out.directProps[el].insert(name);
                        }
                  }
            }
      return out;
      }

// Determine which notify signals of the given element the script
// depends on.  If the element is used as a scalar, connect to ALL
// notify signals; otherwise connect only to the directly referenced
// properties with NOTIFY signals.
static QList<QMetaMethod> dependencySignals(Element* el, bool scalarUse, const QSet<QString>& directProps) {
      QList<QMetaMethod> ret;
      const QMetaObject* meta = el->metaObject();
      // Start at Element's propertyOffset so inherited Element3d /
      // subclass properties (pos, size, corner …) are included,
      // but skip QObject-internal properties (objectName, destroy).
      for (int i = Element::staticMetaObject.propertyOffset(); i < meta->propertyCount(); ++i) {
            QMetaProperty mp = meta->property(i);
            if (!mp.hasNotifySignal())
                  continue;
            QString name = QString::fromUtf8(mp.name());
            if (!(scalarUse || directProps.contains(name)))
                  continue;
            QMetaMethod sig = mp.notifySignal();
            if (sig.isValid() && !ret.contains(sig))
                  ret.append(sig);
            }
      return ret;
      }

      } // namespace

//---------------------------------------------------------
//   ScriptEngine
//---------------------------------------------------------

ScriptEngine::ScriptEngine(QObject* parent) : QObject(parent) {
      _geometryApi = new GeometryApi(this);
      _scriptApi   = new ScriptApi(this);
      ensurePrintGlobal();
      }

ScriptEngine::~ScriptEngine() = default;

//---------------------------------------------------------
//   rebuildRegistry
//    Rebuild the script namespace tree from the current project.
//    When keepBindings is false (loading a project), bindings
//    are re-created from each element's stored `script*` JSON
//    properties after the namespace is registered.
//---------------------------------------------------------

void ScriptEngine::rebuildRegistry(bool keepBindings) {
      // Drop all runtime bindings – they will be re-created below
      // from the element-side stored scripts.
      qDeleteAll(_bindings);
      _bindings.clear();
      _evaluatingBinding = nullptr;
      _rebuilding        = true;

      // Reset the global namespace tree.
      QJSValue global = _engine.globalObject();
      global.setProperty(QStringLiteral("project"), _engine.newObject());

      // Register Config as a top-level namespace object ("config")
      // so scripts can reference config properties like
      // "config.machinesDirectory".
      if (_zcam && _zcam->config()) {
            QJSValue configObj = _engine.newQObject(_zcam->config());
            global.setProperty(QStringLiteral("config"), configObj);
            refreshVectorSnapshots(_zcam->config());
            }

      // Register the imperative API objects ("zcam" and "geom") as
      // global namespace objects so scripts can call
      //   zcam.createElement("rectangle", 10, 20)
      //   geom.regularPolygon(0, 0, 10, 6)
      // These persist across rebuilds (they are not tied to the
      // project tree), so we only register them once.
      if (_scriptApi && !global.property(QStringLiteral("zcam")).isObject()) {
            _scriptApi->setZcam(_zcam);
            _scriptApi->setEngine(&_engine);
            QJSValue zcamObj = _engine.newQObject(_scriptApi);
            global.setProperty(QStringLiteral("zcam"), zcamObj);
            }
      if (_geometryApi && !global.property(QStringLiteral("geom")).isObject()) {
            QJSValue geomObj = _engine.newQObject(_geometryApi);
            global.setProperty(QStringLiteral("geom"), geomObj);
            }

      if (!_zcam || !_zcam->project())
            return;

      // Register every named element under project.<path>.<name>.
      registerElement(_zcam->project(), _engine.globalObject().property(QStringLiteral("project")));

      // Re-create persisted bindings from each element's script JSON,
      // then apply default scripts from the properties() JSON for
      // properties that have no stored script.
      if (!keepBindings) {
            // Restore Config's scripts.
            if (_zcam && _zcam->config()) {
                  Config* cfg = _zcam->config();
                  for (auto it = cfg->_scripts.constBegin(); it != cfg->_scripts.constEnd(); ++it)
                        if (!it.key().isEmpty() && !it.value().script.isEmpty())
                              createBinding(cfg, it.key(), it.value().script);
                  for (int comp = 0; comp < 3; ++comp) {
                        if (cfg->hasScriptComp(comp)) {
                              const QString p = cfg->scriptCompProp(comp);
                              if (!p.isEmpty() && !cfg->scriptComp(comp).isEmpty())
                                    createBinding(cfg, p, comp, cfg->scriptComp(comp));
                              }
                        }
                  applyDefaultScripts(cfg);
                  }

            std::function<void(Element*)> func = [this, &func](Element* e) -> void {
                  if (!e)
                        return;
                  // Restore all scalar scripts from the map.
                  for (auto it = e->_scripts.constBegin(); it != e->_scripts.constEnd(); ++it)
                        if (!it.key().isEmpty() && !it.value().script.isEmpty())
                              createBinding(e, it.key(), it.value().script);
                  for (int comp = 0; comp < 3; ++comp) {
                        if (e->hasScriptComp(comp)) {
                              const QString p = e->scriptCompProp(comp);
                              if (!p.isEmpty() && !e->scriptComp(comp).isEmpty())
                                    createBinding(e, p, comp, e->scriptComp(comp));
                              }
                        }
                  // Apply default scripts from properties() JSON for
                  // properties that have no stored script.
                  applyDefaultScripts(e);
                  for (Element* c : e->children())
                        func(c);
                  };
            func(_zcam->project());
            }

      // Evaluate everything once so initial values propagate.
      _rebuilding = false;
      for (PropertyBinding* b : std::as_const(_bindings))
            b->evaluate();
      }

//---------------------------------------------------------
//   applyDefaultScripts
//    Apply default scripts declared in the element's properties()
//    JSON.  For each property that has a "script" metadata, create
//    an active binding — but only if the element does not already
//    have a manually-set script for that property (stored script
//    from a saved project or user interaction takes precedence).
//---------------------------------------------------------

void ScriptEngine::applyDefaultScripts(Element* element) {
      if (!element)
            return;
      // properties() is defined on Element3d, not on Element itself.
      // Use the metaObject to check if this is an Element3d subclass.
      auto* e3d = qobject_cast<Element3d*>(element);
      if (!e3d)
            return;
      std::string_view propStr = e3d->properties();
      if (propStr.empty())
            return;
      auto defaults = propjson::allDefaultScripts(propStr);
      if (defaults.empty())
            return;
      for (const auto& [name, script] : defaults) {
            QString propQ   = QString::fromStdString(name);
            QString scriptQ = QString::fromStdString(script);
            // Skip if the element already has a stored script for this
            // property (scalar or any vector component).
            if (element->hasScriptFor(propQ))
                  continue;
            bool hasComp = false;
            for (int c = 0; c < 3; ++c) {
                  if (element->hasScriptComp(c) && element->scriptCompProp(c) == propQ) {
                        hasComp = true;
                        break;
                        }
                  }
            if (hasComp)
                  continue;
            // Skip if a binding already exists for this property.
            if (bindingFor(element, propQ))
                  continue;
            // Create the binding as a scalar binding (comp = -1).
            // The default script is active by default.
            createBinding(element, propQ, scriptQ);
            }
      }

//--------------------------------------------------------------------
//     registerSubtree
//--------------------------------------------------------------------

void ScriptEngine::registerSubtree(Element* root) {
      if (!root || !_zcam || !_zcam->project())
            return;
      QJSValue global = _engine.globalObject();
      QJSValue ns     = global.property(QStringLiteral("project"));
      if (!ns.isObject())
            return;
      registerElement(root, ns);
      std::function<void(Element*)> applyDefaults = [this, &applyDefaults](Element* e) {
            if (!e)
                  return;
            applyDefaultScripts(e);
            for (Element* c : e->children())
                  applyDefaults(c);
            };
      applyDefaults(root);
      }

//---------------------------------------------------------
//   registerElement
//---------------------------------------------------------

void ScriptEngine::registerElement(Element* element, QJSValue ns) {
      if (!element || element->name().isEmpty())
            return;

      // Build the list of ancestor names between the project root
      // and this element.  The project itself is the root namespace.
      QStringList parts;
      Element* p = element->parent();
      while (p && p->typeName() != QStringLiteral("project")) {
            if (!p->name().isEmpty())
                  parts.prepend(p->name());
            p = p->parent();
            }

      QJSValue cur = ns;
      for (const QString& part : parts) {
            QJSValue next = cur.property(part);
            if (!next.isObject()) {
                  next = _engine.newObject();
                  cur.setProperty(part, next);
                  }
            cur = next;
            }

      cur.setProperty(element->name(), _engine.newQObject(element));
      refreshVectorSnapshots(element);

      for (Element* c : element->children())
            registerElement(c, ns);
      }

//---------------------------------------------------------
//   buildAndSetSnapshot
//    Build a snapshot JS object for depElement holding all its
//    Q_PROPERTY values (with vector types decomposed to {x,y,z}),
//    preserve existing child references, and set it on *cur*.
//---------------------------------------------------------

void ScriptEngine::buildAndSetSnapshot(QJSValue& cur, Element* depElement) {
      // Build the snapshot object.
      QJSValue snap           = _engine.newObject();
      const QMetaObject* meta = depElement->metaObject();
      for (int i = 0; i < meta->propertyCount(); ++i) {
            QMetaProperty mp = meta->property(i);
            QVariant v       = mp.read(depElement);
            QString name     = QString::fromUtf8(mp.name());
            if (v.metaType().id() == QMetaType::QVector3D) {
                  QVector3D vec = v.value<QVector3D>();
                  QJSValue o    = _engine.newObject();
                  o.setProperty(QStringLiteral("x"), vec.x());
                  o.setProperty(QStringLiteral("y"), vec.y());
                  o.setProperty(QStringLiteral("z"), vec.z());
                  snap.setProperty(name, o);
                  }
            else if (v.metaType().id() == QMetaType::QVector2D) {
                  QVector2D vec = v.value<QVector2D>();
                  QJSValue o    = _engine.newObject();
                  o.setProperty(QStringLiteral("x"), vec.x());
                  o.setProperty(QStringLiteral("y"), vec.y());
                  snap.setProperty(name, o);
                  }
            else if (v.metaType().id() == QMetaType::Bool) {
                  snap.setProperty(name, v.toBool());
                  }
            else if (v.metaType().id() == QMetaType::Int || v.metaType().id() == QMetaType::UInt ||
                     v.metaType().id() == QMetaType::LongLong || v.metaType().id() == QMetaType::ULongLong ||
                     v.metaType().id() == QMetaType::Double || v.metaType().id() == QMetaType::Float) {
                  snap.setProperty(name, v.toDouble());
                  }
            else if (v.metaType().id() == QMetaType::QString) {
                  snap.setProperty(name, v.toString());
                  }
            else if (v.metaType().id() == QMetaType::QColor) {
                  snap.setProperty(name, v.value<QColor>().name(QColor::HexArgb));
                  }
            // Pointers and other complex types are left out.
            }

      // Keep the QObject wrapper as an escape hatch for method calls.
      QJSValue qobjWrapper = _engine.newQObject(depElement);
      snap.setProperty(QStringLiteral("_obj"), qobjWrapper);

      // Expose every Q_INVOKABLE method of the element as a JS
      // function on the snapshot object, delegating to the QObject
      // wrapper.  Without this, scripts that call
      // ``element.childElements()`` or ``element.findByName(...)``
      // fail because the snapshot is a plain JS object, not a
      // QObject wrapper, and does not carry the meta-methods.
      //
      // We walk the meta-object's method table and create a thin
      // JS wrapper function for each Method-type Q_INVOKABLE that
      // calls ``obj._obj.methodName(args...)`` on the underlying
      // QObject wrapper.  The wrapper is a JS function that uses the
      // JS ``arguments`` object to forward all arguments.
      const QMetaObject* methodMeta = depElement->metaObject();
      for (int mi = 0; mi < methodMeta->methodCount(); ++mi) {
            QMetaMethod mm = methodMeta->method(mi);
            if (mm.methodType() != QMetaMethod::Method)
                  continue;
            QString methodName = QString::fromUtf8(mm.name());
            if (methodName.isEmpty())
                  continue;
            // Skip if a property with the same name already exists
            // on the snapshot (e.g. "children" is a Q_PROPERTY whose
            // value is already in the snapshot as a list).
            QJSValue existing = snap.property(methodName);
            if (existing.isCallable() || existing.isArray() || existing.isObject())
                  continue;
            // Build a JS function that forwards to _obj.methodName.
            QString funcSrc = QStringLiteral(
                "(function() { return this._obj.%1.apply(this._obj, arguments); })")
                .arg(methodName);
            QJSValue fn = _engine.evaluate(funcSrc);
            if (fn.isCallable())
                  snap.setProperty(methodName, fn);
            }

      // Preserve the child elements already registered in the old
      // object: they are NOT Q_PROPERTYs of the parent and would be
      // lost when replacing the object with the snapshot.  We copy
      // over every own property of the old object whose name matches
      // a child element name.
      QJSValue existing = cur.property(depElement->name());
      if (existing.isObject()) {
            for (Element* c : depElement->children()) {
                  if (c->name().isEmpty())
                        continue;
                  QJSValue cv = existing.property(c->name());
                  if (cv.isObject())
                        snap.setProperty(c->name(), cv);
                  }
            }
      cur.setProperty(depElement->name(), snap);
      }

//---------------------------------------------------------
//   refreshVectorSnapshots
//    Replace the QObject wrapper of depElement in the
//    JavaScript namespace by a plain JS object holding
//    snapshots of all its Q_PROPERTY values.  This is
//    necessary because the Qt QObject wrapper does not
//    expose .x/.y/.z accessors for QVector2D/QVector3D
//    values, and setting arbitrary properties on the
//    QObject wrapper is silently ignored.
//    The snapshots are refreshed by dependency re-evaluation.
//---------------------------------------------------------

void ScriptEngine::refreshVectorSnapshots(Element* depElement) {
      if (!depElement || depElement->name().isEmpty())
            return;
      if (!_zcam)
            return;
      // Config is not part of the project tree; it lives at the
      // global level as "config".
      if (depElement->typeName() == QStringLiteral("config")) {
            QJSValue cur = _engine.globalObject();
            buildAndSetSnapshot(cur, depElement);
            return;
            }
      // For project-tree elements, require a project.
      if (!_zcam->project())
            return;

      // Walk the namespace from "project" down to the element's name.
      // Every segment must already exist (registerElement created
      // it); if any segment is missing we create it on the fly.
      QStringList parts;
      Element* p = depElement->parent();
      while (p && p->typeName() != QStringLiteral("project")) {
            if (!p->name().isEmpty())
                  parts.prepend(p->name());
            p = p->parent();
            }

      QJSValue cur = _engine.globalObject().property(QStringLiteral("project"));
      for (const QString& part : parts) {
            QJSValue next = cur.property(part);
            if (!next.isObject()) {
                  next = _engine.newObject();
                  cur.setProperty(part, next);
                  }
            cur = next;
            }

      buildAndSetSnapshot(cur, depElement);
      }

//---------------------------------------------------------
//   addElementToTree
//    Keep the namespace in sync when elements are (re)named.
//---------------------------------------------------------

void ScriptEngine::addElementToTree(Element* e) {
      if (!e || e->name().isEmpty() || !_zcam || !_zcam->project())
            return;
      QJSValue global = _engine.globalObject();
      QJSValue ns     = global.property(QStringLiteral("project"));
      if (!ns.isObject())
            return;

      QStringList parts;
      Element* p = e->parent();
      while (p && p->typeName() != QStringLiteral("project")) {
            if (!p->name().isEmpty())
                  parts.prepend(p->name());
            p = p->parent();
            }
      QJSValue cur = ns;
      for (const QString& part : parts) {
            QJSValue next = cur.property(part);
            if (!next.isObject()) {
                  next = _engine.newObject();
                  cur.setProperty(part, next);
                  }
            cur = next;
            }
      cur.setProperty(e->name(), _engine.newQObject(e));
      }

//---------------------------------------------------------
//   eval
//---------------------------------------------------------

ScriptEngine::EvalResult ScriptEngine::eval(const QString& script) {
      EvalResult r;
      if (script.trimmed().isEmpty()) {
            r.setError(QStringLiteral("empty script"));
            return r;
            }
      QJSValue v = _engine.evaluate(script);
      if (v.isError()) {
            r.setError(QStringLiteral("%1:%2: %3")
                    .arg(v.property(QStringLiteral("lineNumber")).toInt())
                    .arg(v.property(QStringLiteral("columnNumber")).toInt())
                    .arg(v.toString()));
            return r;
            }
      r.value = v.toVariant();
      return r;
      }

//---------------------------------------------------------
//   evalImperative
//    Evaluate a multi-statement imperative script.  Unlike
//    evalWithContext() no `with()` scope chain is applied — the
//    script uses fully-qualified names like `zcam.createElement(...)`.
//    A timeout protects against infinite loops.
//---------------------------------------------------------

ScriptEngine::EvalResult ScriptEngine::evalImperative(const QString& script, int timeoutMs) {
      EvalResult r;
      if (script.trimmed().isEmpty()) {
            r.setError(QStringLiteral("empty script"));
            return r;
            }

      // Ensure zcam and geom are registered.
      QJSValue global = _engine.globalObject();
      if (_scriptApi && !global.property(QStringLiteral("zcam")).isObject()) {
            _scriptApi->setZcam(_zcam);
            _scriptApi->setEngine(&_engine);
            global.setProperty(QStringLiteral("zcam"), _engine.newQObject(_scriptApi));
            }
      if (_geometryApi && !global.property(QStringLiteral("geom")).isObject())
            global.setProperty(QStringLiteral("geom"), _engine.newQObject(_geometryApi));
      // Ensure the print() global is present (idempotent).
      ensurePrintGlobal();

      // Wrap the script in an IIFE so that top-level ``return``
      // statements (common in AI-generated scripts) are valid.
      // Without the wrapper QJSEngine::evaluate() treats the script as
      // a Program production where ``return`` is a SyntaxError.
      QString wrappedScript = QStringLiteral("(function() {\n") + script + QStringLiteral("\n})();");

      // Set up a timeout via QTimer + QJSEngine::setInterrupted.
      QTimer timer;
      timer.setSingleShot(true);
      timer.setInterval(timeoutMs);
      QObject::connect(&timer, &QTimer::timeout, [&]() { _engine.setInterrupted(true); });
      timer.start();

      QJSValue v = _engine.evaluate(wrappedScript);

      timer.stop();
      _engine.setInterrupted(false); // reset for next evaluation

      if (v.isError()) {
            r.setError(QStringLiteral("%1:%2: %3")
                    .arg(v.property(QStringLiteral("lineNumber")).toInt())
                    .arg(v.property(QStringLiteral("columnNumber")).toInt())
                    .arg(v.toString()));
            return r;
            }
      r.value = v.toVariant();
      return r;
      }

//---------------------------------------------------------
//   evalImperativeQml
//    QML-facing wrapper for evalImperative.  Returns a QVariantMap
//    with { ok, value } or { ok, error }.
//---------------------------------------------------------

QVariantMap ScriptEngine::evalImperativeQml(const QString& script, int timeoutMs) {
      EvalResult r = evalImperative(script, timeoutMs);
      QVariantMap result;
      if (!r.error.isEmpty()) {
            result[QStringLiteral("ok")]    = false;
            result[QStringLiteral("error")] = r.error;
            }
      else {
            result[QStringLiteral("ok")]    = true;
            result[QStringLiteral("value")] = r.value;
            }
      return result;
      }

//--------------------------------------------------------------------
//     ScriptEngine::ensurePrintGlobal
//      Register the global `print(...)` JavaScript function on the
//      engine's global object.  The wrapper stringifies each argument
//      (JSON.stringify for objects/arrays, String() otherwise) and
//      joins them with a space, then forwards the result to the C++
//      print() method which emits scriptPrinted().  Idempotent —
//      if a global `print` already exists it is left untouched.
//--------------------------------------------------------------------

void ScriptEngine::ensurePrintGlobal() {
      QJSValue global = _engine.globalObject();
      if (global.property(QStringLiteral("print")).isCallable())
            return;
      // Register a thin C++ sink object so the JS closure can call
      // a Q_INVOKABLE method by name.
      QJSValue sink = _engine.newQObject(this);
      global.setProperty(QStringLiteral("_printSink"), sink);
      const char* src = R"js(
(function() {
    var parts = [];
    for (var i = 0; i < arguments.length; ++i) {
        var a = arguments[i];
        if (a === null)
            parts.push("null");
        else if (a === undefined)
            parts.push("undefined");
        else if (typeof a === "object") {
            try {
                parts.push(JSON.stringify(a, null, 2));
            } catch (e) {
                parts.push(String(a));
            }
        } else {
            parts.push(String(a));
        }
    }
    _printSink.print(parts.join(" "));
})
)js";
      QJSValue fn = _engine.evaluate(src);
      if (fn.isCallable())
            global.setProperty(QStringLiteral("print"), fn);
      }

//--------------------------------------------------------------------
//     ScriptEngine::print
//      C++ sink for the JavaScript `print()` global.  Forwards the
//      message to the app log and emits scriptPrinted() so the QML
//      Script Console can display it.
//--------------------------------------------------------------------

void ScriptEngine::print(const QString& msg) {
      Log("script print: {}", msg);
      emit scriptPrinted(msg);
      }

//--------------------------------------------------------------------
//     sanitizeScriptName
//      Turn a free-form user-typed name into a safe, readable
//      file-system name: keep letters, digits and the safe separators
//      '-' / '_' / '.', collapse runs of any other character to a
//      single '-', trim leading/trailing '-' and collapse the result
//      to a sensible length.  Falls back to "script" when nothing
//      survives (e.g. an empty name or a name that is all symbols).
//--------------------------------------------------------------------

QString ScriptEngine::sanitizeScriptName(const QString& name) {
      QString out;
      out.reserve(name.size());
      bool lastDash = true; // suppress a leading dash
      for (const QChar& c : name) {
            if (c.isLetterOrNumber() || c == u'-' || c == u'_' || c == u'.') {
                  lastDash = (c == u'-');
                  out.append(c);
                  }
            else if (c.isSpace()) {
                  if (!lastDash && !out.isEmpty()) {
                        out.append(u'-');
                        lastDash = true;
                        }
                  }
            else {
                  // any other character → separator
                  if (!lastDash && !out.isEmpty()) {
                        out.append(u'-');
                        lastDash = true;
                        }
                  }
            }
      // Trim trailing dashes / dots.
      while (!out.isEmpty() && (out.back() == u'-' || out.back() == u'.'))
            out.chop(1);
      if (out.isEmpty())
            out = QStringLiteral("script");
      // Guard against absurdly long names (file-system limit).
      if (out.size() > 120)
            out = out.left(120);
      return out;
      }

//--------------------------------------------------------------------
//     uniqueScriptName
//      If <base> is already in <taken>, append "-1", "-2", ... until
//      a free name is found (up to 10000 attempts, then give up and
//      fall back to base).
//--------------------------------------------------------------------

QString ScriptEngine::uniqueScriptName(const QString& base, const QStringList& taken) {
      if (!taken.contains(base))
            return base;
      int n = 1;
      QString candidate;
      do {
            candidate = QStringLiteral("%1-%2").arg(base).arg(n);
            ++n;
            } while (taken.contains(candidate) && n < 10000);
      return candidate;
      }

//--------------------------------------------------------------------
//     scriptsDirectory
//      The named scripts live in ~/ZCam/scripts (created on demand),
//      mirroring the AI sessions in ~/ZCam/ai_sessions.
//--------------------------------------------------------------------

QString ScriptEngine::scriptsDirectory() const {
      QString home = QDir::homePath();
      QString dir  = home + "/ZCam/scripts";
      QDir().mkpath(dir);
      return dir;
      }

//--------------------------------------------------------------------
//     nextScriptPath
//      Generate the next available script file path:
//      ~/ZCam/scripts/Script-yy-MM-dd-n.js
//      Mirrors AIAgent::nextSessionPath() which produces
//      Session-yy-MM-dd-n.json.
//--------------------------------------------------------------------

QString ScriptEngine::nextScriptPath() const {
      QDir dir(scriptsDirectory());
      if (!dir.exists())
            dir.mkpath(".");
      QDate today = QDate::currentDate();
      int n       = 1;
      QString path;
      do {
            path = dir.absoluteFilePath(QString("Script-%1-%2.js").arg(today.toString("yy-MM-dd")).arg(n));
            ++n;
            } while (QFile::exists(path) && n < 10000);
      return path;
      }

//--------------------------------------------------------------------
//     scriptFileName
//      Reconstructs the full script filename ("Script-…-n.js")
//      from a compact display name stored in _scriptList.
//--------------------------------------------------------------------

QString ScriptEngine::scriptFileName(int index) const {
      QString name = _scriptList.at(index);
      if (!name.startsWith("Script-"))
            name = "Script-" + name;
      if (!name.endsWith(".js"))
            name += ".js";
      return name;
      }

//--------------------------------------------------------------------
//     refreshScriptList
//      Re-read the scripts directory and rebuild _scriptList with
//      compact display names (strip "Script-" prefix and ".js" suffix).
//      Emits scriptListChanged().
//--------------------------------------------------------------------

void ScriptEngine::refreshScriptList() {
      QDir dir(scriptsDirectory());
      QStringList files = dir.entryList({QStringLiteral("Script-*.js")}, QDir::Files, QDir::Name);
      _scriptList.clear();
      for (const QString& f : files) {
            QString display = f;
            if (display.startsWith("Script-"))
                  display = display.mid(7);
            if (display.endsWith(".js"))
                  display.chop(3);
            _scriptList.append(display);
            }
      emit scriptListChanged();
      }

//--------------------------------------------------------------------
//     scriptNames
//      Sorted list of existing script display names (without the
//      "Script-" prefix and ".js" suffix).  Also refreshes the
//      internal _scriptList so the Q_PROPERTY stays in sync.
//--------------------------------------------------------------------

QStringList ScriptEngine::scriptNames() {
      refreshScriptList();
      return _scriptList;
      }

//--------------------------------------------------------------------
//     scriptText
//      Load the named script from disk.  *name* is a display name
//      (without "Script-" prefix); it is sanitised and the
//      "Script-" prefix is prepended automatically.
//--------------------------------------------------------------------

QString ScriptEngine::scriptText(const QString& name) {
      QString safe = sanitizeScriptName(name);
      if (safe.isEmpty())
            return {};
      // The file on disk is "Script-<safe>.js".
      if (!safe.startsWith("Script-"))
            safe = "Script-" + safe;
      QFile f(scriptsDirectory() + "/" + safe + ".js");
      if (!f.open(QIODevice::ReadOnly))
            return {};
      return QString::fromUtf8(f.readAll());
      }

//--------------------------------------------------------------------
//     saveScript
//      Create or overwrite the script <name>.js with <content>.
//      *name* is a display name (without "Script-" prefix); the
//      "Script-" prefix is prepended automatically.
//--------------------------------------------------------------------

bool ScriptEngine::saveScript(const QString& name, const QString& content) {
      QString safe = sanitizeScriptName(name);
      if (safe.isEmpty())
            return false;
      if (!safe.startsWith("Script-"))
            safe = "Script-" + safe;
      QDir().mkpath(scriptsDirectory());
      QFile f(scriptsDirectory() + "/" + safe + ".js");
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            Warning("script save failed: {}", f.fileName().toStdString());
            return false;
            }
      f.write(content.toUtf8());
      f.close();
      return true;
      }

//--------------------------------------------------------------------
//     selectScript
//      Load the script at the given index into the current state.
//      Updates _currentScriptIndex, _currentScriptName and emits
//      the appropriate signals.
//--------------------------------------------------------------------

void ScriptEngine::selectScript(int index) {
      if (index < 0 || index >= _scriptList.size()) {
            _currentScriptIndex = -1;
            _currentScriptName.clear();
            emit currentScriptChanged();
            emit currentScriptNameChanged();
            return;
            }
      _currentScriptIndex = index;
      _currentScriptName  = scriptFileName(index);
      emit currentScriptChanged();
      emit currentScriptNameChanged();
      }

//--------------------------------------------------------------------
//     setCurrentScript
//      Q_PROPERTY WRITE method — delegates to selectScript().
//--------------------------------------------------------------------

void ScriptEngine::setCurrentScript(int index) {
      if (index == _currentScriptIndex)
            return;
      selectScript(index);
      }

//--------------------------------------------------------------------
//     newScript
//      Create a brand-new empty script with an auto-generated name
//      like "Script-yy-MM-dd-n" (mirroring AIAgent sessions).
//      When *name* is non-empty it is used as a base (sanitised and
//      de-duplicated); when empty a name is auto-generated.
//      Returns the display name (without "Script-" prefix) that
//      was actually created.  Refreshes the script list and selects
//      the new script as the current one.
//--------------------------------------------------------------------

QString ScriptEngine::newScript(const QString& name) {
      QString path;
      QString displayName;
      if (name.isEmpty()) {
            // Auto-generate like AI sessions: Script-yy-MM-dd-n.js
            path        = nextScriptPath();
            QString fn  = QFileInfo(path).fileName();      // "Script-yy-MM-dd-n.js"
            displayName = fn;
            if (displayName.startsWith("Script-"))
                  displayName = displayName.mid(7);
            if (displayName.endsWith(".js"))
                  displayName.chop(3);
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                  f.close();
            }
      else {
            // User-supplied name: sanitise, prepend "Script-", de-duplicate.
            QStringList taken = _scriptList;
            QString safe     = sanitizeScriptName(name);
            if (!safe.startsWith("Script-"))
                  safe = "Script-" + safe;
            QString unique   = uniqueScriptName(safe, taken);
            saveScript(unique.mid(7), QString());     // saveScript prepends "Script-"
            displayName = unique;
            if (displayName.startsWith("Script-"))
                  displayName = displayName.mid(7);
            }
      refreshScriptList();
      int newIdx = _scriptList.indexOf(displayName);
      if (newIdx < 0)
            newIdx = _scriptList.size() - 1;
      selectScript(newIdx);
      return displayName;
      }

//--------------------------------------------------------------------
//     deleteScriptByIndex
//      Delete the script at the given index and refresh the list.
//      If the deleted script was the current one, the first remaining
//      script (if any) becomes current.
//--------------------------------------------------------------------

bool ScriptEngine::deleteScriptByIndex(int index) {
      if (index < 0 || index >= _scriptList.size())
            return false;
      QString fileName = scriptFileName(index);
      QString path     = scriptsDirectory() + "/" + fileName;
      bool removed     = QFile::remove(path);
      Debug("script delete: {} ({})", path.toStdString(), removed ? "ok" : "not found");
      if (!removed)
            return false;
      bool wasCurrent = (_currentScriptIndex == index);
      refreshScriptList();
      if (wasCurrent) {
            // Select the first remaining script, or clear.
            if (_scriptList.isEmpty()) {
                  _currentScriptIndex = -1;
                  _currentScriptName.clear();
                  emit currentScriptChanged();
                  emit currentScriptNameChanged();
                  }
            else
                  selectScript(0);
            }
      else if (_currentScriptIndex > index) {
            // The current index shifted because a lower entry was removed.
            --_currentScriptIndex;
            emit currentScriptChanged();
            }
      return true;
      }

//--------------------------------------------------------------------
//     deleteScript
//      Delete the named script file.  *name* is a display name
//      (without "Script-" prefix).  Returns true if it existed.
//--------------------------------------------------------------------

bool ScriptEngine::deleteScript(const QString& name) {
      QString safe = sanitizeScriptName(name);
      if (safe.isEmpty())
            return false;
      if (!safe.startsWith("Script-"))
            safe = "Script-" + safe;
      QString path = scriptsDirectory() + "/" + safe + ".js";
      bool removed = QFile::remove(path);
      Debug("script delete: {} ({})", path.toStdString(), removed ? "ok" : "not found");
      if (removed)
            refreshScriptList();
      return removed;
      }

//--------------------------------------------------------------------
//     renameScript
//      Rename an existing script.  *newName* is sanitised and
//      de-duplicated (ignoring <oldName> itself).  Returns the new
//      display name (without "Script-" prefix) that was actually
//      used ("" on failure).  Refreshes the script list and keeps
//      the renamed script selected.
//--------------------------------------------------------------------

QString ScriptEngine::renameScript(const QString& oldName, const QString& newName) {
      QString oldSafe = sanitizeScriptName(oldName);
      if (oldSafe.isEmpty())
            return {};
      if (!oldSafe.startsWith("Script-"))
            oldSafe = "Script-" + oldSafe;
      QString oldPath = scriptsDirectory() + "/" + oldSafe + ".js";
      if (!QFile::exists(oldPath))
            return {};
      // Load the content before moving the file.
      QString content = scriptText(oldName);
      QStringList taken = _scriptList;
      // Remove the old display name from the taken list so we can
      // rename onto it without de-duplicating against itself.
      QString oldDisplay = oldSafe;
      if (oldDisplay.startsWith("Script-"))
            oldDisplay = oldDisplay.mid(7);
      taken.removeAll(oldDisplay);
      QString newSafe = sanitizeScriptName(newName.isEmpty() ? oldSafe : newName);
      if (!newSafe.startsWith("Script-"))
            newSafe = "Script-" + newSafe;
      QString unique  = uniqueScriptName(newSafe, taken);
      QString newPath = scriptsDirectory() + "/" + unique + ".js";
      // Write the new file, then delete the old one.
      QFile f(newPath);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            Warning("script rename failed: {}", newPath.toStdString());
            return {};
            }
      f.write(content.toUtf8());
      f.close();
      QFile::remove(oldPath);
      // Refresh and select the renamed script.
      refreshScriptList();
      QString newDisplay = unique;
      if (newDisplay.startsWith("Script-"))
            newDisplay = newDisplay.mid(7);
      int newIdx = _scriptList.indexOf(newDisplay);
      if (newIdx >= 0)
            selectScript(newIdx);
      return newDisplay;
      }

//---------------------------------------------------------
//   createBinding
//---------------------------------------------------------

void ScriptEngine::createBinding(Element* element, QString prop, QString script) {
      createBinding(element, prop, -1, std::move(script));
      }

void ScriptEngine::createBinding(Element* element, QString prop, int comp, QString script) {
      if (!element || prop.isEmpty())
            return;

      // Replace any existing binding for the same (element, prop[, comp]).
      const QString fullName = comp < 0 ? prop : QStringLiteral("%1.%2").arg(prop).arg(comp);
      for (int i = _bindings.size() - 1; i >= 0; --i) {
            PropertyBinding* b = _bindings.at(i);
            if (b->element() == element && b->property() == fullName) {
                  _bindings.removeAt(i);
                  delete b;
                  }
            }

      auto* b = new PropertyBinding(this, element, prop, comp, script);
      _bindings.append(b);

      // Persist the script text on the element so it survives
      // save/load (serialised in Element::toJson()).
      if (comp < 0) {
            element->setScript(prop, script);
            b->setActive(element->_scripts.value(prop).active);
            }
      else {
            element->setScriptCompProp(comp, prop);
            element->setScriptComp(comp, script);
            b->setActive(element->_scriptCompActive.value(comp, true));
            }
      b->evaluate();
      }

bool ScriptEngine::removeBinding(Element* element, const QString& prop) {
      bool removed = false;
      for (int i = _bindings.size() - 1; i >= 0; --i) {
            PropertyBinding* b = _bindings.at(i);
            if (b->element() != element)
                  continue;
            const QString p = b->property();
            // Empty prop matches ALL bindings of this element (used from
            // ~Element to drop every binding of a dying element).
            if (prop.isEmpty() || p == prop || p.startsWith(prop + QStringLiteral("."))) {
                  _bindings.removeAt(i);
                  // Deleted immediately: ~PropertyBinding() calls
                  // disconnectDeps(), so no stale signal delivery can
                  // re-evaluate the binding after removal.
                  delete b;
                  removed = true;
                  }
            }
      // Clear the persisted script text on the element.  For a
      // targeted removal we only strip the scripts of that property;
      // for a wildcard removal (~Element) everything is cleared.
      if (prop.isEmpty())
            element->clearScripts();
      else
            element->clearScriptsFor(prop);
      return removed;
      }

//---------------------------------------------------------
//   removeBindingsFor
//    Drop every binding of the given element (called from
//    ~Element before destruction completes).
//---------------------------------------------------------

bool ScriptEngine::removeBindingsFor(Element* element) {
      return removeBinding(element, QString());
      }

PropertyBinding* ScriptEngine::bindingFor(Element* element, const QString& prop) const {
      for (PropertyBinding* b : _bindings) {
            if (b->element() != element)
                  continue;
            const QString p = b->property();
            if (p == prop || p.startsWith(prop + QStringLiteral(".")))
                  return b;
            }
      return nullptr;
      }

//--------------------------------------------------------------------
//     setBindingActive / isBindingActive
//--------------------------------------------------------------------

bool ScriptEngine::setBindingActive(Element* element, const QString& prop, bool active) {
      bool found = false;
      for (PropertyBinding* b : _bindings) {
            if (b->element() != element)
                  continue;
            const QString p = b->property();
            if (p == prop || p.startsWith(prop + QStringLiteral("."))) {
                  b->setActive(active);
                  found = true;
                  if (active)
                        b->evaluate();
                  }
            }
      // Persist active state on the element so it survives save/load.
      if (found) {
            if (element->_scripts.contains(prop))
                  element->_scripts[prop].active = active;
            for (int i = 0; i < 3; ++i)
                  if (element->scriptCompProp(i) == prop)
                        element->_scriptCompActive[i] = active;
            }
      return found;
      }

bool ScriptEngine::isBindingActive(Element* element, const QString& prop) const {
      for (PropertyBinding* b : _bindings) {
            if (b->element() != element)
                  continue;
            const QString p = b->property();
            if (p == prop || p.startsWith(prop + QStringLiteral(".")))
                  return b->isActive();
            }
      return false;
      }

//---------------------------------------------------------
//   createBindingQml / removeBindingQml / boundComponentsQml
//---------------------------------------------------------

void ScriptEngine::createBindingQml(QObject* element, QString prop, int comp, QString script) {
      auto* el = qobject_cast<Element*>(element);
      if (!el)
            return;
      if (comp < 0)
            createBinding(el, prop, std::move(script));
      else
            createBinding(el, prop, comp, std::move(script));
      }

void ScriptEngine::removeBindingQml(QObject* element, QString prop) {
      if (auto* el = qobject_cast<Element*>(element))
            removeBinding(el, prop);
      }

//---------------------------------------------------------
//   scriptForQml
//---------------------------------------------------------

QString ScriptEngine::scriptForQml(QObject* element, const QString& prop, int comp) const {
      auto* el = qobject_cast<Element*>(element);
      if (!el)
            return {};
      if (comp >= 0)
            return el->scriptCompProp(comp) == prop ? el->scriptComp(comp) : QString();
      return el->hasScriptFor(prop) ? el->script(prop) : QString();
      }

//---------------------------------------------------------
//   scriptErrorQml
//---------------------------------------------------------

QString ScriptEngine::scriptErrorQml(QObject* element, const QString& prop, int comp) const {
      auto* el = qobject_cast<Element*>(element);
      if (!el)
            return {};
      const QString fullName = comp < 0 ? prop : QStringLiteral("%1.%2").arg(prop).arg(comp);
      for (PropertyBinding* b : _bindings)
            if (b->element() == el && b->property() == fullName)
                  return b->lastError();
      return {};
      }

QString ScriptEngine::boundComponentsQml(QObject* element, const QString& prop) const {
      auto* el = qobject_cast<Element*>(element);
      if (!el)
            return {};
      QStringList found;
      for (PropertyBinding* b : _bindings) {
            if (b->element() != el)
                  continue;
            // Skip inactive bindings — an inactive binding does not
            // make the property read-only in the inspector.
            if (!b->isActive())
                  continue;
            const QString p = b->property();
            if (p == prop) {
                  if (!found.contains(QStringLiteral("all")))
                        found.append(QStringLiteral("all"));
                  }
            else if (p.startsWith(prop + QStringLiteral("."))) {
                  bool ok  = false;
                  int comp = p.section(u'.', -1).toInt(&ok);
                  if (ok && comp >= 0 && comp <= 2)
                        found.append(QString::number(comp));
                  }
            }
      std::sort(found.begin(), found.end(), [](const QString& a, const QString& b) {
            if (a == QStringLiteral("all"))
                  return true;
            if (b == QStringLiteral("all"))
                  return false;
            return a.toInt() < b.toInt();
            });
      return found.join(QStringLiteral(","));
      }

QVariant ScriptEngine::testScript(const QString& script) {
      EvalResult r = eval(script);
      if (!r.error.isEmpty())
            return r.error;
      return r.value;
      }

//--------------------------------------------------------------------
//     elementJsPath
//--------------------------------------------------------------------
//   Build the fully qualified JS namespace path for *element*:
//   "project.<ancestor1>.<ancestor2>.<elementName>".
//   Returns an empty string if the element has no name or is the
//   project root itself.
static QString elementJsPath(Element* element) {
      if (!element || element->name().isEmpty())
            return {};
      // Config is a top-level namespace object ("config"), not
      // nested under "project".
      if (element->typeName() == QStringLiteral("config"))
            return element->name();
      QStringList parts;
      Element* p = element->parent();
      while (p && p->typeName() != QStringLiteral("project")) {
            if (!p->name().isEmpty())
                  parts.prepend(p->name());
            p = p->parent();
            }
      QString path = QStringLiteral("project");
      for (const QString& part : parts)
            path += QStringLiteral(".") + part;
      path += QStringLiteral(".") + element->name();
      return path;
      }

//--------------------------------------------------------------------
//     buildScopeChain
//--------------------------------------------------------------------
//   Build the list of JS namespace paths for the *element* and all
//   its ancestors up to (but not including) the project root.
//   The list is ordered from the element itself (innermost scope)
//   to the outermost ancestor, so that the resulting with-chain
//   gives the element itself the highest priority.
static QStringList buildScopeChain(Element* element) {
      QStringList chain;
      Element* e = element;
      while (e && e->typeName() != QStringLiteral("project") && e->typeName() != QStringLiteral("config")) {
            if (!e->name().isEmpty())
                  chain.append(elementJsPath(e));
            e = e->parent();
            }
      return chain;
      }

//--------------------------------------------------------------------
//     ScriptEngine::evalWithContext
//--------------------------------------------------------------------
//   Evaluate *script* with *element* as the local context.  The
//   script is wrapped in nested ``with`` statements that put the
//   element and each of its ancestors into the JS scope chain,
//   innermost first.  This lets the user write bare property names
//   (e.g. ``width``) and bare element names (e.g. ``cad``) which JS
//   resolves via the scope chain — no text substitution needed.
//
//   The with-objects are the JS namespace objects already registered
//   in the ``project`` tree (which include property snapshots via
//   refreshVectorSnapshots and child element references).
ScriptEngine::EvalResult ScriptEngine::evalWithContext(const QString& script, Element* element) {
      if (!element || script.trimmed().isEmpty()) {
            EvalResult r;
            r.setError(QStringLiteral("empty script"));
            return r;
            }

      // Build the scope chain: element, parent, grandparent, ...
      QStringList chain = buildScopeChain(element);

      // Wrap the script in nested with() statements.
      // with(a) { with(b) { ... script ... } }
      // Innermost scope = element, outermost = topmost ancestor.
      QString wrapped;
      for (const QString& path : chain)
            wrapped += QStringLiteral("with(%1) { ").arg(path);
      wrapped += QStringLiteral("(\n") + script + QStringLiteral("\n)");
      for (int i = 0; i < chain.size(); ++i)
            wrapped += QStringLiteral(" }");

      return eval(wrapped);
      }

//--------------------------------------------------------------------
//     ScriptEngine::testScriptWithContext
//--------------------------------------------------------------------

QVariant ScriptEngine::testScriptWithContext(const QString& script, QObject* element) {
      auto* el = qobject_cast<Element*>(element);
      if (!el)
            return testScript(script);
      EvalResult r = evalWithContext(script, el);
      if (!r.error.isEmpty())
            return r.error;
      return r.value;
      }

bool ScriptEngine::isWritingComponentBinding(Element* element, const QString& propBase) const {
      if (!element || !_evaluatingBinding)
            return false;
      const PropertyBinding* b = _evaluatingBinding;
      if (b->element() != element)
            return false;
      return b->_targetComp >= 0 && b->_targetProp == propBase;
      }

//---------------------------------------------------------
//   toJs / fromJs
//---------------------------------------------------------

QJSValue ScriptEngine::toJs(QJSEngine& engine, const QVariant& v, QMetaType mt) {
      switch (mt.id()) {
            case QMetaType::Bool: return engine.toScriptValue(v.toBool());
            case QMetaType::Int:
            case QMetaType::UInt:
            case QMetaType::LongLong:
            case QMetaType::ULongLong: return engine.toScriptValue(v.toLongLong());
            case QMetaType::Float:
            case QMetaType::Double: return engine.toScriptValue(v.toDouble());
            case QMetaType::QString: return engine.toScriptValue(v.toString());
            case QMetaType::QVector2D: {
                  QVector2D vv = v.value<QVector2D>();
                  QJSValue arr = engine.newArray(2);
                  arr.setProperty(0, engine.toScriptValue(vv.x()));
                  arr.setProperty(1, engine.toScriptValue(vv.y()));
                  return arr;
                  }
            case QMetaType::QVector3D: {
                  QVector3D vv = v.value<QVector3D>();
                  QJSValue arr = engine.newArray(3);
                  arr.setProperty(0, engine.toScriptValue(vv.x()));
                  arr.setProperty(1, engine.toScriptValue(vv.y()));
                  arr.setProperty(2, engine.toScriptValue(vv.z()));
                  return arr;
                  }
            case QMetaType::QColor: {
                  QColor c = v.value<QColor>();
                  return engine.toScriptValue(c.name(QColor::HexArgb));
                  }
            default: return engine.toScriptValue(v);
            }
      }

QVariant ScriptEngine::fromJs(const QJSValue& v, QMetaType mt) {
      if (v.isError())
            return {};
      // toVariant() may have converted a JS array to a QVariantList;
      // re-convert to QJSValue loses isArray() in some Qt versions.
      // Check the QVariant form first for vector types.
      QVariant var = v.toVariant();
      switch (mt.id()) {
            case QMetaType::Bool: return v.toBool();
            case QMetaType::Int: return v.toInt();
            case QMetaType::UInt: return v.toUInt();
            case QMetaType::Double:
            case QMetaType::Float: return v.toNumber();
            case QMetaType::QString: return v.toString();
            case QMetaType::QVector2D: {
                  // Accept either a JS array [x, y] or a JS object
                  // {x: ..., y: ...}.  The object form is natural
                  // because the snapshot objects in the with-scope
                  // use {x, y} properties.
                  if (v.isArray()) {
                        QJSValue vx = v.property(0);
                        QJSValue vy = v.property(1);
                        QVector2D out(vx.toNumber(), vy.toNumber());
                        return QVariant::fromValue(out);
                        }
                  // QVariantList from a JS array that lost isArray()
                  if (var.typeId() == QMetaType::QVariantList) {
                        const auto list = var.toList();
                        if (list.size() >= 2)
                              return QVariant::fromValue(QVector2D(list[0].toDouble(), list[1].toDouble()));
                        }
                  QJSValue vx = v.property(QStringLiteral("x"));
                  QJSValue vy = v.property(QStringLiteral("y"));
                  QVector2D out(vx.toNumber(), vy.toNumber());
                  return QVariant::fromValue(out);
                  }
            case QMetaType::QVector3D: {
                  // Accept either a JS array [x, y, z] or a JS object
                  // {x: ..., y: ..., z: ...}.
                  if (v.isArray()) {
                        QJSValue vx = v.property(0);
                        QJSValue vy = v.property(1);
                        QJSValue vz = v.property(2);
                        QVector3D out(vx.toNumber(), vy.toNumber(), vz.toNumber());
                        return QVariant::fromValue(out);
                        }
                  if (var.typeId() == QMetaType::QVariantList) {
                        const auto list = var.toList();
                        if (list.size() >= 3)
                              return QVariant::fromValue(
                                  QVector3D(list[0].toDouble(), list[1].toDouble(), list[2].toDouble()));
                        }
                  QJSValue vx = v.property(QStringLiteral("x"));
                  QJSValue vy = v.property(QStringLiteral("y"));
                  QJSValue vz = v.property(QStringLiteral("z"));
                  QVector3D out(vx.toNumber(), vy.toNumber(), vz.toNumber());
                  return QVariant::fromValue(out);
                  }
            case QMetaType::QColor: {
                  QString s = v.toString();
                  QColor c(s);
                  return QVariant::fromValue(c);
                  }
            default: return v.toVariant();
            }
      }

//---------------------------------------------------------
//   PropertyBinding
//---------------------------------------------------------

PropertyBinding::PropertyBinding(
    ScriptEngine* engine, Element* target, QString prop, int comp, QString script)
    : _engine(engine), _targetElement(target), _targetProp(std::move(prop)), _targetComp(comp),
      _script(std::move(script)) {
      }

PropertyBinding::~PropertyBinding() {
      disconnectDeps();
      }

//---------------------------------------------------------
//   dependencyTriggered
//    Slot wired to every dependency NOTIFY signal.
//---------------------------------------------------------

void PropertyBinding::dependencyTriggered() {
      if (_deleted || !_targetElement || !_active)
            return;
      if (_engine && _engine->_evaluatingBinding == this)
            return;
      if (_engine)
            _engine->refreshBinding(this);
      }

//---------------------------------------------------------
//   disconnectDeps
//---------------------------------------------------------

void PropertyBinding::disconnectDeps() {
      for (const DepInfo& d : std::as_const(_deps))
            if (d.element && d.connection)
                  QObject::disconnect(d.connection);
      _deps.clear();
      }

//---------------------------------------------------------
//   bind
//    Static-dependency analysis + evaluation + dynamic
//    verification of dependencies.
//---------------------------------------------------------

void PropertyBinding::bind() {
      disconnectDeps();
      _deleted = false;
      _error.clear();

      if (!_targetElement) {
            _deleted = true;
            _error   = QStringLiteral("no target element");
            return;
            }

      // 0) Build the scope chain for the target element.  The script
      //    is wrapped in nested with() statements so bare property
      //    names and bare element names are resolved by JS itself via
      //    the scope chain (element -> parent -> grandparent -> ...).
      QStringList scopeChain = buildScopeChain(_targetElement);
      QString wrappedScript;
      for (const QString& path : scopeChain)
            wrappedScript += QStringLiteral("with(%1) { ").arg(path);
      wrappedScript += QStringLiteral("(\n") + _script + QStringLiteral("\n)");
      for (int i = 0; i < scopeChain.size(); ++i)
            wrappedScript += QStringLiteral(" }");

      // 1) Static dependency analysis on the ORIGINAL (unwrapped)
      //    script.  We must scan the bare identifiers here — in the
      //    wrapped script they appear as "rect1.size.y" where "size"
      //    is preceded by '.' and would be mistaken for a property
      //    access.  In the original script "size.y * 0.5" the scanner
      //    sees "size" as a bare identifier and resolves it via
      //    contextElement to the target element, then tracks "size"
      //    (a QVector2D property) as the dependency for the ".y"
      //    component access.
      //
      //    Refresh the vector snapshots of every element the script
      //    reads BEFORE evaluation so vector component access
      //    (.x/.y/.z) works (see refreshVectorSnapshots).
      DependencyScanResult scan =
          scanDependencies(_script, Element::namesMap(), {_targetElement->name()}, _targetElement);
      for (Element* depEl : scan.elements)
            _engine->refreshVectorSnapshots(depEl);

      // 2) Evaluate and apply the value.
      ScriptEngine::EvalResult r = _engine->eval(wrappedScript);
      if (!r.error.isEmpty()) {
            _deleted = true;
            _error   = r.error;
            Warning("script error for {}.{}: {}", _targetElement->name(), property(), r.error);
            return;
            }

      const QMetaObject* meta = _targetElement->metaObject();
      QByteArray propBytes    = _targetProp.toUtf8();
      int idx                 = meta->indexOfProperty(propBytes.constData());
      if (idx < 0) {
            _deleted = true;
            _error   = QStringLiteral("unknown property %1").arg(_targetProp);
            return;
            }
      QMetaProperty mp = meta->property(idx);
      QMetaType mt     = mp.metaType();

      QVariant newValue;
      if (_targetComp < 0)
            newValue = ScriptEngine::fromJs(_engine->_engine.toScriptValue(r.value), mt);
      else {
            QVariant cur = _targetElement->property(propBytes.constData());
            double v     = r.value.toDouble();
            if (mt.id() == QMetaType::QVector3D) {
                  QVector3D vec = cur.value<QVector3D>();
                  switch (_targetComp) {
                        case 0: vec.setX(v); break;
                        case 1: vec.setY(v); break;
                        case 2: vec.setZ(v); break;
                        }
                  newValue = QVariant::fromValue(vec);
                  }
            else if (mt.id() == QMetaType::QVector2D) {
                  QVector2D vec = cur.value<QVector2D>();
                  switch (_targetComp) {
                        case 0: vec.setX(v); break;
                        case 1: vec.setY(v); break;
                        }
                  newValue = QVariant::fromValue(vec);
                  }
            else
                  newValue = v;
            }

      if (newValue.isValid() && newValue != _targetElement->property(propBytes.constData())) {
            // Prevent our own write from triggering re-evaluation.
            bool wasEvaluating          = (_engine->_evaluatingBinding != nullptr);
            _engine->_evaluatingBinding = this;
            _targetElement->setProperty(propBytes.constData(), newValue);
            if (!wasEvaluating)
                  _engine->_evaluatingBinding = nullptr;
            }

      // 3) Wire up the dependency NOTIFY signals so a change in any
      //    referenced element re-evaluates this binding.  The static
      //    scan from step 1 is reused.
      for (Element* depEl : scan.elements) {
            bool scalarUse          = scan.scalarElements.contains(depEl->name());
            QList<QMetaMethod> sigs = dependencySignals(depEl, scalarUse, scan.directProps.value(depEl));
            for (const QMetaMethod& sig : sigs) {
                  DepInfo info;
                  info.element     = depEl;
                  info.signal      = sig;
                  info.signalIndex = sig.methodIndex();
                  const int sIdx   = info.signalIndex;
                  const int rIdx   = dependencySlotIndex();
                  if (sIdx >= 0 && rIdx >= 0)
                        info.connection = QMetaObject::connect(depEl, sIdx, this, rIdx, Qt::AutoConnection);
                  _deps.append(info);
                  }
            }
      }

//---------------------------------------------------------
//   refreshBinding
//---------------------------------------------------------

void ScriptEngine::refreshBinding(PropertyBinding* b) {
      if (!b || b->_deleted || !b->isActive())
            return;
      b->evaluate();
      }

//---------------------------------------------------------
//   evaluate
//    Re-run the script and apply the result.  Called whenever
//    any dependency emits its NOTIFY signal.
//---------------------------------------------------------

void PropertyBinding::evaluate() {
      if (_deleted || !_targetElement || !_active)
            return;
      bind();
      }
