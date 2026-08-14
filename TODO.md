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

# MOP Colors

Das aktuelle konzept der Element Farben ist falsch.

Ich möchte auf dem 3D-Canvas anhand der Farben der Elemente sehen können,
welchem Mop (Machine Operation) sie zugeordnet sind. Z.Z. ist nur ein Mop,
der LaserMop implementiert. Es gibt in einem Projekt normalerweise mehrere
LaserMop. Die Farben der Mop sollen automatisch aus einer Liste zugewiesen
werden. Es gibt maximal 32 verschiedene Mop Farben. Die Mop Farbe ist also
ein int (dem Farb-Index in die Farb-Konfigtabelle) Property von
Mop (LaserMop). Elemente werden nun in der Farbe des zugeordneten Mop
dargestellt. Die Konfiguration der Farbe für sichtbare Elemente entfällt.
Dafür kann der Farbindex der Mop (der normalerweise automatisch fortlaufend
zugewiesen wird, verändert werden). Dies wird dem Benutzer in der Inspector
Gui als speziellen Color-Dialog, der diese 32 Farben als kleine Kästchen
in zwei Reihen dargstellt, zur Auswahh gegeben.

Aktionen:

- [x] Farbauswahl für sichtbare Elemente entfernen
- [x] Farbconfiguration der verschiedenen Element Typen entfernen
- [x] ColorDialog zur Auswahl aus den 32 Mop Farben erstellen
- [x] LaserMop um Color Index erweitern (neuer property typ)
- [x] Sichtbare Elemente haben aus Sicht der qml gui immer noch eine Farbe,
  die jedoch nicht mehr statisch sonder dynamisch aus dem zugeordneten
  Mop (LaserMop) ermittelt wird.
- [x] Erstelle eine Basisklasse Mop
- [x] LaserMop soll von Mop abgeleitet werden
- [x] Erstelle eine Klasse NopMop die von Mop abgeleitet wird
  NopMop macht gar nichts und wird als Default für Cad eingesetzt. Alle
  Elemente erben Mop von Cad so das für alle Elemente immer ein Mop gesetzt
  ist.
- [x] Als initiale Farbpalette für unsere 32 Mop Farben orientieren wir uns
  an den Layer Farben von LightBurn
