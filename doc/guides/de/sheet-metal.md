# Blech

## Bevor Sie beginnen

Aktivieren Sie einen bearbeitbaren Bauteilkörper und wählen Sie **Blech** über die
Schaltflächen der Arbeitsbereiche. Verwenden Sie für den folgenden Abwicklungsablauf ein
berechnetes Blechmodell mit definierten Flachbereichen und Biegungen.

## Schritte

1. Öffnen Sie **Blecheigenschaften…**. Prüfen Sie die vom Bauteil verwendete
   Materialdicke und den K-Faktor; bestätigen Sie die vorgesehenen Einstellungen.
2. Prüfen Sie das gebogene Modell. Bestätigen Sie Biegerichtungen und
   Materialseiten, bevor Sie den abgewickelten Zustand erstellen.
3. Wählen Sie **Abwickeln**. Behalten Sie alle geeigneten Bereiche bei oder
   aktivieren Sie **Einzelne Elemente auswählen** und wählen Sie die benötigten
   Bereiche im Baum oder in der Ansicht aus.
4. Bestätigen Sie mit **OK** und prüfen Sie die abgewickelte Geometrie. Speichern Sie das Bauteil und verwenden Sie dann den Befehl **DXF** der Blechsymbolleiste für den unterstützten Export der Abwicklung.
5. Verwenden Sie **Zurückbiegen**, wenn die nachfolgende Historie wieder den
   gebogenen Zustand benötigt.

## Was Sie prüfen sollten

Abwickeln und Zurückbiegen sind Historienoperationen; die vorangehenden
Definitionen bleiben verfügbar. Öffnen Sie für Änderungen erneut die
Eigenschaften oder verwenden Sie Rückgängig/Wiederholen für übernommene Aktionen.

Prüfen Sie die Abwicklungsmaße anhand Ihres Fertigungsverfahrens. Ein
standardmäßiger K-Faktor ist keine materialspezifische Kalibrierung.
**Blech aus Körper** erkennt unterstützte tangential verbundene ebene/zylindrische
Hüllen; eine beliebige Freiformumwandlung liegt außerhalb dieses Erkennungswegs.

Siehe [Blech](../../SHEET_METAL.md), [Blechzustände](../../SHEET_STATE_DEVELOPMENT.md)
und [Umwandlung und DXF](../../SHEET_EXCHANGE.md).
