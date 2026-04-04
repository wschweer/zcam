#
#     this helper file defines several project tasks as
#     "make" dependencies for easy command line usage
#

PROJECT=zcam

export LOGFILE=${HOME}/${PROJECT}/${PROJECT}.log

${PROJECT}:
	#export QT_FATAL_WARNINGS=true
	cd build; cmake --build . --parallel 32 && cd .. && build/${PROJECT}

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

#
#     "install" target
#     copies project into HOME/bin for local use
#     (for this to work HOME/bin must exist and must be part of your PATH
#
i:
	cp build/${PROJECT}  /home/ws/bin

#
#     git push helper
#
push:
	git rebase -i origin/main

#
#     initialize the project to use the gcc compiler
#
init-gcc:
	rm -rf build; mkdir build; cd build; cmake -G Ninja ..

#
#     initialize the project to use the clang compiler
#
init:
	rm -rf build; mkdir build; cd build; cmake -D CMAKE_CXX_COMPILER=clang++ -G Ninja ..
