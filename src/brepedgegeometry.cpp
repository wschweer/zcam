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

#include "brepedgegeometry.h"
#include "brepgeometry.h"

#include <QVector>

//---------------------------------------------------------
//---------------------------------------------------------
//   BrepEdgeGeometry  (ctor / dtor)
//---------------------------------------------------------
//---------------------------------------------------------

BrepEdgeGeometry::BrepEdgeGeometry(QQuick3DObject* parent)
    : QQuick3DGeometry(parent) {
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   setSource
//---------------------------------------------------------
//---------------------------------------------------------

void BrepEdgeGeometry::setSource(BrepGeometry* src) {
      if (_source == src)
            return;
      if (_source)
            disconnect(_source, nullptr, this, nullptr);
      _source = src;
      emit sourceChanged();
      if (_source) {
            connect(_source, &BrepGeometry::hasEdgeDataChanged, this,
                    &BrepEdgeGeometry::rebuild);
            rebuild();
            }
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   rebuild
//---------------------------------------------------------
//---------------------------------------------------------

void BrepEdgeGeometry::rebuild() {
      clear();
      if (!_source || !_source->hasEdgeData()) {
            setVertexData(QByteArray());
            setIndexData(QByteArray());
            update();
            return;
            }

      // Upload the raw edge data computed by the source geometry.
      // Vertex layout matches the solid (pos/normal/uv) but only
      // pos is meaningful for lines; the shader uses the position
      // attribute only.
      setStride(BrepGeometry::edgeVertexStride());
      setPrimitiveType(PrimitiveType::Lines);
      setVertexData(_source->edgeVertexData());
      setIndexData(_source->edgeIndexData());
      setBounds(_source->boundsMin(), _source->boundsMax());
      addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
      addAttribute(Attribute::IndexSemantic, 0, Attribute::U32Type);
      update();
      }
