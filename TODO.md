### Implement Save/Load project

### Implement Recipes GUI
- implement a gui to add/remove/edit the Recipe assets. The gui should be located in the Recipes panel.
  The panel is split into a left list part showing a list of all available Recipes (names). The
  user selects a recipe and details are shown on the right side. The right side has a header with
 informations from Recipe and a body consisting of a left list showing the names of all LaserLayerSettings
 and an right panel showing details of a LaserLayerSetting. The user can also add and delete
 LaserLayerSettins.

### Implement Recipes
- implement the functions ZCam::loadAssets() and ZCam::saveAssets() as defined in zcam.h and the mock
  in zcam.cpp. Assets should be stored in JSON using the lohmann library.
  Implement the necessary json functions for LaserLayerSetting, LaserLayersSetting and Recipe (mocks are
  in recipe.cpp).


### Extend App Framework

- Create a new top level layout with a menu bar, a tool bar, a tab bar and an
  stack widget
- create tabs "Main" "Config"
- The tabs switch corresponding stack widget panels
- move the current main widget into the "main" stack widget
- populate the menu bar with  a "File" pulldown menu with the usual entries
  "new" "open" "save" "save as" "import" etc.
- add an "Edit" pulldown with "undo" and "redo" actions
- add most important file operations and undo/redo also on the tool bar
- create actions for the "File" entries.
- connect File dialogs to the file operations.
- implement the logic to load/save a project file.
- on app exit check for a dirty project file and implement the usaual logic as
  asking the user etc.
- on loading a project check for existing project and implement the usual logic
  to prevent accidental data loss
- implement stubs for reading/writing a project file
- create/load minimalistic b/w icons for all operations

### Create Inspector

- [x] split the main window in Main.qml vertical into a left panel and a right panel
- [x] move the View3DPanel into the right panel
- [x] split the left panel into an upper and lower part
- [x] in the upper part add a TreeView which consists of a tree of Element3d objects with the root in
      ZCam::topLevel()
- [x] in Element3d add an QString property "name"
- [x] the tree view should show the "name" of every Element3d
- [x] make the objects in the tree view selectable
- [x] in the lower part add an object called Inspector which shows selected properties of the selected
      element in the tree view.
- [x] Implement an inspector panel for the "Text" class of Element3d elements
      Read the entries to show from the static json object "properties". Use nlohmann::ordered_json
      to preserve the object order. Use first entry "class" together with property "name" for the inspector title
      Fix the json "properties" if necessary.
- [x] Construct an entry for every following property item in the properties list. Every property corresponds to
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
