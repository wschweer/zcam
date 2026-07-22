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
#include <QQmlEngine>
#include <QJSEngine>
#include <QVector3D>
#include <QFont>
#include <QColor>
#include <QRectF>
#include <QtQml/qqmlregistration.h>
#include <nlohmann/json.hpp>

#include "project.h"
#include "recipe.h"
#include "machines.h"
#include "group.h"

class Project;
class Element3d;
class TreeModel;
//---------------------------------------------------------
//   Config
//---------------------------------------------------------

class Config : public QObject
{
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      PROPV(int, iconSize, 32)
      PROPV(int, navCubeSize, 200)
      PROPV(double, handleSize, 0.02)
      PROPV(QString, font, QStringLiteral("NotoSans"))
      PROPV(int, fontSize, 12)
      PROPV(QColor, panelBG, QColor("darkGray"))
      PROPV(QColor, accentColor, QColor("teal"))
      PROPV(QColor, gridColor, QColor("#808080"))
      PROPV(QColor, markColor, QColor("#000000"))
      PROPV(QColor, moveColor, QColor("#0000ff"))
      PROPV(QColor, framingColor, QColor("#00ff00"))
      PROPV(bool, showGrid, true)
      PROPV(double, gridSpacing, 10.0)
      PROPV(QString, defaultMachine, QString())
      PROPV(QString, artworkDirectory, QString())
      PROPV(QString, iconDirectory, QString::fromUtf8("/usr/share/icons"))
      PROPV(QString, machinesDirectory, QString())
      PROPV(QString, recipesDirectory, QString())

      PROPV(double, dxfScale, 72.0)

      inline static constexpr std::string_view _properties {
         R"json({
                  "class": "Config",
                  "items": [
                    {
                      "columns": {
                        "count": 2,
                        "cat": "GUI",
                        "items": [
                          {
                            "name": "iconSize",
                            "label": "Icon Size",
                            "type": "int",
                            "cat": "GUI",
                            "min": 16,
                            "max": 128,
                            "default": 32
                          },
                          {
                            "name": "navCubeSize",
                            "label": "Nav Cube Size",
                            "type": "int",
                            "cat": "GUI",
                            "min": 80,
                            "max": 400,
                            "default": 200
                          },
                          {
                            "name": "handleSize",
                            "label": "Handle Size",
                            "type": "float",
                            "cat": "GUI",
                            "min": 0.01,
                            "max": 1.0,
                            "default": 0.2,
                            "precision": 2,
                            "step": 0.05,
                            "bigStep": 0.5
                          },
                          {
                            "row": {
                              "font": {
                                "label": "Font",
                                "type": "font",
                                "cat": "GUI",
                                "default": "NotoSans"
                              },
                              "fontSize": {
                                "label": "Font Size",
                                "type": "int",
                                "cat": "GUI",
                                "min": 6,
                                "max": 72,
                                "default": 12
                              }
                            },
                            "label": "Font",
                            "colSpan": 2
                          }
                        ]
                      }
                    },
                    {
                      "columns": {
                        "count": 2,
                        "cat": "View",
                        "items": [
                          {
                            "name": "showGrid",
                            "label": "Show Grid",
                            "type": "bool",
                            "cat": "View",
                            "default": true
                          },
                          {
                            "name": "gridSpacing",
                            "label": "Grid Spacing",
                            "type": "float",
                            "cat": "View",
                            "unit": "mm",
                            "min": 1.0,
                            "max": 100.0,
                            "default": 10.0,
                            "precision": 1,
                            "step": 0.5,
                            "bigStep": 5.0
                          }
                        ]
                      }
                    },
                    {
                      "columns": {
                        "count": 2,
                        "cat": "Colors",
                        "items": [
                          {
                            "name": "panelBG",
                            "label": "Panel BG",
                            "type": "color",
                            "cat": "Colors"
                          },
                          {
                            "name": "accentColor",
                            "label": "Accent Color",
                            "type": "color",
                            "cat": "Colors"
                          },
                          {
                            "name": "gridColor",
                            "label": "Grid Color",
                            "type": "color",
                            "cat": "Colors"
                          },
                          {
                            "name": "framingColor",
                            "label": "Framing Color",
                            "type": "color",
                            "cat": "Colors"
                          },
                          {
                            "name": "markColor",
                            "label": "Mark Color",
                            "type": "color",
                            "cat": "Colors"
                          },
                          {
                            "name": "moveColor",
                            "label": "Move Color",
                            "type": "color",
                            "cat": "Colors"
                          }
                        ]
                      }
                    },
                    {
                      "columns": {
                        "count": 2,
                        "cat": "Project",
                        "items": [
                          {
                            "name": "defaultMachine",
                            "label": "Default Machine",
                            "type": "machineName",
                            "cat": "Project",
                            "default": ""
                          },
                          {
                            "name": "artworkDirectory",
                            "label": "Artwork Path",
                            "type": "path",
                            "cat": "Project",
                            "default": ""
                          },
                          {
                            "name": "iconDirectory",
                            "label": "Icon Path",
                            "type": "path",
                            "cat": "Project",
                            "default": "~/ZCam/icons"
                          },
                          {
                            "name": "machinesDirectory",
                            "label": "Machines Path",
                            "type": "path",
                            "cat": "Project",
                            "default": ""
                          },
                          {
                            "name": "recipesDirectory",
                            "label": "Recipes Path",
                            "type": "path",
                            "cat": "Project",
                            "default": ""
                          },
                          {
                            "name": "dxfScale",
                            "label": "DXF Scale",
                            "type": "float",
                            "cat": "Project",
                            "unit": "dpmm",
                            "min": 0.001,
                            "max": 1000.0,
                            "default": 72.0,
                            "precision": 3,
                            "step": 0.1,
                            "bigStep": 1.0
                          }
                        ]
                      }
                    }
                  ]
                })json"};

    public:
      explicit Config(QObject* parent = nullptr) : QObject(parent) {}
      const std::string_view properties() const { return _properties; }
      nlohmann::json toJson() const;
      bool fromJson(const nlohmann::json&);
};
//---------------------------------------------------------
//   ZCam
//---------------------------------------------------------

