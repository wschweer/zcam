## ZCad
## 3D Panel
### Navigation

| Action | Input |
| :--- | :--- |
| Scale | `Ctrl` + Mouse wheel |
| Move | Middle mouse button + drag |
| Rotate | Right mouse button + drag |

Eine 3DConnexion "SpaceMouse" wird automatisch erkannt, wenn vorhanden.


### Views

| Icon | Action |
| :--- | :--- |
| <img src="../icons/view-top.svg" style="width:18px;"/> | Oben |
| <img src="../icons/view-bottom.svg" style="width:18px;"/> | Unten |
| <img src="../icons/view-front.svg" style="width:18px;"/> | Front |
| <img src="../icons/view-rear.svg" style="width:18px;"/> | Rückseite |
| <img src="../icons/view-left.svg" style="width:18px;"/> | Links |
| <img src="../icons/view-right.svg" style="width:18px;"/> | Rechts |
| <img src="../icons/view-fullscreen.svg" style="width:18px;"/> | Skaliere auf Bildschirm |
| <img src="../icons/view-isometric.svg" style="width:18px;"/> | Isometrische Projektion |
| <img src="../icons/view-perspective.svg" style="width:18px;"/> | Perspectivische Projektion |

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
