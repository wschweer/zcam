# 9-Punkt Galvo Kalibrierung

Implementiere die 9-Punkt Galvo Kalibrierung.
Der User muss den "Galvo Test 9" auf Laser Papier brennen und die Länge
von 12 Linien ausmessen und die Ergebnisse in einer Tabelle Eintragen.

Es soll ein Popup erscheinen mit folgenden Komponenten:

- Eine Titlezeile mit dem Namen des aktuellen (zu korrigierenden) Lasers
  (Machine)

- eine Grafik, die die 12 horizontalen und vertikalen Linien bezeichnet
  und dem Nutzer eine Zuordnung
  zu den 12 Eingabefeldern für die Längenwerte ermöglicht.
  Die Grafik soll direkt in Qml erstellt werden (Canvas Objekt?)

- neben der Grafik 12 Eingabefelder für die 12 Linienlängen

- Eine Button-Reihe mit den Buttons "Change Calibration" und "Abort".
  Change Calibration setzt die ermittelten Korrekturwerte für
  die Linsenkorrektur.

Zur Berechnung der Korrekturwerte:

Ermittel werden soll galvoScale und galvoBulk in machine.h
Wenn alle gemessenen Linie gleich der Field-Breite * .5 sind (Field
ist quadratisch), dann ist scale = 1.0 und bulk = 0.0.

Das äussere Rechteck der gemessenen Grafik entspricht der konfigurierten
Field Grösse des Lasers. Der Wertebereich des galvos is -32767 -> 32767.
Die Fieldgrösse ist -25800 -> 25800.

Die Kissen/Tonnen abweichung berechnet sich wie folgt:
```
            // Berechne die 65x65 Korrekturmatrix
            // für einen -32767 +32767 Scanbereich.
            // Jedes Feld enthält die Abweichung von der korrekten
            // Position.

            double kx = galvoBulge().x();
            double ky = galvoBulge().y();
            int scale = 0x10000 / 64;

            for (double y = -32; y <= 32; ++y) {
                  for (double x = -32; x <= 32; ++x) {
                        // berechne die nicht-lineare Verzerrung
                        double r  = x * x + y * y;
                        // bei x == 0 und y == 0 sind die Korrekturwerte 0
                        int corrX = kx * r * x;
                        int corrY = ky * r * y;
                        .
                        .
                        }
                  }
            }
```

Die vom Anwender ermittelten Werte werden gemittelt und durch Rückrechnung
nach obiger Formel sollen Korrekturwerte ermittel werden, die die ermittelten
Werte möglichst gut abdecken.
Implementiere die Berechnungen in c++.
