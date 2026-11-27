# FEM

## Bevor Sie beginnen

Bereiten Sie einen berechneten Volumenkörper in einem Bauteil vor und weisen Sie
geeignete Materialeigenschaften zu. Wählen Sie eine Analyse, die zum
unterstützten physikalischen Modell passt.

## Schritte

1. Deaktivieren Sie den aktuellen Körper, um zur Bauteilwurzel zu wechseln, und wählen Sie **FEM erstellen**. Wählen Sie unter **FEM-Körpereigenschaften** den Quellkörper und die Analyse und bestätigen Sie mit **OK**.
2. Definieren Sie seine Bedingungen: mechanische Lagerungen, Kräfte oder Druck
   für einen mechanischen Fall; unterstützte thermische Bedingungen für
   Wärmeleitung.
3. Erzeugen Sie das Volumennetz ausdrücklich. Prüfen Sie die Netzdiagnose vor
   dem Lösen.
4. Starten Sie die passende Analyse im FEM-Bereich: **Lineare Statik**, **Stationäre thermische Analyse**, **Thermomechanische Analyse**.
5. Wählen Sie je nach Fall Verschiebungs-, Spannungs- oder Temperaturergebnisse.
   Prüfen Sie Extremwerte und platzierte Messpunkte; vergleichen Sie die
   Ergebnisse nach einer Netzverfeinerung.

## Was Sie prüfen sollten

Die mechanische Analyse ist eine isotrope linear-elastische statische Analyse
mit kleinen Verschiebungen. Die thermische Analyse ist stationär. Optionale
Wärmeausdehnung ist eine einseitige Kopplung bei kleinen Dehnungen; der
kombinierte Modus berücksichtigt außerdem Kraft und Druck.

Die Verformungsvergrößerung ändert die Darstellung, nicht die berechnete
Verschiebung. Eine Animation veranschaulicht einen statischen Fall, keine
Dynamik. Kontakt, Plastizität, instationäre Erwärmung und temperaturabhängige
Eigenschaften liegen außerhalb dieses Umfangs.

Bearbeiten Sie Definitionen über die Eigenschaften und berechnen Sie nach
Eingabeänderungen neu. Das native Speichern erhält die unterstützten
FEM-Definitionen und Messreferenzen. Ein erfolgreicher Vernetzungs- oder
Solverlauf allein belegt keine ingenieurtechnische Genauigkeit.

Siehe [FEM-Funktionen und Grenzen](../../FEM_STATUS.md).
