cmake_minimum_required(VERSION 3.31)

project(zcam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(QT_QML_GENERATE_QMLLS_INI On)

set(CMAKE_VERBOSE_MAKEFILE OFF)
set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_BUILD_TYPE Release)
# set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_AUTOMOC ON)

# linuxdeploy handles library bundling; skip RPATH rewriting on install
set(CMAKE_SKIP_INSTALL_RPATH ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)

find_package(PkgConfig REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Quick3D Qml QuickControls2 QmlIntegration Multimedia)
find_package(Qt6 COMPONENTS WebEngineQuick QUIET)
find_package(Qt6 COMPONENTS Quick3DPrivate QUIET)
find_package(Qt6Multimedia QUIET)
find_package(OpenCV REQUIRED)
get_target_property(QT_CORE_LIB Qt6::Core LOCATION)
get_filename_component(QT_LIB_DIR "${QT_CORE_LIB}" DIRECTORY)
set(CMAKE_BUILD_RPATH "${QT_LIB_DIR}")

pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)

# libpcap for raw Ethernet communication (RKQ laser board)
# Try pkg-config first, fall back to manual discovery.
pkg_check_modules(PCAP IMPORTED_TARGET libpcap)
if(NOT PCAP_FOUND)
    # Fallback: use headers/libs extracted to external/pcap
    set(PCAP_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/external/pcap/usr/include)
    set(PCAP_LIBRARY ${CMAKE_SOURCE_DIR}/external/pcap/usr/lib/x86_64-linux-gnu/libpcap.so)
    if(EXISTS ${PCAP_INCLUDE_DIR}/pcap/pcap.h AND EXISTS ${PCAP_LIBRARY})
          set(PCAP_FOUND TRUE)
    endif()
endif()

qt_standard_project_setup()

# Set policies to suppress warnings
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(zcam
      src/main.cpp
      src/logger.cpp src/logger.h
      )

# Apply pcap include path immediately after target creation so all
# subsequent sources (including qt_add_qml_module SOURCES) can find it.
if(PCAP_FOUND AND NOT TARGET PkgConfig::PCAP)
      target_include_directories(zcam PRIVATE ${PCAP_INCLUDE_DIR})
endif()

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
        qml/BRepShape.qml
        qml/ImageShape.qml
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
        qml/GalvoCalibrationDialog.qml
        qml/ManualPanel.qml
    SOURCES
        src/zcam.cpp src/zcam.h
        src/machine.cpp src/machine.h
        src/machines.cpp src/machines.h
        src/machinemodel.cpp src/machinemodel.h
        src/configmodel.cpp src/configmodel.h
        src/layersettingmodel.cpp src/layersettingmodel.h
        src/propertyjson.cpp src/propertyjson.h
        src/recipe.cpp src/recipe.h
        src/laser_recipe.cpp src/laser_recipe.h
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
        src/laser_bjjcz.cpp src/laser_bjjcz.h
        src/laser_rkq.cpp src/laser_rkq.h
        src/machinegcode.cpp src/machinegcode.h

        src/usb.cpp src/usb.h
        src/clipper.cpp src/clipper.h
        src/group.cpp src/group.h
        src/materialtest.cpp src/materialtest.h
        src/undo.cpp src/undo.h
        src/svg.cpp
        src/truncatedcubegeometry.cpp src/truncatedcubegeometry.h
        src/dxfimport.cpp src/dxfimport.h src/dxftess.h
        src/importipc2581.cpp src/importipc2581.h
        src/xmlreader.cpp src/xmlreader.h src/xmlwriter.cpp src/xmlwriter.h
        src/brepelement.cpp src/brepelement.h
        src/brepgeometry.cpp src/brepgeometry.h
        src/brepedgegeometry.cpp src/brepedgegeometry.h
        src/brepimport.cpp src/brepimport.h

        src/geometryworker.cpp src/geometryworker.h
        src/mediabrowser.cpp src/mediabrowser.h
        src/cameraelement.cpp src/cameraelement.h
        src/cameraoverlaygeometry.cpp src/cameraoverlaygeometry.h
        src/cameratexturedata.cpp src/cameratexturedata.h
        src/imageelement.cpp src/imageelement.h
        src/imageimport.cpp src/imageimport.h
        src/cal.cpp src/cal.h
        src/ogr.cpp src/ogr.h
        src/galvocalibration.cpp src/galvocalibration.h

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
      icons/camera.svg
      icons/file-new.svg
      icons/file-open.svg
      icons/file-save.svg
      icons/file-save-as.svg
      icons/file-import.svg
      icons/edit-undo.svg
      icons/edit-redo.svg
      icons/config.svg
      icons/folder-browse.svg
      # dark/black icons (used in main toolbar)
      icons/dark/file-new.svg
      icons/dark/file-open.svg
      icons/dark/file-save.svg
      icons/dark/file-save-as.svg
      icons/dark/file-import.svg
      icons/dark/file-export.svg
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

