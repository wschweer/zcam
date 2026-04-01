### Create Inspector

- [ ] split the main window in Main.qml vertical into a left panel and a right panel
- [ ] move the View3DPanel into the right panel
- [ ] split the left panel into an upper and lower part
- [ ] in the upper part add a TreeView which consists of a tree of Element3d objects with the root in
      ZCam::topLevel()
- [ ] in Element3d add an QString property "name"
- [ ] the tree view should show the "name" of every Element3d
- [ ] make the objects in the tree view selectable
- [ ] in the lower part add an object called Inspector which shows selected properties of the selected
      element in the tree view.
- [ ] Implement an inspector panel for the "Text" class of Element3d elements
      Read the entries to show from the static json object "properties". Use nlohmann::ordered_json
      to preserve the object order. Use first entry "class" together with property "name" for the inspector title
      Fix the json "properties" if necessary.
- [ ] Construct an entry for every following property item in the properties list. Every property corresponds to
      an QObject property with same name as defined with the PROP and PROPV macros. The user can edit
      every property. Construct an appropriate qml object and use information from the json properties data.


### Create Project

In this task create the basic files and directories for a Qt-QML project
which consists of a Qt-QML gui and an implementation in c++:

- [x] create a subdirectory "qml" for all *.qml files
- [x] create a subdirectory "src" for all *.cpp and *.h files
- [x] Create an QML application widget; size it initially by 400x400 pixel
- [x] Create main() in main.cpp. main() initializes qt and starts the
        qml application widget
- [x] Create an empty c++ class ZCam() in zcam.cpp and zcam.h
        The class is a singleton and should be acessible by qml. Put the empty construktor into zcam.cpp.
- [x] Create/Modify CMakeLists.txt for cmake. Use the ninja build system with
      cmake. The minimal c++ standard is c++23. The project name and the name of the excutable is "zcam".
