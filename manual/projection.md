# Perspektivische Projektion für das Lasern (WYSIWYG)

Stand: 2026 — **IMPLEMENTIERT** (Option A: einstellbar als Property von `Cam`).
Referenz-Implementierung: `projectPathListToXY()` in `src/element3d.cpp`.

## Anforderungen (vom Benutzer festgelegt)

1. **Der Laser bekommt ausschließlich 2D-Koordinaten in mm.**
   Er hat nichts mit Projektion/Galvo-Optik zu tun. Die Projektion ist eine
   reine Darstellungseigenschaft.

2. **Elemente, die exakt auf der z=0-Ebene liegen, bleiben 1:1 in mm
   unverändert.** Die mm-Größe auf z=0 darf sich nicht ändern.

3. **Nur Geometrie mit z ≠ 0** (z. B. ein um die Y-Achse gedrehter Text)
   **verändert sich durch die perspektivische Projektion** — sie wird
   verzerrt, so wie sie aus der Blickrichtung erscheint.

4. **Was gelasert wird = was man in der Draufsicht sieht.** Die Perspektive
   bildet die z ≠ 0-Geometrie „von oben" als Zentralprojektion auf die
   z=0-Ebene ab.

5. **„Feste Skalierung":** Unabhängig von Zoom/Pan und der frei rotierbaren
   3D-Ansicht. Ergebnis immer in mm auf z=0.

## Kern-Transformation

Für jeden Punkt `p = (x, y, z)` in Szene-mm:

```
orthografisch:   (x, y, z)  →  (x, y)
perspektivisch:  (x, y, z)  →  (x·s, y·s),   s = H / (H − z)
```

mit `H` = Höhe des Projektionszentrums (der Perspektivkamera) über z=0.

Eigenschaften:
- z = 0  → s = 1  → **z=0-Ebene bleibt 1:1 in mm** ✔ (Anforderung 2)
- z ≠ 0  → s ≠ 1  → erhöhte/rotierte Geometrie wird verzerrt ✔ (Anforderung 3)
- Kein Bezug zu Galvo/Optik/Zoom ✔ (Anforderungen 1, 5)

Dies ist die einzige Transformation, die alle Anforderungen gleichzeitig
erfüllt. Eine echte Screen-Space-Projektion (NDC + Viewport, FOV, Zoom) würde
die z=0-Ebene zoom-abhängig skalieren und verletzt Anforderung 2 — sie kommt
für den Laser nicht in Frage.

## Woher kommt `H`? — Entscheidung: Option A (fixe Blickhöhe, einstellbar)

`H` ist ein konstanter, konfigurierbarer Wert — **keine** Galvo-Objektivhöhe.
Je kleiner `H`, desto stärker der perspektivische Effekt; je größer, desto
ortho-ähnlicher. Gewählt wurde **Option A** (fixe, einstellbare Blickhöhe als
Property von `Cam`), nicht Option B (Live-Kamerahöhe aus der View), weil die
QML-Kamera praktisch immer auf ~1000 mm Höhe steht (Zoom läuft über
`root.scale`, nicht über Kamerahöhe) und Option B damit bei normaler Ansicht
praktisch orthografisch wäre.

## Implementierung

### 1. `Cam`: neue Properties `perspective` + `projectionHeight` (cam.h)

```cpp
PROPV(bool, perspective, false)            ///< Zentralprojektion an/aus
PROPV(double, projectionHeight, 1000.0)    ///< Blickpunkt-Höhe [mm] über z=0
```

- Inspector-Zeile „Projection": Checkbox `perspective`, Feld `projectionHeight`.
- Serialisierung über das bestehende `properties()`-JSON
  (`parseAllPropertyNames`, Typen `bool`/`float` → `double`).
- Im Constructor werden `perspectiveChanged`/`projectionHeightChanged` mit
  `zcam->setCamDirty(true)` verbunden → der Cam-Refresh-Button wird aktiv,
  sobald sich die Projektion ändert.
- Default: `perspective = false` → unverändertes orthografisches Verhalten
  (abwärtskompatibel), `projectionHeight = 1000` mm.

### 2. `projectPathListToXY()` erweitert (element3d.h / element3d.cpp)

```cpp
PathsD projectPathListToXY(const Element3d* element, bool perspective = false,
                           double projectionHeight = 0.0);
```

Perspektivischer Zweig nach `matrix.map()`:

```cpp
double denom = H - double(r.z());
if (denom < zEps) denom = zEps;   // 0.1 mm — Pol-Klemme
double s = H / denom;
cp.push_back({r.x() * s, r.y() * s});
```

`perspective == false` oder `projectionHeight <= 0` → orthografisch (z droppen),
1:1 wie bisher. Die Klemme `zEps` verhindert, dass Punkte nahe/über der
Blickpunkt-Höhe gegen unendlich skalieren.

### 3. Aufrufer verdrahtet (lesen `perspective`/`projectionHeight` vom Cam)

- `recipe.cpp` `collectLayerPath()`        — Layer-Polygone (z. B. convexHull)
- `recipe.cpp` `processTileLines()`        — Fill/Wobble/Linien (Anzeige)
- `recipe.cpp` `collectLaserPath()`        — eigentlicher Laser-Pfad
- `fixture.cpp` `Fixture::size()`          — Tile-Größe / Panel-Raster

### Was sich NICHT ändert

- **Fill/Hatch** (`createFill`, `Clipper::hatch`): läuft auf den projizierten
  2D-Polygonen mit konstantem `interval` → physikalisch konstante Dichte.
- Panel-Raster, Framing, Konvexhülle, BoundingBox: unverändert — arbeiten
  auf den bereits projizierten 2D-Daten.
- Die frei rotierbare 3D-Ansicht (View3DPanel.qml) bleibt ein reines
  Darstellungs-Feature, entkoppelt vom Laser-Pfad.

## Hinweis zur Verwendung

- `perspective` einschalten, wenn Objekte z ≠ 0 haben (um X/Y rotierte Texte,
  BREP-Volumen) und als perspektivische Draufsicht gelasert werden sollen.
- `projectionHeight` klein wählen (z. B. 50–200 mm) für einen deutlich
  sichtbaren perspektivischen Effekt; groß (≥ 1000 mm) für fast orthografisch.
- Flache Elemente (z = 0) sind von der Perspektive unberührt (s = 1) und
  bleiben maßstabsgetreu in mm.
