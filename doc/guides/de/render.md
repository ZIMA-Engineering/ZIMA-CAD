# Rendern

## Bevor Sie beginnen

Berechnen Sie die Quellvolumenkörper in einem Bauteil. Legen Sie ihre Farben und
ihr Erscheinungsbild fest und richten Sie dann eine Ausgangsansicht ein.

## Schritte

1. Aktivieren Sie die Bauteilwurzel und wählen Sie **Rendering erstellen**.
2. Behalten Sie **Sichtbare Körper** bei oder verwenden Sie **Ausgewählte Körper**,
   um Quellen ausdrücklich auszuwählen. Legen Sie Bildabmessungen und
   Abtastqualität fest und bestätigen Sie mit **OK**.
3. Fügen Sie bei Bedarf Kameras und Lichter hinzu oder bearbeiten Sie sie.
   Wählen Sie die aktive Kamera in den Eigenschaften des Render-Körpers aus.
4. Wählen Sie **Bild rendern**. Die separate Render-Registerkarte zeigt den
   Fortschritt und das Ergebnis; **Render-Galerie** öffnet diese Registerkarte
   erneut.
5. Exportieren Sie das fertige Bild als PNG oder JPEG. Verwenden Sie **Im Bauteil speichern**, wenn Sie die Momentaufnahme zusätzlich im nativen Bauteil
   behalten möchten.

## Was Sie prüfen sollten

Das Rendern verwendet eine Momentaufnahme bereits berechneter Geometrie.
Änderungen an der Quelle überschreiben kein älteres gerendertes Bild. Beim
Schließen der Render-Registerkarte läuft der Auftrag weiter; **Rendering anhalten**
bricht ihn ab, und das Schließen des zugehörigen Dokuments beendet den Auftrag.

Mehr Abtastwerte können sichtbares Rauschen verringern. Gespeicherte Bilder
bleiben historische Aufzeichnungen und vergrößern das Dokument. Der anfängliche
Renderer unterstützt Szenen aus Bauteilvolumenkörpern; Baugruppenszenen,
Animation, Texturen und Glasbrechung liegen außerhalb dieses Umfangs.

Siehe [Rendern](../../RENDER.md).
