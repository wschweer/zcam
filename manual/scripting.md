## Scripting

Einige Properties können gescriptet werden. Du erkennst sie am
[<img src="bound-expression.svg" style="width:25px;"/>]()
Button im Inspector. Ein gescripteter Wert wird mit JavaScript berechnet.

Der Button klappt einen Editor auf mit dem du das Script erstellen kannst.

Der Wert im Inspector Eingabefeld kann nach dem Aktivieren des Scripts nicht mehr
editiert werden, was farbig gekennzeichnet wird.

Rückgabewert ist der Wert der angegebenen JavaScript Expression.

```
      12                // gibt 12 zurück
      12*3              // gibt 36 zurück
      2;3               // gibt 3 zurück
      [12,24,8]         // gibt einen Vektor zurück (z.B. für Position)
      {x=12,y=24,z=8}   // alternative Schreibweise für Position
```

Bei Properties, die aus mehreren Werten bestehen, z.B. Position (Vector3d) oder Size (Vector2d)
kann im Scripteditor angegeben werden, welcher Wert zurückgegeben werden soll. Der andere Wert
bleibt dann editierbar. Du kannst so z.B. bei ```Size``` die Breite (x) von der Höhe (y) abhängig
machen.

Script for ```size.All```<br>
Component: All  __Width__ Height
```
      size.y * 0.5            // Breite ist jetzt immer halb so gross wie die Höhe
```

Elemente werden im Script mit ihren Namen benannt. ```project.cad.layer1.rectangle4.fill``` bezeichnet
z.B. das ```fill``` Property in einem Element im Projektbaum.

Der ```Active``` schalter aktiviert das Script. Wenn ```Active``` aus ist, verhält sich das
Property wie ein ungescripteter Wert.
