# Umstrukturierung der Layer

Der Projekt Tree stellt die Hierarchie der Elemente dar
wie sie auf dem Canvas dargestellt werden.
Die Zweige des Trees bilden Gruppen, die gemeinsam bewegt
werden können, da jeder Knoten ein eigenes lokales Koordinatensystem
hat und sich im Koordinatensystem des Parent bewegt.

Die Zuordnung von LaserLayer zu den einzelnen Elementen erfolgt
bislang nur über Layer die mit einem LaserLayer verbunden sind.
Alle Kinder eines Layer wurden dem LaserLayer zugeordnet und mit dessen
Parametern für den Laser gerendert.
Diese Zuordnung kann in Konflikt mit der Gruppierung der Elemente
kommen. Wir ändern sie deshalb.

Ein Layer heisst jetzt "Group" und dient nur noch der Display Gruppierung
von Elementen. Er besitzt ein lokales Koordinatensystem wie alle Element3d
aber er hat nichts anzuzeigen.
Jedes andere Element wie z.B. Polygon kann jedoch auch eine
Gruppe bilden.

LaserLayer hat keinen Verweis mehr auf irgendwelche Layer. Das
entsprechende Property entfällt.

Jedes Element im Project Tree ab und inclusive Cad hat einen Verweis auf
einen LaserLayer. Der Verweis kann leer sein. Dann erbt das Element den
LaserLayer von seinem Parent. Dieser LaserLayer Verweis ist ein neues
Property aller Element3d.

Wenn Cad einen LaserLayer gesetzt hat und alle kinder von Cad nicht,
dann werden sie alle mit dem Cad LaserLayer gerendert.

Cam Daten werden wie folgt gerendert: Es wird über alle LaserLayer iteriert
und jeweils alle zu rendernden Elemente die diesem LaserLayer zugeordnet
sind werden bearbeitet.
