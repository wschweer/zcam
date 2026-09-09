# NPED — ZCam Project Summary

## Overview
ZCam is a Qt6/QML-based manufacturing tool for G-code CNC machines and fiber laser
engraving. It manages projects with CAD geometry, CAM processing, laser/CNC machine
configurations, recipes, and a 3D viewport.

## Build
```bash
cd build && cmake .. && cmake --build .
```
Or use the Ninja build system (build.ninja is pre-generated).

## Key Architecture
- **Machine** (QObject): Concrete, instantiable class holding shared machine
  properties (travel, precision, etc.) and a pointer to an Engine.  The machine
  type can be changed dynamically by swapping the Engine via changeType().
  No longer virtual.
- **Engine** (QObject): Virtual base class for all machine engine types.
  Encapsulates type-specific behaviour (laser state machine, G-code generation,
  hardware I/O).  Machine owns an Engine pointer and delegates to it.
- **Laser : Engine**: Laser-specific engine with framing/marking state machine,
  background threads, and board communication (initEngine, markLayer, etc.).
- **LaserBJJCZ : Laser**, **LaserRKQ : Laser**: Concrete laser engine implementations.
- **Framing : Element3d** (child of the hidden-by-default **Cam**): the single
  source of truth for the laser framing contour.  `Cam::framing()` finds it
  and `Laser::runFraming()` drives the laser along it.  Three modes selected
  by the `framingType` property (enum `FramingType`:
  `BoundingBox=0`, `ConvexHull=1`, `Rectangle=2`):
  - **BoundingBox** / **ConvexHull** — auto contour derived from the CAM
    burn geometry (`cam->boundingBox()` / `cam->convexHull()`), already in
    project-root (work-field) space; stored in `_pathList`/`_geometry` for the
    canvas (rendered as a non-filled line-strip outline) and non-interactive
    (`hasHandles()`/`draggable()` false).
  - **Rectangle** — a *user-defined, user-editable rectangle* implemented
    exactly like the `Rectangle` element: a `size` (QVector2D) property (the
    inspector's "Size" row), centered at the element origin, with the usual
    `pos`/`rot`/`scale` rows.  It is fully editable on the 3-D canvas like a
    Rectangle: `hasHandles()` true, four corner handles (`isVertex`/
    `vertexPos`/`vertexWorldPos`/`setVertexPos`, same opposite-corner-stays-
    fixed resize convention as `Rectangle::setVertexPos` but **without**
    lockSize) and body-drag via `draggable()` true.  Undo works through the
    generic `HandleDragCommand` (`setVertexPos`).
  - **Canvas visibility**: because Framing is a child of the *hidden* Cam, it
    overrides `ancestorsShow()` → `true` (made `virtual` in `Element3d`) so it
    always renders / is selectable on canvas (QtQuick3D node `visible`
    does not propagate to children in this codebase — that is precisely what
    `ancestorsShow` controls in ProjectTree.qml).  The Fixture/laser-layer
    subtree is unaffected (it still respects the Cam's show flag).
  - **Laser contour** — the laser reads `Framing::worldContour()` (not
    `pathList()`) — the contour in work-field space.  It is recomputed in
    `update()` on the main thread via `projectPathListToXY(this, persp, h, vc)`
    (the same projection the marking paths use, read from the Cam).  For the
    auto types the Framing transform is identity (child of origin-Cam, pos=0)
    so this is a no-op equal to the old `pathList()[0]`; for Rectangle it
    applies the user's pos/rot/scale + camera projection so the laser frames
    exactly the outline shown on canvas.  `refreshCamAndFraming()` calls
    `update()` before each framing run so the background thread sees the
    current contour.
- **Cam display (CamShape.qml)**: `Cam::updateCam()` collects each
  `LaserMop`'s display lines (subset 0 = MarkTo / mark segments,
  subset 1 = MoveTo / **jump** segments) — replicated over the panel
  grid in a `GeometryWorker` background thread — into the Cam's
  `TessGeometry` (one material-mapped subset per half-layer) and
  publishes the per-layer Mop colours via the `layerColors`
  `Q_PROPERTY`.  **CamShape.qml** builds the material list
  dynamically: one `PrincipledMaterial` per layer for the marks
  (the Mop's colour), while **all move/jump subsets share one
  material bound to the configured Config "Move Color"
  (`Config::moveColor`)** — jump paths are NOT drawn in the Mop
  colour, so all travel moves read as a single consistent colour.
  Qt Quick 3D maps materials to geometry subsets positionally, and
  the same material instance may appear multiple times in a
  material list, which makes sharing safe.  The binding to
  `ZCam.config.moveColor` is live, so a Config colour change
  recolours the jumps immediately; an alpha in the configured colour
  is honoured if the user wants dimmed jumps.
- **MachineGCode : Engine**: G-code CNC engine.
- **Machines**: Container that loads/saves machine JSON files.
- **Project**: Top-level element owning CAD, CAM, Fixture, undo stack, and Machine.
  Central rename API `Project::renameElement(element, newName)` (Q_INVOKABLE):
  element names are unique project-wide and managed centrally in the static
  `Element::names` registry.  The new name is sanitized to a valid JS
  identifier and de-duplicated (`foo` → `foo-1`, ...) via the same rules as
  `Element::setName()` (predicted by `Project::uniqueNameFor()`); the change
  is recorded as an undoable `RenameElementCommand`, and the actual resulting
  name is returned.  `changeProperty(el, "name", ...)` routes through it, so
  the tree-view inline editor, QML, the AI agent and the stdin remote control
  all share one code path.
- **Config : Element**: Application-wide configuration (GUI, colors, paths,
  SpaceMouse, DXF import settings).  Derives from Element so its properties
  can be made scriptable via the ScriptEngine.  Registered in the JS namespace
  as `config` (a top-level sibling of `project`) so scripts can reference
  config properties like `config.machinesDirectory`.  Script bindings are
  persisted in assets.json alongside the config properties.
- **ConfigModel**: QAbstractListModel exposing Config properties to QML,
  analogous to MachineModel.  Implements the full scripting Q_INVOKABLE API
  (isScriptBound, setScript, scriptFor, scriptError, setScriptActive,
  isScriptActive, boundComponents, testScript, testScriptWithContext,
  removeScript, elementProperty) so the PropertyEditor f(x) button works
  for scriptable Config properties (e.g. path-type properties marked with
  `"scriptable": true`).
- **InspectorModel / MachineModel / RecipeModel**: QAbstractListModel exposing properties to QML.
  RecipeModel wraps a LaserRecipe (set via `recipe` property) and uses the
  `LaserRecipe::properties()` JSON to build the inspector GUI dynamically,
  analogous to MachineModel wrapping a Machine and LayerSettingModel wrapping a
  LaserPass.  Used in ConfigRecipes.qml to replace the hard-coded recipe detail
  fields (name, description, numPasses) with a PropertyEditor-driven layout.
  LaserRecipe is a QObject (non-copyable) stored in
  `std::vector<std::unique_ptr<LaserRecipe>>` inside Recipe; RecipeModel takes a
  raw `LaserRecipe*` pointer and reads/writes properties via the Qt meta-object
  system.
- **Property JSON**: Each class defines a `properties()` JSON string describing
  the GUI layout (rows/cells format).  Each cell can include a `"tooltip"` string
  field; when present, the PropertyEditor shows a QML ToolTip on hover over the
  property label (top-level) or ValueBox (sub-delegate).  The C++ helper
  `propjson::tooltipForName()` retrieves the tooltip for a given property name.
- **CameraElement : Element3d**: Manages an attached Linux webcam (V4L2/Qt
  Multimedia). The camera device is selected from the list of available inputs
  (combobox in the inspector, `cameraName` type), the live image is shown in the
  inspector (`cameraView` type, zoomable/pannable) and drives the camera overlay
  in the XY plane (pos/rot/overlaySize + trapezX/trapezY keystone correction via
  CameraOverlayGeometry). CameraTextureData binds to the element for the 3D
  overlay texture; visibility gates camera capture.
- **ImageElement : Element3d**: Displays a pixel-based image (PNG, JPEG, BMP,
  GIF, TIFF, WEBP) on the 3D canvas as a textured quad in the XY plane.
  ImagePlaneGeometry provides a unit quad with UVs; ImageTextureData loads the
  image file and uploads it as an RGBA8 texture. The element's scale represents
  the physical size in mm; on import the larger axis defaults to 100 mm with
  aspect-ratio preserved (lockScale = Lock). The filePath property stores the
  source path; the image is reloaded from disk on project load. Import via
  `ZCam::importFile()`, drag-drop on the 3D canvas, or `importImageAt()` for
  positioned placement.
- **Nest : Group**: A nesting container that packs its child Element3d
  elements into a rectangular bin using **libnest2d** (system-installed
  `libnest2d-dev` with the Clipper backend and NLopt optimizer).  The bin
  size (`binSize` property, QVector2D) defaults to the current machine's
  `maxTravel` (X, Y) on construction via `initBinFromMachine()`.  Nesting
  properties: `spacing` (minimum distance between packed items, mm),
  `allowRotation` (permit 90° rotations during packing), `arrange` (auto-
  arrange on add — reserved for future use).  The `Q_INVOKABLE bool nest()`
  method collects each child's fill-path polygon (child-local coordinates,
  normalised so its bounding box starts at (0,0)), converts to integer
  Clipper coordinates (scale 1 mm = 1 000 000 units), runs
  `libnest2d::nest()` with `BottomLeftPlacer` + `FirstFitSelection`, and
  applies the resulting translation and rotation to each child's `pos`
  and `rot` properties through `Project::changeProperty()` (undoable as a
  single macro).  Returns `false` when nothing could be packed (no child
  geometry, no valid items, nothing fits into the bin) and emits
  `ZCam::nestFinished(packed, total)` in the success case so the QML
  status bar can report the result ("Nest: N of M item(s) packed").  The
  bin outline is always visible as a rectangle drawn via the selection
  geometry.  **Bin resize handles**: when the Nest is the current
  selection the 3D viewport shows the standard four corner handles
  (orange #Sphere models, same vertex-handle machinery as Rectangle /
  Ellipse — `Nest` overrides `hasHandles/vertexCount/isVertex/vertexPos/
  vertexWorldPos/setVertexPos`; no QML changes needed because those are
  virtual Q_INVOKABLEs on Element3d).  Dragging a corner resizes the
  bin: the OPPOSITE corner stays fixed in world space and both
  `binSize` and `pos` are updated (`Nest::setVertexPos`, local coords
  via the inverse globalMatrix, same convention as
  `Rectangle::setVertexPos`; corner indices 0=BL (0,0), 1=BR (w,0),
  2=TR (w,h), 3=TL (0,h), matching the selection-geometry rectangle).
  Because a bin drag changes TWO properties, `ZCam::startVertexDrag/
  endVertexDrag` snapshot the binSize/pos pair at drag start and push a
  dedicated `NestBinCommand` (undo.h / project.cpp) which restores both
  values on undo/redo — the generic positional `HandleDragCommand`
  would not reconstruct the old state.  `Nest::set_binSize()` bumps the
  vertex revision so the handles follow inspector-driven bin-size
  edits as well.  Created from the Cad context menu ("Add Nest") in
  TreeViewPanel.qml or via `Project::addNest()` Q_INVOKABLE.  Also
  available via `zcam.createElement("nest", ...)` in the ScriptApi and
  the AI agent `create_element` tool.  Serialized as `"nest"` in the
  project file JSON (registered in `Element::fromJson`).
- **QML method-call pitfall / ZCam::invokeElementMethod()**: `ZCam.`
  `currentElement` and friends are exposed to QML/JS with their STATIC
  type (`Element3d*`).  The JS wrapper therefore only shows methods of
  the static class — methods declared in derived classes come through
  as `undefined` (verified: `typeof el.nest === "undefined"` on a Nest,
  `typeof el.optimize === "undefined"` on a Polygon).  A guarded call
  like `if (el && el.nest) el.nest();` silently does NOTHING — this was
  the reason "Run Nest" (and "Optimize" in the polygon menu) appeared
  broken while the underlying C++ worked.  The fix is the generic
  `ZCam.invokeElementMethod(element, method [, args])` Q_INVOKABLE,
  which dispatches by method name on the element's DYNAMIC
  meta-object (with the signature auto-normalised to `"method()"`),
  converts arguments to the parameters' meta types and invokes via
  `QMetaMethod::invoke`.  Returns `false` (and logs a Warning) when the
  method is missing or the invocation fails.  QML follows this pattern
  for Nest (`nestMenu` → `invokeElementMethod(el, "nest")`), Polygon
  (`polygonMenu` → `invokeElementMethod(el, "optimize")`); script access
  is available as `zcam.invokeElementMethod(name, method)` in the
  ScriptApi (which delegates to the ZCam singleton).  When writing new
  QML handlers that call subclass-specific Q_INVOKABLE methods on
  `ZCam.currentElement`, always route them through this invoker.
- **Polygon bezier drawing**: While drawing a polygon with the polygon tool,
  cubic bezier segments can be inserted between two anchor points in two ways:
  - **Ctrl+click** — one-off bezier segment (mode not toggled).
  - **'b' key** — persistent bezier mode; each subsequent click places a
    bezier segment until 'b' is pressed again or the drawing ends.
  `Polygon::continueDrawingBezier(p)` rewrites the pending segment as
  CurveTo (c1) + CurveToData1 (c2) + CurveToData2 (endPoint), with control
  points seeded at ⅓/⅔ of
  the chord).  The control points appear as draggable vertex handles via
  the existing `isControlPoint()`/`vertexPos()` machinery, and serialize
  to SVG (`c` command) and JSON.  The lasso (Ctrl+drag) is suppressed
  while a polygon is being drawn so Ctrl+click is unambiguous.
  **PainterPath Bezier convention**: CurveTo = c1 (first control point),
  CurveToData1 = c2 (second control point), CurveToData2 = endPoint.
  This matches `cubicTo()`, `makeSpline()`, SVG export (`C c1 c2 end`)
  and `toPathList()` (`Bezier::fromPoints(start, c1, c2, end)`).
  **Segment editing**: When a polygon line segment is selected (not
  drawing), pressing 'b' converts the line to a cubic bezier
  (`convertSelectedSegmentToBezier()`), and 'p' splits the line at its
  midpoint by inserting a new vertex (`splitSelectedSegment()`).  Both
  operations are undoable via `PolygonPathCommand`.
  **Bezier helper lines**: The dashed S→C1 / E→C2 association lines
  (connecting a bezier segment's anchors to its control points, drawn in red
  by Shape.qml's controlHandleModel) mirror the vertex-handle visibility
  logic in `Polygon::isVertex()` exactly: hidden while drawing; shown for
  every bezier segment when the polygon is selected as a whole (no segment
  selected); only for the selected segment(s) in segment mode.  They are
  rebuilt by `Polygon::updateControlHandles()` on every geometry change and
  on every segment-selection change.
  **Lasso segment selection**: A lasso drag (Ctrl+drag) also selects
  individual line/bezier segments of polygons.  `ZCam::lassoSelect()`
  walks the whole element tree (`collectLassoSegments()`); for every
  visible, non-drawing Polygon it selects all segments whose world-space
  midpoint (`segmentMidpoint()`) lies inside the lasso polygon (independent
  of whether the polygon's bounding-box center was selected).  Segment
  selection state is stored per-polygon as a focused segment
  (`_selectedSegment`, always the first lasso-selected one so keyboard
  editing has a deterministic target) plus a `_lassoSelectedSegments` list;
  `selectedSegmentIndices()`/`hasSelectedSegment()` unify both.  The
  first polygon with a selected segment becomes the current element.  A new
  lasso drag fully replaces the previous segment state (polygons outside the
  lasso get cleared).  `convertSelectedSegmentToBezier()` and
  `splitSelectedSegment()` act on every selected segment at once (processed
  in descending index order so index shifts stay valid), then clear the
  selection.
  **Polygon optimize()**: `Polygon::optimize()` (Q_INVOKABLE) simplifies the
  outline with an exact, single right-to-left pass over the logical segments:
  (1) a cubic-bezier segment whose two control points both lie on the chord
  (start→end) actually draws a straight line and is replaced by a single
  LineTo (the three CurveTo/CurveToData1/CurveToData2 elements collapse to one
  LineTo); (2) two consecutive straight segments A→B→C whose three anchors
  are collinear are merged into one segment A→C, removing the redundant
  middle vertex B.  Because it only ever combines a segment with the one to
  its right (already reduced) it runs right-to-left and a whole run of
  collinear vertices folds down to a single segment (A→B→C→D ⇒ A→D).  The
  collinearity test is scale-invariant (perpendicular deviation < 1e-5 of the
  chord, and a degenerate chord never counts) so reductions are exact — only
  geometry that already renders as a straight line is simplified.  If nothing
  changes it is a no-op (undo stack untouched, project not dirtied); otherwise
  the change is one undoable `PolygonPathCommand` and any stale segment
  selection is cleared.
  Exposed to the user as the **"Optimize"** entry of the dedicated polygon
  context menu (right-click a polygon in the Project Tree — `polygonMenu` in
  `TreeViewPanel.qml`; distinct from `shapeMenu` which text/ellipse/rectangle
  use).  Because it is `Q_INVOKABLE` it is also reachable via the AI agent /
  stdin remote control (`invoke_method {element, method:"optimize"}`).
- **ScriptEngine (QJSEngine singleton)**: JavaScript property bindings
  ("Scripting", siehe TODO.md).  Jede benannte Element-Instanz ist im
  JavaScript-Namespace `project.<pfad>.<name>` sichtbar (Namen werden in
  Element::setName zu gültigen JS-Identifiern sanitisiert).  PropertyBinding
  evaluiert ein Script und schreibt das Ergebnis in ein Skalar- oder
  Vektor-Komponenten-Property; statische Dependency-Analyse (Identifier-Scan)
  verdrahtet NOTIFY-Signale aller referenzierten Elemente über
  QMetaObject::connect(signalIdx → slotIdx) zur Re-Evaluierung.  Da Qt's
  QObject-Wrapper für QVector2D/3D keine Komponenten-Properties anbietet,
  werden Vektor-Properties vor jeder Evaluierung als {x,y,z}-Snapshots in
  den Namespace geschrieben (refreshVectorSnapshots).  Scripts werden auf dem
  Element persistiert (script/scriptProp/scriptComp) und im Projektfile
  serialisiert.  Inspector (PropertyEditor.qml) zeigt pro numerischem
  Property einen f(x)-Button (bound-expression*.svg) mit Popup
  (Editor, Live-Auswertung, Fehleranzeige, Active-Checkbox); gebundene
  Properties sind im GUI readonly/grau.
  **Default-Scripts**: Das properties()-JSON kann pro Cell ein `"script"`-Feld
  enthalten, das ein Default-Script definiert (analog zum `"default"`-Wert).
  Ein Element mit einem Default-Script ist automatisch scriptable (der f(x)-Button
  wird angezeigt) und das Default-Script wird beim Erstellen des Elements
  (addChild) bzw. beim Laden eines Projekts (rebuildRegistry) als aktives
  Binding registriert, sofern kein manuell gespeichertes Script für diese
  Property existiert.  Siehe `propjson::allDefaultScripts()` und
  `ScriptEngine::applyDefaultScripts()`.
- **Imperative Scripting (Scripting II)**: In addition to reactive property
  bindings, the ScriptEngine exposes two global JavaScript objects for
  imperative multi-statement scripts (loops, conditions, functions):
  **`zcam`** (ScriptApi, ~35 Q_INVOKABLE methods) — element CRUD
  (createElement, deleteElement, renameElement, moveElement), property
  access (getProperty, setProperty — undoable), element queries
  (listElements, findElement, findByName (alias), selectElement, currentElement), geometry
  queries (worldBoundingBox, vertexWorldPos, vertexCount, containsWorldPoint,
  isInside), boolean geometry ops (unionPolygons, differencePolygons,
  intersectPolygons, offsetPolygon — create new Polygon from Clipper2
  result), vertex manipulation (addVertex, addBezier, setVertexPos,
  optimizePolygon), transforms (setPos, setRot, setScale), app control
  (newProject, saveProject, undo, redo, beginBatch, endBatch, refreshCam,
  exportSvg, importFile, importSvgAt, screenshot), machine/laser
  (machineName, setMachine, startFraming, startMarking, stopLaser), and
  layer/fixture management (addLayer, addFixture, addLaserMop, setMops).
  **`geom`** (GeometryApi, ~25 Q_INVOKABLE methods) — pure math/geometry
  helpers operating on plain [x,y] arrays: vector helpers (vec2, vec3,
  distance, angle, midpoint, rotate, scale, normalize), path construction
  (regularPolygon, starPolygon, rectPath, circlePath, arcPath), boolean ops
  on path data (unionPaths, differencePaths, intersectPaths, offsetPaths),
  geometric queries (pathArea, pathPerimeter, pointInPath, pathBoundingBox,
  simplifyPath), path transforms (translatePath, rotatePath, scalePath,
  mirrorPath), and array/grid helpers (gridPositions, circularPositions,
  linePositions).  Path convention: a path is a JS array of [x,y] pairs.
  Evaluated via `ScriptEngine::evalImperative()` with QTimer-based timeout
  (QJSEngine::setInterrupted, default 10 s).  The script is wrapped in an
  IIFE (`(function(){ ... })();`) so that top-level `return` statements
  (common in AI-generated scripts) are valid — without the wrapper
  QJSEngine treats the script as a Program production where `return` is a
  SyntaxError.  Accessible from the
  **Script Console** (ScriptPanel.qml, toggled by the amber "JS" toolbar
  button), the **AI agent** (`run_script` tool), and the **stdin remote
  control** (`run_script` command).
  **print()**: Scripts can emit console output with the global JavaScript
  function `print(msg1, msg2, ...)`.  `ScriptEngine::ensurePrintGlobal()`
  (called from the constructor) registers a JS `print()` wrapper on the
  engine's global object that stringifies every argument (`JSON.stringify`
  with 2-space indent for objects/arrays, `String()` otherwise, `null`/
  `undefined` as literals), joins them with a single space, and forwards the
  result to the C++ `ScriptEngine::print(const QString&)` method.  That
  method writes the line to the app log and emits the `scriptPrinted(const
  QString&)` signal; the Script Console (ScriptPanel.qml) connects to that
  signal via `Connections` and appends the text to its output area (which is
  console-style: output accumulates across runs, and the view auto-scrolls
  to the newest line).  `print()` works in the Script
  Console, the AI agent and the stdin remote control alike because it is
  pure ScriptEngine state.
  **Named scripts (Script Console)**: The Script Console (ScriptPanel.qml,
  toggled by the amber "JS" toolbar button) is organised like the AI
  sessions: scripts are persisted one-per-file in
  `~/ZCam/scripts/Script-yy-MM-dd-n.js` (mirroring the AI sessions in
  `~/ZCam/ai_sessions/Session-yy-MM-dd-n.json`).  Script names are
  auto-generated like sessions (date + counter) with the "Script-" prefix
  to distinguish them from AI sessions.  The available scripts are shown
  in a **ComboBox** bound to the `ScriptEngine` Q_PROPERTYs `scriptList`
  and `currentScript` (same pattern as AiPanel.qml's session selector).
  Picking an entry loads that script into the editor.  A **"+"** button
  creates a new (empty) script with an auto-generated name and selects it
  immediately; a **"−"** button deletes the current one.  Running stays
  available via **Ctrl+Return** (consistent with the AI panel).  Editor
  content is auto-saved (debounced ~800 ms).  The C++ API on
  `ScriptEngine`: `scriptsDirectory()`, `scriptNames()`, `scriptText(name)`,
  `saveScript(name, content)`, `newScript(name = "")` (auto-generates name
  when empty, returns display name, selects it as current), `selectScript(index)`,
  `deleteScriptByIndex(index)`, `deleteScript(name)`, `renameScript(oldName, newName)`
  (returns display name), plus the static helpers `sanitizeScriptName()`
  (safe file-system name: letters/digits/'-'/'_'/'.'; other chars → single '-';
  fallback "script") and `uniqueScriptName(base, taken)` (appends "-1", "-2",
  ... on collision).  Q_PROPERTYs: `scriptList` (QStringList, NOTIFY
  `scriptListChanged`), `currentScript` (int, NOTIFY `currentScriptChanged`),
  `currentScriptName` (QString, NOTIFY `currentScriptNameChanged`).
  **Element children in scripts**: `Element::children` is a Q_PROPERTY
  (returns a `QList<Element*>`), so in JavaScript it is an array, not a
  callable function — calling `element.children()` fails with
  "Property 'children' is not a function".  Use the `Q_INVOKABLE
  childElements()` method instead: `element.childElements()` returns the
  same list and is callable from JS.  In practice, iterating
  `element.children` (without parentheses) also works for reading, but
  `childElements()` is the canonical callable form.
- **Selftest**: `build/zcam --script-test` lädt /tmp/script-test.zcam,
  prüft Laden/Evaluieren, Dependency-Reaktion, Laufzeit-Bindings,
  Serialisierung und Binding-Remove.
- **GalvoCalibration (QObject)**: Computes galvo correction values
  (galvoScale, galvoBulge, galvoOffset) from 12 measured line lengths of the
  "Galvo Test 9" burn pattern (materialtest.cpp).  The pipeline per axis:
  1) beam-offset estimate from the left/right pair asymmetry, 2) the measured
  pairs are centred for the offset cross-terms, 3) fitAxis() fits the
  machine's physical response phys(g,G) = A*g - K*(g²+G²)*g (gain and lens
  bulge) from the three centred pair averages, and 4) solveCorrection()
  inverts the actual marking path (mapToGalvo counts → writeCorrectionTable)
  with a small Newton iteration to obtain the galvoScale compensation factor
  and the galvoBulge correction-table coefficient so the corrected pattern
  measures the nominal field size.  galvoScale is a compensation factor
  (> 1 for an under-travelling machine); galvoBulge is in correction-table
  units.  galvoBulge4 is not computed from the 9-point pattern (set to 0).
  IMPORTANT GEOMETRY: the marking path maps the field onto ±25800 counts
  (not the full ±32767), so the field edge lands at correction-table grid
  ±25.195, not ±32 — the calibration uses MEASUREMENT_GRID=25800/1024.
