# Perspektivische Projektion für das Lasern (WYSIWYG)

Stand: 2026.  Referenz-Implementierung: `projectPathListToXY()` in
`src/element3d.cpp` (orthografische Projektion, z wird verworfen).

## Ausgangslage (Was heute passiert)

Der Datenpfad zum Laser läuft ausschließlich über 2D-Polygone (Clipper, mm,
XY-Ebene):

```
CAD Element3d (pathList)
   │  globalMatrix()            ← volle 3D-Transformation (Translate/Rot/Scale)
   ▼
projectPathListToXY()          ← ORTHOGRAFISCH: Punkte als (x, y, 0) mappen,
   │                              z-Verhalten ignorieren
   ▼
2D PathsD (project-root space, mm)
   │  Recipe::createFill()      ← Hatch/Füllung in 2D
   │  wobble()
   │  panel-grid Offsets
   ▼
Recipe::collectLaserPath()
   ▼
Laser (mark/ framing)
```

Die Füllung (`Recipe::createFill`) findet bereits **in der 2D-Domäne** statt –
sie trifft aber auf **falsch projizierte** Polygone, sobald Objekte um X/Y
rotiert sind oder über der XY-Ebene liegen (z ≠ 0).

Wenn die Szene aus einer beliebigen Rotation heraus (root.eulerRotation ≠ 0)
gelasert werden soll, muss die perspektivische Kamera-Projektion auf die
Polygone übertragen werden – und das Ergebnis am Ende wieder in die
Laser-Ebene (z = 0, mm) zurücktransformiert werden.

## Was zu tun ist

### 1. Kamera-Parameter aus der QML-Szene holen

In `qml/View3DPanel.qml` stehen die relevanten Werte:

- `camera2` (PerspectiveCamera), Position & `fieldOfView`
- `root.eulerRotation`, `root.position`, `root.scale`  (Welt-Transform)
- `panel.width`, `panel.height` (Viewport in Pixeln)

Diese Werte müssen auf die C++-Seite gebracht werden (neue `PROPV` an ZCam /
Cam, oder `Q_INVOKABLE` mit Parametern). Aktuell errechnet der Laser den
2D-Pfad komplett ohne Rückgriff auf die View.

### 2. Projektions-Pipeline anpassen (`elementPathListTo3D` / neue Funktion)

Die bisherige Funktion `projectPathListToXY()` muss so erweitert werden,
dass sie zwischen

- `Orthographic` (altes Verhalten: z wegwerfen) und
- `Perspective`   (Kamera-Projektion)

umschaltbar ist (`Cam::perspective` / `ZCam::perspectiveCamera`).

Implementierungs-Variante (perspektivisch):

```cpp
for each path in element->pathList():
    for each pt in path:
        // 1. lokaler Punkt in 3D-Hauptkoordinaten (x, y, z=0)
        QVector3D vLocal(pt.x(), pt.y(), 0.0f);
        // 2. in Welt/Koordinatenraum bringen (Root-Node = Welt)
        QVector3D vWorld = element->globalMatrix().map(vLocal);
        // 3. durch Kamera-View projizieren (ViewProj-Matrix aus QML/C++ bauen)
        QVector3D vClip = viewProjMatrix.map(vWorld);   // inkl. Division
        // 4. NDC → Screen-Koordinaten (Pixel)
        // 5. Screen → Welt-XY-Ebene zurück (Ray-Plane-Intersection
        //    entlang des Blickstrahls auf z == 0)
        cp.push_back({backProjected.x(), backProjected.y()});
```

Wichtig: Die Ausgabe muss **in mm auf der Laser-Ebene (z = 0)** liegen, damit
Panel-Raster, Framing und Galvo-Kalibrierung weiter funktionieren. Eine Rein
Screen-Pixel-Projektion reicht nicht.

### 3. Polygon-Geometrie als 3D-Pfad verfügbar machen

`pathList()` liefert aktuell 2D-Punkte mit z = 0. Für die Kamera-Projektion
reicht das, denn die Transformationsmatrizen (`globalMatrix()`) enthalten
die gesamte 3D-Information. Es ist **keine** Änderung an `PathList` nötig.

Edge-Cases, die bedacht werden müssen:

- Punkte hinter der Kamera (w < 0) -> projizieren invalid
- Punkte mit z ≠ 0 (rotierte Objekte) → projizieren korrekt
- Bézier-Kontrollpunkte → ebenfalls projizieren, nicht nur Vertices

### 4. Füllung in der projizierten 2D-Domäne

