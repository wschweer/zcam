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

#include "brepelement.h"
#include "brepgeometry.h"

#include <QFileInfo>
#include <QVector3D>

#include <BRepTools.hxx>
#include <TopoDS_Shape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <TopoDS.hxx>
#include <Geom_Curve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Standard_ErrorHandler.hxx>
#include <Standard_Failure.hxx>

#include "logger.h"
#include "project.h"
#include "treemodel.h"
#include "undo.h"
#include "zcam.h"

//---------------------------------------------------------
//---------------------------------------------------------
//   BrepElement  (ctor / dtor)
//---------------------------------------------------------
//---------------------------------------------------------

BrepElement::BrepElement(ZCam* zcam, Element* parent) : Element3d(zcam, parent) {
      set_model(QStringLiteral("BRepShape.qml"));
      _brepGeometry  = new BrepGeometry();
      _edgeGeometry  = new BrepEdgeGeometry();
      _edgeGeometry->setSource(_brepGeometry);
      // Default steel-blue CAD colour; ProjectTree.qml binds this to
      // the model material (instance.color = element.curColor), so
      // without a valid colour the object renders black.
      setColor(QColor(120, 150, 180));
      }

BrepElement::~BrepElement() = default;

//---------------------------------------------------------
//---------------------------------------------------------
//   typeName
//---------------------------------------------------------
//---------------------------------------------------------

