# CAM

## Bevor Sie beginnen

Bereiten Sie berechnete Rohteil- und Zielkörper in einem Bauteil sowie bei Bedarf
Spannmittelkörper vor. Deaktivieren Sie den aktuellen Körper, um **CAM erstellen**
zu erreichen.
Bereiten Sie für Taschen- oder Konturfräsen eine vorhergehende native Profilskizze vor.

## Schritte

1. Wählen Sie **CAM erstellen**. Wählen Sie Rohteil, Ziel und Spannmittel aus und
   platzieren Sie dann das Arbeitskoordinatensystem der Aufspannung.
2. Öffnen Sie **Maschine** und **CAM-Werkzeuge**. Prüfen Sie Verfahrgrenzen,
   Werkzeuggeometrie, Schnittbedingungen und den Versatz des Werkstücknullpunkts.
   Definieren Sie bei Bedarf unter **Spannskizzen** die Kontakt- und
   Backenoberkantenskizzen mit ihren orientierten Seiten.
3. Wählen Sie unter **Bearbeitungsoperationen** **Planfräsen hinzufügen**,
   **Tasche hinzufügen**, **Kontur hinzufügen** oder **Manuelle Operation hinzufügen**.
   Klicken Sie für eine Tasche oder Kontur auf das Referenzfeld **Bearbeitungsprofil**
   und wählen Sie eine vorhergehende Skizze im Baum oder in der Ansicht.
   Legen Sie Werkzeug, obere und untere Ebene, Zustelltiefen, Aufmaße,
   Sicherheitsabstand und Werkzeugbahnrichtung fest. Legen Sie bei einer Tasche
   **Seitliche Zustellung** fest; wählen Sie bei einer Kontur **Innen** oder **Außen**.
   Prüfen Sie die Operationsreihenfolge. **OK** im Editor
   überträgt Änderungen in den Entwurf der Aufspannung; erst **OK** in deren
   Eigenschaften übernimmt sie endgültig.
4. Wählen Sie **CAM berechnen**. Prüfen Sie im Fenster **Generisches NC-Programm** das Programm, die zugehörige Werkzeugbewegung und das berechnete Restmaterial.
5. Prüfen Sie Kollisions- und Spannberichte. Verwenden Sie **Generischen NC-Text exportieren…** nur unter Beachtung seiner angegebenen Bedeutung als generisches Programm.

## Was Sie prüfen sollten

Gelbe Pfade stellen Schnittvorschub dar; gestrichelte rote Pfade stellen
Eilgangbewegungen dar. Die ausdrücklich gestartete Berechnung prüft
Maschinengrenzen und simuliert den vom Werkzeug überstrichenen Materialbereich;
das schrittweise Durchgehen des Ergebnisses berechnet kein Material neu.

Taschen- und Konturfräsen ist dreiachsige 2,5D-Bearbeitung geschlossener nativer
Skizzenbereiche parallel zur XY-Ebene der Aufspannung, einschließlich Inseln und
getrennter Schleifen. Es erfordert einen zylindrischen Schneidbereich mit flacher
Stirn. Der Einstieg erfolgt durch schneidendes Eintauchen; Verbindungsbewegungen
ziehen auf Sicherheitshöhe zurück. **Bahntoleranz** begrenzt die Approximation
nichtkreisförmiger Kurven; Geraden und Kreisbögen behalten analytische Bahnen.

Spannskizzen definieren orientierte Ausschluss-/Kontaktbereiche und Prüfungen
der Backenoberkante. Tatsächliche Spannmittelkörper bleiben zusätzliche
Hindernisse. Ein Schnitt von Schneide oder Schaft mit einem Spannmittel oder
Ausschlussvolumen ist eine Kollision. Materialabtrag unterhalb der Backenoberkante
und Kontaktverlust erzeugen getrennte Warnungen; verbleibender geometrischer
Kontakt belegt keine ausreichende Haltekraft. Ergebnisse mit Kollisionen können
nicht als Rohteil für eine spätere Aufspannung dienen.

**Abbrechen** in der Aufspannung verwirft ausstehende Änderungen. Das erneute
Öffnen eines aktuellen Ergebnisses wiederholt die Simulation nicht. Manuelle
Operationen unterstützen lineare Bewegungen und Kreisbewegungen in XY.
Steuerungspostprozessoren, Drehen, 4-/5-Achs-Bewegungen, adaptiver oder rampenförmiger
Einstieg, automatische Platzierung von Spannmitteln aus der Bibliothek und
Werkzeugkonvertierung aus beliebigen Volumenkörpern bleiben nicht verfügbar.
Ein generischer NC-Export ist kein unmittelbar für die Maschinensteuerung geeigneter Code.

Siehe [CAM-Ablauf](../../USER_MANUAL.md#first-stage-cam-workflow) und
[aktueller Umfang des Profilfräsens](../../CAM_BODY_DESIGN.md#profile-milling-stage-2026-11-26).
