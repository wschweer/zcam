# Perspektivische Projektion für das Lasern (WYSIWYG)

Stand: 2026 (nach Klarstellung durch den Benutzer).
Referenz-Implementierung: `projectPathListToXY()` in `src/element3d.cpp`.

## Entscheidungen (vom Benutzer festgelegt)

1. **Projektion ist FIX von oben auf die XY-Ebene bei z=0.**
   → Keine Abhängigkeit von der frei rotierbaren 3D-Ansicht (root.eulerRotation).
   → Die „perspektivische Projektion" ist eine **Zentralprojektion** von einem
     festen Punkt auf der Z-Achse in Höhe `h` (dem Galvo/Objektiv) auf die
     Arbeitsebene z=0.
   → Mathematisch ist das eine **Skalierung XY mit dem Faktor `s = h / (h − z)`**.

2. **Hatch-Dichte physikalisch konstant, wie im Recipe angegeben**
   (Linien/mm bzw. Linienabstand `interval`).
   → Da die Projektion **auf die z=0-Ebene in mm** erfolgt (nicht in
     Screen-Pixel) und der Laser ebenfalls auf z=0 arbeitet, ist der
     konstante Recipe-Linienabstand in der projizierten 2D-Domäne
     **automatisch = physischer Abstand auf dem Werkstück**.
   → **Die bestehende Hatch-Logik (`createFill`, `Clipper::hatch`) braucht
     KEINE Änderung.**

## Kern-Mathematik

Die perspektivische Projektion von oben auf z=0 ist im Gegensatz zur
bisherigen orthografischen Projektion (z einfach verwerfen) eine
zentralperspektivische Vergrößerung:

```
orthografisch (heute):   (x, y, z)  →  (x, y)              // z wegwerfen
perspektivisch (Ziel):   (x, y, z)  →  (x·s, y·s)          // s = h / (h − z)
```

