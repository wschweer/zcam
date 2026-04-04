# ZCad
## 3D Panel
### Navigation

| Action | Input |
| :--- | :--- |
| Scale | `Ctrl` + Mouse wheel |
| Move | Middle mouse button + drag |
| Rotate | Right mouse button + drag |

A 3DConnexion "SpaceMouse" is automatically detected if present.


### Views

| Icon | Action |
| :--- | :--- |
| <img src="../icons/view-top.svg" style="width:18px;"/> | top |
| <img src="../icons/view-bottom.svg" style="width:18px;"/> | bottom |
| <img src="../icons/view-front.svg" style="width:18px;"/> | front |
| <img src="../icons/view-rear.svg" style="width:18px;"/> | rear |
| <img src="../icons/view-left.svg" style="width:18px;"/> | left |
| <img src="../icons/view-right.svg" style="width:18px;"/> | right |
| <img src="../icons/view-fullscreen.svg" style="width:18px;"/> | scale to screen |
| <img src="../icons/view-isometric.svg" style="width:18px;"/> | isometric projection |
| <img src="../icons/view-perspective.svg" style="width:18px;"/> | perspective projection |

# Development
## Overview

### Technology and Tools

- C++24
- Qt 6.11
- QML
- Qt Quick 3D

The GUI uses Qt 6.11 with QML for the UI, rather than the classic approach via QWidgets.
The main window is a 3D Canvas built with QML.
However, the bulk of the code is C++.

### Element
The class Element is the base class for all graphical elements in ZCam. It implements a
Geometry Element (TessGeometry), whose base class is QQuick3DGeometry(), and which is required
by Qt Quick 3D to build the 3D scene.

### Interface Qt Quick 3D - C++

The basic 3D element is a Node. The 3D Canvas builds a tree structure of nodes that
have their counterparts on the C++ side. The C++ module and the canvas are connected
via signals/slots. The following signals in ZCam control the canvas:

```c++
      void remove3dElement(Element*);           // signal 3d gui to remove an element from the scene graph
      void add3dElement(Element*);              // signal 3d gui to add a new element into the scene graph
      void addSubElement(Element*, Element*);   // signal 3d gui to add a new subelement into the scene graph
      void rootElementChanged(Element*);        // signal 3d gui to rebuild scene graph
```

The root of the node tree can be found in ZCam::topLevel():

```cpp
  class ZCam : public QObject
      {
      ...
      Q_PROPERTY(TopLevel* topLevel READ topLevel WRITE setTopLevel NOTIFY topLevelChanged)
      ...
      TopLevel* _topLevel{nullptr};
      ...
```

`setTopLevel(...element...)` triggers the `topLevelChanged()` signal, which signals the QML part
in ProjectTree.qml: `base.onRootElementChanged()` that the project needs to be re-rendered.

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
