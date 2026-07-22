#
#     this helper file defines several project tasks as
#     "make" dependencies for easy command line usage
#

PROJECT=zcam

export LOGFILE=${HOME}/${PROJECT}/${PROJECT}.log

${PROJECT}:
	#export QT_FATAL_WARNINGS=true
	cd build; cmake --build . --parallel 32 && cd .. && build/${PROJECT}

uv:
	#export QT_FATAL_WARNINGS=true
	cd build; cmake --build . --parallel 32 && sudo setcap cap_net_raw+ep zcam && cd .. && build/${PROJECT}

#
#     "test" target
#
t:
	./build/${PROJECT}

#
#     "debug" target
#
d:
	gdb build/${PROJECT} core*

profile:
	valgrind --tool=callgrind build/${PROJECT}
	valgrind --tool=callgrind callgrind.out.*

#
#     "install" target
#     copies project into HOME/bin for local use
#     (for this to work HOME/bin must exist and must be part of your PATH
#
i:
	cp build/${PROJECT}  §{HOME}/bin

#
#     git push helper
#
push:
	git rebase -i origin/main

#
#     "appimage" target
#     Builds the project, installs into a staging AppDir via cmake --install,
#     then uses linuxdeploy + linuxdeploy-plugin-qt to bundle the app
#     (including Qt libraries, plugins and QML modules) into a portable AppImage.
#
appimage: build/${PROJECT} tools/linuxdeploy tools/linuxdeploy-qt tools/lib/libtiff.so.5
	rm -rf AppDir
	mkdir -p AppDir
	cd build && cmake --install . --prefix ../AppDir/usr
	export QMAKE=/home/ws/Qt/6.11.1/gcc_64/bin/qmake6 \
	  && export LD_LIBRARY_PATH=$$(pwd)/tools/lib:$$LD_LIBRARY_PATH \
	  && export QML_SOURCES_PATHS=$$(pwd)/qml \
	  && export EXTRA_QML_PLUGINS=QtQuick3D \
	  && export EXTRA_QT_PLUGINS=quick3dassetimport \
	  && export APPIMAGE_EXTRACT_AND_RUN=1 \
	  && export DEPLOY_GLIBC_VERSION=2.35 \
	  && export PATH=$$(pwd)/tools:$$PATH \
	  && ./tools/linuxdeploy --appimage-extract-and-run \
	    --appdir AppDir \
	    --desktop-file zcam.desktop \
	    --icon-file zcam.png \
	    --plugin qt \
	    --output appimage

tools/linuxdeploy:
	mkdir -p tools
	wget -O tools/linuxdeploy \
	  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
	chmod +x tools/linuxdeploy

tools/linuxdeploy-qt:
	mkdir -p tools
	wget -O tools/linuxdeploy-qt \
	  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
	chmod +x tools/linuxdeploy-qt
	ln -sf linuxdeploy-qt tools/linuxdeploy-plugin-qt.AppImage

tools/lib/libtiff.so.5:
	mkdir -p tools/lib
	ln -sf /usr/lib/x86_64-linux-gnu/libtiff.so.6 tools/lib/libtiff.so.5

build/${PROJECT}:
	#export QT_FATAL_WARNINGS=true
	cd build; cmake --build . --parallel 32

#
#     initialize the project to use the gcc compiler
#
init-gcc:
	rm -rf build; mkdir build; cd build; cmake -G Ninja ..

#
#     initialize the project to use the clang compiler
#     also initializes and updates the libdxfrw git submodule
#
init:
	git submodule update --init --recursive libdxfrw
	rm -rf build; mkdir build; cd build; cmake -D CMAKE_CXX_COMPILER=clang++ -G Ninja ..