- Ein Punkt auf z=0     → s = 1        (unverändert)
- Ein Punkt über z=0    → s > 1        (erscheint vergrößert/„näher")
- Ein Punkt unter z=0   → s < 1        (erscheint verkleinert)
- z → h (Punkt nähert sich dem Projektionszentrum) → s → ∞  (Singularität,
  muss geklemmt werden: `denom = max(h − z, ε)`)

Da das Galvo-Feld physikalisch begrenzt ist und alle zu lasernden Objekte
auf/unter der Arbeitsebene z=0 liegen (z ≤ 0), ist `denom = h − z ≥ h > 0`,
also numerisch unproblematisch. Nur für z nahe an h (Objekt ragt fast bis
zum Galvo) muss geklemmt werden.

## Was zu tun ist

### 1. Neue Maschineneigenschaft: Projektionszentrum-Höhe `h`

`h` ist der physikalische Abstand Galvo/Objektiv → Arbeitsfläche z=0 (mm).
Das ist eine **Maschinen-Eigenschaft** (je nach verbauter Linse/Mechanik),
also in `src/machine.h` als `PROPV(double, projectionHeight, <default>)`.

Wichtig: die Serialisierung läuft über die `_propertiesQ/MOPA/UV`-JSON in
`src/laser_bjjcz.cpp`. Dort muss ein neues Feld ergänzt werden, sonst wird
`h` weder gespeichert/geladen noch im Inspector angezeigt.

**Design-Alternativen für h:**
- (a) **Absolut** in mm (z. B. 250 mm): direkt der Objektiv-Abstand.
  Perspektivischer Effekt wirkt nur, wenn Objekte z ≠ 0 haben.
- (b) **0 = orthografisch** (Rückwärtskompatibel): `h ≤ 0` schaltet die
  Perspektive ab → altes Verhalten. Empfohlen als Default, damit
  bestehende Projekte/Maschinen unverändert bleiben.

### 2. `projectPathListToXY()` erweitern (element3d.cpp / element3d.h)

Signatur um `projectionHeight` erweitern:

```cpp
Clipper2Lib::PathsD projectPathListToXY(const Element3d* element,
                                        double projectionHeight = 0.0);
```

Implementierung (nach `matrix.map(...)`):

```cpp
auto r = matrix.map(QVector3D(float(pt.x()), float(pt.y()), 0.0f));
if (projectionHeight > 0.0) {
      double denom = projectionHeight - r.z();
      if (denom < 0.1)          // Klemme nahe Projektionszentrum
            denom = 0.1;
      double s = projectionHeight / denom;
      cp.push_back({r.x() * s, r.y() * s});
      }
else {
      cp.push_back({r.x(), r.y()});   // orthografisch (bisheriges Verhalten)
      }
```

Hinweis: Es existiert bereits ein **ungespeicherter Editor-Buffer**
`src/.element3d.cpp,` mit genau diesem Ansatz
(`projectPathListToXY(element, projectionHeight)` + `H/denom`-Clamping).
Der Ansatz ist korrekt und kann übernommen werden — allerdings ist die
Singularitäts-Klemme `denom < 0.01` für mm-Einheiten sehr aggressiv;
`0.1` mm ist praxisnäher. Bézier-Kontrollpunkte werden implizit behandelt,
da `pathList()` die bereits aufgelösten Punkte liefert.

### 3. Aufrufer mit `h` versorgen

Vier Stellen rufen `projectPathListToXY(ce)` auf und müssen die Höhe
der aktiven Maschine übergeben:

- `src/recipe.cpp:114`  (`collectLayerPath`)
- `src/recipe.cpp:139`  (`processTileLines`)  → Fill/Hatch
- `src/recipe.cpp:266`  (`collectLaserPath`)  → eigentlicher Laser
- `src/fixture.cpp:57`  (`Fixture::size`)

Zugriff auf die Maschine: `zcam->project()->machine()` →
`machine()->projectionHeight()`.

Am besten eine kleine Hilfsfunktion in `element3d.cpp`:

```cpp
double currentProjectionHeight(const ZCam* zcam) {
      auto* proj = zcam ? zcam->project() : nullptr;
      auto* m    = proj ? proj->machine() : nullptr;
      return m ? m->projectionHeight() : 0.0;
      }
```

und die Aufrufer rufen `projectPathListToXY(ce, currentProjectionHeight(zcam))`.

### 4. Was sich NICHT ändert

- **Fill/Hatch** (`Recipe::createFill`, `Clipper::hatch`): arbeitet auf den
  projizierten 2D-Polygonen mit konstantem `interval` → physikalisch
  konstante Dichte auf z=0. **Keine Änderung.**
- **Panel-Raster-Offsets** (mm): unverändert.
- **Cam-Geometrie / convexHull / boundingBox / Framing**: arbeiten auf den
  projizierten 2D-Polygonen, unverändert.
- **QML-View** (`View3DPanel.qml`): Die frei rotierbare 3D-Ansicht bleibt ein
  reines Darstellungs-Feature. Die Laser-Projektion ist davon entkoppelt.

## Offener Design-Punkt (Benutzer entscheidet)

**Semantik von z in der Szene.** Liegen die zu lasernden Objekte
- **immer auf z=0** (flache CAD-Geometrie, der Normalfall) → dann hat die
  perspektivische Projektion **keinen sichtbaren Effekt**, weil s=1. Die
  Perspektive wäre dann nur bei 3D-Objekten (BREP, um X/Y rotierte Elemente)
  relevant.
- oder wird die **Objekt-Höhe über der Arbeitsebene** (Dicke des Werkstücks,
  z > 0) berücksichtigt, sodass die Draufsicht-Vergrößerung sichtbar wird.

Das bestimmt, ob `h` (a) der tatsächliche Objektiv-Abstand ist (groß, kaum
Effekt) oder (b) ein künstlich kleiner Wert, der den perspektivischen Effekt
bewusst erzeugt (z. B. um ein 3D-Objekt so zu lasern, wie es aus einer
bestimmten Blickhöhe aussieht).

**Meine Empfehlung:** `PROPV(double, projectionHeight, 0.0)` mit
**0 = orthografisch** als Default (rückwärtskompatibel), und der Benutzer
setzt bei Bedarf die Höhe des Galvos (≈ 250 mm) oder eine künstliche
Blickhöhe.

## Reihenfolge der Umsetzung

1. `machine.h`: `PROPV(double, projectionHeight, 0.0)` hinzufügen
2. `laser_bjjcz.cpp` `_propertiesQ/MOPA/UV` + `machinegcode.cpp`:
   Inspector-Feld „Projection Height" (mm) ergänzen
3. `element3d.h/.cpp`: Signatur + perspektivischer Zweig (Klemme)
4. Aufrufer (recipe.cpp ×3, fixture.cpp ×1) mit `h` verdrahten
5. Bauen, Test: BREP-Würfel mit z-Ausdehnung → mit/ohne h lasern und
   Framing-Rahmen mit Projektion vergleichen