- **ManualPanel (QML)**: Integrated manual viewer. Displays MkDocs-generated
  HTML inside a `WebEngineView`. The HTML is built by CMake from `manual/docs/*.md`
  via `mkdocs build` at configure time, then embedded as a Qt resource
  (`qrc:/manual/`). German is the primary language, English is an automatically
  translated variant. Language switching is done via buttons in the panel.
  Requires `Qt6::WebEngineQuick` (initialised in `main.cpp`).
- **ZCamFileDialog (QML)**: Custom Material-dark file picker that replaces
  Qt's platform-native `FileDialog`. On KDE Plasma the native dialog comes
  from the xdg-desktop-portal and its sidebar (folders like "Benutzerverzeichnis",
  "Desktop", "Downloads") renders dark-on-dark. ZCamFileDialog always uses the
  dark theme regardless of the desktop environment; it provides a sidebar
  (Home/Desktop/Documents/Downloads), a FolderListModel file list, filter
  ComboBox and a path editor. It exposes the same API as Qt's FileDialog
  (title, fileMode, nameFilters, defaultSuffix, selectedFile, accepted/rejected)
  and is used throughout Main.qml and GalvoCalibrationDialog.qml.
  The configured `projectsDirectory` (Config property) is automatically
  added as a sidebar favorite entry via `ZCam::setupFileDialogFavorites()`
  which writes to `QSettings("QtProject", "qquickfiledialog")` using the
  `beginWriteArray("favorites")` format expected by Qt's
  `QQuickSideBarPrivate::readSettings()`.  The favorite appears at the
  top of the sidebar in all file dialogs.  In addition to the projects
  directory, the parent directory of the currently open project is added
  as a favorite ("Current") so the user can quickly navigate to where
  the active project lives.  `setupFileDialogFavorites()` is called at
  startup, after `openProject()`, after `saveAs()`, and after
  `newProject()` so the sidebar always reflects the current state.
  The Open Project dialog also sets `currentFolder` to the
  projectsDirectory so it opens there by default.

