# Scripting

Status: IMPLEMENTIERT (siehe src/scriptengine.{h,cpp}, qml/PropertyEditor.qml).

Scripting erlaubt es, fixe werte in der gui durch ein javascript zu ersetzen, welches den
Wert dynamisch aus u.U. anderen Werten berechnet. Dieses "binding" funktioniert so das
bei einer Änderung eines "anderen" Wertes das abhängige property neu berechnet wird.
Es werden aktuelle Qt Mechanismen verwendet.

Umsetzung:

- [x] Alle Elemente des Project Trees werden per Namen in der js Engine registriert
  sodas ihre properties von js aus sichtbar sind:

      project.cad.layer1.circle2.x = 22 * 5 + project.cad.layer1.rectangle2.width

  (ScriptEngine::rebuildRegistry/registriert jedes Element mit seinem eindeutigen,
  zu einem gültigen JS-Identifier sanitisierten Namen; Vektor-Properties werden als
  {x,y,z}-Snapshut-Objekte vor jeder Evaluierung aktualisiert, da der Qt QObject
  wrapper für QVector2D/3D keine Komponenten-Properties exponiert.)

- [x] Elemente müssen registriert und de-registriert werden wenn sie in den projectbaunm
  eingefügt oder entfernt werden.  (Element::setName / ~Element / rebuildRegistry)

- [x] Elemente bekommen die Eigenschaft "scriptable". Der bool ist default false.
  (Umsetzung über gespeicherte script/scriptProp/scriptComp-Properties am Element;
  Bindings sind nur aktiv, wenn ein Script gesetzt ist.)

- [x] Ein Button rechts neben jedem property im inspector der scriptable ist (expression symbol).
  Das expression icon gibt es in zwei ausführungen: wenn ein script aktiv/inactiv ist
  (icons/bound-expression.svg / bound-expression-unset.svg)

- [x] Ein popup menu zur Eingabe des scripts, des script ergebnisses und einer evtl. Fehlermeldung
  (bei z.B. Syntaxfehler des scripts)
  Das popup erscheint beim drücken des script buttons
  Das popup hat eine checkbox  um das script zu aktivieren. Wird es deaktiviert, dann wird das
  property fix auf den letzten evaluierten Wert gesetzt.
  (ScriptPopup in PropertyEditor.qml mit live-Auswertung, Fehleranzeige und
  Active-Checkbox, InspectorModel::setScript/removeScript/testScript)

- [x] serialisiere den script im projectfile
  (Element::toJson/fromJson → "script"/"scriptComp" JSON; Round-Trip im
  --script-test Selftest verifiziert)

- [x] die Werte im Inspector müssen grau dargestellt werden, wenn sie das Ergebnis eines scripts
  sind und vom Benutzer nicht geändert werden können.
  (InspectorModel::isScriptBound → Delegate disabled; setData/setSubProperty/
  setColumnProperty blockieren Schreibzugriffe auf gebundene Properties.)
