# Verrundung und Fase

## Bevor Sie beginnen

Sie benötigen einen berechneten Volumenkörper in einem aktiven bearbeitbaren
Bauteilkörper. Diese Befehle wählen Kanten des tatsächlichen Eingangskörpers an
ihrer Position in der Historie aus.

## Schritte

1. Wählen Sie **Verrundung** oder **Fase**. Erstellung und spätere Bearbeitung
   verwenden dasselbe Eigenschaftenfenster des jeweiligen Befehls.
2. Klicken Sie auf die gewünschten Eingangskanten, um sie hinzuzufügen oder zu entfernen, und prüfen Sie die Liste der gewählten Kanten.
3. Wählen Sie für eine Verrundung einen konstanten Radius R oder variable Radien R1 → R2 und prüfen Sie die Richtung. Wählen Sie für eine Fase A × A, A × B oder A + Winkel; prüfen Sie bei asymmetrischen Werten die Referenzseite.
4. Wählen Sie **ZIMA** oder **OCCT** und prüfen Sie die automatisch ergänzten tangentialen Abschnitte. Das Entfernen einer Zeile entfernt nur diesen Kantenabschnitt; ZIMA wechselt bei einem Berechnungsfehler nicht zu OCCT.
5. Bestätigen Sie mit **OK**, um zu berechnen. **Abbrechen** verwirft die
   ausstehende Änderung.

## Was Sie prüfen sollten

Das Ergebnis sollte den vorgesehenen Kanten und der vorgesehenen Seite folgen.
Beim erneuten Öffnen der Eigenschaften wird die vorangehende scharfkantige
Eingangsgeometrie angezeigt, damit Kanten repariert oder ersetzt werden können.
Ein Doppelklick auf eine Verrundung zeigt ihre Parametermaße an.

Ein fehlgeschlagener erster Klick auf OK meldet den Fehler und lässt den Entwurf
geöffnet. Ein zweiter Klick auf OK ohne Änderungen kann eine strukturell gültige
Definition als ungelöst behalten; dadurch wird ihre Geometrie nicht gültig.
Reparieren Sie sie über die Eigenschaften.

Siehe [Verwendung der Kantenbearbeitung](../../USER_MANUAL.md#fillet-and-chamfer) und
[Parameterregeln](../../EDGE_TREATMENT_COMMANDS.md).
