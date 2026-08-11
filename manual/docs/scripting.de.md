# Scripting

Einige Properties können gescriptet werden. Du erkennst sie am
<img src="../assets/bound-expression.svg" style="width:25px;"/>
Button im Inspector. Ein gescripteter Wert wird mit JavaScript berechnet.

Der Button klappt einen Editor auf, mit dem du das Script erstellen kannst.

Der Wert im Inspector-Eingabefeld kann nach dem Aktivieren des Scripts nicht mehr
editiert werden — er wird farbig als schreibgeschützt gekennzeichnet.

Rückgabewert ist der Wert der angegebenen JavaScript-Expression.

```
      12                // gibt 12 zurück
      12*3              // gibt 36 zurück
      2;3               // gibt 3 zurück
      [12,24,8]         // gibt einen Vektor zurück (z.B. für Position)
      {x=12,y=24,z=8}   // alternative Schreibweise für Position
```

Bei Properties, die aus mehreren Werten bestehen, z.B. Position (Vector3d) oder
Size (Vector2d), kann im Scripteditor angegeben werden, welcher Wert zurückgegeben
werden soll. Der andere Wert bleibt dann editierbar. Du kannst so z.B. bei
`size` die Breite (x) von der Höhe (y) abhängig machen.

Script für `size` — Komponente: **All** (Width + Height)

```
      size.y * 0.5            // Breite ist jetzt immer halb so groß wie die Höhe
```

## Element-Referenzen

Elemente werden im Script mit ihren Namen referenziert. Der Namespace folgt dem
Projektbaum:

```
      project.cad.layer1.rectangle4.fill
```

Dies bezeichnet z.B. das `fill`-Property in einem Element im Projektbaum. Der Pfad
setzt sich zusammen aus `project` gefolgt von den Elementnamen entlang des Pfads
im Projektbaum.

## Active-Schalter

Der `Active`-Schalter aktiviert das Script. Wenn `Active` aus ist, verhält sich
das Property wie ein ungescripteter Wert und kann wieder frei editiert werden.

## Default-Scripts

Das `properties()`-JSON kann pro Cell ein `"script"`-Feld enthalten, das ein
Default-Script definiert (analog zum `"default"`-Wert). Ein Element mit einem
Default-Script ist automatisch scriptable (der f(x)-Button wird angezeigt) und
das Default-Script wird beim Erstellen des Elements bzw. beim Laden eines
Projekts als aktives Binding registriert, sofern kein manuell gespeichertes
Script für diese Property existiert.