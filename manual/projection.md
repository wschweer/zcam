# Perspektivische Projektion für das Lasern (WYSIWYG)

Stand: 2026 (nach zweiter Klarstellung durch den Benutzer).
Referenz-Implementierung: `projectPathListToXY()` in `src/element3d.cpp`.

## Entscheidungen / Anforderungen (vom Benutzer festgelegt)

1. **Der Laser bekommt ausschließlich 2D-Koordinaten in mm.**
   Er hat nichts mit Projektion/Galvo-Optik zu tun. Die Projektion ist eine
   reine Darstellungseigenschaft.

2. **Elemente, die exakt auf der z=0-Ebene liegen, bleiben 1:1 in mm
   unverändert.** Die mm-Größe auf z=0 darf sich nicht ändern.

3. **Nur Geometrie mit z ≠ 0** (z. B. ein um die Y-Achse gedrehter Text)
   **verändert sich durch die perspektivische Projektion** — sie wird
   verzerrt, so wie sie aus der Blickrichtung erscheint.

4. **Was gelasert wird = was man in der Draufsicht sieht.**
   Die Ortho-Draufsicht (current) ist die 1:1-mm-Abbildung. Die
   Perspektive soll die z ≠ 0-Geometrie „von oben" als Zentralprojektion
   auf die z=0-Ebene abbilden.

5. **„Feste Skalierung":** Die Projektion ist unabhängig von Zoom/Pan und der
   frei rotierbaren 3D-Ansicht. Ergebnis immer in mm auf z=0.

## Kern-Transformation

Für jeden Punkt `p = (x, y, z)` in Szene-mm:

```
orthografisch (heute):   (x, y, z)  →  (x, y)
perspektivisch (Ziel):   (x, y, z)  →  (x·s, y·s),   s = H / (H − z)
```

mit `H` = Höhe des Projektionszentrums (der Perspektivkamera) über z=0.

Eigenschaften:
- z = 0   → s = 1   → **z=0-Ebene bleibt 1:1 in mm** ✔ (Anforderung 2)
- z ≠ 0   → s ≠ 1   → erhöhte/rotierte Geometrie wird verzerrt ✔ (Anforderung 3)
- Kein Bezug zu Galvo/Optik/Zoom ✔ (Anforderungen 1, 5)

**Dies ist die einzige Transformation, die alle Anforderungen gleichzeitig
erfüllt.** Eine echte Screen-Space-Projektion (über NDC + Viewport, mit FOV
und Zoom) würde die z=0-Ebene abhängig vom Zoom skalieren und verletzt
Anforderung 2 — deshalb kommt sie für den Laser nicht in Frage.

## Woher kommt `H`? — DER OFFENE DESIN-PUNKT

`H` ist die **einzige neue Größe**. Es ist NICHT die Galvo-Objektivhöhe, es
ist die Höhe des virtuellen Projektionszentrums (Blickpunkt der
Perspektiv-Draufsicht). Je kleiner `H`, desto stärker der perspektivische
Effekt; je größer `H`, desto ortho-ähnlicher.

Es gibt zwei konzeptionell verschiedene Optionen — **der Benutzer muss
wählen**:

### Option A — Fixe Blickhöhe (empfohlen, einfach)

`H` ist ein konstanter, konfigurierbarer Wert (z. B. ein Property an ZCam
oder Cam, Default z. B. 1000 mm = „Standard-Blickhöhe", mit der die View
auch initialisiert wird: `camera2.position.z = 1000`).

- Ortho/Persp-Umschalter steuert, ob zusätzlich `s` angewendet wird.
- Unabhängig vom aktuellen Kamera-Standort in der View → reproduzierbar.
- `H = 0` oder „perspective aus" → orthografisch (1:1), abwärtskompatibel.

### Option B — Live aus der aktuellen Perspektiv-Ansicht

`H` = `camera2.position.z` (die aktuelle Höhe der Perspektivkamera aus der
QML-View). Die Projektion folgt der aktuellen Ansicht, aber normalisiert auf
z=0-mm (unabhängig vom Zoom, da Zoom in `root.scale`/`root.position` steckt,
nicht in der Kamerahöhe).

