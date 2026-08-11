# Perspective Projection for Laser Engraving (WYSIWYG)

Status: 2026 — **IMPLEMENTED** (Option A: configurable as a property of `Cam`).
Reference implementation: `projectPathListToXY()` in `src/element3d.cpp`.

## Requirements (defined by the user)

1. **The laser receives exclusively 2D coordinates in mm.**
   It has nothing to do with projection/galvo optics. The projection is a
   pure rendering property.

2. **Elements that lie exactly on the z=0 plane remain 1:1 in mm
   unchanged.** The mm size on z=0 must not change.

3. **Only geometry with z ≠ 0** (e.g. text rotated around the Y axis)
   **is altered by the perspective projection** — it is distorted as
   it appears from the viewing direction.

4. **What gets lasered = what you see in the top view.** The perspective
   maps the z ≠ 0 geometry "from above" as a central projection onto the
   z=0 plane.

5. **"Fixed scale":** Independent of zoom/pan and the freely rotatable
   3D view. Result is always in mm on z=0.

## Core Transformation

For each point `p = (x, y, z)` in scene-mm, with viewpoint
`eye = (cx, cy, H)`:

```
orthographic:    (x, y, z)  →  (x, y)
perspective:     out_xy = (cx, cy) + (p_xy − (cx, cy)) · s,   s = H / (H − z)
```

with `H` = height of the viewpoint above z=0 and `viewCenter = (cx, cy)` =
vertical foot of the viewpoint on the work plane.

**Important:** The scaling occurs **radially around the foot point `(cx, cy)`**,
not around the coordinate origin `(0,0)`. Scaling around the origin distorts
the geometry offset relative to the camera representation. Only scaling around
the foot point of the viewing direction (`out = c + (p_xy − c)·s`) corresponds to
the GPU central projection of the canvas camera.

Properties:
- z = 0 → s = 1 → **z=0 plane remains 1:1 in mm** ✔ (requirement 2)
- z ≠ 0 → s ≠ 1 → elevated/rotated geometry is distorted ✔ (requirement 3)
- No relation to galvo/optics/zoom ✔ (requirements 1, 5)

## Where does `H` come from? — Decision: Option A (fixed view height, configurable)

`H` is a constant, configurable value — **not** a galvo lens height.
The smaller `H`, the stronger the perspective effect; the larger, the more
ortho-like. **Option A** was chosen (fixed, configurable view height as a
property of `Cam`), not Option B (live camera height from the view), because
the QML camera is practically always at ~1000 mm height (zoom is via
`root.scale`, not camera height) and Option B would therefore be practically
orthographic in normal view.

## Implementation

### 1. `Cam`: new properties `perspective`, `projectionHeight`, `viewCenter` (cam.h)

```cpp
PROPV(bool, perspective, false)            ///< Central projection on/off
PROPV(double, projectionHeight, 1000.0)    ///< Viewpoint height [mm] above z=0
PROPV(QVector2D, viewCenter, QVector2D(0.0, 0.0))  ///< Foot point (x,y) [mm] on z=0
```

### 2. `projectPathListToXY()` extended (element3d.h / element3d.cpp)

```cpp
PathsD projectPathListToXY(const Element3d* element, bool perspective = false,
                           double projectionHeight = 0.0);
```

Perspective branch after `matrix.map()`:

```cpp
double denom = H - double(r.z());
if (denom < zEps) denom = zEps;   // 0.1 mm — pole clamp
double s = H / denom;
cp.push_back({cx + (r.x() - cx) * s, cy + (r.y() - cy) * s});
```

`perspective == false` or `projectionHeight <= 0` → orthographic (drop z),
1:1 as before. The clamp `zEps` prevents points near/above the viewpoint height
from scaling toward infinity.

### 3. Callers wired (read `perspective`/`projectionHeight`/`viewCenter` from Cam)

- `recipe.cpp` `collectLayerPath()`        — layer polygons (e.g. convexHull)
- `recipe.cpp` `processTileLines()`        — fill/wobble/lines (display)
- `recipe.cpp` `collectLaserPath()`        — actual laser path
- `fixture.cpp` `Fixture::size()`          — tile size / panel grid

### What does NOT change

- **Fill/Hatch** (`createFill`, `Clipper::hatch`): runs on the projected 2D
  polygons with constant `interval` → physically constant density.
- Panel grid, framing, convex hull, bounding box: unchanged — work on the
  already projected 2D data.
- The freely rotatable 3D view (View3DPanel.qml) remains a pure rendering
  feature, decoupled from the laser path.