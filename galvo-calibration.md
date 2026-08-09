# **Galvo-Scanner und Linse: Geschwindigkeits-Empfehlungen**

[https://gemini.google.com/app/45d26a6f2616262c](https://gemini.google.com/app/45d26a6f2616262c)

*User prompt: Welche geschwindigkeiten werden für einen SG7110A Galvo Spiegel und 254mm Linse empfohlen für travel und max mark?*

Response: Für einen **SG7110A Galvo-Scanner** in Kombination mit einer **254-mm-F-Theta-Linse** gelten folgende Richtwerte für Software-Einstellungen (z. B. EzCAD, LightBurn):

| Parameter | Empfohlener Praxisbereich | Max. Grenzwert (Hardware) |
| :---- | :---- | :---- |
| **Travel / Jump Speed** *(Positioniergeschwindigkeit)* | **4.000 – 8.000 mm/s** | **10.000 – 12.000 mm/s** |
| **Mark Speed** *(Markiergeschwindigkeit)* | **500 – 2.500 mm/s** | **5.000 – 7.000 mm/s** |

### **Einflüsse und technische Zusammenhänge**

> * **Hebelwirkung der 254-mm-Linse:** Gegenüber einer standardmäßigen 160-mm-Linse verlängert die F254-Linse den optischen Weg um den Faktor **1,59** (254/160≈1,59). Bei gleicher Winkelgeschwindigkeit der Galvo-Spiegel bewegt sich der Laserstrahl auf der Materialoberfläche entsprechend 1,59-mal schneller.
> * **Nutzung der Max. Markiergeschwindigkeit:** Die Spiegel-Mechanik des SG7110A schafft theoretisch bis zu 7.000 mm/s. Ab ca. **4.000−5.000 mm/s** nimmt jedoch die Konturtreue (Verrundung von Ecken, Überschwingen der Galvo-Motoren) spürbar zu. Für saubere Schriften und feine Vektoren sind Arbeitsgeschwindigkeiten bis 2.500 mm/s optimal.
> * **Energiedichte auf dem Material:** Die F254-Linse hat ein größeres Arbeitsfeld (meist 175×175 mm bis 180×180 mm), fokussiert den Strahl jedoch auf einen größeren Spot-Durchmesser. Dadurch sinkt die Energiedichte (Leistung pro Fläche). Um tief genug zu gravieren oder dunkle Anlassfarben zu erzeugen, muss die Markiergeschwindigkeit deutlich geringer angesetzt werden als bei kleineren Linsen.

### **Empfohlene Begleitparameter (Delays)**

Aufgrund der höheren Massenträgheit und der größeren Distanz bei der F254-Linse sollten die Verzögerungszeiten in der Software leicht angepasst werden, um Linienüberschneidungen oder offene Konturen zu vermeiden:

> * **Jump Delay:** 350−500 *μ*s (höhere Sprunggeschwindigkeiten verlangen längere Pausen, damit die Spiegel zur Ruhe kommen).
> * **Laser On / Off Delay:** ca. 100−200 *μ*s (zur Vermeidung von Ausbrennpunkten am Anfang/Ende einer Vektorlinie).
> * **End Delay:** 200−400 *μ*s.

**Sources**

> 1. [hurrySCAN10 Dig 1064nm 254mm 112466 e | PDF | Image Scanner | Laser \- Scribd](https://www.scribd.com/document/831259268/hurrySCAN10-dig-1064nm-254mm-112466-e)
> 2. [InScript Software](https://www.taechueng.co.kr/uploads/media/inscript_software_manual_en.pdf)
> 3. [Sino Galvo SG7110 SG7310E High Speed 10000mm Galvanometer Scanner Galvo Scanner Fot Fiber Laser Marking Machine \- AliExpress](https://he.aliexpress.com/i/1005011810586249.html)

---

*User prompt: wie berechne ich den jump delay (nach einem jump?)*

Response: Der **Jump Delay** lässt sich physikalisch/theoretisch berechnen, wird in der Praxis der Lasergravur jedoch meist **empirisch über ein Testmuster** kalibriert, da die mechanische Trägheit der Spiegel vom spezifischen Kopf abhängt.

### **1\. Physikalische Berechnung (Theorie)**

Die benötigte Verzögerungszeit nach einem Sprung setzt sich aus der rein rechnerischen Fahrzeit für die Distanz und der mechanischen Einschwingzeit (*Settle Time*) des Galvo-Motors zusammen:

Jump Delay (*μs*)=(Jump Speed (mm/s)Sprungdistanz (mm)​×1.000.000)+*t*settle​

> * **Jump Speed:** Die eingestellte Verfahrgeschwindigkeit (z. B. 5.000 mm/s).
> * ***t*****settle​ (Einschwingzeit):** Zeit, die der Motor benötigt, um nach dem Abbremsen starr auf der Zielposition einzurasten (beim SG7110A typischerweise 200−400 *μs*).

### **2\. Berechnung in der Steuerung (z. B. EzCAD)**

Laser-Softwares wie EzCAD nutzen intern meist zwei gekoppelte Variablen, um die Verzögerung je nach Wegstrecke dynamisch anzupassen:

Gesamte Verzo¨gerung (*μs*)=(Sprungdistanz×Jump Dist TC)+Jump Delay

> * **Jump Delay (Jump Position TC):** Der feste Basis-Delay, den der Spiegel nach jedem Positionierstopp benötigt.
> * **Jump Dist TC (Time Constant):** Ein entfernungsabhängiger Korrekturfaktor für sehr lange Sprünge über das gesamte Sichtfeld der F254-Linse.

### **3\. Praktische Ermittlung (Kalibrierungs-Methode)**

Da Berechnungen auf dem Papier die Spiegelmasse und die Hebelwirkung der F254-Linse nicht exakt abbilden können, wird der optimale Wert in folgenden Schritten ermittelt:

> 1. **Testmuster erzeugen:** Gravieren Sie eine Schraffur (Hatch) oder zwei versetzte, parallele Linien, zwischen denen der Strahl ohne Laserleistung springen muss.
> 2. **Ergebnis bewerten:**
   * **Jump Delay zu NIEDRIG:** Der Laser schaltet ein, während der Spiegel noch bremst oder nachschwingt. Symptome sind gebogene/hakenförmige Linienanfänge („Fischhaken“) oder unvollständige Ecken.
   * **Jump Delay zu HOCH:** Der Spiegel steht bereits vollkommen still, bevor der Laser zündet. Das Schnittbild ist perfekt, aber der Gravierprozess dauert unnötig lange.
> 3. **Feineinstellung:** Starten Sie bei 300 *μs* und erhöhen Sie den Wert in 50-*μs*\-Schritten, bis der Linienanfang absolut gerade und scharfkantig ist.

**Sources**

> 1. [6.0 High-speed Fly Laser Mark Control System](https://v6-file.globalso.com/file_manage/2393/20241204/6-0-high-speed-fly-laser-manual.pdf)
> 2. [Instruction for Delay Parameters in Ezcad Ezcad3 \- Linxuan Laser](https://www.linxuanlaser.com/instruction-for-delay-parameters-in-ezcad/)
> 3. [Step response time \- Scanlab](https://www.scanlab.de/en/service/glossary/step-response-time)

---

*User prompt: Welche Pausen muss ich machen, wenn der Spiegel beim markieren die Richtung wechselt?*

Response: Wenn der Galvo-Spiegel während des Markierens die Richtung ändert (z. B. an den Ecken eines Polygons oder bei Richtungswechseln einer Gravurlinie), ist der entscheidende Parameter der **Corner Delay** (in EzCAD oft *Polygon Delay* oder *Ecken-Verzögerung* genannt).

Für das System aus **SG7110A** und der **F254-Linse** gelten dafür folgende Richtwerte:

> * **Empfohlener Corner Delay:** **100−250 *μs*** *(Guter Startwert: 150 μs)*

### **Warum diese Pause nötig ist**

Ein Galvo-Spiegel besitzt eine mechanische Masse. Ändert er bei voller Markiergeschwindigkeit abrupt die Richtung, kann der Motor nicht winkelgenau ohne Verzögerung abbiegen. Der Corner Delay weist die Steuerung an, an den Ecken kurz innezuhalten, damit die Mechanik der Bewegungsrichtung folgen kann.

### **Das Fehlerbild beim Einstellen**

Anhand des Gravurbilds an scharfen 90∘\-Ecken lässt sich der Wert exakt justieren:

> * **Corner Delay zu NIEDRIG (\<100 *μs*):**
  * **Symptom:** Die Ecken werden abgerundet („verwaschen“) oder der Laser zieht Diagonallinien über die Ecke hinweg, weil der Spiegel noch in der Kurve ist, während der Laser bereits weiterfeuert.
> * **Corner Delay zu HOCH (\>300 *μs*):**
  * **Symptom:** An den Ecken entstehen deutliche Einbrennpunkte oder Löcher. Der Laser bleibt zu lange auf der gleichen Stelle stehen, während die Spiegel die Richtung wechseln.

### **Verwandte Parameter bei Richtungswechseln**

> 1. **Mark Delay (End-Verzögerung einer Vektorlinie):**
   * **Empfehlung:** **100−200 *μs***
   * Stoppt die Markierbewegung am Ende eines Vektorselements kurz, bevor der Laser abgeschaltet wird, um unvollständige Endpunkte zu verhindern.
> 2. **Corner Speed / Minimum Speed Limit (falls von der Software unterstützt):**
   * Reduziert die Gravurgeschwindigkeit in engen Kurven automatisch, damit der Laser im Eckbereich nicht übersteuert.

Gravierst du aktuell feine Vektorgrafiken mit vielen scharfen Ecken oder eher eine dichte Flächenschraffur (Hatch)?

---
