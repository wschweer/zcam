## Build

The build of ZCam is controlled by a Makefile:

Go to the project directory.

- ```make init``` creates a build directory and initializes necessary submodules
- ```make``` build ZCam and starts the app
- ```make i``` copies (installs) ZCam in ```$HOME/bin```

in summary:
```
make init
make
make i
```