# ---------------------------------------------------------------------------
# Embed all files under data/ZCam/ as a Qt resource so they can be copied
# to ~/ZCam on first install.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE DATA_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS "data/ZCam/*")
# Remove hidden files (filenames starting with a dot)
foreach(f ${DATA_FILES})
      get_filename_component(fname ${f} NAME)
      if(fname MATCHES "^\\.")
            list(REMOVE_ITEM DATA_FILES ${f})
      endif()
endforeach()

qt_add_resources(zcam data
    PREFIX "/"
    BASE "data"
    FILES ${DATA_FILES}
    )

# ---------------------------------------------------------------------------
# Grid line shader snippets (CustomMaterial screen-space expansion).
# Qt resolves Shader/CustomMaterial urls relative to the owning QML
# document, whose qrc import path is "ZCam/qml/" — so the snippets
# must live under "qrc:/ZCam/shaders/".
# ---------------------------------------------------------------------------
qt_add_resources(zcam gridline_shaders
    PREFIX "/ZCam"
    BASE "."
    FILES shaders/gridline.vert shaders/gridline.frag
    )

# ---------------------------------------------------------------------------
# Manual — MkDocs generated HTML embedded via Qt resources.
# The HTML is built from manual/docs/*.md by `mkdocs build` into
# <build>/manual_site/.  The build runs at CMake configure time so the
# generated files are available immediately for qt_add_resources.
# Re-running cmake re-triggers the build whenever any markdown file or
# mkdocs.yml changes.
# ---------------------------------------------------------------------------
find_program(MKDOCS_EXECUTABLE mkdocs)
if(MKDOCS_EXECUTABLE)
    set(MKDOCS_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/manual)
    set(MKDOCS_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/manual_site)
    set(MKDOCS_CONFIG ${MKDOCS_SOURCE_DIR}/mkdocs.yml)

    # Glob all markdown files as configure-time dependencies
    file(GLOB MKDOCS_MD_FILES ${MKDOCS_SOURCE_DIR}/docs/*.md)

    # Compute a hash of all markdown files + mkdocs.yml to detect changes
    set(MKDOCS_DEPS_HASH "")
    foreach(f ${MKDOCS_MD_FILES} ${MKDOCS_CONFIG})
        file(SHA1 ${f} _hash)
        string(APPEND MKDOCS_DEPS_HASH ${_hash})
    endforeach()
    string(SHA1 MKDOCS_DEPS_HASH ${MKDOCS_DEPS_HASH})

    # Check if a rebuild is needed
    set(MKDOCS_HASH_FILE ${CMAKE_CURRENT_BINARY_DIR}/manual_site/.mkdocs_hash)
    set(NEEDS_REBUILD TRUE)
    if(EXISTS ${MKDOCS_HASH_FILE})
        file(READ ${MKDOCS_HASH_FILE} _old_hash)
        string(STRIP ${_old_hash} _old_hash)
        if(_old_hash STREQUAL MKDOCS_DEPS_HASH)
            set(NEEDS_REBUILD FALSE)
        endif()
    endif()

    if(NEEDS_REBUILD)
        message(STATUS "Manual: Building MkDocs HTML...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${MKDOCS_OUTPUT_DIR}
        )
        execute_process(
            COMMAND ${MKDOCS_EXECUTABLE} build
                    --config-file ${MKDOCS_CONFIG}
                    --site-dir ${MKDOCS_OUTPUT_DIR}
            WORKING_DIRECTORY ${MKDOCS_SOURCE_DIR}
            RESULT_VARIABLE MKDOCS_RESULT
            OUTPUT_VARIABLE MKDOCS_OUTPUT_VAR
            ERROR_VARIABLE MKDOCS_ERROR_VAR
        )
        if(MKDOCS_RESULT EQUAL 0)
            file(WRITE ${MKDOCS_HASH_FILE} ${MKDOCS_DEPS_HASH})
            message(STATUS "Manual: MkDocs HTML built successfully")
        else()
            message(WARNING "Manual: MkDocs build failed: ${MKDOCS_ERROR_VAR}")
        endif()
    else()
        message(STATUS "Manual: MkDocs HTML up to date")
    endif()

    # -----------------------------------------------------------------
    # Fix font paths: the readthedocs theme CSS references fonts at
    # "../fonts/" (root-level), but MkDocs places them under
    # "css/fonts/" with different filenames.  Create a root-level
    # "fonts/" directory with the expected names.
    # This runs every configure to ensure fonts are correct even if
    # the MkDocs build was skipped (cache hit).
    # -----------------------------------------------------------------
    set(MKDOCS_FONTS_SRC ${MKDOCS_OUTPUT_DIR}/css/fonts)
    set(MKDOCS_FONTS_DST ${MKDOCS_OUTPUT_DIR}/fonts)
    file(MAKE_DIRECTORY ${MKDOCS_FONTS_DST})
    # FontAwesome — same name, just copy
    file(COPY ${MKDOCS_FONTS_SRC}/fontawesome-webfont.eot DESTINATION ${MKDOCS_FONTS_DST})
    file(COPY ${MKDOCS_FONTS_SRC}/fontawesome-webfont.svg DESTINATION ${MKDOCS_FONTS_DST})
    file(COPY ${MKDOCS_FONTS_SRC}/fontawesome-webfont.ttf DESTINATION ${MKDOCS_FONTS_DST})
    file(COPY ${MKDOCS_FONTS_SRC}/fontawesome-webfont.woff DESTINATION ${MKDOCS_FONTS_DST})
    file(COPY ${MKDOCS_FONTS_SRC}/fontawesome-webfont.woff2 DESTINATION ${MKDOCS_FONTS_DST})
    # Lato — rename from mkdocs names to CSS-expected names
    file(INSTALL ${MKDOCS_FONTS_SRC}/lato-normal.woff2         DESTINATION ${MKDOCS_FONTS_DST} RENAME Lato-Regular.woff2)
    file(INSTALL ${MKDOCS_FONTS_SRC}/lato-normal.woff          DESTINATION ${MKDOCS_FONTS_DST} RENAME Lato-Regular.woff)
    file(INSTALL ${MKDOCS_FONTS_SRC}/lato-bold.woff2           DESTINATION ${MKDOCS_FONTS_DST} RENAME Lato-Bold.woff2)
    file(INSTALL ${MKDOCS_FONTS_SRC}/lato-bold.woff            DESTINATION ${MKDOCS_FONTS_DST} RENAME Lato-Bold.woff)
    file(INSTALL ${MKDOCS_FONTS_SRC}/lato-bold-italic.woff2   DESTINATION ${MKDOCS_FONTS_DST} RENAME Lato-BoldItalic.woff2)
    file(INSTALL ${MKDOCS_FONTS_SRC}/lato-bold-italic.woff    DESTINATION ${MKDOCS_FONTS_DST} RENAME Lato-BoldItalic.woff)
    file(INSTALL ${MKDOCS_FONTS_SRC}/lato-normal-italic.woff2 DESTINATION ${MKDOCS_FONTS_DST} RENAME Lato-Italic.woff2)
    file(INSTALL ${MKDOCS_FONTS_SRC}/lato-normal-italic.woff  DESTINATION ${MKDOCS_FONTS_DST} RENAME Lato-Italic.woff)
    # Roboto Slab — rename from mkdocs names to CSS-expected names
    file(INSTALL ${MKDOCS_FONTS_SRC}/Roboto-Slab-Regular.woff2 DESTINATION ${MKDOCS_FONTS_DST} RENAME RobotoSlab-Regular.woff2)
    file(INSTALL ${MKDOCS_FONTS_SRC}/Roboto-Slab-Bold.woff2    DESTINATION ${MKDOCS_FONTS_DST} RENAME RobotoSlab-Bold.woff2)
    # Also create empty .ttf files — the CSS references .ttf variants
    # but MkDocs only ships .woff/.woff2.  Browsers fall back to .woff2.
    file(TOUCH ${MKDOCS_FONTS_DST}/Lato-Regular.ttf)
    file(TOUCH ${MKDOCS_FONTS_DST}/Lato-Bold.ttf)
    file(TOUCH ${MKDOCS_FONTS_DST}/Lato-BoldItalic.ttf)
    file(TOUCH ${MKDOCS_FONTS_DST}/Lato-Italic.ttf)

    # Collect all generated HTML files for embedding
    file(GLOB_RECURSE MANUAL_HTML_FILES
        RELATIVE ${MKDOCS_OUTPUT_DIR}
        ${MKDOCS_OUTPUT_DIR}/*
    )


    # -----------------------------------------------------------------
    # Post-process: fix broken links in the generated HTML.
    # The mkdocs-static-i18n plugin generates broken cross-language
    # navigation links (href="manual.en.md") and directory URLs
    # (href="manual/") that the Qt resource system cannot resolve.
    # The fix-links.sh script rewrites them to proper index.html URLs.
    # -----------------------------------------------------------------
    execute_process(
        COMMAND bash ${MKDOCS_SOURCE_DIR}/fix-links.sh ${MKDOCS_OUTPUT_DIR}
        RESULT_VARIABLE FIX_LINKS_RESULT
    )
    if(NOT FIX_LINKS_RESULT EQUAL 0)
        message(WARNING "Manual: fix-links.sh failed with code ${FIX_LINKS_RESULT}")
    endif()

    # Re-collect files after post-processing
    file(GLOB_RECURSE MANUAL_HTML_FILES
        RELATIVE ${MKDOCS_OUTPUT_DIR}
        ${MKDOCS_OUTPUT_DIR}/*
    )

    # Prefix each file with the output directory for qt_add_resources.
    # Exclude the hash file and binary sitemap from the resource.
    set(MANUAL_RESOURCE_FILES)
    foreach(f ${MANUAL_HTML_FILES})
        if(f STREQUAL ".mkdocs_hash")
            continue()
        endif()
        if(f MATCHES "\\.gz$")
            continue()
        endif()
        list(APPEND MANUAL_RESOURCE_FILES "${MKDOCS_OUTPUT_DIR}/${f}")
    endforeach()

    if(MANUAL_RESOURCE_FILES)
        qt_add_resources(zcam manual
            PREFIX "/manual"
            BASE "${MKDOCS_OUTPUT_DIR}"
            FILES ${MANUAL_RESOURCE_FILES}
        )
        list(LENGTH MANUAL_RESOURCE_FILES _manual_count)
        message(STATUS "Manual: ${_manual_count} files embedded")
    else()
        message(WARNING "Manual: No HTML files found to embed")
    endif()
else()
    message(WARNING "MkDocs not found — manual panel will show an error page. Install mkdocs to enable the manual.")
endif()

target_link_libraries(zcam PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Quick
      Qt6::Quick3D
      Qt6::Quick3DPrivate
      Qt6::QuickControls2
      Qt6::Qml
      Qt6::QmlIntegration
      Qt6::Multimedia
      Qt6::WebEngineQuick
      ${OpenCV_LIBS}
      PkgConfig::UDEV
      PkgConfig::LIBUSB
      spnav
      )

# Link libpcap (pkg-config or fallback path)
if(PCAP_FOUND)
      if(TARGET PkgConfig::PCAP)
            target_link_libraries(zcam PRIVATE PkgConfig::PCAP)
      else()
            target_link_libraries(zcam PRIVATE ${PCAP_LIBRARY})
      endif()
endif()

# Explicitly include the src directory so ZCam is visible to the autogenerated code
target_include_directories(zcam PRIVATE src)
target_include_directories(zcam PRIVATE clipper2/include)
target_include_directories(zcam PRIVATE tess2)
target_include_directories(zcam PRIVATE libdxfrw/src)

#--------------------------------------------------------------------
#     OpenCASCADE (system-installed)
#--------------------------------------------------------------------
find_package(OpenCASCADE REQUIRED)

target_link_libraries(zcam PRIVATE
      TKernel TKMath TKBRep TKPrim TKMesh TKShHealing TKGeomBase TKG3d TKG2d TKTopAlgo TKGeomAlgo TKHLR
      )

# ---------------------------------------------------------------------------
# Install targets – required by "make appimage" (linuxdeploy)
# ---------------------------------------------------------------------------
install(TARGETS zcam RUNTIME DESTINATION bin)
install(FILES zcam.desktop DESTINATION share/applications)
install(FILES zcam.png DESTINATION share/icons/hicolor/256x256/apps RENAME zcam.png)