## Command-Line File Argument

ZCam can be started with a file path as the first non-option command-line
argument:
- **`.zcam` project file** → the project is opened directly (replaces
  the "restore last project" startup path).
- **Importable file** (SVG, DXF, DWG, BREP, IPC-2581 XML, image: PNG/JPEG/
  BMP/GIF/TIFF/WEBP) → a fresh empty project is created (by the ZCam
  constructor) and the file is imported into it via `ZCam::importFile()`.
- **No argument** → normal startup (`restoreLastProject()`).

Implementation: `main()` scans `app.arguments()` for the first non-option
argument that is an existing file and stores it via
`ZCam::setStartupFilePath()` (static).  The QML startup timer
(`Main.qml`, `restoreTimer`) calls `ZCam::handleStartupFile()` instead of
`restoreLastProject()`; this method dispatches based on the file suffix.

## File Imports
- **SVG / DXF / BREP**: `ZCam::importFile()` dispatches by suffix to the
  respective importers (`svg.cpp`, `dxfimport.cpp`, `brepimport.cpp`).
  After building the element tree (and flushing buffered lines), `DxfImport::import()`
  recursively calls `Polygon::optimize()` on every imported Polygon
  (`optimizeAllPolygons()`) so redundant geometry is simplified on import:
  degenerate bezier segments (both control points on the chord) collapse to a
  straight line, and consecutive collinear vertices (A→B→C) are merged into a
  single segment (A→C). `optimize()` is a no-op for already-clean polygons.
  **DXF colour-based classification**: The DXF importer reads each entity's
  ACI colour (AutoCAD Color Index, DXF group code 62) and resolves BYLAYER
  (256) and BYBLOCK (0) against the LAYER table.  Entities of different
  colours are grouped into separate colour sub-Groups (named after the ACI
  label, e.g. "Cyan", "White") inside the DXF layer Group, so cut and mark
  contours are visually and structurally separated.  Each imported Element3d
  (Polygon, Ellipse, Text) has its `color` property set to the resolved ACI
  colour via `aciToQColor()`.  Buffered LINE entities are chained only within
  the same (layer, colour) group so polylines never mix colours.  When
  multiple colours are present, one `LaserMop` per colour is created and
  linked to the corresponding sub-group; sensible defaults are applied
  (ACI 4/Cyan → Cut with kerfOffset=-0.05, ACI 7/White → Mark with
  kerfOffset=0).  Block entities (`BlockEntity`) carry the colour too, and
  `expandBlockEntity()` applies it to expanded elements and targets the
  correct colour sub-group.
