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

#pragma once

#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "clipper.h"

class ZCam;
class Element;
class Element3d;
class Project;
class Group;
class TreeModel;
class QJSEngine;

//--------------------------------------------------------------------
//     ScriptApi
//--------------------------------------------------------------------
//   Imperative JavaScript API exposed as the global ``zcam`` object
//   in the ScriptEngine.  All methods operate on the live project
//   tree through the existing ZCam / Project / Element C++ API.
//
//   Element arguments accept either a name string (resolved via
//   Element::byName) or an Element QObject* (passed directly from JS
//   via the ``project`` namespace).  This dual-acceptance is handled
//   by resolveElement().
//
//   All mutations are undoable — they route through
//   Project::changeProperty(), ZCam::createRectangle(), etc. which
//   push UndoCommand instances.  Use beginBatch()/endBatch() to
//   collapse multiple mutations into a single undo step.
class ScriptApi : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("Access via ScriptEngine 'zcam' global")

    private:
      ZCam* _zc {nullptr};
      QJSEngine* _engine {nullptr};

      Element* resolveElement(const QVariant& nameOrObj) const;
      static QString elementName(const QVariant& nameOrObj);

      /// Helper: find the target layer for new elements.  Walks the
      /// current element's parent chain for a visible Group, or falls
      /// back to the first visible layer under the Cad root.
      Group* findTargetLayer() const;

      /// Helper: convert a Polygon/Rectangle/Ellipse element's world
      /// geometry to Clipper2 PathsD (in root/world coordinates).
      Clipper2Lib::PathsD elementWorldPaths(Element3d* el) const;
      /// Helper: create a new Polygon from Clipper2 PathsD (in world
      /// coordinates) and add it to the current layer.  Returns the
      /// new Polygon or nullptr.
      Element3d* createPolygonFromWorldPaths(const Clipper2Lib::PathsD& paths) const;

    public:
      ScriptApi(QObject* parent = nullptr);
      void setZcam(ZCam* zc) { _zc = zc; }
      void setEngine(QJSEngine* eng) { _engine = eng; }
      //------------------------------------------------------------------
      //  Element creation / deletion
      //------------------------------------------------------------------

      /// Create a new element of *type* at (x, y).
      /// type ∈ "rectangle" | "polygon" | "ellipse" | "text" | "group".
      /// *parent* (optional) is the parent element name or object.
      /// Returns the new Element as a QObject*, or nullptr on failure.
      Q_INVOKABLE QObject* createElement(
          const QString& type, double x = 0, double y = 0, const QString& parent = QString());

      /// Delete the named element (undoable).
      Q_INVOKABLE bool deleteElement(const QString& name);

      /// Rename an element.  Returns the actual (de-duplicated) name.
      Q_INVOKABLE QString renameElement(const QString& name, const QString& newName);

      /// Move an element to a new parent (optional row, -1 = append).
      Q_INVOKABLE bool moveElement(const QString& name, const QString& newParent, int row = -1);

      //------------------------------------------------------------------
      //  Property access
      //------------------------------------------------------------------

      /// Read a property of the named element.  Returns the value
      /// (number, string, array, or null on error).
      Q_INVOKABLE QVariant getProperty(const QString& name, const QString& property);

      /// Write a property of the named element (undoable).
      /// *value* is converted to the property's Qt type.
      Q_INVOKABLE bool setProperty(const QString& name, const QString& property, const QVariant& value);

      //------------------------------------------------------------------
      //  Element queries
      //------------------------------------------------------------------

      /// List all elements in the project tree.
      /// Returns an array of {name, type, path} objects.
      /// *maxDepth* (-1 = unlimited) limits the traversal depth.
      Q_INVOKABLE QVariantList listElements(int maxDepth = -1);

      /// Find an element by name.  Returns the Element QObject* or null.
      /// ``findByName`` is an alias for AI-generated scripts that
      /// naturally call ``zcam.findByName(...)``.
      Q_INVOKABLE QObject* findElement(const QString& name);
      Q_INVOKABLE QObject* findByName(const QString& name) { return findElement(name); }
      /// Get the current (primary selected) element, or null.
      Q_INVOKABLE QObject* currentElement();

      /// Select an element by name (or deselect if name is empty).
      Q_INVOKABLE bool selectElement(const QString& name);

      /// Clear the selection.
      Q_INVOKABLE void clearSelection();

      /// Invoke a Q_INVOKABLE method on an element through its DYNAMIC
      /// meta-object (see ZCam::invokeElementMethod).  Needed because
      /// QObject wrappers for resolved elements use the static type of
      /// the pointer they were first wrapped with — methods declared
      /// only in derived classes (Nest::nest, Polygon::optimize, ...)
      /// are invisible from JS (typeof el.nest === "undefined").
      Q_INVOKABLE bool invokeElementMethod(
          const QString& name, const QString& method, const QVariantList& args = QVariantList());

      //------------------------------------------------------------------
      //  Geometry queries (element-based)
      //------------------------------------------------------------------

      /// World-space bounding box of an element → [minX, minY, maxX, maxY].
      Q_INVOKABLE QVariantList worldBoundingBox(const QString& name);

      /// Local bounding box of an element → [minX, minY, maxX, maxY].
      Q_INVOKABLE QVariantList boundingBox(const QString& name);

      /// World position of vertex *index* → [x, y, z].
      Q_INVOKABLE QVariantList vertexWorldPos(const QString& name, int index);

      /// Number of vertices (handles) of the element.
      Q_INVOKABLE int vertexCount(const QString& name);

      /// True if the world point (x, y) is inside the element's world bbox.
      Q_INVOKABLE bool containsWorldPoint(const QString& name, double x, double y);

      /// True if element *inner* is geometrically inside element *outer*
      /// (Clipper2 difference test on world paths).
      Q_INVOKABLE bool isInside(const QString& inner, const QString& outer);

      //------------------------------------------------------------------
      //  Geometry operations (element-based, create new Polygon)
      //------------------------------------------------------------------

      /// Boolean union of two polygon elements → new Polygon element name.
      Q_INVOKABLE QString unionPolygons(const QString& a, const QString& b);

      /// Boolean difference (A minus B) → new Polygon element name.
      Q_INVOKABLE QString differencePolygons(const QString& a, const QString& b);

      /// Boolean intersection → new Polygon element name.
      Q_INVOKABLE QString intersectPolygons(const QString& a, const QString& b);

      /// Offset (inflate/deflate) a polygon by *delta* → new Polygon name.
      Q_INVOKABLE QString offsetPolygon(const QString& name, double delta);

      //------------------------------------------------------------------
      //  Polygon vertex manipulation
      //------------------------------------------------------------------

      /// Append a line-to vertex to a polygon.
      Q_INVOKABLE bool addVertex(const QString& polygonName, double x, double y);

      /// Append a cubic bezier segment to a polygon.
      Q_INVOKABLE bool addBezier(
          const QString& polygonName, double c1x, double c1y, double c2x, double c2y, double ex, double ey);

      /// Set the position of vertex *index* (local coordinates).
      Q_INVOKABLE bool setVertexPos(const QString& name, int index, double x, double y, double z = 0);

      /// Optimize polygon geometry (remove redundant vertices).
      Q_INVOKABLE bool optimizePolygon(const QString& name);

      //------------------------------------------------------------------
      //  Transform
      //------------------------------------------------------------------

      /// Set the position of an element (undoable).
      Q_INVOKABLE bool setPos(const QString& name, double x, double y, double z = 0);

      /// Set the rotation of an element in degrees (undoable).
      Q_INVOKABLE bool setRot(const QString& name, double x, double y, double z = 0);

      /// Set the scale of an element (undoable).
      Q_INVOKABLE bool setScale(const QString& name, double x, double y, double z = 1);

      //------------------------------------------------------------------
      //  App control
      //------------------------------------------------------------------

      /// Create a new empty project.
      Q_INVOKABLE void newProject();

      /// Save the current project.
      Q_INVOKABLE bool saveProject();

      /// Undo the last operation.
      Q_INVOKABLE void undo();

      /// Redo the last undone operation.
      Q_INVOKABLE void redo();

      /// Begin a batch of operations (single undo step).
      Q_INVOKABLE void beginBatch();

      /// End a batch of operations.
      Q_INVOKABLE void endBatch();

      /// Refresh CAM data.
      Q_INVOKABLE void refreshCam();

      /// Export the CAD tree to an SVG file.
      Q_INVOKABLE bool exportSvg(const QString& path);

      /// Export the CAD tree to a DXF file.
      Q_INVOKABLE bool exportDxf(const QString& path);

      /// Import an external file (SVG, DXF, image, ...) into the project.
      Q_INVOKABLE bool importFile(const QString& path);

      /// Import an SVG file and position it at (x, y).
      Q_INVOKABLE bool importSvgAt(const QString& path, double x, double y);

      /// Capture a screenshot of the 3D canvas.  Returns the file path.
      Q_INVOKABLE QString screenshot();

      //------------------------------------------------------------------
      //  Machine / laser
      //------------------------------------------------------------------

      /// Get the current machine name.
      Q_INVOKABLE QString machineName();

      /// Set the current machine by name.
      Q_INVOKABLE bool setMachine(const QString& name);

      /// Start laser framing mode.
      Q_INVOKABLE bool startFraming();

      /// Start laser marking.
      Q_INVOKABLE bool startMarking();

      /// Stop laser framing/marking.
      Q_INVOKABLE void stopLaser();

      //------------------------------------------------------------------
      //  Layer / fixture
      //------------------------------------------------------------------

      /// Add a new CAD layer.  Returns the layer element name.
      Q_INVOKABLE QString addLayer();

      /// Add a new fixture.  Returns the fixture element name.
      Q_INVOKABLE QString addFixture();

      /// Add a new laser MOP (laser layer) to a fixture.
      Q_INVOKABLE QString addLaserMop(const QString& fixtureName);

      /// Assign a named laser MOP (laser layer) to an element by
      /// setting its ``mop`` property.  The MOP name is resolved
      /// via ZCam::mopPtr(); pass an empty string to clear the
      /// assignment (inherit from parent).  The change is undoable.
      Q_INVOKABLE bool setMops(const QString& elementName, const QString& mopsName);

      //------------------------------------------------------------------
      //  Utility
      //------------------------------------------------------------------

      /// Log a message to the application log.
      Q_INVOKABLE void log(const QString& msg);

      /// Evaluate a sub-expression in the project namespace.
      /// Returns the result value or null on error.
      Q_INVOKABLE QVariant eval(const QString& script);
      };