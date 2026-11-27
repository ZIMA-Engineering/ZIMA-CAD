# Sweep

## Bevor Sie beginnen

Aktivieren Sie einen bearbeitbaren Bauteilkörper. Wählen Sie je nach benötigtem
Pfad **2D-Sweep**, **3D-Sweep** oder **Helix-Sweep**; dies sind separate Werkzeuge.

## Schritte: 2D-Sweep

1. Platzieren Sie den Container und wählen Sie seine Pfadebene **XY / XZ / YZ**.
2. Öffnen Sie die **Bahnskizze…**. Zeichnen Sie einen zusammenhängenden offenen Pfad oder eine unterstützte geschlossene Kette. Ein offener Pfad darf außerhalb des Ursprungs beginnen; seine definierte Richtung bestimmt die erste Station. Beenden Sie die Skizze.
3. Öffnen Sie die **Skizze** der ersten Station und definieren Sie ein nicht
   leeres Profil. Seine Ebene steht senkrecht zur Pfadtangente.
4. Fügen Sie weitere Profile nach Bedarf hinzu. Eine leere spätere Station übernimmt das vorherige Profil; die Abschlussstation eines geschlossenen Pfads verwendet das erste Profil. Ein anderes vorgegebenes Profil erzeugt einen Übergang.
5. Wählen Sie **Körper**, **Dünnwandiges Formelement** oder **Fläche**, prüfen Sie die
   Materialoperation und bestätigen Sie mit **OK**. **Abbrechen** verwirft
   ausstehende Änderungen.

## Was Sie prüfen sollten

Ein dünnwandiges Ergebnis benötigt eine Dicke und eine Seite; ein Flächenergebnis hat keine Endkappen und ist nur additiv. Verwenden Sie beim 3D-Sweep **Punktreihenfolge**, um Profilbegrenzungen
einander zuzuordnen. Der Helix-Sweep benötigt seinen Grundkreis/Startpunkt,
die radiale Führung, die Steigung und den Querschnitt.

**Eigene Genauigkeit** ändert die Approximationstoleranz des Körpers
in Millimetern, nicht nur die Darstellungsqualität. Behalten Sie die für Ihr
Modell erforderliche Genauigkeit bei.

Siehe [2D-Sweep](../../SWEEP_2D.md), [3D-Sweep](../../3D_CURVE_AND_SWEEP.md) und
[Helix-Sweep](../../HELICAL_SWEEP.md).