QString BrepElement::typeName() {
      return QStringLiteral("brep");
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   properties
//---------------------------------------------------------
//---------------------------------------------------------

const std::string_view BrepElement::properties() const {
      return _properties;
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   set_brepFilePath
//---------------------------------------------------------
//---------------------------------------------------------

void BrepElement::set_brepFilePath(const QString& path) {
      if (_sourcePath == path)
            return;
      _sourcePath = path;
      if (_brepGeometry)
            _brepGeometry->setFilePath(path);
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   loadFile
//---------------------------------------------------------
//---------------------------------------------------------

bool BrepElement::loadFile(const QString& path) {
      set_brepFilePath(path);
      // Force a geometry reload even when the path was already set
      // (e.g. after a project load where the file path was restored
      // before the geometry existed).  Otherwise BrepGeometry keeps
      // its stale data and the 3D model stays empty and unpickable.
      if (_brepGeometry)
            _brepGeometry->updateGeometry();

      TopoDS_Shape shape;
      if (!loadShapeFromFile(path, shape))
            return false;

      // Bounding box from the actual tessellated vertices (works
      // for solids, shells and faces alike).
      QVector3D bMin, bMax;
      if (computeMeshBounds(shape, 0.1, 0.5, bMin, bMax)) {
            _worldBBox = QRectF(bMin.x(), bMin.y(), bMax.x() - bMin.x(),
                                bMax.y() - bMin.y());
            _meshMin   = bMin;
            _meshMax   = bMax;
            _hasShape  = true;    // tessellated mesh loaded successfully
            updateSelectionGeometry();
            }

      // 2D outline from free edges (wireframe part of a brep file).
      // Solid parts may not yield a polyline — that's fine, the mesh
      // itself still makes the element visible.
      PathList pl;
      QRectF bbox;
      if (buildPolylineFromShape(shape, 0.1, pl, bbox)) {
            _polylinePathList = std::move(pl);
            _hasPolyline      = true;
            pathList()        = _polylinePathList;
            updateSelectionGeometry();
            }
      return true;
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   updateSelectionGeometry
//    Override: build the 3D wireframe selection box (12 edges of
//    _meshMin/_meshMax brick) instead of the flat z=0 rectangle
//    the base implementation generates around boundingBox().
//---------------------------------------------------------
void BrepElement::updateSelectionGeometry() {
      if (!_selectionGeometry)
            return;
      if (!_hasShape || _meshMin.x() > _meshMax.x()) {
            _selectionGeometry->setLines(Clipper2Lib::PathsD());
            return;
            }

      // 8 corners of the local bounding brick.
      QVector3D c[8];
      for (int i = 0; i < 8; ++i)
            c[i] = QVector3D((i & 1) ? _meshMax.x() : _meshMin.x(), (i & 2) ? _meshMax.y() : _meshMin.y(),
                             (i & 4) ? _meshMax.z() : _meshMin.z());
      // 12 edges: 4 at z-min, 4 at z-max, 4 vertical.
      // Corner index bit layout: bit0=x, bit1=y, bit2=z.
      // Bottom face (z = min): 0-1, 1-3, 3-2, 2-0
      // Top face    (z = max): 4-5, 5-7, 7-6, 6-4
      // Vertical edges:        0-4, 1-5, 2-6, 3-7
      std::vector<QVector3D> p0, p1;
      auto edge = [&](int a, int b) {
            p0.push_back(c[a]);
            p1.push_back(c[b]);
            };
      edge(0, 1);
      edge(1, 3);
      edge(3, 2);
      edge(2, 0);
      edge(4, 5);
      edge(5, 7);
      edge(7, 6);
      edge(6, 4);
      edge(0, 4);
      edge(1, 5);
      edge(2, 6);
      edge(3, 7);
      _selectionGeometry->setEdges3D(p0, p1);
      }

//---------------------------------------------------------
//   insertIntoProject
//---------------------------------------------------------
//---------------------------------------------------------

bool BrepElement::insertIntoProject(ZCam* zcam, Element* parent, int row) {
      if (!zcam || !parent)
            return false;
      auto* cmd = new InsertElementCommand(zcam, parent, this, row);
      zcam->project()->undo()->push(cmd);
      return true;
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   toJson / fromJson / fixup
//---------------------------------------------------------
//---------------------------------------------------------

json BrepElement::toJson() const {
      json data = Element3d::toJson();
      if (!_sourcePath.isEmpty())
            data["brepFilePath"] = _sourcePath.toUtf8().constData();
      return data;
      }

void BrepElement::fromJson(const json& data) {
      Element3d::fromJson(data);
      if (data.contains("brepFilePath"))
            set_brepFilePath(QString::fromUtf8(data.at("brepFilePath").get<std::string>().c_str()));
      // Reload the shape: only the file path is serialised, so after a
      // project load the mesh, the 2D outline and the cached bounding
      // boxes (_worldBBox/_meshMin/_meshMax) must be rebuilt from disk
      // — otherwise the element stays unpickable (degenerate box).
      if (!_sourcePath.isEmpty())
            loadFile(_sourcePath);
      }

void BrepElement::fixup() {
      Element3d::fixup();
      // fromJson() already reloads the shape; nothing extra to do here.
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   loadShapeFromFile  (static)
//---------------------------------------------------------
//---------------------------------------------------------

bool BrepElement::loadShapeFromFile(const QString& path, TopoDS_Shape& shape) {
      if (path.isEmpty())
            return false;
      OCC_CATCH_SIGNALS
      try {
            BRep_Builder builder;
            return BRepTools::Read(shape, path.toUtf8().constData(), builder) == Standard_True
                    && !shape.IsNull();
            }
      catch (const Standard_Failure&) {
            return false;
            }
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   buildPolylineFromShape  (static)
//---------------------------------------------------------
//---------------------------------------------------------

bool BrepElement::buildPolylineFromShape(const TopoDS_Shape& shape, double deflection,
                                         PathList& pathList, QRectF& worldBBox) {
      if (shape.IsNull())
            return false;
      OCC_CATCH_SIGNALS
      try {
            bool has = false;
            for (TopExp_Explorer edgeExp(shape, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
                  const TopoDS_Edge& edge = TopoDS::Edge(edgeExp.Current());
                  // Wireframe part: only edges that do NOT belong to a face.
                  bool onFace = false;
                  for (TopExp_Explorer fExp(shape, TopAbs_FACE); fExp.More(); fExp.Next()) {
                        const TopoDS_Face& face = TopoDS::Face(fExp.Current());
                        for (TopExp_Explorer eInF(face, TopAbs_EDGE); eInF.More(); eInF.Next()) {
                              if (eInF.Current().IsSame(edge)) {
                                    onFace = true;
                                    break;
                                    }
                              }
                        if (onFace)
                              break;
                        }
                  if (onFace)
                        continue;
                  Standard_Real first, last;
                  Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
                  if (curve.IsNull())
                        continue;
                  GeomAdaptor_Curve gac(curve, first, last);
                  GCPnts_TangentialDeflection disc(gac, 0.0, deflection);
                  if (disc.NbPoints() < 2)
                        continue;
                  Clipper2Lib::PathD path;
                  for (int i = 1; i <= disc.NbPoints(); ++i) {
                        const gp_Pnt& p = disc.Value(i);
                        path.push_back({p.X(), p.Y()});
                        worldBBox |= QRectF(p.X(), p.Y(), 0.0, 0.0);
                        has = true;
                        }
                  if (!path.empty())
                        pathList.push_back(std::move(path));
                  }
            return has;
            }
      catch (const Standard_Failure&) {
            return false;
            }
      }

//---------------------------------------------------------
//---------------------------------------------------------
//   computeMeshBounds  (static)
//---------------------------------------------------------
//---------------------------------------------------------

bool BrepElement::computeMeshBounds(const TopoDS_Shape& shape, double deflection, double angle,
                                    QVector3D& bMin, QVector3D& bMax) {
      if (shape.IsNull())
            return false;
      OCC_CATCH_SIGNALS
      try {
            TopoDS_Shape s = shape;
            BRepMesh_IncrementalMesh mesher(s, deflection, Standard_False, angle, Standard_True);
            bool has = false;
            for (TopExp_Explorer fExpl(s, TopAbs_FACE); fExpl.More(); fExpl.Next()) {
                  const TopoDS_Face& face = TopoDS::Face(fExpl.Current());
                  TopLoc_Location loc;
                  Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
                  if (tri.IsNull() || tri->NbNodes() < 1)
                        continue;
                  for (int i = 1; i <= tri->NbNodes(); ++i) {
                        gp_Pnt p = tri->Node(i);
                        if (!loc.IsIdentity())
                              p = p.Transformed(loc.Transformation());
                        if (!has) {
                              bMin = bMax = QVector3D(float(p.X()), float(p.Y()), float(p.Z()));
                              has   = true;
                              continue;
                              }
                        bMin.setX(std::min(bMin.x(), float(p.X())));
                        bMin.setY(std::min(bMin.y(), float(p.Y())));
                        bMin.setZ(std::min(bMin.z(), float(p.Z())));
                        bMax.setX(std::max(bMax.x(), float(p.X())));
                        bMax.setY(std::max(bMax.y(), float(p.Y())));
                        bMax.setZ(std::max(bMax.z(), float(p.Z())));
                        }
                  }
            return has;
            }
      catch (const Standard_Failure&) {
            return false;
            }
      }
