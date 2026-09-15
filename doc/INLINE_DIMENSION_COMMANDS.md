# Přímé hodnoty kót a společné příkazy

Přímé potvrzení hodnoty ve View používá stejné modelové operace jako
Vlastnosti a CLI. Názvy příkazů a JSON polí zůstávají anglické.

| Úprava ve View | Příkaz | Společná operace |
| --- | --- | --- |
| Katalogový rozměr závitu otvoru | `opening.set`, pole `designation` | `select_opening_thread_size` a `commit_opening` |
| Offset komponentové vazby | `component.set`, pole `placement_references` | `prepare_component_edit` a `commit_component_properties` |
| Rádius bodu samostatné 3D křivky Part/Assembly | `construction.set`, pole `radius_mm` | `commit_construction` |
| Rádius bodu vložené dráhy Sweep 3D | `sweep3d.set`, pole `path.points` | `commit_sweep` |

Bod vložené dráhy se mění přes vlastnící Sweep, nikoli příkazem pro
samostatnou konstrukci. Jeho ID a ID ostatních bodů se zachovají.
Křivka je polyline se zapnutým zaoblením; příliš velký rádius se zamítne
bez změny dokumentu. Hodnoty délky jsou v mm, úhlové offsety ve stupních.

Katalog zachová vlastní průměr profilu a společná pravidla délky otvoru
včetně výběhu závitu. Komponentové vazby zachovají meze a zámek hodnoty,
původní reference a vlastnictví bezprostřední sestavy. Shodná hodnota
nepřidává krok Undo. Samostatná konstrukce nepřepočítává těleso;
změna otvoru nebo vložené dráhy provede výslovný výpočet příslušné operace.

Je-li otevřené okno Vlastnosti, přímá editace nadále upravuje jeho návrh.
O potvrzení rozhoduje jeho OK/Zrušit. Tento krok nezavádí nové manipulátory
pro změnu hodnot tažením.

## Oprava zobrazení v Assembly

Nová regrese odhalila, že samostatná 3D křivka v Assembly uchovávala
rádius, ale inspekce křivky nepřidávala jeho kótu do View. Assembly nyní
používá stejnou funkci `curve3d_radius_dimensions` jako Part. Kóta se
odvodí z uložené křivky bez OCCT a přidá pouze pro aktivní dokument.
Při zobrazení v nadřazené sestavě prochází běžným převodem scény výskytu.

## Ověření

`console_ui_verification.cpp` potvrzuje skutečný číselný editor a
rozbalovací katalog. Porovnává celé uložené `.prtz` / `.asmz` po GUI
a po stejném CLI zásahu; ověřuje Undo, shodné hodnoty, neplatný rádius,
meze offsetu a zámek. Dvakrát vložená stejná podsestava ověřuje, že
se kóta nabízí právě jednou, na aktivním výskytu, a editace potvrzuje
správný zdrojový dokument. Dosavadní testy pracovního okna dále pokrývají
editaci s otevřenými Vlastnostmi, Cancel a opětovné zobrazení kót.

Formáty a startovní šablony se nemění. Katalog zůstává na 296 příkazech.
Výsledky aktuální sady jsou uvedeny v
[CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).

Podrobné argumenty:
[OPENING_COMMANDS.md](OPENING_COMMANDS.md),
[COMPONENT_PROPERTY_COMMANDS.md](COMPONENT_PROPERTY_COMMANDS.md),
[CONSTRUCTION_COMMANDS.md](CONSTRUCTION_COMMANDS.md),
[SWEEP_COMMANDS.md](SWEEP_COMMANDS.md).
