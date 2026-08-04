---

<span style="font-size:48px">ZCam — Benutzerhandbuch</span>

---

[TOC]

- [Entwicklung](#entwicklung)
- [Installation](#installation)

---

# Überblick

ZCam ist ein CAM-Programm (Computer Aided Manufacturing). Es erlaubt dir, CAD-Daten
(DXF, SVG) zu importieren und zu bearbeiten. ZCam generiert daraus Steuerdaten für
Galvo-Faserlaser oder G-Code für CNC-Maschinen.

Die Anwendung basiert auf C++23, Qt 6, QML und Qt Quick 3D. Das Hauptfenster ist ein
3D-Canvas; der Großteil der Logik befindet sich in C++, während QML die UI steuert.

---

# GUI

## Übersicht

Die GUI besteht aus mehreren Panels, zwischen denen über Buttons in der Panelleiste
gewechselt werden kann.

| Panel | Beschreibung |
| :--- | :--- |
| **Main** | Die Hauptarbeitsfläche — Projektbaum, Inspector und 3D-Viewport. |
| **Recipes** | Archiv von Laser-Rezepten (Parametersätze für verschiedene Gravuraufgaben). |
| **Machines** | Konfiguration deiner Maschine(n) — Laser oder G-Code-CNC. |
| **Config** | Anwendungsweite Einstellungen (GUI, Farben, Verzeichnisse, etc.). |
| **Laser** | Seitenpanel zur Lasersteuerung (Init/Exit, Framing, Marking, Testmodus). |
| **Media Browser** | Durchsuchen von Artwork (SVG-Icons, Schriften) für den Import ins Projekt. |

### Menüleiste

| Menü | Einträge |
| :--- | :--- |
| **Datei** | Neu, Öffnen, Speichern, Speichern unter, Importieren, Beenden |
| **Bearbeiten** | Rückgängig, Wiederherstellen, Konfigurieren (wechselt zum Config-Tab) |
| **Tools** | Materialtest, Galvotest |
| **Hilfe** | Über |

### Werkzeugleiste

Die Werkzeugleiste bietet Schnellzugriff auf die wichtigsten Dateioperationen
(Neu, Öffnen, Speichern, Speichern unter, Importieren), Rückgängig/Wiederherstellen,
Media-Browser-Umschaltung und Laser-Panel-Umschaltung.

### Tab-Leiste

Die Tab-Leiste schaltet zwischen den vier Hauptpanels um: **Main**, **Recipes**,
**Machines** und **Config**.

Rechts in der Tab-Leiste befinden sich:

- **Fixture-Auswahl** — ein Dropdown zur Auswahl der aktiven Fixture für die
  CAM-Verarbeitung.
- **Cam-Button** — berechnet die CAM-Daten neu. Der Button ist nur aktiviert, wenn
  die CAM-Daten veraltet sind (d.h. nach Geometrieänderungen, die noch nicht
  verarbeitet wurden).

### Statusleiste

Die Statusleiste am unteren Fensterrand zeigt Meldungen und die aktuelle
Mausposition im 3D-Viewport an.

---

## Das `Main` Panel

Das Main Panel ist horizontal in Subpanels unterteilt, die du teilweise optional
ein-/ausblenden kannst.

<div style="display: flex; align-items: center;">
  <div style="flex: 1; padding-right: 20px;">
    <img src="project.png" alt="Projektlayout" width=200px>
  </div>
  <div style="flex: 2;">

- **Project** — Dieses Panel ist vertikal in den **Projektbaum** und den
  **Inspector** geteilt.

**Projektbaum** zeigt eine hierarchische Ansicht der Projektelemente. In der oberen
Hälfte stehen CAD-Elemente, die den Input des CAM-Prozessors darstellen. Unten siehst
du CAM-Elemente, die den CAD-Input verarbeiten und den Output für die **Maschine**
erzeugen.

**Inspector** zeigt die Eigenschaften ("Properties") des aktuell ausgewählten
Projektelements.

  </div>
</div>

### Projektbaum

Der Projektbaum zeigt die vollständige Elementhierarchie:

```
Project
├── Camera
├── Cad
│   ├── Group (Layer 1)
│   │   ├── Rectangle
│   │   ├── Polygon
│   │   ├── Ellipse
│   │   └── Text
│   └── Group (Layer 2)
├── Cam
│   └── (CAM-Output-Elemente)
├── Fixture 1
│   └── Framing
├── Fixture 2
│   └── Framing
└── ...
```

- **Klick** zum Auswählen eines Elements.
- **Doppelklick** auf eine Gruppe/Layer zum Umbenennen.
- **Drag & Drop** von Elementen zum Umordnen (Reparenting).
- Der Auf-/Zuklapp-Zustand wird über Sitzungen hinweg gespeichert.

### Elementhierarchie und Gruppen

#### Baumstruktur

Alle Projektelemente sind in einem einzigen Baum organisiert, dessen Wurzel das
`Project`-Element ist. Der Baum hat eine feste Top-Level-Struktur:

```
Project
├── Camera         (optional, kameraunterstützte Positionierung)
├── Cad             (CAD-Input — enthält gesamte Zeichengeometrie)
│   ├── Group       (ein CAD-Layer, auch "Layer" oder "Group" genannt)
│   │   ├── Rectangle
│   │   ├── Polygon
│   │   ├── Ellipse
│   │   └── Text
│   └── Group
├── Cam             (CAM-Output — berechnet aus CAD + Recipes)
├── Fixture 1       (eine Fixture mit Framing-Kontur)
│   ├── Framing
│   └── Recipe      (LaserLayer — weist CAD-Elementen Laserparameter zu)
├── Fixture 2
│   ├── Framing
│   └── Recipe
└── ...
```

**Wichtige Regeln:**

- **Cad** ist der Container für die gesamte CAD-Eingabegeometrie. Er enthält eine
  oder mehrere **Groups** (Layer), die jeweils Formen (Rectangle, Polygon,
  Ellipse, Text) enthalten.
- **Cam** ist der Container für den CAM-Output. Er wird automatisch aus der
  CAD-Geometrie und den Recipe-/LaserLayer-Zuordnungen berechnet.
- **Fixture**-Elemente befinden sich unter Cam. Jede Fixture hat ein
  **Framing**-Unterelement (die Framing-Kontur) und ein oder mehrere
  **Recipe**-Unterelemente (LaserLayers).
- **Recipe**-Elemente (LaserLayers) weisen CAD-Geometrie Laserparameter zu. Sie
  sind Kinder von Fixtures.

#### Gruppen erstellen

Gruppen (Layer) können auf zwei Arten erstellt werden:

1. **Kontextmenü**: Rechtsklick auf das **Cad**-Element im Projektbaum und Auswahl
   von **Add Group**. Eine neue Group wird als Kind von Cad erstellt.
2. **Drag & Drop Reparenting**: Jedes Element kann zum Kind eines anderen Elements
   gemacht werden, indem es im Projektbaum auf das Ziel gezogen wird (siehe unten).

Jedes Element3d kann Kinder haben — nicht nur Groups. Ein Rectangle, Polygon oder
Text-Element kann ebenfalls Kindelemente haben und so eine Untergruppe bilden. Das
bedeutet, dass jede Form als Gruppen-Parent fungieren kann.

#### Koordinatensystem

ZCam verwendet ein **hierarchisches lokales Koordinatensystem**. Jedes Element3d
hat sein eigenes lokales Koordinatensystem, das durch drei Eigenschaften definiert
ist:

| Eigenschaft | Typ | Beschreibung |
| :--- | :--- | :--- |
| **pos** | `QVector3D` | Position (x, y, z) im Koordinatensystem des Parents (mm) |
| **rot** | `QVector3D` | Rotation um x-, y-, z-Achsen (Grad) |
| **scale** | `QVector3D` | Skalierungsfaktor pro Achse |

Die **lokale Transformationsmatrix** eines Elements ist:

```
M_local = Translate(pos) × Rotate(rot) × Scale(scale)
```

Die **globale (Welt-) Transformation** ist das Produkt aller Parent-Matrizen von der
Wurzel bis zu diesem Element:

```
M_global = M_root × ... × M_parent × M_local
```

Ein Punkt im lokalen Raum eines Elements wird in den Weltraum transformiert durch:

```
P_world = M_global × P_local
```

**Wichtige Punkte:**

- Der Ursprung (0, 0, 0) liegt in der linken unteren Ecke des Arbeitsbereichs.
- Alle Koordinaten sind in Millimetern.
- Das Koordinatensystem ist rein positiv (alle Geometrie sollte positive
  Koordinaten innerhalb des Scanfelds haben).
- Mirror-Eigenschaften (`mirrorX`, `mirrorY`) werden als negative Skalierungswerte
  in der lokalen Matrix angewendet.
- Wenn ein Element reparented wird, werden seine lokalen pos/rot/scale
  **automatisch neu berechnet**, sodass die Weltraumposition gleich bleibt (kein
  visuelles Springen). Die Formel lautet:
  `M_new_local = inverse(M_new_parent_global) × M_old_global`.

#### Elementgruppen verschieben

Da jedes Element sein eigenes lokales Koordinatensystem innerhalb seines Parents
hat, verschiebt das Verschieben einer **Parent-Gruppe** automatisch alle ihre
Kinder. Das liegt daran, dass die Weltpositionen der Kinder durch Multiplikation
mit der Transformationsmatrix des Parents berechnet werden.

- **Eine Gruppe im 3D-Viewport ziehen** → alle Kinder bewegen sich zusammen.
- **Die pos-Eigenschaft einer Gruppe im Inspector ändern** → alle Kinder bewegen
  sich zusammen.
- **Eine Gruppe rotieren oder skalieren** → alle Kinder rotieren oder skalieren
  um den Ursprung der Gruppe.
- **Eine Gruppe spiegeln** (`mirrorX`/`mirrorY`) → alle Kinder werden gespiegelt.

Diese hierarchische Transformation ermöglicht es dir, komplexe Layouts durch
verschachtelte Gruppen zu erstellen. Du kannst beispielsweise ein Logo als Gruppe
von Formen erstellen und dann das gesamte Logo als einzelne Einheit positionieren,
rotieren und skalieren.

### CAD-zu-Recipe-Zuordnung

Die Zuordnung zwischen CAD-Geometrie und Laserparametern (Recipes/LaserLayers)
wird über die **`laserLayer`-Eigenschaft** auf jedem Element3d verwaltet:

#### Funktionsweise

1. Jedes **Recipe**-Element (LaserLayer) ist ein Kind einer Fixture und
   referenziert ein **LaserRecipe** (einen benannten Parametersatz aus dem
   Recipes-Panel).
2. Jedes Element3d im CAD-Baum hat eine `laserLayer`-Eigenschaft, die ein
   Recipe-Element referenzieren kann.
3. Wenn die `laserLayer`-Eigenschaft eines Elements null ist, **erbt** es den
   laserLayer von seinem Parent. Dies wird durch `effectiveLaserLayer()`
   bestimmt, das die Parent-Kette nach oben geht, bis ein nicht-null `laserLayer`
   gefunden wird.
4. Während der CAM-Verarbeitung sammelt das Recipe alle CAD-Elemente, deren
   `effectiveLaserLayer()` mit sich selbst übereinstimmt, und verarbeitet sie mit
   seinen Laserparametern.

#### Zuordnungs-Workflow

1. Erstelle eine **Fixture** (Rechtsklick Cam → Add Fixture).
2. Erstelle einen **LaserLayer** (Recipe) unter der Fixture (Rechtsklick Fixture →
   Add Laserlayer).
3. Wähle ein LaserRecipe für den LaserLayer aus (im Inspector, die "Recipe"-
   Eigenschaft).
4. Wähle ein CAD-Element oder eine Group und setze seine **"Recipe"**-
   (laserLayer-) Eigenschaft im Inspector auf den LaserLayer.
5. Alle Kinder dieses Elements, die keinen eigenen laserLayer gesetzt haben, erben
   ihn automatisch.

#### Beispiel

```
Fixture "Gravur"
├── Framing
└── Recipe "Tiefe Gravur"  (recipe: "50W langsam tief")

Cad
├── Group "Logo"           (laserLayer: → "Tiefe Gravur")
│   ├── Rectangle           (erbt "Tiefe Gravur")
│   ├── Polygon             (erbt "Tiefe Gravur")
│   └── Group "Details"     (erbt "Tiefe Gravur")
│       ├── Text            (erbt "Tiefe Gravur")
│       └── Ellipse         (erbt "Tiefe Gravur")
└── Group "Rahmen"         (laserLayer: → "Leichte Markierung")
    └── Polygon             (erbt "Leichte Markierung")
```

In diesem Beispiel verwenden alle Elemente unter "Logo" die Parameter "Tiefe
Gravur", während Elemente unter "Rahmen" "Leichte Markierung" verwenden. Ein
Element kann den geerbten laserLayer überschreiben, indem es seine eigene
`laserLayer`-Eigenschaft setzt.

### Drag & Drop im 3D-Viewport

#### Elemente verschieben

Um ein Element im 3D-Viewport zu verschieben:

1. **Klicke** auf ein Element, um es auszuwählen. Die Bounding-Box des Elements
   erscheint mit Griffpunkten.
2. **Klicke und ziehe** innerhalb der Bounding-Box, um das Element zu verschieben.
   Das Element folgt der Maus auf der XY-Ebene.
3. **Lasse** die Maustaste los, um das Element an der neuen Position abzulegen.

Das Ziehen ist auf die XY-Ebene (die Laser-Arbeitsfläche) beschränkt. Die
Verschiebung wird als undoable Befehl aufgezeichnet, wenn das Ziehen endet
(`endElementDrag()`).

- **Gruppen**: Ein Klick innerhalb der Bounding-Box einer Gruppe wählt die
  gesamte Gruppe (inklusive aller Kinder) aus und zieht sie. Die Gruppe agiert
  als einzelne verschiebbare Einheit.
- **Eckpunkt-Bearbeitung**: Wenn ein Polygon ausgewählt ist, erscheinen einzelne
  Eckpunkt-Griffe. Das Ziehen eines Eckpunkt-Griffs bewegt nur diesen Eckpunkt
  (nicht das gesamte Element).
- **`Ctrl` + Mausrad**: Skaliert das ausgewählte Element.
- **`Shift` + Mausrad**: Skaliert gleichmäßig (Seitenverhältnis beibehalten).

#### Drag & Drop Reparenting (Projektbaum)

Elemente können im Projektbaum per Drag & Drop neu organisiert werden:

| Ablageposition | Visuelle Anzeige | Aktion |
| :--- | :--- | :--- |
| **Oberes Drittel** einer Zeile | Blaue Linie oben | **Vor** dem Ziel einfügen (innerhalb desselben Parents umordnen) |
| **Mittleres Drittel** einer Zeile | Hervorgehobene Zeile | **In** das Ziel ablegen (als Kind reparenten) |
| **Unteres Drittel** einer Zeile | Blaue Linie unten | **Nach** dem Ziel einfügen (innerhalb desselben Parents umordnen) |

- **Auf eine Group ablegen** macht das gezogene Element zu einem Kind dieser Group.
  Die lokalen pos/rot/scale des Elements werden neu berechnet, um seine
  Weltposition zu erhalten.
- **Auf eine Form** (Rectangle, Polygon, etc.) ablegen macht das gezogene Element
  zu einem Kind dieser Form und bildet so eine Untergruppe.
- **Auf einen Container** (Cad, Cam, Fixture) ablegen fügt das Element als letztes
  Kind ein.
- **Umordnen**: Vor/nach einer Zeile ablegen ordnet das Element innerhalb
  desselben Parents neu.
- Self-Drop und Ablegen in einen Nachfahren werden verhindert.

Alle Drag & Drop-Operationen im Baum sind undoable.

### Media Browser

Der Media Browser ist ein umschaltbares Seitenpanel (Button "M" in der
Werkzeugleiste) mit drei Unter-Panels:

#### Fonts-Panel

System-Schriftarten durchsuchen und auf Text-Elemente anwenden:

1. **Wähle ein Text-Element** im Projektbaum oder 3D-Viewport aus.
2. **Öffne den Media Browser** (klicke auf den "M"-Button in der Werkzeugleiste)
   und wechsle zum **Fonts**-Tab.
3. Durchsuche die Schriftartliste (wechsle zwischen Alle und Favoriten mit den
   "All"/"Favs"-Buttons).
4. Klicke auf eine Schriftfamilie, um sie auszuwählen. Die rechte Seite zeigt eine
   Live-Vorschau mit der ausgewählten Schriftart und dem Style.
5. Passe den **Style** (Regular, Bold, Italic, etc.) über das Dropdown an.
6. Bearbeite den **Beispieltext**, um verschiedenen Text vorabzussehen.
7. **`Ctrl` + Mausrad** im Vorschaubereich, um die Vorschaugröße zu skalieren.
8. Klicke auf **Apply**, um die ausgewählte Schriftart auf das aktuelle
   Text-Element anzuwenden. Die Änderung ist über den Undo-Stack rückgängig
   machbar.
9. Alternativ: Verwende den **Schriftart-Picker** im Inspector (der "font"-
   Property-Typ zeigt einen FontFamilyButton, der das Media-Browser-Fonts-Panel
   automatisch öffnet und die aktuelle Schriftart vorauswählt).
10. Klicke auf den ★/☆-Button, um eine Schriftart zu den Favoriten
    hinzuzufügen/zu entfernen.

#### Artwork-Panel

SVG/DXF/DWG-Artwork aus einem konfigurierten Verzeichnis durchsuchen und auf den
3D-Canvas ziehen:

1. Konfiguriere das **Artwork-Verzeichnis** im Config-Panel (Config → Directories →
   Artwork Directory). Der Verzeichnisbaum erscheint auf der linken Seite des
   Artwork-Panels.
2. Navigiere im Verzeichnisbaum und wähle einen Ordner. Unterstützte Dateien
   (`.svg`, `.dxf`, `.dwg`) erscheinen als Kachel-Thumbnails auf der rechten
   Seite.
3. **Klicke** auf eine Kachel, um sie auszuwählen.
4. **Ziehe** eine Kachel aus dem Panel auf den 3D-Viewport. Eine grüne
   Bounding-Box-Vorschau folgt dem Mauszeiger auf der XY-Ebene.
5. **Lasse** die Maus los, um die Datei an der Cursor-Position zu importieren. Die
   SVG/DXF-Geometrie wird als Polygon-Elemente im aktuell sichtbaren CAD-Layer
   erstellt, positioniert so, dass die linke untere Ecke der importierten
   Bounding-Box an der Drop-Position liegt.

Die Artwork-Kacheln können mit `Ctrl` + Mausrad für bessere Sichtbarkeit skaliert
werden.

#### Icons-Panel

SVG-Icons aus dem konfigurierten Icon-Verzeichnis durchsuchen. Dieses Panel wird
hauptsächlich als Referenz verwendet und kann ebenfalls verwendet werden, um
SVG-Icons auf den Canvas zu ziehen.

### Drag & Drop-Import auf den 3D-Canvas

ZCam unterstützt zwei Methoden zum Importieren von SVG/DXF/DWG-Dateien auf den
3D-Canvas:

#### Methode 1: Aus dem Dateisystem (Desktop Drag & Drop)

1. Ziehe eine `.svg`-, `.dxf`- oder `.dwg`-Datei aus deinem Dateimanager (z.B.
   Dolphin, Nautilus) direkt auf den 3D-Viewport.
2. Ein **türkisfarbenes Overlay** erscheint mit dem Text "Drop SVG / DXF to
   import", und eine grüne Bounding-Box-Vorschau folgt dem Mauszeiger.
3. **Lasse** die Maus los, um die Datei an der Cursor-Position zu importieren. Die
   Geometrie wird so platziert, dass die linke untere Ecke der Bounding-Box am
   Drop-Punkt liegt.

#### Methode 2: Aus dem Media-Browser-Artwork-Panel

1. Öffne den Media Browser und wechsle zum **Artwork**-Tab.
2. Navigiere zum Ordner mit deinen SVG/DXF-Dateien.
3. **Ziehe** eine Artwork-Kachel auf den 3D-Viewport.
4. Das gleiche Vorschau- und Drop-Verhalten wie bei Methode 1 gilt.

#### Interne Funktionsweise

- **`startSvgDrag(path)` / `startDxfDrag(path)`**: Parst die Datei, berechnet die
  Bounding-Box und erstellt ein `TessGeometry`-Rechteck als Vorschau. Die QML-
  Schicht positioniert diese Vorschau an der Cursor-Position auf der XY-Ebene.
- **`importSvgAt(path, x, y)` / `importDxfAt(path, x, y)`**: Importiert die Datei
  und positioniert die Geometrie so, dass die linke untere Ecke der Bounding-Box
  bei (x, y) in Szenenkoordinaten liegt.
- **`endSvgDrag()`**: Bereinigt die Vorschaugeometrie.
- Die `DropArea`-QML-Komponente verarbeitet die Drag-Entered-, Position-Changed-
  und Dropped-Events und wandelt Bildschirmkoordinaten über `screenToScene()`
  in Szenenkoordinaten um.


### Inspector

Der Inspector zeigt die Eigenschaften des ausgewählten Elements. Eigenschaften sind
in Zeilen ("rows") angeordnet, jede mit einem Label und einer oder mehreren Zellen
("cells" = Untereigenschaften). Zu den Property-Typen gehören:

| Typ | Beschreibung |
| :--- | :--- |
| `text` | Texteingabe |
| `bool` | Checkbox |
| `int` | Spinbox (Ganzzahl) |
| `float` | Double-Spinbox |
| `vector3d` | Drei Double-Spinboxen (x, y, z) |
| `scale` | Drei Double-Spinboxen mit optionalem Lock-Modus |
| `vector2d` | Zwei Double-Spinboxen |
| `font` | Schriftart-Auswahl |
| `halign` | Horizontale Ausrichtung |
| `multiline` | Mehrzeilige Texteingabe |
| `singleline` | Einzeilige Texteingabe |
| `line` | Linien-/Stiftstil-Auswahl |
| `color` | Farbauswahl |
| `layer` | CAD-Layer-Auswahl |
| `recipe` | Rezept-Auswahl |
| `machine` | Maschinen-Auswahl |
| `machineName` | Maschinenname (nur Lesezugriff) |
| `machineType` | Maschinentyp-Auswahl |
| `override` | Parameter-Override-Typ-Auswahl |
| `pulsewidth` | Pulsbreiten-Auswahl |
| `lineJoin` | Linienverbindungs-Stil |
| `lineEnd` | Linienend-Stil (Cap) |
| `lockScale` | Skalierungs-Lock-Modus (Aus / Lock / Square) |
| `lockSize` | Größen-Lock-Modus |
| `framingType` | Framing-Typ (BoundingBox / ConvexHull) |
| `cameraName` | Kameragerät-Auswahl |
| `cameraView` | Live-Kameravorschau (zoombar/verschiebbar) |
| `empty` | Leerer Platzhalter (keine Eingabe) |

---

## 3D Panel
### Navigation

| Aktion | Eingabe |
| :--- | :--- |
| Zoomen | `Ctrl` + Mausrad |
| Verschieben | Mittlere Maustaste + Ziehen |
| Rotieren | Rechte Maustaste + Ziehen |
| Auswählen | Linke Maustaste Klick |
| Mehrfachauswahl | `Ctrl` + Linksklick |

Eine 3DConnexion "SpaceMouse" wird automatisch erkannt, wenn vorhanden, und kann für
alle Navigationsoperationen verwendet werden.

### Ansichten

| Icon | Aktion | Icon | Aktion |
| :--- | :--- | :--- | :--- |
<img src="../icons/view-top.svg" style="width:18px;"/> | Oben | <img src="../icons/view-bottom.svg" style="width:18px;"/> | Unten
<img src="../icons/view-front.svg" style="width:18px;"/> | Front | <img src="../icons/view-rear.svg" style="width:18px;"/> | Rückseite
<img src="../icons/view-left.svg" style="width:18px;"/> | Links | <img src="../icons/view-right.svg" style="width:18px;"/> | Rechts
<img src="../icons/view-isometric.svg" style="width:18px;"/> | Isometrische Projektion | <img src="../icons/view-perspective.svg" style="width:18px;"/> | Perspektivische Projektion
<img src="../icons/view-fullscreen.svg" style="width:18px;"/> | Auf Bildschirm skalieren | |

### Zeichen-Werkzeuge

Werkzeuge werden aus der Werkzeugleiste ausgewählt. Folgende Werkzeuge stehen zur
Verfügung:

| Werkzeug | Beschreibung |
| :--- | :--- |
| **Pointer** (Standard) | Auswählen, Verschieben, Rotieren und Skalieren von Elementen. |
| **Rechteck** | Klicken und Ziehen, um ein Rechteck zu erstellen. |
| **Kreis/Ellipse** | Klicken und Ziehen, um eine Ellipse zu erstellen. |
| **Polygon** | Klicken zum Hinzufügen von Eckpunkten; Doppelklick oder `Enter` zum Schließen. `Esc` bricht ab. |
| **Text** | Klicken zum Platzieren eines Textelements; Tippen zum Bearbeiten. |

### Interaktives Bearbeiten

Wenn ein Element ausgewählt ist:

- **Griffpunkte ziehen** zum Skalieren (Eckpunkte bei Rechtecken/Ellipsen).
- **Elementkörper ziehen** zum Verschieben.
- **Eckpunkt-Griffe** erscheinen bei Polygonen — ziehen zum Neupositionieren einzelner Eckpunkte.
- **`Ctrl` + Mausrad** skaliert das ausgewählte Element.
- **`Shift` + Mausrad** skaliert gleichmäßig (Seitenverhältnis beibehalten).

### Kamera-Overlay

Wenn ein `CameraElement` zum Projekt hinzugefügt wird, zeigt der 3D-Viewport ein
Live-Kamera-Overlay in der XY-Ebene. Das Overlay kann positioniert, rotiert und
skaliert werden. Die Trapez-Korrekturparameter (`trapezX`, `trapezY`) ermöglichen
die Ausrichtung des Overlays mit der physischen Kameraprojektion.

---

## Recipes Panel

Das Recipes Panel verwaltet das Archiv der Laser-Rezepte. Ein Rezept enthält einen
oder mehrere **Durchgänge** (LaserPass), jeweils mit eigenem Satz Laserparametern:

| Parameter | Beschreibung |
| :--- | :--- |
| **Power** | Laserleistung (% des Maximums) |
| **Speed** | Marking-Geschwindigkeit (mm/s) |
| **Frequency** | Pulsfrequenz (kHz) |
| **Pulse Width** | Pulsdauer (ns) — nur MOPA/UV-Laser |
| **Num Passes** | Anzahl der Durchgänge über die Geometrie |
| **Interval** | Schraffurzeilenabstand (mm) — auch als LPI oder LPMM angegeben |
| **Start Angle** | Anfangs-Schraffurwinkel (°) |
| **Angle Increment** | Rotation pro Durchgang (°) |
| **Zigzag** | Schraffurrichtung jede Zeile wechseln |
| **Interleave** | Anzahl der verschachtelten Schraffursätze |
| **Wobble** | Wobble (oszillierende Füllung) aktivieren |
| **Wobble Step** | Wobble-Schrittweite (mm) |
| **Wobble Size** | Wobble-Amplitude (mm) |
| **Override Timings** | Benutzerdefinierte Laser-Timing-Parameter anstelle der Maschinen-Defaults |
| **On/Off/End/Polygon Delay** | Laser-Timing-Parameter (μs) |
| **Jump Speed / Min/Max Jump Delay / Jump Distance Limit** | Sprung-Parameter (Travel) |

Jeder Durchgang kann einzeln aktiviert oder deaktiviert werden. Rezepte werden als
JSON-Dateien im konfigurierten Rezeptverzeichnis gespeichert.

---

## Machines Panel

Das Machines Panel verwaltet deine Maschinenkonfigurationen. Jede Maschine hat
einen Typ und (bei Lasern) einen Board-Typ:

| Maschinentyp | Board-Typ | Verbindung |
| :--- | :--- | :--- |
| Q-switched Laser | BJJCZ | USB |
| MOPA Laser | BJJCZ | USB |
| UV Laser | RKQ-LM-441 | Ethernet (Raw) |
| GCode CNC | — | Serial/Network (G-Code) |

### Maschinen-Eigenschaften

| Eigenschaft | Beschreibung |
| :--- | :--- |
| **Name** | Maschinenname (im Projekt angezeigt) |
| **Type** | Maschinentyp (Q-switched / MOPA / UV / GCode CNC) |
| **Board Type** | Controller-Board-Typ (BJJCZ / RKQ-LM-441) |
| **Description** | Freitext-Beschreibung |
| **Max Travel** | Maximale Verfahrtwege (X, Y, Z) in mm |
| **Travel Speed** | Standard-Vorschubgeschwindigkeit (mm/min für CNC, mm/s für Laser) |
| **Framing Speed** | Geschwindigkeit beim Framing (mm/s) |
| **Safe Dist 1 / Safe Dist 2** | Sicherheitsabstände für Z-Achsen-Bewegung |
| **Max Feed** | Maximale Feed-Rate (X, Y, Z) |
| **Max Acceleration** | Maximale Beschleunigung (X, Y, Z) |
| **Min/Max Spindle** | Spindeldrehzahlbereich (RPM) |
| **Precision** | Positionierungsgenauigkeit (mm) |
| **NC Precision** | NC-Genauigkeit (mm) |
| **Circle Precision** | Bogen-Interpolationsgenauigkeit (mm) |

### Laser-spezifische Eigenschaften

| Eigenschaft | Beschreibung |
| :--- | :--- |
| **Galvo P1 / P2 / P3** | Galvo-Kalibrierungsparameter |
| **Galvo Scale** | X/Y Galvo-Skalierungsfaktoren |
| **Galvo Shear X / Y** | Galvo-Scherkorrektur |
| **Galvo Rotate** | Galvo-Rotationskorrektur (°) |
| **Galvo Swap XY** | X/Y Galvo-Achsen tauschen |
| **On/Off/End/Polygon Delay** | Standard Laser-Timing-Parameter (μs) |
| **Jump Speed / Min/Max Jump Delay / Jump Distance Limit** | Sprung-Parameter |
| **Min/Max Frequency** | Unterstützter Frequenzbereich (kHz) |
| **Tickle Pulse / Tickle Freq** | Tickle (Keep-Alive) Puls-Einstellungen |
| **Enable FPK** | First Pulse Killer (FPK) aktivieren |
| **FPK Start Power / FPK Increment** | FPK-Rampenparameter |
| **UV Min/Max Pulse** | UV-Laser Pulsbreitenbereich (ns) |
| **Eth Device** | Ethernet-Gerätename (nur RKQ, z.B. `enp11s0`) |

---

## Config Panel

Das Config Panel verwaltet anwendungsweite Einstellungen:

| Kategorie | Eigenschaften |
| :--- | :--- |
| **GUI** | Icon-Größe, Navigation-Cube-Größe, Handle-Größe, Schriftart, Schriftgröße |
| **Colors** | Panel-Hintergrund, Akzent, Raster, Marking, Move, Framing Farben |
| **Grid** | Raster anzeigen, Rasterabstand |
| **Directories** | Standardmaschine, Artwork-Verzeichnis, Icon-Verzeichnis, Maschinenverzeichnis, Rezeptverzeichnis |
| **DXF** | DXF-Skalierungsfaktor (Standard: 72.0, konvertiert DXF-Zoll in mm) |

---

## Laser Panel

Das Laser Panel ist ein Seitenpanel (Umschaltung über das Laser-Icon in der
Werkzeugleiste), das Echtzeit-Lasersteuerung bietet:

| Kontrolle | Beschreibung |
| :--- | :--- |
| **Laser An/Aus** | Laser initialisieren oder herunterfahren (`init()` / `exit()`) |
| **Status** | Aktueller Laserzustand (Off, Idle, Framing, Marking, etc.) |
| **Fortschrittsbalken** | Zeigt Marking-Fortschritt (currentTime / estimatedEnd) |
| **Framing** | Framing starten/stoppen (Geometrie mit rotem Zeiger umfahren) |
| **Marking** | Marking starten/stoppen (tatsächliche Gravur ausführen) |
| **Test Mode** | Testmodus aktivieren (Marking ohne Laserleistung — Trockenlauf) |
| **Stop** | Aktuelle Operation abbrechen |

Die Laser-State-Machine durchläuft: **Off → Idle → Framing/Marking → Idle → Off**.

---

# Galvo Laser

## Arbeitsfläche und Koordinatensystem

Der Nullpunkt des Koordinatensystems liegt in der linken unteren Ecke des
Arbeitsbereichs. ZCam verwendet ein rein positives Koordinatensystem.

Der Arbeitsbereich (Scanfeld) hängt von der Brennweite der installierten Optik
(F-Theta-Optik) ab:

| Linsentyp | Brennweite | Scanfeld | Arbeitsabstand | Spot-Größe | Typischer Einsatzzweck & Empfehlung |
|:---|:---|:---|:---|:---|:---|
| F-100 | 100 mm | 70 × 70 mm | ~115 mm | Sehr klein (~15–20 µm) | Maximale Präzision, feine Tiefengravuren, Schneiden dünner Bleche. Ideal für 20W-Laser. |
| F-160 / F-163 | 160 mm | 110 × 110 mm | ~175–185 mm | Klein (~25–30 µm) | Der Standard-Allrounder. Perfekte Balance aus Feldgröße, feinem Fokus und hoher Energiedichte. |
| F-210 / F-220 | 210 mm | 150 × 150 mm | ~230–250 mm | Mittel (~35–40 µm) | Guter Kompromiss, wenn das 110er Feld knapp zu klein ist, aber noch Gravurleistung benötigt wird. |
| F-254 | 254 mm | 175 × 175 mm | ~280–295 mm | Mittel-Groß (~45–50 µm) | Für größere Werkstücke. Benötigt oft mindestens 30W–50W für tiefe Metallgravuren. |
| F-290 / F-300 | 290 mm | 200 × 200 mm | ~320–350 mm | Groß (~55–60 µm) | Große Flächen. Gravuren werden spürbar flacher. Hauptsächlich für Beschriftungen oder starke Laser (50W+). |
| F-330 / F-350 | 330 mm | 220 × 220 mm | ~350–390 mm | Sehr Groß (~65–75 µm) | Große Bauteile, Gehäusebeschriftungen, Farbumschlag auf Kunststoffen. |
| F-420 | 420 mm | 300 × 300 mm | ~460–480 mm | Sehr Groß (~80–90 µm) | Maximale Feldgröße. Nur für sehr starke Laser (50W–100W) oder reine Oberflächenmarkierungen sinnvoll. |

## Framing

Framing ist der Prozess des Umfahrens der Geometrie mit dem Laserzeiger (rotes
Licht, keine Marking-Leistung), um Position und Ausrichtung vor dem tatsächlichen
Marking zu überprüfen.

Zwei Framing-Konturtypen sind verfügbar:

- **BoundingBox** — Ein achsparalleles Rechteck um die gesamte Geometrie.
- **ConvexHull** — Ein Konvexe-Hülle-Polygon, das die Geometrie eng umschließt
  (Standard).

Framing läuft in einem Hintergrund-Thread. Die Framing-Kontur wird vor jedem
Framing-Start automatisch neu berechnet, um sicherzustellen, dass sie der aktuellen
Geometrie folgt.

## Marking

Marking ist der eigentliche Gravier-/Schneideprozess. ZCam:

1. Erstellt den Laserpfad aus dem CAM-Output (LaserPath: MoveTo/MarkTo-Elemente).
2. Wendet die Rezeptparameter an (Leistung, Geschwindigkeit, Frequenz, Pulsbreite, etc.).
3. Sendet den Pfad an das Laser-Board über die konkrete Laser-Subklasse
   (LaserBJJCZ über USB, oder LaserRKQ über Raw Ethernet).
4. Führt das Marking in einem Hintergrund-Thread aus und aktualisiert den
   Fortschrittsbalken in Echtzeit.

### Parameter-Overrides

Jeder LaserLayer (Recipe-Element im Projektbaum) kann bis zu zwei Parameter des
Basis-Rezepts überschreiben. Die Override-Typen sind:

| Wert | Parameter |
| :--- | :--- |
| 0 | Keiner |
| 1 | Geschwindigkeit |
| 2 | Leistung |
| 3 | Intervall |
| 4 | Frequenz |
| 5 | Anzahl (Durchgänge) |
| 6 | Puls (Pulsbreite) |

Dies ist nützlich für Materialtests: Richte ein Raster von Elementen mit
unterschiedlichen Override-Werten ein, um die optimalen Parameter zu finden.

## Materialtest

Das Menü **Tools → Materialtest** erstellt ein Materialtest-Element. Dies
generiert ein Raster von Testquadraten mit variierender Leistung und Geschwindigkeit
(oder anderen Parametern), um die optimalen Einstellungen für ein bestimmtes
Material zu finden.

## Galvotest

Das Menü **Tools → Galvotest** erstellt ein Galvo-Kalibrierungstest-Element zur
Überprüfung der Galvo-Ausrichtung und Korrekturparameter.

---

# Kamerasystem

ZCam unterstützt eine angeschlossene Linux-Webcam (V4L2 / Qt Multimedia) für
kameraunterstützte Positionierung:

1. **Kamera-Element hinzufügen** zum Projekt (Project → Add Camera).
2. **Kameragerät auswählen** im Inspector (cameraName-Eigenschaft).
3. **Live-Vorschau** erscheint im Inspector (cameraView-Typ) — Zoomen mit Mausrad,
   Verschieben durch Ziehen.
4. **3D-Overlay** wird in der XY-Ebene des 3D-Viewports angezeigt. Positionieren,
   Rotieren und Skalieren des Overlays, um es an die physische Kameraansicht
   anzupassen.
5. **Keystone-Korrektur** — verwende `trapezX` und `trapezY`, um Trapezverzerrung
   zu korrigieren, wenn die Kamera nicht perfekt senkrecht zur Arbeitsfläche steht.
6. **Opacity** — Transparenz des Overlays steuern (0.0 = vollständig transparent,
   1.0 = vollständig deckend).
7. **Brightness** — V4L2-Helligkeitsregelung (-1.0 bis 1.0, 0.0 = Standard).
8. **Auflösung / Bildrate** — spezifische Kameraauflösung (z.B. "1920x1080") und
   Bildrate (z.B. "30/1") auswählen, oder leer lassen für Defaults.

Die Kamera-Overlay-Textur wird von `CameraTextureData` verwaltet, das an das
`CameraElement` bindet und die 3D-Overlay-Textur für Qt Quick 3D bereitstellt.

---

# Dateiformate

## Projektdateien

ZCam-Projekte werden als JSON-Dateien gespeichert (`.zcam`-Erweiterung). Die
Projektdatei enthält:

- Projektmetadaten (Pfad, Name)
- Maschinenreferenz (nach Name)
- CAD-Elemente (Gruppen, Formen, Text)
- CAM-Elemente
- Fixtures (mit Framing-Einstellungen)
- Kamera-Element (mit Kamera- und Overlay-Einstellungen)
- Undo-Stack-Zustand

## Importformate

| Format | Erweiterung | Beschreibung |
| :--- | :--- | :--- |
| DXF | `.dxf` | AutoCAD DXF via libdxfrw — erstellt Layers, Polygone, Ellipsen |
| SVG | `.svg` | SVG via NanoSVG — erstellt Polygone aus SVG-Pfaden |

### DXF-Import

DXF-Dateien werden über die libdxfrw-Bibliothek importiert. Der Import erstellt
Projektelemente (Layers → Gruppen, Polylinien → Polygone, Ellipsen) im CAD-Element
des aktuellen Projekts. Der DXF-Skalierungsfaktor (im Config-Panel konfigurierbar,
Standard 72.0) konvertiert DXF-Zoll-basierte Einheiten in Millimeter.

### SVG-Import

SVG-Dateien werden über NanoSVG importiert. Koordinaten werden im Pixel-Raum
gelesen und mit dem Standard-96-DPI-Faktor (25.4 / 96 ≈ 0.2646 mm/px) in Millimeter
umgerechnet. SVG-Pfade werden in Polygon-Elemente im aktuell sichtbaren CAD-Layer
umgewandelt.

Drag-and-Drop-Import wird unterstützt: Die SVG/DXF-Bounding-Box wird im 3D-Viewport
vorschauangezeigt, und das Element wird an der Drop-Position platziert.

---

# Installation

## Galvo Laser

### RKQ-LM-441 Board

UV-Laser sind oft mit dem RKQ-LM-441 Board ausgestattet. Als Software wird RK-CAD
verwendet, welche als Besonderheit die Innengravur von Kristallblöcken ermöglicht.
Diese Boards werden über Ethernet mit dem Rechner verbunden.

Für einen problemlosen Betrieb empfehle ich, eine separate Netzwerkkarte für die
Verbindung Rechner–Laser zu verwenden:

1. Installiere eine separate Netzwerkkarte.
2. Konfiguriere die Netzwerkkarte im Netzwerkmanager so, dass sie nicht vom System
   verwendet wird (d.h. keine automatische Verbindung).
3. Konfiguriere ZCam für die Verwendung des RKQ-Controllers und trage die
   verwendete Netzwerkschnittstelle ein (z.B. `enp11s0`).
4. Gib ZCam die Rechte, Raw-Sockets öffnen zu können:

   ```bash
   sudo setcap cap_net_raw+ep /pfad/zu/zcam
   ```

5. Teste, ob der Connect-Button eine Verbindung zum Laser herstellt.

### BJJCZ Boards

Dies ist die am weitesten verbreitete Controller-Variante für Galvo-Laser von
Beijing JCZ Technology. Diese Boards werden überwiegend mit der hauseigenen
Software EZCAD betrieben und sind meist LightBurn-kompatibel. Sie werden über USB
mit dem Rechner verbunden.

Um auf die USB-Ports zugreifen zu können, musst du bestimmte Rechte konfigurieren:

1. Richte eine Gruppe `plugdev` ein:

   ```bash
   sudo groupadd plugdev
   ```

2. Trage dich als Mitglied der Gruppe `plugdev` ein:

   ```bash
   sudo usermod -aG plugdev $USER
   ```

3. Verbinde den Laser über USB mit dem Rechner und schalte ihn ein. Finde die
   Vendor-ID und Produkt-ID deines Geräts:

   ```bash
   lsusb
   ```

   Beispielausgabe:
   ```
   Bus 003 Device 006: ID 9588:9899 BJJCZ USBLMCV2
   ```

4. Konfiguriere udev durch Erstellen einer Datei
   `/etc/udev/rules.d/70-fiber-laser.rules` mit der ermittelten Vendor- und
   Produkt-ID:

   ```
   SUBSYSTEM=="usb", ATTR{idVendor}=="9588", ATTR{idProduct}=="9899", MODE="0666", GROUP="plugdev"
   ```

5. Udev-Regeln neu laden:

   ```bash
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```

6. Aus- und wieder einloggen (oder Neustart), damit die Gruppenmitgliedschaft
   wirksam wird.

---

# Tastenkürzel

| Kürzel | Aktion |
| :--- | :--- |
| `Ctrl+N` | Neues Projekt |
| `Ctrl+O` | Projekt öffnen |
| `Ctrl+S` | Projekt speichern |
| `Ctrl+Shift+S` | Projekt speichern unter |
| `Ctrl+Z` | Rückgängig |
| `Ctrl+Y` | Wiederherstellen |
| `Ctrl+Q` | Beenden |
| `Esc` | Aktuelle Operation abbrechen (Polygon-Zeichnung, Text-Bearbeitung) |
| `Enter` | Polygon schließen / Text bestätigen |
| `Delete` | Ausgewähltes Element löschen |
| `Ctrl+Mausrad` | 3D-Viewport zoomen / ausgewähltes Element skalieren |

---

# Entwicklung

## Übersicht

### Technologie und Tools

- C++23
- Qt 6.11
- QML
- Qt Quick 3D

Die GUI nutzt Qt 6.11 mit QML für die UI und nicht den klassischen Ansatz über
QWidgets. Das Hauptfenster ist ein 3D-Canvas, der mit QML aufgebaut wird. Die
Hauptmasse des Codes ist jedoch C++.

### Architektur

```
QObject
├── Element                    — Basisklasse aller Projektelemente
│   └── Element3d             — 3D-Element mit pos/rot/scale/geometry
│       ├── Cad               — CAD-Input-Container
│       ├── Cam               — CAM-Output-Container
│       ├── Group             — ein CAD-Layer (Gruppe von Formen)
│       ├── Rectangle         — Rechteck-Form
│       ├── Polygon           — Polygon/Linien-Form
│       ├── Ellipse           — Ellipsen/Kreis-Form
│       ├── Text              — Text-Form
│       ├── Fixture           — Fixture mit Framing
│       ├── Framing           — Framing-Kontur-Element
│       ├── Stock             — Rohteil-Definition
│       ├── CameraElement     — Kameragerät + Overlay
│       └── Recipe            — Laser-Layer mit Rezept + Overrides
├── Machine (virtuell)        — Basisklasse für alle Maschinentypen
│   ├── Laser (virtuell)      — Laser-Maschine mit Framing/Marking-FSM
│   │   ├── LaserBJJCZ        — BJJCZ USB-Laser
│   │   └── LaserRKQ          — RKQ Ethernet-Laser
│   └── MachineGCode          — G-Code CNC-Maschine
├── ZCam                      — Top-Level Anwendungs-Controller
├── Project                   — besitzt CAD, CAM, Fixture, Undo-Stack, Machine
├── Machines                   — Container für Maschinen-JSON-Dateien
└── InspectorModel/MachineModel — QAbstractListModel für QML
```

### Element

Die Klasse `Element` ist die Basisklasse aller Projektelemente in ZCam. Sie
implementiert ein Geometrie-Element (`TessGeometry`), dessen Basisklasse
`QQuick3DGeometry()` ist und von Qt Quick 3D zum Aufbau der 3D-Szene benötigt wird.

### Interface Qt Quick 3D — C++

Das Basis-3D-Element ist eine `Node`. Der 3D-Canvas baut eine Baumstruktur von
Nodes auf, die ihr Gegenstück auf der C++-Seite haben. C++-Modul und Canvas sind
über Signal/Slots miteinander verbunden. Folgende Signale in ZCam steuern den
Canvas:

```c++
void remove3dElement(Element*);           // Signal 3D-GUI: Element aus Scene Graph entfernen
void add3dElement(Element*);              // Signal 3D-GUI: neues Element zum Scene Graph hinzufügen
void addSubElement(Element*, Element*);   // Signal 3D-GUI: neues Sub-Element zum Scene Graph hinzufügen
void rootElementChanged(Element*);        // Signal 3D-GUI: Scene Graph neu aufbauen
```

Die Wurzel des Node-Baumes findet sich in `ZCam::topLevel()`:

```cpp
class ZCam : public QObject
    {
    ...
    Q_PROPERTY(TopLevel* topLevel READ topLevel WRITE setTopLevel NOTIFY topLevelChanged)
    ...
    TopLevel* _topLevel{nullptr};
    ...
```

`setTopLevel(...element...)` löst das Signal `topLevelChanged()` aus, welches dem
QML-Teil in `ProjectTree.qml`: `base.onRootElementChanged()` signalisiert, dass das
Projekt neu gerendert werden muss.

```qml
function onRootElementChanged(e) {
    // alten Baum zerstören
    var n = base.children.length;
    for (var i = 0; i < n; ++i) {
        base.children[i].destroy(100);
    }
    if (e)
        base.addElement(base, e);    // Shape-Komponente hinzufügen
}
```

### Properties

Die Liste der Element-Properties wird an mehreren Stellen benötigt:

- zur Konstruktion der QML-GUI-Elemente
- zum Schreiben/Lesen der Projektdatei

Die Liste der für ein Element verfügbaren Properties und ihre Eigenschaften werden
in einem JSON-String konfiguriert und können über
`std::string_view Element3d::properties()` abgerufen werden.

#### Property-JSON-Format

Eine "row" besteht aus einem oder mehreren Properties "cells". Eine "cell" kann den
Typ "empty" haben und nimmt nur leeren Platz ein. Eine "row" hat ein Label und eine
"cell" ein optionales "sublabel". Rows können in mehreren Spalten angeordnet werden
("columns" ist optional, Default ist 1). Eine Row kann leer sein "{}" und nimmt in
der GUI dann nur Platz ein.

Beispiel:

```json
{
    "class": "Text",
    "columns": 1,
    "rows": [
        {
            "label": "Location",
            "cells": [
                {
                    "name": "property1",
                    "sublabel": "x",
                    "type": "float"
                },
                {
                    "name": "property2",
                    "sublabel": "y",
                    "type": "double"
                }
            ]
        },
        {
            "label": "Rotation",
            "cells": [
                {
                    "name": "property3",
                    "type": "vector3d"
                }
            ]
        }
    ]
}
```

Liste der Property-Typen:

| Typ | Beschreibung |
| ---- | ---- |
| `text` | Texteingabe |
| `bool` | Checkbox |
| `int` | Spinbox |
| `float` | DoubleSpinBox |
| `vector3d` | Drei Double-Spinboxen |
| `scale` | Drei Double-Spinboxen (mit Lock-Modus) |
| `vector2d` | Zwei Double-Spinboxen |
| `font` | Schriftart-Auswahl |
| `halign` | Horizontale Ausrichtung |
| `multiline` | Mehrzeiliger Text |
| `singleline` | Einzeiliger Text |
| `line` | Linien-/Stiftstil |
| `color` | Farbauswahl |
| `layer` | CAD-Layer |
| `recipe` | Rezept |
| `machine` | Maschine |
| `machineName` | Maschinenname (nur Lesezugriff) |
| `machineType` | Maschinentyp |
| `override` | Override-Parameter-Typ |
| `pulsewidth` | Pulsbreite |
| `lineJoin` | Linienverbindungs-Stil |
| `lineEnd` | Linienend-Stil |
| `lockScale` | Skalierungs-Lock-Modus |
| `lockSize` | Größen-Lock-Modus |
| `framingType` | Framing-Typ |
| `cameraName` | Kameragerät-Name |
| `cameraView` | Live-Kamera-Ansicht |
| `empty` | Leerer Platzhalter |

### Machine-Klassenhierarchie

Die `Machine`-Klasse ist die virtuelle Basisklasse für alle Maschinentypen:

```
QObject
└── Machine (virtuell)
    ├── Laser (virtuell)
    │   ├── LaserBJJCZ   — USB-Kommunikation (BJJCZ-Boards)
    │   └── LaserRKQ     — Ethernet-Kommunikation via libpcap (RKQ-LM-441)
    └── MachineGCode     — G-Code CNC-Maschine
```

`Machine::create()` ist eine Factory-Methode, die die korrekte konkrete Unterklasse
basierend auf den Maschinentyp- und Board-Typ-Strings erstellt. Die
JSON-Serialisierung verwendet `metaObject()`, um sicherzustellen, dass die korrekte
Metatabelle der konkreten Unterklasse verwendet wird.

### Laser-State-Machine

Der Laser arbeitet über eine State-Machine mit folgenden Zuständen:

| Zustand | Beschreibung |
| :--- | :--- |
| `Off` | Laser ist nicht initialisiert |
| `Idle` | Laser ist an, aber nicht beim Framing oder Marking |
| `Framing` | Framing-Thread läuft (Geometrie wird umfahren) |
| `Marking` | Marking-Thread läuft (Gravur wird ausgeführt) |

Übergänge werden gesteuert durch `init()`, `exit()`, `startFraming()`,
`startMarking()` und `stop()`. Die konkreten Engine-Methoden
(`initEngine()`, `startFramingEngine()`, `stopMarkingEngine()`, etc.) werden von
`LaserBJJCZ` und `LaserRKQ` implementiert.

### Coding-Konventionen

- C++23, Qt6, QML
- `PROP(T, name)` / `PROPV(T, name, value)` Makros für Q_PROPERTY mit NOTIFY
- `nlohmann::json` für Serialisierung
- `std::format`-basiertes Logging (logger.h)
- Keine schweren OOP-Hierarchien; bevorzuge Wert-Semantik und Komposition
- Jede C++-Klassendefinition und jede Funktion/Methode beginnt mit einem
  Header-Kommentarblock
- Jede C++-Datei beginnt mit dem Standard-ZCam-Copyright-Header

### Third-Party-Code

Zur Bequemlichkeit enthält ZCam einige Third-Party-Quellen:

- **clipper2** von Angus Johnson — Lizenz: [Boost](https://www.boost.org/LICENSE_1_0.txt)
- **tess2** von Mikko Mononen — Lizenz: SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
- **libdxfrw** — DXF-Datei lesen/schreiben
- **nanosvg** — SVG-Datei parsen
- **libpcap** — Raw-Ethernet-Frame-Capture (für RKQ-LM-441-Kommunikation)