class ZCam : public QObject
{
      Q_OBJECT
      QML_ELEMENT
      QML_SINGLETON

      Q_PROPERTY(bool camDirty READ camDirty NOTIFY camDirtyChanged)

      PROPV(Config*, config, nullptr)
      PROPV(Project*, project, nullptr)
      PROPV(Element3d*, rootElement, nullptr)

      // currentElement: custom setter so that curColorChanged is emitted
      // on the old and new element whenever the selection changes,
      // regardless of whether the change originates from QML (TreeView)
      // or from C++ (3D canvas pick).
      Q_PROPERTY(
          Element3d* currentElement READ currentElement WRITE setCurrentElement NOTIFY currentElementChanged)

      PROPV(Element3d*, hoverElement, nullptr)
      PROPV(TreeModel*, treeModel, nullptr)
      PROPV(Machines*, machines, nullptr)
      PROPV(LaserReceipes*, recipes, nullptr)
      PROPV(QString, currentTool, QString("pointer"))

      void loadAssets();

      bool _camDirty {false};

      Element3d* _currentElement {nullptr};

      // State for handle drag undo
      QPointer<Element3d> _vertexDragElement;
      int _vertexDragIndex {-1};
      QVector3D _vertexDragOrigPos;

      // State for element drag/rotate/scale undo
      QPointer<Element3d> _elementDragElement;
      QVector3D _elementDragOrigPos;
      QVector3D _elementDragOrigRot;
      QVector3D _elementDragOrigScale;

      // Pending segment-selection state: when the user clicks on an
      // already-selected polygon, the segment selection is deferred to
      // endElementDrag() so that click+drag moves the polygon (with
      // bounding box visible) while a pure click (no drag) selects the
      // nearest segment.
      QPointer<Element3d> _pendingSegmentElement;
      QVector3D _pendingSegmentClickPos;
      bool _pendingSegmentToggleOff {false};
      // State for magnetic grid snap during element drag.
      // The reference point for each element is (0,0) in local coords.
      // When the reference point crosses a grid line, the element "snaps"
      // to that line.  Further dragging accumulates in _snapExcess and
      // once it exceeds half the minor spacing, the element "breaks free"
      // and moves freely until the next line is crossed.
      struct SnapState {
            bool activeX {false}; ///< currently snapped on X axis
            bool activeY {false}; ///< currently snapped on Y axis
            double excessX {0.0}; ///< accumulated drag beyond the snap point (X)
            double excessY {0.0}; ///< accumulated drag beyond the snap point (Y)
            void reset() {
                  activeX = activeY = false;
                  excessX = excessY = 0.0;
            }
      };
      SnapState _snapState;

