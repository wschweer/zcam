### Create Project

In this task create the basic files and directories for a Qt-QML project
which consists of a Qt-QML gui and an implementation in c++:

- [ ] create a subdirectory "qml" for all *.qml files
- [ ] create a subdirectory "src" for all *.cpp and *.h files
- [ ] Create an QML application widget; size it initially by 400x400 pixel
- [ ] Create main() in main.cpp. main() initializes qt and starts the
        qml application widget
- [ ] Create an empty c++ class ZCam() in zcam.cpp and zcam.h
        The class is a singleton and should be acessible by qml. Put the empty construktor into zcam.cpp.
- [ ] Create/Modify CMakeLists.txt for cmake. Use the ninja build system with
      cmake. The minimal c++ standard is c++23. The project name and the name of the excutable is "zcam".
