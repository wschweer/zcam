cmake_minimum_required(VERSION 3.31)

project(zcam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(QT_QML_GENERATE_QMLLS_INI On)

set(CMAKE_VERBOSE_MAKEFILE OFF)
set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_BUILD_TYPE Release)
#set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_AUTOMOC ON)

# linuxdeploy handles library bundling; skip RPATH rewriting on install
set(CMAKE_SKIP_INSTALL_RPATH ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

find_package(PkgConfig REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Quick3D Qml QuickControls2 QmlIntegration)

pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)

qt_standard_project_setup()

# Set policies to suppress warnings
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(zcam
      src/main.cpp
      src/logger.cpp src/logger.h
      )

qt_add_qml_module(zcam
    URI "ZCam"
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/MainPanel.qml
        qml/ConfigRecipes.qml
        qml/ConfigMachines.qml
        qml/View3DPanel.qml
        qml/TestCube.qml
        qml/ToolRow.qml
        qml/ToolColumn.qml
        qml/ProjectTree.qml
        qml/Shape.qml
        qml/GridShape.qml
        qml/LaserLayer1.qml
        qml/TreeViewPanel.qml
        qml/InspectorPanel.qml
        qml/PropertyEditor.qml
        qml/FontFamilyButton.qml
        qml/LockCheckBox.qml
        qml/LaserPanel.qml
        qml/ConfigSystem.qml
        qml/ConfigGuiPanel.qml
        qml/ConfigColorsPanel.qml
        qml/ConfigColor.qml
        qml/NavigationCube.qml
        qml/CamShape.qml
        qml/MediaBrowser.qml
        qml/MediaFontsPanel.qml
        qml/MediaArtworkPanel.qml
        qml/MediaIconsPanel.qml
    SOURCES
        src/zcam.cpp src/zcam.h
        src/machine.cpp src/machine.h
        src/machines.cpp src/machines.h
        src/machinemodel.cpp src/machinemodel.h
        src/configmodel.cpp src/configmodel.h
        src/layersettingmodel.cpp src/layersettingmodel.h
        src/propertyjson.cpp src/propertyjson.h
        src/recipe.cpp src/recipe.h
        src/projectmanager.cpp src/projectmanager.h
        src/spacemouse.cpp
        src/spacemouse.h
        src/tessgeometry.cpp src/tessgeometry.h
        src/bezier.cpp src/bezier.h
        src/painterpath.cpp src/painterpath.h
        src/element.cpp src/element.h
        src/element3d.cpp src/element3d.h
        src/text.cpp src/text.h
        src/polygon.cpp src/polygon.h
        src/rectangle.cpp src/rectangle.h
        src/ellipse.cpp src/ellipse.h
        src/treemodel.cpp src/treemodel.h
        src/inspector_model.cpp src/inspector_model.h
        src/project.cpp src/project.h
        src/cad.cpp src/cad.h
        src/cam.cpp src/cam.h
        src/fixture.cpp src/fixture.h
        src/stock.cpp src/stock.h
        src/framing.cpp src/framing.h
        src/grid.cpp src/grid.h
        src/laser.cpp src/laser.h
        src/laserlayer.cpp src/laserlayer.h
        src/laserengine.cpp src/laserengine.h
        src/usb.cpp src/usb.h
        src/clipper.cpp src/clipper.h
        src/layer.cpp src/layer.h
        src/materialtest.cpp src/materialtest.h
        src/undo.h
        src/svg.cpp
        src/truncatedcubegeometry.cpp src/truncatedcubegeometry.h
        src/dxfimport.cpp src/dxfimport.h

        src/geometryworker.cpp src/geometryworker.h

        src/mediabrowser.cpp src/mediabrowser.h

        clipper2/src/clipper.engine.cpp
        clipper2/src/clipper.offset.cpp
        clipper2/src/clipper.rectclip.cpp

        libdxfrw/src/libdxfrw.cpp
        libdxfrw/src/libdwgr.cpp
        libdxfrw/src/drw_base.cpp
        libdxfrw/src/drw_entities.cpp
        libdxfrw/src/drw_objects.cpp
        libdxfrw/src/drw_classes.cpp
        libdxfrw/src/drw_header.cpp
        libdxfrw/src/intern/dxfreader.cpp
        libdxfrw/src/intern/dxfwriter.cpp
        libdxfrw/src/intern/dwgbuffer.cpp
        libdxfrw/src/intern/dwgreader.cpp
        libdxfrw/src/intern/dwgreader15.cpp
        libdxfrw/src/intern/dwgreader18.cpp
        libdxfrw/src/intern/dwgreader21.cpp
        libdxfrw/src/intern/dwgreader24.cpp
        libdxfrw/src/intern/dwgreader27.cpp
        libdxfrw/src/intern/dwgutil.cpp
        libdxfrw/src/intern/rscodec.cpp
        libdxfrw/src/intern/drw_textcodec.cpp
        libdxfrw/src/intern/drw_dbg.cpp

        tess2/bucketalloc.cpp
        tess2/dict.cpp
        tess2/geom.cpp
        tess2/mesh.cpp
        tess2/priorityq.cpp
        tess2/sweep.cpp
        tess2/tess.cpp
      )

target_compile_options(zcam PRIVATE -DUSINGZ)

qt_add_resources(zcam icons FILES
      # white icons (used in menus / dark backgrounds)
      icons/view-top.svg
      icons/view-bottom.svg
      icons/view-front.svg
      icons/view-rear.svg
      icons/view-left.svg
      icons/view-right.svg
      icons/view-isometric.svg
      icons/view-perspective.svg
      icons/view-fullscreen.svg
      icons/file-new.svg
      icons/file-open.svg
      icons/file-save.svg
      icons/file-save-as.svg
      icons/file-import.svg
      icons/edit-undo.svg
      icons/edit-redo.svg
      icons/config.svg
      # dark/black icons (used in main toolbar)
      icons/dark/file-new.svg
      icons/dark/file-open.svg
      icons/dark/file-save.svg
      icons/dark/file-save-as.svg
      icons/dark/file-import.svg
      icons/dark/edit-undo.svg
      icons/dark/edit-redo.svg
      icons/dark/config.svg
      icons/cam-refresh.svg
      icons/dark/cam-refresh.svg
      icons/laser-off.svg
      icons/laser.svg

      icons/visible.svg
      icons/invisible.svg
      icons/select.svg
      icons/Draft_Polygon.svg
      icons/Draft_Rectangle.svg
      icons/Draft_Circle.svg
      icons/Draft_Text.svg
      qml/Main.qml
      )

# Bundle the QtQuickControls2 style configuration so Material/Dark is active
qt_add_resources(zcam conf
    PREFIX "/"
    FILES qtquickcontrols2.conf
    )

target_link_libraries(zcam PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Quick
      Qt6::Quick3D
      Qt6::QuickControls2
      Qt6::Qml
      Qt6::QmlIntegration
      PkgConfig::UDEV
      PkgConfig::LIBUSB
      spnav
      )

# Explicitly include the src directory so ZCam is visible to the autogenerated code
target_include_directories(zcam PRIVATE src)
target_include_directories(zcam PRIVATE clipper2/include)
target_include_directories(zcam PRIVATE tess2)
target_include_directories(zcam PRIVATE libdxfrw/src)

# ---------------------------------------------------------------------------
# Install targets – required by "make appimage" (linuxdeploy)
# ---------------------------------------------------------------------------
install(TARGETS zcam RUNTIME DESTINATION bin)
install(FILES zcam.desktop DESTINATION share/applications)
install(FILES zcam.png DESTINATION share/icons/hicolor/256x256/apps RENAME zcam.png)
