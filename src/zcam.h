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
#include <QVector2D>
#include <QFont>
#include <QColor>
#include <QRectF>
#include <QtQml/qqmlregistration.h>
#include <nlohmann/json.hpp>

#include "project.h"
#include "recipe.h"
#include "machines.h"
#include "logger.h"
#include "group.h"
#include "macros.h"

class Project;
class Element3d;
class Mop;
class TreeModel;
class GalvoCalibration;
class ScriptEngine;
class Config;

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

      // Multi-selection list (lasso selection).  When non-empty,
      // all elements in this list are highlighted on the 3D canvas.
      // currentElement is the primary selection (first in the list).
      Q_PROPERTY(QList<Element3d*> selectedElements READ selectedElements NOTIFY selectedElementsChanged)
      QList<Element3d*> _selectedElements;

      PROPV(TreeModel*, treeModel, nullptr)
      PROPV(Machines*, machines, nullptr)
      PROPV(Recipe*, recipes, nullptr)
      PROPV(QString, currentTool, QString("pointer"))
      PROPV(GalvoCalibration*, galvoCalibration, nullptr)
      PROPV(ScriptEngine*, scriptEngine, nullptr)

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
      void logPosition(const char* caller);
      // State for magnetic grid snap during element drag.
      // The reference point for each element is (0,0) in local coords.
      //
      // Simple nearest-line algorithm (per axis, independently):
      // the cursor always "owns" a virtual position that follows the
      // mouse unhindered; the element snaps to the grid line nearest
      // to the cursor and sticks to it until the cursor moves more
      // than half the minor spacing away — then the element jumps to
      // the new nearest line.  There is no special "break free"
      // threshold on top of the snap distance, so the element can
      // never lag a full grid cell behind the cursor.
      struct SnapState {
            QVector3D refPos;              ///< world position of the element reference point (0,0 local)
            QVector3D cursorPos;           ///< world position the cursor currently points at (element origin
                                           ///< at drag start, cursorPos + drag delta afterwards)
            bool hasCursorPos {false};     ///< true once cursorPos/refPos/lastSnappedX/Y are seeded
            double lastSnappedX {0.0};     ///< last snapped line position on X (valid when hasCursorPos)
            double lastSnappedY {0.0};     ///< last snapped line position on Y (valid when hasCursorPos)
            bool lastSnapModifier {false}; ///< last Shift state — detects mid-drag modifier change
            };
      SnapState _snapState;

      /// True while an element drag is in progress.  Set once at
      /// startElementDrag() and cleared once at endElementDrag() —
      /// it never toggles in the middle of a drag.
      Q_PROPERTY(bool snapDragActive READ snapDragActive NOTIFY snapDragActiveChanged)
      bool _snapDragActive {false};
      bool snapDragActive() const { return _snapDragActive; }

      /// World position of the element reference point (origin).
      /// Kept in sync in every drag frame by dragged() / scaled().
      Q_PROPERTY(QVector3D snapRefPos READ snapRefPos NOTIFY snapRefPosChanged)
      QVector3D snapRefPos() const;

      //----------------------------------------------------------------
      //   Live 3D view-camera mirror
      //    The QML 3D panel (View3DPanel.qml) continuously pushes the
      //    perpendicular foot point of the canvas camera on z=0 and its
      //    height above z=0 — BOTH in root-local millimetres, computed
      //    exactly like screenToScene()/updateGridViewport() via
      //    cam.mapFromViewport + root.mapPositionFromScene — via
      //    updateViewCamera().  Cam::grabCameraView() consumes these to
      //    align its projection viewCenter / projectionHeight so the
      //    laser projection matches what is shown on the canvas.
      //    These are transient view state, not part of the project file.
      //----------------------------------------------------------------
      Q_PROPERTY(QVector2D viewCameraCenter READ viewCameraCenter NOTIFY viewCameraChanged)
      Q_PROPERTY(double viewCameraHeight READ viewCameraHeight NOTIFY viewCameraChanged)

    public:
      QVector2D viewCameraCenter() const { return _viewCameraCenter; }
      double viewCameraHeight() const { return _viewCameraHeight; }
      /// Called from QML (3D panel) whenever the canvas view changes
      /// (pan / zoom / rotate).  Publishes the camera's perpendicular
      /// foot point on z=0 (cx, cy) and its height above z=0, both in
      /// root-local millimetres.
      Q_INVOKABLE void updateViewCamera(double cx, double cy, double height);
    Q_SIGNALS:
      void viewCameraChanged();

    protected:
      QVector2D _viewCameraCenter {QVector2D(0.0, 0.0)};
      double _viewCameraHeight {1000.0};

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
      void selectedElementsChanged();
      /// Emitted when snapRefPos changes: snap engages / disengages,
      /// the reference point moves during a drag (with or without grid
      /// snap), at drag start, and during pivot-scale.
      void snapRefPosChanged();
      /// Emitted once when a drag with grid snap starts (true) and ends
      /// (false).  Unlike snapRefPosChanged this never toggles mid-drag.
      void snapDragActiveChanged();
      void showFontMediaBrowserRequested();
      void dragPreviewGeometryChanged();
      void remove3dElement(Element3d*); // signal 3d gui to remove an element from the scene graph
      void add3dElement(Element3d*);    // signal 3d gui to add a new element into the scene graph
      void addSubElement(Element3d*,
          Element3d*); // signal 3d gui to add a new subelement into the scene graph
      //      void rootElementChanged(Element3d*);        // signal 3d gui to rebuild scene graph
      void startDragElement(Element3d*); // signal 3d gui to drag this element

      /// Emitted when an element drag/rotate/scale operation ends.
      /// The inspector listens to this to refresh displayed values
      /// that were suppressed during the drag (see InspectorModel::
      /// propertyChangedSlot).
      void elementDragEnded();

      /// Emitted when a brand-new empty project was created.
      void projectCreated();
      /// Emitted after a project file was successfully loaded.
      void projectLoaded(const QString& path);
      /// Emitted after the project was saved.
      void projectSaved(const QString& path);
      /// Emitted after assets (config, machines, recipes) were saved.
      void assetsSaved();
      /// Emitted after the project CAD data was exported to an SVG file.
      void svgExported(const QString& path);

      /// Emitted when the user wants to open the Recipe editor for a
      /// specific recipe (e.g. via the Edit button in the inspector).
      void recipeEditorRequested(const QString& name);

    public:
      explicit ZCam(QObject* parent = nullptr);
      static ZCam* create(QQmlEngine*, QJSEngine*);
      void undoChangeProperty(Element*, const char*, QVariant) {}
    public slots:
      void onMopColorChanged();

    public:
      // ── Project lifecycle (moved from ProjectManager) ───────────────────
      /// Start a fresh, unnamed project.  Returns false if user cancelled.
      Q_INVOKABLE void newProject(bool clearPersistedPath = true);
      Q_INVOKABLE void createTestProject();
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

      /// Clear all tracking pointers (hoverElement, currentElement,
      /// _selectedElements) that reference the given element.  Called
      /// from the Element3d destructor to prevent dangling-pointer
      /// dereferences when elements are deleted (e.g. when
      /// MaterialTest::createChildren() recreates its children).
      void forgetElement(Element3d* el);

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

      /// Add the configured projectsDirectory as a favorite to the
      /// Qt Quick FileDialog sidebar (QSettings: QtProject/qquickfiledialog).
      /// Called once at startup after assets are loaded.
      void setupFileDialogFavorites();

      /// Called from QML when an element is dragged in the 3D viewport.
      /// When the project's Grid has snap enabled, grid lines act
      /// magnetically: the element's reference point (0,0 in local
      /// coords), which follows the cursor, snaps to the nearest grid
      /// line and only jumps to another line once the cursor is closer
      /// to that line (distance > half the minor spacing).
      Q_INVOKABLE void dragged(Element3d* element, const QVector3D& delta, int modifiers);

      /// Called from QML during a drag to re-anchor the grid-snap
      /// reference point to the current cursor position (in root
      /// coordinates).  Must be invoked whenever the canvas camera
      /// pans or rotates mid-drag:  the delta streams from
      /// screenToScene() are only valid as long as the camera is
      /// fixed, so after a camera jump they would corrupt the snap
      /// position without a re-anchor.
      Q_INVOKABLE void updateDragAnchor(Element3d* element, const QVector3D& cursorPos);

      /// Called from QML when an element is rotated in the 3D viewport.
      Q_INVOKABLE void rotated(Element3d* element, const QVector3D& deltaRotation, int modifiers);

      /// Called from QML when an element is scaled in the 3D viewport.
      /// The pivot point (in world/root coordinates) is the center of
      /// the scaling operation — typically the current mouse position.
      /// The element's position is adjusted so that the pivot point
      /// stays fixed in world space, analogous to zooming the canvas.
      Q_INVOKABLE void scaled(
          Element3d* element, const QVector3D& scaleFactor, int modifiers, const QVector3D& pivot);
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
      /// If the currently selected element is among the candidates,
      /// cycling returns the next candidate (parent).  This allows the
      /// user to select a parent element by clicking again on the same
      /// spot where a child was already selected.
      /// Returns nullptr if no element is hit.
      Q_INVOKABLE Element3d* pickElement(double x, double y);

      /// Picking helper used to select an element on click.
      /// If the currently selected element is draggable, has children,
      /// and the drag point lies inside its world bounding box, the
      /// element itself is returned instead of any smaller child
      /// underneath.  This turns the visible selection bounding box
      /// into a drag handle: dragging it moves the whole element
      /// (and therefore all its children).
      /// In all other cases this delegates to pickElement().
      Q_INVOKABLE Element3d* pickDragTarget(double x, double y);

      /// Ray-based picking used by the 3D viewport.  Tests the pick ray
      /// (origin/direction in root coordinates) against the world 3D
      /// bounding boxes of all visible, selectable elements and returns
      /// the element whose box is hit closest to the origin.  Unlike
      /// pickElement() this also works for volumetric elements (BREP)
      /// whose z-offset differs from zero and in tilted views where a
      /// z = 0 plane projection would miss the visible object.
      Q_INVOKABLE Element3d* pickElementAtRay(const QVector3D& origin, const QVector3D& dir);

      /// Dump all ray-pick candidates (hit t, name, world 3D box)
      /// to the application log — diagnosis helper for picking issues.
      Q_INVOKABLE void debugRayPick(const QVector3D& origin, const QVector3D& dir);
      /// Simple log bridge for QML diagnosis output (console.log from
      /// QML does not necessarily reach zcam.log depending on how the
      /// app was started).
      //      Q_INVOKABLE void logLine(const QString& msg) { Debug("qml: {}", msg.toUtf8().constData()); }
      /// Convenience helper for QML: unproject the viewport point
      /// (x, y in pixels) of the given View3D through its camera and
      /// root node and pick with the resulting ray via
      /// pickElementAtRay().  Doing the unprojection in C++ avoids
      /// the QQuick3D coordinate-flip pitfalls of the equivalent QML
      /// expression (mapFromViewport returns y-flipped scene
      /// coordinates, and root.mapPositionFromScene in QML resolves
      /// against the parent Node of View3DPanel).
      Q_INVOKABLE Element3d* pickAt(QObject* view3d, QObject* rootNode, double x, double y);

      /// Lasso selection: given a polygon in world (root) coordinates
      /// (a list of points), select all visible, selectable elements
      /// whose world bounding-box center lies inside the polygon.
      /// Sets _selectedElements and emits selectedElementsChanged().
      /// The first element in the list becomes the currentElement.
      /// If the polygon has fewer than 3 points or no elements are
      /// hit, the selection is cleared.
      Q_INVOKABLE void lassoSelect(const QList<QVector3D>& polygon);

      /// Clear the multi-selection list.
      Q_INVOKABLE void clearSelection();
      /// Clear only the multi-selection list without changing currentElement.
      /// Used when switching to a new single selection to avoid an
      /// intermediate null state that would clear the InspectorModel.
      Q_INVOKABLE void clearSelectionList();
      /// Returns the list of currently selected elements (lasso).
      QList<Element3d*> selectedElements() const { return _selectedElements; }
      /// Returns true if the given element is in the lasso selection.
      Q_INVOKABLE bool isSelected(const Element3d* el) const;

      /// Add an element to the multi-selection list.  The element
      /// becomes the current (primary) element shown in the inspector.
      /// If the element is already selected, it just becomes current.
      Q_INVOKABLE void addToSelection(Element3d* el);
      /// Remove an element from the multi-selection list.  If it was
      /// the current element, the next remaining element (or nullptr)
      /// becomes current.
      Q_INVOKABLE void removeFromSelection(Element3d* el);
      /// Toggle the selection state of an element.  If the element is
      /// not yet selected, it is added and becomes current.  If it is
      /// already selected, it is removed; if it was current, the next
      /// remaining element (or nullptr) becomes current.
      Q_INVOKABLE void toggleSelection(Element3d* el);

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
      /// Returns the Mop* pointer for a given name, or nullptr.
      Q_INVOKABLE Mop* laserLayerPtr(const QString& name) const;

      /// Returns a list of all Recipe names from ZCam::recipes.
      Q_INVOKABLE QStringList recipeNames() const;
      /// Returns the Recipe* pointer for a given recipe name, or nullptr.
      Q_INVOKABLE LaserRecipe* recipePtr(const QString& name) const;
      /// Opens the Recipe editor tab and selects the recipe with the given name.
      /// Emits recipeEditorRequested(name) so the QML layer can switch tabs
      /// and select the recipe in the tree.
      Q_INVOKABLE void openRecipeEditor(const QString& name) { emit recipeEditorRequested(name); }
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

      /// Group the currently selected elements (lasso multi-selection
      /// or single current element) into a new Group element.  The
      /// new Group is created as a sibling of the first selected element
      /// (i.e. a child of that element's parent Layer).  All selected
      /// elements are re-parented into the new Group, preserving their
      /// world-space transforms.  The new Group becomes the current
      /// element.  The operation is undoable as a single macro.
      Q_INVOKABLE void groupSelectedElements();

      /// Combine all selected Polygon elements that share the same
      /// parent (tree level) into a single new Polygon.  The paths
      /// of all selected polygons are transformed to the common
      /// parent's local coordinate space, unioned via Clipper2,
      /// and the result becomes the painterPath of the new Polygon.
      /// The original polygons are deleted.  The new Polygon takes
      /// the place of the first selected polygon.  Only Polygon
      /// elements are considered; other element types are ignored.
      /// The operation is undoable as a single macro.
      Q_INVOKABLE void combineSelectedPolygons();

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
      Q_INVOKABLE void calibrationScan();
      Q_INVOKABLE void createGalvoTest64();
      Q_INVOKABLE void galvotest65img(const QString&);

      void importSvg(const QString& path);

      /// Returns the bounding box (in mm) of the SVG at the given path.
      /// The box reflects the Y-mirrored, px→mm-converted path data.
      /// Returns an empty QRectF if the SVG cannot be parsed.
      Q_INVOKABLE QRectF svgBoundingBox(const QString& path);

      /// Import an SVG file and position the resulting Polygon so that
      /// the bounding box's bottom-left corner is at (x, y) in the
      /// parent layer's local coordinate space.
      Q_INVOKABLE void importSvgAt(const QString& path, double x, double y);

      /// Import an image file (PNG, JPEG, BMP, ...) and position the
      /// resulting ImageElement so the bounding box's bottom-left
      /// corner is at (x, y) in scene coordinates.
      Q_INVOKABLE bool importImageAt(const QString& path, double x, double y);

      /// Compute the bounding box (in mm) of an image file, assuming
      /// the default scale (larger axis = 100 mm).
      Q_INVOKABLE QRectF imageBoundingBox(const QString& path);

      /// Export the project's CAD tree to an SVG file.
      /// The CAD hierarchy (Cad root, Group layers and nested groups)
      /// is mapped to nested SVG <g> elements.  Polygon (incl. cubic
      /// bezier segments), Rectangle, Ellipse and Text elements are
      /// exported; all other element types are skipped.  Coordinates
      /// are written in millimetres with the Y axis flipped to match
      /// the SVG top-left origin.  Returns true on success.
      Q_INVOKABLE bool exportSvg(const QString& path);

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
      QPointer<Element3d> elementDragElement() { return _elementDragElement; }
      };