      // SVG drag-preview state
      TessGeometry* _dragPreviewGeometry {nullptr};
      QString _svgDragPath;
      QRectF _svgDragBBox;

      Group* findFirstVisibleLayer(Element* root) const;
      /// Find the Layer that is the current element itself, or the
      /// nearest Layer ancestor of the current element, walking up
      /// the parent chain until Cad is reached.  Returns nullptr if
      /// there is no current element, no Layer is found in the chain,
      /// or the found Layer is not visible.
      Group* findCurrentLayer() const;

    signals:
      void camDirtyChanged();
      void currentElementChanged();
      void showFontMediaBrowserRequested();
      void dragPreviewGeometryChanged();
      void remove3dElement(Element3d*); // signal 3d gui to remove an element from the scene graph
      void add3dElement(Element3d*);    // signal 3d gui to add a new element into the scene graph
      void addSubElement(Element3d*,
                         Element3d*); // signal 3d gui to add a new subelement into the scene graph
      //      void rootElementChanged(Element3d*);        // signal 3d gui to rebuild scene graph
      void startDragElement(Element3d*); // signal 3d gui to drag this element

      /// Emitted when a brand-new empty project was created.
      void projectCreated();
      /// Emitted after a project file was successfully loaded.
      void projectLoaded(const QString& path);
      /// Emitted after the project was saved.
      void projectSaved(const QString& path);

    public:
      explicit ZCam(QObject* parent = nullptr);
      static ZCam* create(QQmlEngine*, QJSEngine*);
      void undoChangeProperty(Element*, const char*, QVariant) {}
      // ── Project lifecycle (moved from ProjectManager) ───────────────────
      /// Start a fresh, unnamed project.  Returns false if user cancelled.
      Q_INVOKABLE void newProject(bool clearPersistedPath = true);
      void startNewProject(bool clearPersistedPath = true);
      void endNewProject();

      /// Open a project from *path*.  Pass an empty string to trigger the
      /// file-dialog logic from C++.  When *skipCamUpdate* is true, the
      /// expensive Cam::updateCam() call is skipped.
      Q_INVOKABLE bool openProject(const QString& path, bool skipCamUpdate = false);

      /// Save to the current path; falls through to saveAs if none is set.
      Q_INVOKABLE bool save();

      /// Save under a new path.
      Q_INVOKABLE bool saveAs(const QString& path);

      /// Import an external file into the current project.
      Q_INVOKABLE bool importFile(const QString& path);

      /// Try to restore the last-opened project at application startup.
      Q_INVOKABLE bool restoreLastProject();

      /// update 3d canvas and project tree
      void update();

    private:
      bool writeProjectFile(const std::string& path);
      bool readProjectFile(const std::string& path, bool skipCamUpdate = false);

    public:
      Q_INVOKABLE void saveAssets();
      bool camDirty() const { return _camDirty; }
      void setCamDirty(bool v);
      Element3d* currentElement() const { return _currentElement; }
      void setCurrentElement(Element3d* el);

      /// Return the default machines directory: $(HOME)/ZCam/machines
      static QString defaultMachinesDirectory();
      /// Return the default recipes directory: $(HOME)/ZCam/recipes
      static QString defaultRecipesDirectory();
      /// Return the default artwork directory: $(HOME)/ZCam/artwork
      static QString defaultArtworkDirectory();
      /// Return the default icon directory: ~/ZCam/icons
      static QString defaultIconDirectory();
      /// Return the configured machines directory, or the default if empty.
      QString machinesDirectory() const;
      /// Return the configured recipes directory, or the default if empty.
      QString recipesDirectory() const;
      /// Expand a leading '~' to the user's home directory.
      Q_INVOKABLE static QString expandPath(const QString& path);

      /// Called from QML when an element is dragged in the 3D viewport.
      /// When the project's Grid has snap enabled, grid lines act
      /// magnetically: the element's reference point (0,0 in local
      /// coords) snaps to a grid line when it crosses it, and the
      /// element only breaks free after the drag exceeds half the
      /// minor grid spacing beyond the snap point.
      Q_INVOKABLE void dragged(Element3d* element, const QVector3D& delta, int modifiers);

