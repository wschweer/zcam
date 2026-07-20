# Entwicklung
## Übersicht

### Technologie und Tools

- C++24
- Qt 6.11
- QML
- Qt Quick 3D

Die GUI nutzt Qt 6.11 mit QML für die UI und nicht den klassischen Ansatz über QQWidgets.
Das Hauptfenster ist ein 3D Canvas der mit QML aufgebaut wird.
Die Hauptmasse des Codes ist jedoch C++.

### Element
Die Klasse Element ist die Basisklasse aller grafischen Elemente in ZCam. Sie implementiert ein
Geometry Element (TessGeometry), dessen Basisklasse QQuick3DGeometry() ist und von Qt Quick 3D
zum Aufbau der 3D-Szene benötigt wird.

### Interface Qt Quick 3D - C++

Das Basis-3D Element ist eine Node. Der 3D Canvas baut eine Baumstruktur von Nodes auf die
ihr Gegenstück auf der C++ Seite haben. C++ Modul und Canvas sind über Signal/Slots miteinander
verbunden. Folgende Signale in Zcam steuern den Canvas:

```c++
      void remove3dElement(Element*);           // signal 3d gui to remove an element from the scene graph
      void add3dElement(Element*);              // signal 3d gui to add a new element into the scene graph
      void addSubElement(Element*, Element*);   // signal 3d gui to add a new subelement into the scene graph
      void rootElementChanged(Element*);        // signal 3d gui to rebuild scene graph
```

Die Wurzel des Node-Baumes findet sich in Zcam::topLevel():

```cpp
  class ZCam : public QObject
      {
      ...
      Q_PROPERTY(TopLevel* topLevel READ topLevel WRITE setTopLevel NOTIFY topLevelChanged)
      ...
      TopLevel* _topLevel{nullptr};
      ...
```

`setTopLevel(...element...)` löst das Signal `topLevelChanged()` aus welches dem Qml Part
in ProjectTree.qml: `base.onRootElementChanged()` signalisiert, das das Projekt neu gerendered
werden muss.

```qml
function onRootElementChanged(e) {
   // destroy old tree
   var n = base.children.length;
   for (var i = 0; i < n; ++i) {
       base.children[i].destroy(100);
       }
   if (e)
       base.addElement(base, e);    // add Shape component
   }
```

### Properties

Die Liste der Element properties werden an mehreren Stellen benötigt:

- zur Konstruktion der qml GUI Element
- zum Schreiben/Lesen  der Projektdatei

Die Liste der für ein Element verfügbaren Properties und ihre Eigenschaften werden in einem
JSON string konfiguriert und können über `std::string_view Element3d::properties()` abgerufen werden.

Liste der Property Typen:

| Typ |  Beschreibung |
| ---- | ---- |
"text"                | Texteingabe
"bool"                | Checkbox
"int"                 | Spinbox
"float"               | DoubleSpinBox
"vector3d"            | drei Double Spinboxen
"scale"               | drei Double Spinboxen
"vector2d"            | zwei Double Spinboxen
"font"                |
"halign"              |
"multiline"           |
"singleline"          |
"line"                |
"color"               |
"layer"               |
"recipe"              |
"machine"             |
"machineName"         |
"machineType"         |
"override"            |
"pulsewidth"          |
"lineJoin"            |
"lineEnd"             |
"lockScale"           |
"lockSize"            |
"framingType"         |
"empty"               |
