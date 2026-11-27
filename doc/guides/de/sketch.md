# Skizze

## Bevor Sie beginnen

Aktivieren Sie einen bearbeitbaren Körper. Öffnen Sie **Skizze** in den
Eigenschaften des übergeordneten Features, nachdem Sie dessen Platzierung und
Arbeitsebene gewählt haben.

## Schritte

1. Wählen Sie das Geometriewerkzeug, beispielsweise **Rechteck**, **Kreis** oder
   **Polylinie**. Bestätigen Sie die Definitionspunkte mit der linken Maustaste.
2. Verwenden Sie vorhandene Punkte und angebotene Bedingungen, um exakte
   Verbindungen herzustellen. Kennzeichnen Sie Hilfsgeometrie als
   Konstruktionsgeometrie, damit sie nicht Teil des Profils wird.
3. Fügen Sie Maße hinzu. Geben Sie nach dem Platzieren eines neuen steuernden
   Maßes seinen Wert ein und drücken Sie die Eingabetaste. Referenzmaße und
   gesperrte Maße bleiben schreibgeschützt.
4. Wählen und ziehen Sie einen Punkt, um die verbleibenden Freiheitsgrade zu
   prüfen. Ändern Sie ein Maß erneut, um sicherzustellen, dass sich das Profil
   wie beabsichtigt verhält.
5. Wählen Sie **OK**, um zu den Feature-Eigenschaften zurückzukehren, und dort erneut **OK**, um das Feature zu übernehmen. **Abbrechen** verwirft die aktuelle Skizzenbearbeitung.

## Was Sie prüfen sollten

Das Profil muss zum verwendenden Befehl passen: Ein Volumenkörper benötigt
normalerweise einen geschlossenen Bereich, während unterstützte Dünn-Operationen
eine offene Kette verwenden können. Externe Referenzen bleiben schreibgeschützt.
Widersprüchliche Bedingungen oder Maße werden zurückgewiesen; vollständig
bestimmte Geometrie kann sich nicht frei bewegen.

Siehe [Skizzenmodus](../../USER_MANUAL.md#sketch-mode) und
[Bemaßung und Auswahl](../../USER_MANUAL.md#dimensions-and-selection).