- „Was man sieht" wird wortwörtlich gelasert (abhängig von aktueller Höhe).
- Erfordert Übergabe von `camera2.position.z` von QML an C++ vor jedem
  Laserauftrag.
- Kamerahöhe ~1000 mm bei typischen Objekt-größen → s ≈ 1.001, praktisch
  kein Effekt. Um einen sichtbaren Effekt zu haben, müsste man die Kamera
  sehr nah heranfahren.

**Wichtige Erkenntnis aus der View (View3DPanel.qml):**
In der QML-View ist die Kamera praktisch IMMER auf ~1000 mm Höhe
(`camera2.position` z=1000, Zoom über `root.scale`, nicht über Kamerahöhe).
Das bedeutet: Eine an die View gekoppelte Projektion (Option B) ergäbe bei
normaler Ansicht praktisch eine Orthografie. Ein sichtbarer perspektivischer
Effekt auf z ≠ 0-Geometrie entsteht nur mit einem deutlich kleineren `H`.

## Implementierung (element3d.cpp / zcam.h / QML)

### 1. `ZCam`: neuer Zustand „perspektivisch laser"

```cpp
// zcam.h
Q_PROPERTY(bool perspectiveCamera READ perspectiveCamera
           WRITE setPerspectiveCamera NOTIFY perspectiveCameraChanged)
PROPV(bool, perspectiveCamera, false)
PROPV(double, projectionHeight, 0.0)   // [mm] 0 = ortho (default)
```

### 2. `projectPathListToXY()` erweitern

```cpp
// element3d.h — Signatur mit Default-Param (Aufrufer können angepasst,
// aber auch unverändert bleiben für ortho)
PathsD projectPathListToXY(const Element3d* element,
                           bool perspective = false,
                           double projectionHeight = 0.0);
```

```cpp
// element3d.cpp — nach matrix.map():
auto r = matrix.map(QVector3D(float(pt.x()), float(pt.y()), 0));
if (perspective && projectionHeight > 0.0) {
      double denom = projectionHeight - double(r.z());
      if (denom < 0.1) denom = 0.1;       // Singularitäts-Klemme
      double s = projectionHeight / denom;
      cp.push_back({double(r.x()) * s, double(r.y()) * s});
      }
else
      cp.push_back({double(r.x()), double(r.y())});   // 1:1 ortho
```

### 3. Aufrufer verdrahten (4 Stellen)

`recipe.cpp:114`, `recipe.cpp:139` (Fill), `recipe.cpp:266`,
`fixture.cpp:57` → übergeben `zcam->perspectiveCamera()` und
`zcam->projectionHeight()`.

### 4. QML: Ortho/Persp-Button mit C++-Zustand synchronisieren

In `View3DPanel.qml` bei den Umschaltern (`panel.perspectiveCamera = true/false`,
Zeilen ~1585/1600) zusätzlich `ZCam.perspectiveCamera = ...` setzen und
`ZCam.projectionHeight` aus einem konfigurierbaren Wert (Option A) oder
`camera2.position.z` (Option B).

### Was sich NICHT ändert

- **Fill/Hatch** (`createFill`, `Clipper::hatch`): läuft auf den projizierten
  2D-Polygonen mit konstantem `interval` → physikalisch konstante Dichte.
- Panel-Raster, Frame, Framing, Konvexhülle: unverändert auf den
  projizierten 2D-Daten.

## Zusammenfassung der offenen Entscheidung

- **Option A (fixe Blickhöhe):** reproduzierbar, einstellbare Stärke des
  Effekts, empfohlen.
- **Option B (Live-Kamerahöhe):** wortwörtlich „was man sieht", aber bei
  normaler Ansicht praktisch ortho; Effekt nur bei sehr nahem Heranzoomen.

Beide teilen denselben C++-Code (s = H/(H−z)); nur die Quelle von `H` und
die QML-Verdrahtung unterscheiden sich.
