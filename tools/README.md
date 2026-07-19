# Build tools

This directory holds helper tools for AppImage creation.
The files are downloaded on demand by `make appimage` if missing.

## Required tools

- **linuxdeploy** – creates the AppDir structure and bundles libraries
- **linuxdeploy-qt** (symlinked as `linuxdeploy-plugin-qt.AppImage`) – Qt plugin
  for linuxdeploy that deploys Qt libraries, plugins and QML modules
- **lib/libtiff.so.5** – compatibility symlink (system has libtiff.so.6,
  Qt image format plugin expects libtiff.so.5)

## Manual setup

```bash
mkdir -p tools/lib
wget -O tools/linuxdeploy \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget -O tools/linuxdeploy-qt \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x tools/linuxdeploy tools/linuxdeploy-qt
ln -sf linuxdeploy-qt tools/linuxdeploy-plugin-qt.AppImage
ln -sf /usr/lib/x86_64-linux-gnu/libtiff.so.6 tools/lib/libtiff.so.5
```