- **Images (PNG, JPEG, BMP, GIF, TIFF, WEBP)**: `ImageImport::import()` /
  `ImageImport::importAt()` create an `ImageElement` in the CAD tree.
  Dispatched by suffix from `ZCam::importFile()` and drag-drop on the 3D canvas.
- **IPC-2581 (revision C)**: `importipc2581.cpp` parses the PCB "digital twin"
  XML. All layers become Groups with Polygon children (arcs/primitives are
  flattened) below a new import layer; negative polarity and Cutout geometry is
  imported as red filled polygons. A linked Recipe (laser layer) is added to the
  active fixture. Sniffed by root element so plain `.xml` imports are routed
  correctly (see `ImportIpc2581::isIpc2581File`).

## AI Integration
- **AIAgent (QObject, QML_SINGLETON)**: LLM-driven agent that lets a
  language model control the ZCam application through a set of JSON
  "tools".  Talks to an Ollama server over HTTP (`/api/chat` streaming).
  Exposed to QML as the `AIAgent` singleton.
  - **Tools**: new_project, save_project, start_session, end_session, undo,
    redo, create_element, delete_element, rename_element, move_element, read_property,
    write_property, list_properties, describe_property, invoke_method,
    list_methods, list_elements, screenshot,
    get_current_element, select_element, get_selected_elements,
    select_elements, clear_selection, set_mops.
  - **Element renaming**: `Project::renameElement(element, newName)` is the
    central Q_INVOKABLE rename API.  Element names are unique project-wide
    and managed centrally in the static `Element::names` registry;
    `Element::setName()` sanitizes names to valid JS identifiers and de-
    duplicates them (`foo`, `foo-1`, ...).  `renameElement()` predicts the
    resulting name via `Project::uniqueNameFor()` (same rules, no side
    effects), no-ops when the resolved name equals the current one, and
    otherwise records the change as an undoable `RenameElementCommand`
    (undo/redo restores the exact pre-rename name).  `Project::changeProperty(
    el, "name", ...)` also routes through it.  Reachable from QML, the AI
    agent (`rename_element` tool) and the stdin remote control.
  - **Selection access**: the `get_current_element` and `get_selected_elements`
    tools expose `ZCam::currentElement` and `ZCam::selectedElements` to the
    AI agent and the stdin remote control.  `select_element` sets the
    primary selection by name; `select_elements` replaces the multi-selection
    with a list of named elements; `clear_selection` deselects everything.
    The `status` command also reports `selectedElements`.
  - **Screenshot (vision)**: the `screenshot` tool captures the 3-D canvas
    exactly as displayed.  `ZCam::grabCanvas()` grabs the real GPU-rendered
    pixels of the registered canvas `View3D` (see `setCanvasItem()` in
    View3DPanel.qml) out of the window; `ZCam::saveCanvasScreenshot()`
    writes a full-resolution PNG to `~/ZCam/screenshots/`.  The tool embeds a
    down-scaled (default 1280 px) base64 PNG as an `images` array on the
    Ollama tool-result message so a multimodal model can actually *see* the
    canvas.  Parameters: `inline` (also return a data-URL), `max_width`,
    `with_image` (omit the base64 payload entirely — used by the remote
    control so stdout stays small).
  - **Stdin remote control**: every tool above is also reachable from another
    program over stdin (see `## Stdin Remote Control` and
    `src/remotecontrol.{h,cpp}`), for headless test/automation use.
    For `screenshot` the base64 payload is suppressed by default — the
    response carries the saved PNG `file` path and dimensions; pass
    `{"with_image":true}` to get the base64 inline.
  - **Tool schema**: built via `MCPToolBuilder` in OpenAI/Ollama format
    ({type, function{name, description, parameters}}).
  - **Session management**: each conversation is automatically saved as a
    JSON file in `~/ZCam/ai_sessions/`.  Sessions can be listed, loaded,
    and deleted from the QML panel.
  - **Config**: Ollama model, base URL, temperature, and context size are
    configurable via Config properties (`ollamaModel`, `ollamaBaseUrl`,
    `aiTemperature`, `aiContextSize`) in the "AI" category.
  - **Context-window / tool-result limits**: Tool results are capped at
    8000 chars (`truncateToolResult()`, ~2300 tokens) before being appended
    to the session history.  Ollama front-truncates (drops the oldest
    messages first) when the prompt exceeds `num_ctx`; if the original user
    message is the one dropped, the Qwen3.8 renderer's `validateMessages()`
    rejects the request with HTTP 500 "no user query found in messages"
    (shown in the GUI as "internal server error").  Keeping tool results
    under ~50 % of the context window avoids this.  Network failures are
    logged to zcam.log (request URL, error string, HTTP status, first 1 KB
    of the response body) in addition to being emitted to the UI via
    `agentError()`.
