//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "imageelement.h"
#include "logger.h"
#include "project.h"
#include "undo.h"
#include "zcam.h"

#include <QFileInfo>
#include <QImage>
#include <QSize>
#include <cfloat>

//=========================================================
//  ImagePlaneGeometry
//=========================================================

//---------------------------------------------------------
//   ImagePlaneGeometry
//---------------------------------------------------------

ImagePlaneGeometry::ImagePlaneGeometry(QQuick3DObject* parent) : QQuick3DGeometry(parent) {
      rebuild();
      }

//---------------------------------------------------------
//   rebuild
//    Build a unit quad (-0.5..+0.5) in the XY plane with
//    normals pointing +Z and UVs spanning 0..1.
//    Vertex layout: position(3) + normal(3) + uv(2) = 8 floats
//---------------------------------------------------------

void ImagePlaneGeometry::rebuild() {
      //   corners (bottom-left, bottom-right, top-right, top-left)
      //   UV: (0,1) (1,1) (1,0) (0,0)  — origin bottom-left
      struct Vert { float x, y, z, nx, ny, nz, u, v; };
      const Vert verts[4] = {
            {-0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
            { 0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
            { 0.5f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
            {-0.5f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
            };
      const int indices[6] = {0, 1, 2, 0, 2, 3};

      QByteArray vertexData;
      vertexData.resize(6 * 8 * sizeof(float));
      auto* vd = reinterpret_cast<float*>(vertexData.data());
      for (int i = 0; i < 6; ++i) {
            const Vert& v = verts[indices[i]];
            vd[i * 8 + 0] = v.x;
            vd[i * 8 + 1] = v.y;
            vd[i * 8 + 2] = v.z;
            vd[i * 8 + 3] = v.nx;
            vd[i * 8 + 4] = v.ny;
            vd[i * 8 + 5] = v.nz;
            vd[i * 8 + 6] = v.u;
            vd[i * 8 + 7] = v.v;
            }

      clear();
      setStride(8 * sizeof(float));
      setPrimitiveType(PrimitiveType::Triangles);
      addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
      addAttribute(Attribute::NormalSemantic, 3 * sizeof(float), Attribute::F32Type);
      addAttribute(Attribute::TexCoordSemantic, 6 * sizeof(float), Attribute::F32Type);
      setVertexData(vertexData);
      setBounds(QVector3D(-0.5f, -0.5f, 0.0f), QVector3D(0.5f, 0.5f, 0.0f));
      update();
      }

//=========================================================
//  ImageTextureData
//=========================================================

//---------------------------------------------------------
//   ImageTextureData
//---------------------------------------------------------

ImageTextureData::ImageTextureData(QQuick3DObject* parent) : QQuick3DTextureData(parent) {
      setFormat(Format::RGBA8);
      setSize(QSize(2, 2));
      }

ImageTextureData::~ImageTextureData() = default;

//---------------------------------------------------------
//   loadFromFile
//    Load an image file from disk, convert to RGBA8888,
//    and upload as texture data.
//---------------------------------------------------------

void ImageTextureData::loadFromFile(const QString& path) {
      if (path.isEmpty()) {
            Log("ImageTextureData: empty path");
            return;
            }
      QImage img(path);
      if (img.isNull()) {
            Warning("ImageTextureData: failed to load image: {}", path.toUtf8().constData());
            return;
            }
      QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
      if (rgba.isNull()) {
            Warning("ImageTextureData: failed to convert image to RGBA: {}", path.toUtf8().constData());
            return;
            }
      _filePath = path;
      if (rgba.size() != size())
            setSize(rgba.size());
      setTextureData(QByteArray::fromRawData(
            reinterpret_cast<const char*>(rgba.constBits()), rgba.sizeInBytes()));
      emit textureDataNodeDirty();
      }

//=========================================================
//  ImageElement
//=========================================================

//---------------------------------------------------------
//   ImageElement
//---------------------------------------------------------

ImageElement::ImageElement(ZCam* zcam, Element* parent) : Element3d(zcam, parent) {
      setName(QStringLiteral("image"));
      set_model(QStringLiteral("ImageShape.qml"));
      // Lock aspect ratio by default (preserve image proportions)
      set_lockScale(static_cast<int>(LockScaleMode::Lock));
      // Default scale: 100 mm on the larger axis, aspect-ratio preserved.
      // Updated in loadFile() once the actual image dimensions are known.
      set_scale(QVector3D(100.0f, 100.0f, 1.0f));

      _textureData = new ImageTextureData();
      QJSEngine::setObjectOwnership(_textureData, QJSEngine::CppOwnership);
      _planeGeometry = new ImagePlaneGeometry();
      QJSEngine::setObjectOwnership(_planeGeometry, QJSEngine::CppOwnership);

      // A neutral grey so the bounding-box overlay is visible even
      // before an image is loaded.
      setColor(QColor(180, 180, 180));
      }

ImageElement::~ImageElement() {
      delete _textureData;
      delete _planeGeometry;
      }

//---------------------------------------------------------
//   loadFile
//    Load an image file from the given path, upload the
//    texture, and adjust the default scale to preserve the
//    image aspect ratio with the larger dimension = 100 mm.
//    Returns true on success.
//---------------------------------------------------------

bool ImageElement::loadFile(const QString& path) {
      if (path.isEmpty())
            return false;
      QImage img(path);
      if (img.isNull()) {
            Warning("ImageElement::loadFile: cannot load image: {}", path.toUtf8().constData());
            _imageLoaded = false;
            return false;
            }
      set_filePath(path);
      _textureData->loadFromFile(path);
      _imageLoaded = true;

      // Adjust the default scale so the larger dimension is 100 mm
      // and the aspect ratio is preserved.  Only set the default
      // scale when the scale has not been customized yet (i.e. the
      // element was just created for import or the scale is still
      // at the constructor default of 100×100).
      int w = img.width();
      int h = img.height();
      if (w <= 0 || h <= 0)
            return true;
      double aspect = double(w) / double(h);
      if (aspect >= 1.0) {
            // wider than tall → X = 100, Y = 100/aspect
            set_scale(QVector3D(100.0f, float(100.0 / aspect), 1.0f));
            }
      else {
            // taller than wide → Y = 100, X = 100*aspect
            set_scale(QVector3D(float(100.0 * aspect), 100.0f, 1.0f));
            }

      updateSelectionGeometry();
      emit textureDataChanged();
      return true;
      }

//---------------------------------------------------------
//   contentBoundingBox
//    The geometry is a unit quad (-0.5..+0.5), so the local
//    content bounding box is [-0.5, -0.5, 0.5, 0.5] → a
//    1×1 rectangle centered at origin.
//---------------------------------------------------------

QRectF ImageElement::contentBoundingBox() const {
      return QRectF(-0.5, -0.5, 1.0, 1.0);
      }

//---------------------------------------------------------
//   update
//    Called by the scene graph when the element is (re)added
//    to the 3D canvas.  Reloads the image from disk if the
//    file path was restored from JSON but the texture has
//    not been loaded yet.
//---------------------------------------------------------

void ImageElement::update(int flags) {
      Q_UNUSED(flags);
      if (!_imageLoaded && !filePath().isEmpty())
            loadFile(filePath());
      updateSelectionGeometry();
      }

//---------------------------------------------------------
//   toJson
//---------------------------------------------------------

json ImageElement::toJson() const {
      json data = Element3d::toJson();
      if (!filePath().isEmpty())
            data["filePath"] = filePath().toUtf8().constData();
      return data;
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void ImageElement::fromJson(const json& data) {
      Element3d::fromJson(data);
      if (data.contains("filePath")) {
            QString path = QString::fromUtf8(data.at("filePath").get<std::string>().c_str());
            set_filePath(path);
            // Defer the actual file load to fixup() / update() so
            // the scale is not overwritten by the import default.
            // However, we need to load it now for the texture to be
            // available; the scale was already restored from JSON.
            if (!path.isEmpty()) {
                  _textureData->loadFromFile(path);
                  _imageLoaded = true;
                  }
            }
      }

//---------------------------------------------------------
//   fixup
//---------------------------------------------------------

void ImageElement::fixup() {
      Element3d::fixup();
      if (!_imageLoaded && !filePath().isEmpty())
            loadFile(filePath());
      }