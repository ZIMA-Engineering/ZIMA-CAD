# Baugruppe

## Bevor Sie beginnen

Bereiten Sie native Bauteile oder Unterbaugruppen vor und erstellen Sie dann eine
Baugruppe (`.asmz`). Jedes eingefügte Vorkommen besitzt eine eigene Identität
und Platzierung.

## Schritte

1. Fügen Sie mit **Komponente einfügen** ein Bauteil oder eine verschachtelte Baugruppe
   hinzu. Prüfen Sie das Vorkommen im Baum.
2. Öffnen Sie die **Eigenschaften** des Vorkommens. Aktivieren Sie ein
   Komponentenreferenzfeld zur Auswahl, wählen Sie seine Geometrie und danach
   die entsprechende Referenz der unmittelbar zugehörigen Baugruppe aus.
3. Wählen Sie die angebotene Verknüpfung und den Wert. Fügen Sie unabhängige
   Referenzen hinzu, bis die vorgesehenen translatorischen und rotatorischen
   Freiheitsgrade verbleiben.
4. Prüfen Sie die Platzierung und bestätigen Sie mit **OK**. **Abbrechen** nimmt
   die ausstehenden Änderungen einschließlich abhängiger Platzierungen zurück.
5. Verwenden Sie **Aktiv**, um die Quelle einer Komponente zu bearbeiten. Ein
   Bauteil bietet Modellierungswerkzeuge; eine Unterbaugruppe bietet ihre eigenen
   Baugruppenwerkzeuge. Kehren Sie mit **Zurück zur Baugruppe** zurück.

## Was Sie prüfen sollten

Nur die unmittelbar zugehörige Baugruppe positioniert eine Komponente.
Aktivieren Sie eine Unterbaugruppe, bevor Sie deren interne Platzierungen
bearbeiten. Die übergeordnete Gesamtszene bleibt als Kontext sichtbar, und
externe Geometrie dient als schreibgeschütztes Referenzmaterial.

Die aktuell berechnete Bauteilgeometrie erscheint in ihren Vorkommen.
**Regenerieren** löst Verknüpfungen und baugruppeneigene Operationen
ausdrücklich neu; das Wechseln zwischen Registerkarten tut dies nicht.
Neue Material-Features werden in Bauteilen erstellt, nicht auf Baugruppenebene.

Siehe [Baugruppenablauf](../../USER_MANUAL.md#basic-assembly-workflow) und
[Baugruppenreferenzen](../../ASSEMBLY_REFERENCES.md).