- **AiPanel.qml**: QML panel with a chat interface — upper half shows the
  AI output / conversation log (scrollable TextArea), lower half has a
  user input field with send/stop button.  A toolbar provides session
  management (new, select, delete) and shows the current model.
  Shown as a **right-hand side panel** in Main.qml (an extra `SplitView`
  item between the StackLayout and the LaserPanel, `SplitView.minimumWidth:
  300` / `maximumWidth: 500`).  Its visibility is toggled by a dedicated
  **"AI" toolbar button** (`aiPanelBtn`, a checkable `ToolButton` styled like
  the Media Browser "M" button — teal `Shade700` background when active so the
  checked state is clearly visible).  The button's checked state is persisted
  in the window `settings` via `settings.aiPanelVisible` / the checkable
  `actionShowAiPanel`.  It is NOT a tab of the `TabBar`/`StackLayout`.

  The `TabBar` buttons are radio buttons; the "AI" and "M" (Media) toolbar
  buttons are toggle buttons.  All other `TabBtn` entries behave as radio
  (only one active at a time, the StackLayout `currentIndex` follows
  `tabBar.currentIndex`).
  The AI panel is resizable (drag the splitter): `SplitView.minimumWidth: 300`,
  `preferredWidth: 420`, `maximumWidth: 800`.
  The user prompt is a **multi-line `TextArea`** (minimum height = 4 lines,
  grows with content up to `maximumHeight: 280`).  The prompt is submitted
  with **Ctrl+Return** (a `send()` helper on the panel; there is **no**
  persistent Send button).  A small **Stop** button (orange) appears only
  while the agent is busy so a running request can still be cancelled;
  otherwise a dimmed "Ctrl+Return to send" hint is shown in its place.