      /// Called from QML when an element is rotated in the 3D viewport.
      Q_INVOKABLE void rotated(Element3d* element, const QVector3D& deltaRotation, int modifiers);

      /// Called from QML when an element is scaled in the 3D viewport.
      /// The pivot point (in world/root coordinates) is the center of
      /// the scaling operation — typically the current mouse position.
      /// The element's position is adjusted so that the pivot point
      /// stays fixed in world space, analogous to zooming the canvas.
      Q_INVOKABLE void scaled(Element3d* element, const QVector3D& scaleFactor, int modifiers,
                              const QVector3D& pivot);
      /// Called from QML when the user starts dragging an element.
      /// Records the original transform for the undo command and
      /// resets the magnetic-snap state.
      Q_INVOKABLE void startElementDrag(Element3d* element);

      /// Called from QML when the user finishes dragging an element.
      /// Creates and pushes a single undo command with the original and final transforms.
      Q_INVOKABLE void endElementDrag();

      /// Called from QML when an element is hovered
      Q_INVOKABLE void hover(Element3d* element);
      Q_INVOKABLE void doubleClick(Element3d* element) {}
      Q_INVOKABLE void mousePress(Element3d* element, int buttons, int modifiers, double x, double y);

      /// Custom picking: traverse the element tree and return the
      /// topmost (smallest area) visible Element3d whose world bounding
      /// box contains the given world-space point (x, y).  Elements
      /// are searched from innermost (smallest area) to outermost.
      /// Returns nullptr if no element is hit.
      Q_INVOKABLE Element3d* pickElement(double x, double y);

      /// Picking variant used at the start of a left-button drag.
      /// When the currently selected element is a Group and the point
      /// lies inside the Group's world bounding box, the Group itself is
      /// returned instead of any smaller child underneath.  This turns
      /// the visible selection bounding box into a drag handle: dragging
      /// it moves the whole Group (and therefore all its children).
      /// In all other cases this delegates to pickElement().
      Q_INVOKABLE Element3d* pickDragTarget(double x, double y);

      /// Called from QML when the user starts dragging a handle.
      /// Records the original handle position for the undo command.
      Q_INVOKABLE void startVertexDrag(Element3d* element, int vertexIndex);

      /// Called from QML during dragging a handle.
      /// Sets the handle to the given world position (live update, no undo).
      Q_INVOKABLE void dragVertexTo(Element3d* element, int vertexIndex, const QVector3D& worldPos);

      /// Called from QML when the user finishes dragging a handle.
      /// Creates and pushes the undo command with the original and final positions.
      Q_INVOKABLE void endVertexDrag(Element3d* element, int vertexIndex);

      /// Select a segment of a Polygon element by segment index.
      /// The segment is highlighted in the 3D viewport and only its
      /// endpoint vertices show handles.  Pass -1 to clear the selection.
      Q_INVOKABLE void selectSegment(Element3d* element, int segmentIndex);

      /// Clear the current segment selection on the given element (if any).
      Q_INVOKABLE void clearSegmentSelection(Element3d* element);

      /// Find and select the segment of the given Polygon element that is
      /// closest to the given world position.  Returns the selected segment
      /// index, or -1 if the element has no segments.
      Q_INVOKABLE int selectNearestSegment(Element3d* element, const QVector3D& worldPos);

      /// Returns a list of all Layer element names in the current project.
      Q_INVOKABLE QStringList layerNames() const;
      /// Returns the Layer* pointer for a given layer name, or nullptr.
      Q_INVOKABLE Group* layerPtr(const QString& name) const;

      /// Returns a list of all LaserLayer element names in the current project.
      Q_INVOKABLE QStringList laserLayerNames() const;
      /// Returns the LaserLayer* pointer for a given name, or nullptr.
      Q_INVOKABLE Recipe* laserLayerPtr(const QString& name) const;

      /// Returns a list of all Recipe names from ZCam::recipes.
      Q_INVOKABLE QStringList recipeNames() const;
      /// Returns the Recipe* pointer for a given recipe name, or nullptr.
      Q_INVOKABLE LaserRecipe* recipePtr(const QString& name) const;