Die Hatch-Logik (`Clipper::hatch`, `Recipe::createFill`) bleibt unverändert –
sie arbeitet auf den **projizierten** 2D-Polygonen weiter. Der Unterschied:
Das Polygon-Material wird vorab perspektivisch verzerrt, die `interval`
(Abstand zwischen Hatch-Lines) entspricht damit nicht mehr einem konstanten
physikalischen Abstand auf der Arbeitsfläche, sondern einem konstanten
Abstand in der projizierten Ebene.

Wenn die Fülldichte auf dem Werkstück (nicht auf der Projektion) konstant sein
soll, müsste das `interval` pro Zeile an die lokale Projektionsvergrößerung
angepasst werden (aufwendiger). Das ist eine bewusste Design-Entscheidung.

### 5. Panel-Raster & Framing aktualisieren

Die nachfolgenden Schritte arbeiten alle auf dem bereits transformierten
2D-Pfad:

- `Cam::updateCam()` / `Cam::convexHull()` / `Cam::boundingBox()`
  → bekommen die perspektivisch verzerrten Polygone, müssen also nicht
  angepasst werden
- Panel-Offsets (mm) → funktionieren unverändert auf dem 2D-Ergebnis
- `Framing::update()` → funktioniert unverändert
- `Recipe::collectLaserPath()` → funktioniert unverändert

### 6. Synchronisation View ↔ Mark-Pfad

Damit „was der Benutzer sieht“ auch wirklich das ist, was gelasert wird:

- Aktuelle Kamera-Position/Rotation aus QML an
  `Cam::updateCam()` übergeben (nicht nur beim Klick auf „Cam Refresh“,
  sondern auch vor jedem Marking-Vorgang – `Laser::refreshCamAndFraming()`
  ruft `zcam->refreshCam()` auf; dort kann die aktuelle View-Matrix
  mitgegeben werden).
- Beim Umschalten Ortho ↔ Perspective muss `camDirty` gesetzt und ein
  Refresh ausgelöst werden.
- Wenn die perspektivische Ansicht aktiv ist, sollte die Eingabe von
  Rotationen am Element (pos/rot via Canvas) mit Vorsicht behandelt
  werden – ein perspektivisch verzogener 2D-Screen→Scene-Mapping ist
  nicht mehr eindeutig.

### 7. Umschaltbarkeit sicherstellen

Bereits vorhanden: `panel.perspectiveCamera` (QML, View3DPanel.qml) – in
Settings gespeichert (`property alias projection`).

Fehlt: Übergabe an C++. Vorschlag:
- `ZCam::perspectiveCamera {get; set;}` als PROP in C++
- In `ZCam::refreshCam()` bzw. vor `collectLaserPath()` den aktuellen
  Viewport-Zustand als `CamProjection` struct übergeben:
```
struct CamProjection {
    bool        perspective;
    QVector3D   cameraPos;
    QVector3D   eulerRot;    // root
    QVector3D   rootPos;
    QVector3D   rootScale;
    float       fovDeg;
    float       viewportW, viewportH;
};
```

## Reihenfolge der Umsetzung

1. `ZCam::perspectiveCamera` + `CamProjection` struct anlegen und aus
   QML heraus setzen (bei Kamera-/View-Änderung)
2. `projectPathListToXY()` um Parameter `CamProjection` erweitern;
   bisherige `ortho`-Variante als Default beibehalten
3. Perspektivische Pfad-Transformation implementieren + auf z = 0
   zurückprojizieren
4. `Recipe::collectLayerPath()` / `processTileLines()` /
   `collectLaserPath()` auf neue Signatur umstellen
5. Umschalt-Button (pCamera) auf `camDirty` + `refreshCam` verdrahten
6. Tests: Würfel (BREP) drehen, perspektivisch lasern, Framing-Rahmen
   mit sichtbarer Kontur vergleichen

## Offene Design-Fragen

- Soll die perspektivische Projektion relativ zur aktuellen Ansicht
  (user rotiert mit Maus) oder relativ zu einer fixen „Laser-Kamera“
  (z. B. Galvo-Blick von oben, Objektiv-Verzeichnung) erfolgen?
- Soll bei aktiver perspektivischer Projektion die Hatch-Dichte
  („interval“) ortsabhängig skaliert werden, um auf dem Werkstück
  eine konstante physikalische Dichte zu erhalten?
- Soll das Ergebnis nach der Projektion wieder auf die z = 0-Ebene
  (mm) zurückgerechnet werden (für Galvo-Kalibrierung), oder als
  reiner Screen-Space-Pfad direkt an den Galvo gehen?