## Stdin Remote Control (test / automation)

The app can be driven from another program over **stdin**, without touching
the GUI.  This is a *line-based, request/response* protocol that re-uses the
exact same tool interface the AI agent uses (`AIAgent`), so every AI tool is
also a remote-control command.  It is wired up unconditionally in `main.cpp`
(`RemoteControl`, see `src/remotecontrol.{h,cpp}`) and is inert when stdin is
a normal terminal — it only acts when lines are piped into the process.

### How it works
- The parent process launches the app normally and keeps its stdin pipe open:
  ```bash
  zcam > out.jsonl 2> app.log &
  ```
- It then writes **one command per line** to the app's stdin (fd 0) and reads
  **one JSON line per command** from its stdout (fd 1).  Every response is
  flushed immediately, so it is safe to do this synchronously (write a line,
  read a line, repeat).
- All logging/diagnostics go to **stderr** (`app.log`) and a log file — they
  never pollute the stdout protocol channel.

### Command grammar
```
<tool> [arguments-json]
```
- `<tool>` is any `AIAgent` tool name (see below).
- `arguments-json` is an optional **single-line JSON object** with the tool's
  parameters.  Omit it entirely for tools that take no arguments.
- Whitespace, CRLF and leading/trailing spaces are tolerated.

### Special commands
| command            | effect |
|--------------------|--------|
| `help` / `tools`   | list every tool with its description + parameter schema (start here!) |
| `status`           | app state: hasProject, projectPath, projectName, currentElement, aiAgentActive |
| `quit` / `exit`    | close the app (returns `{"ok":true,"cmd":"quit"}`) |