      /// Create a new Rectangle element at the given world position
      /// and add it to the current Layer (the Layer of the selected
      /// element) or the first visible Layer as fallback.  Returns
      /// the new Rectangle or nullptr if no layer is available.
      Q_INVOKABLE Element3d* createRectangle(double x, double y);

      /// Create a new Polygon element at the given world position
      /// and add it to the current Layer (the Layer of the selected
      /// element) or the first visible Layer as fallback.  Returns
      /// the new Polygon or nullptr if no layer is available.
      Q_INVOKABLE Element3d* createPolygon(double x, double y);

      /// Create a new Ellipse element at the given world position
      /// and add it to the current Layer (the Layer of the selected
      /// element) or the first visible Layer as fallback.  Returns
      /// the new Ellipse or nullptr if no layer is available.
      Q_INVOKABLE Element3d* createEllipse(double x, double y);

      /// Create a new Text element at the given world position
      /// and add it to the current Layer (the Layer of the selected
      /// element) or the first visible Layer as fallback.  Returns
      /// the new Text or nullptr if no layer is available.
      Q_INVOKABLE Element3d* createText(double x, double y);

      /// Apply the given font family to the currently selected Text element.
      /// The change is routed through the undo system and marks the project dirty.
      Q_INVOKABLE void applyFontToCurrentText(const QString& family);

      /// Re-parent an element to a new parent Element3d.  The element's
      /// local pos/rot/scale are adjusted so that its world-space
      /// transform stays the same (the visual position doesn't jump).
      /// The operation is undoable via the MoveElementCommand which
      /// also takes care of scene-graph updates.
      ///
      /// This is the core of the drag-drop grouping mechanism:
      /// when the user drops one draggable element onto another, the
      /// dropped element becomes a child of the target element.
      /// Because every Element3d already supports children and has a
      /// transformation matrix, any element can act as a group.
      Q_INVOKABLE void reparentElement(Element3d* element, Element3d* newParent);

      /// Delete the current element if it is deletable.
      /// The operation is undoable.
      Q_INVOKABLE void deleteCurrentElement();

      /// Center the given element on the workspace midpoint.
      /// The workspace size is determined by the current machine's
      /// maxTravel (X and Y).  Only Text, Polygon, Ellipse and
      /// Rectangle elements are accepted; Z is always set to zero.
      /// The operation is undoable.
      Q_INVOKABLE void centerOnWorkspace(Element3d* element);

      /// Recalculate cam data and clear the dirty flag.
      Q_INVOKABLE void refreshCam();

      Q_INVOKABLE void createMaterialTest();
      Q_INVOKABLE void createGalvoTest();
      void importSvg(const QString& path);

      /// Returns the bounding box (in mm) of the SVG at the given path.
      /// The box reflects the Y-mirrored, px→mm-converted path data.
      /// Returns an empty QRectF if the SVG cannot be parsed.
      Q_INVOKABLE QRectF svgBoundingBox(const QString& path);

      /// Import an SVG file and position the resulting Polygon so that
      /// the bounding box's bottom-left corner is at (x, y) in the
      /// parent layer's local coordinate space.
      Q_INVOKABLE void importSvgAt(const QString& path, double x, double y);

      /// Prepare a drag-preview geometry for the SVG at the given path.
      /// The geometry is a rectangle outline matching the SVG bounding box.
      /// Call endSvgDrag() to clean up.
      Q_INVOKABLE void startSvgDrag(const QString& path);
      Q_INVOKABLE void startDxfDrag(const QString& path);
      Q_INVOKABLE void endSvgDrag();

      /// Compute the bounding box of a DXF file in millimetres.
      Q_INVOKABLE QRectF dxfBoundingBox(const QString& path);

      /// Import a DXF/DWG file and position it so the bounding box's
      /// bottom-left corner is at (x, y) in scene coordinates.
      Q_INVOKABLE bool importDxfAt(const QString& path, double x, double y);

      /// The drag-preview geometry (rectangle outline) for the current
      /// SVG drag operation, or nullptr when no drag is active.
      Q_PROPERTY(TessGeometry* dragPreviewGeometry READ dragPreviewGeometry NOTIFY dragPreviewGeometryChanged)
      TessGeometry* dragPreviewGeometry() const { return _dragPreviewGeometry; }
};