### Response format
One compact JSON object per command on stdout:
```json
{ "ok": true,  "cmd": "write_property", ...tool result fields... }
{ "ok": false, "cmd": "foo", "error": "what went wrong" }
```
`ok` is always present; the remaining fields are whatever the tool returned
(e.g. `name`, `value`, `elements`, `properties`, ...).

### Available tools (the full command set)
| command            | args (JSON object) |
|--------------------|--------------------|
| `new_project`      | — |
| `save_project`     | — |
| `start_session`    | —  (began undo macro; pair with end_session) |
| `end_session`      | — |
| `undo`             | — |
| `redo`             | — |
| `create_element`   | `{type, x?, y?, name?, parent?}`  type ∈ rectangle\|polygon\|ellipse\|text\|group\|nest |
| `delete_element`   | `{name}` |
| `rename_element`   | `{name, new_name}`  — name must be unique; central registry de-duplicates (`box-1`), actual name returned |
| `move_element`     | `{name, new_parent, new_row?}` |
| `read_property`    | `{element, property}` |
| `write_property`   | `{element, property, value}`  value as string / JSON array |
| `list_properties`  | `{element, include_values?}` |
| `describe_property`| `{element, property}` |
| `invoke_method`    | `{element, method, arguments?}`  arguments = JSON array/object string |
| `list_methods`     | `{element}` |
| `list_elements`    | `{max_depth?}` |
| `screenshot`       | `{inline?, max_width?, with_image?}`  — base64 `image`/`dataUrl` suppressed by default over stdin; the saved PNG `file` path is always returned |
| `get_current_element` | — |
| `select_element`   | `{name?}`  name omitted/empty = deselect |
| `get_selected_elements` | — |
| `select_elements`  | `{names: ["r1","poly2",...]}`  empty array = clear |
| `clear_selection`   | — |
| `set_mops`         | `{element, mops_name?}`  assign a laser MOP (laser layer) to an element; empty mops_name = clear |

### Example session
```bash
$ zcam > out.jsonl 2> app.log &
$ PID=$!; exec 3>&1                      # keep a handle to the pipe if needed
$ printf 'create_element {"type":"rectangle","x":10,"y":10,"name":"box"}\n' >&0
$ printf 'write_property {"element":"box","property":"lineWidth","value":"1.5"}\n' >&0
$ cat out.jsonl
{"cmd":"create_element","name":"box","ok":true,"type":"rectangle"}
{"cmd":"write_property","element":"box","newValue":1.5,"oldValue":0.5,"ok":true,"property":"lineWidth"}
$ printf 'list_elements\n' >&0; printf 'quit\n' >&0
# ... then `wait $PID`
```

### Minimal driver sketch (bash, one command at a time)
```bash
send() {                      # send one command line, wait for one response line
      local f="$1"            # fd to the app's stdin
      printf '%s\n' "$2" >"/dev/fd/$f"
      }
# Open a bidirectional pipe once (e.g. with coproc or two FIFOs), then:
#   send 3 'help'
#   send 3 'status'
#   send 3 'create_element {"type":"rectangle","name":"r1"}'
#   send 3 'read_property {"element":"r1","property":"name"}'
#   send 3 'quit'
```

### Notes for an AI model driving the app
1. Always start with `help` to discover the exact tool names and argument
   schemas for the running version.
2. Use `status` to confirm the app is ready (`aiAgentActive` and `hasProject`
   must be true before element commands will work).
3. Use `list_elements` to learn element names, and `list_properties` / `describe_property`
   to learn the exact property names before `read_property` / `write_property`
   (property names are case-sensitive and differ between element types, e.g.
   a Rectangle exposes `size` (QVector2D) and `lineWidth` — not `width`).
4. `write_property` `value` is best passed as a JSON string (e.g. `"1.5"`,
   `"[80,60]"`, `"#FF0000"`); it is converted to the property's Qt type.
5. Wrap a batch of element/property mutations in `start_session` … `end_session`
   so they collapse into a single undo step (or `undo` them all at once).
6. The GUI stays live and visible; the user can watch the app change in real
   time as you issue commands.

## Coding Conventions
- C++23, Qt6, QML
- use camel case variable names
- abstract object names begin with a capital letter
- `PROP(T, name)` / `PROPV(T, name, value)` macros for Q_PROPERTY with NOTIFY
- `nlohmann::json` for serialization
- `std::format`-based logging (logger.h)
- **QML SplitView idiom**: `QtQuick.Controls` `SplitView` is a `Control` and does
  **not** expose an `item` property.  The split panes must be **direct child
  items** (sized via the attached properties `SplitView.preferredWidth/Height`,
  `SplitView.minimumWidth/Height`, `SplitView.fillWidth/Height`) — the working
  pattern is `qml/MainPanel.qml`.  `SplitView.item { ... }` breaks QML loading
  with “Cannot assign to non-existent property 'item'".  The handle delegate is
  set as a plain `handle:` property, and the orientation is `Qt.Horizontal` or
  `Qt.Vertical`.

## New c++ classes and functions/methos:
Start every c++ class definition and every function/method with this header:

      #--------------------------------------------------------------------
      #     <function/class name>
      #--------------------------------------------------------------------

A c++ class is structured this way:   private - protected - public.

## New c++ files
Start every c++ file with this file header:

      //=============================================================================
      //  ZCam - manufactoring tool for G-code machines and Fiber Laser
      //
      //  Copyright (C) 2025-2026 Werner Schweer
      //
      //  This program is free software; you can redistribute it and/or modify
      //  it under the terms of the GNU General Public License version 2
      //  as published by the Free Software Foundation and appearing in
      //  the file LICENCE.GPL
      //=============================================================================

## C++ Header files
Protect header files with ```#pragma once```