# Mapa zdrojáků hlavního okna

## Rozdělení z 2026-09-11

Původní `cpp/app/assembly_workspace_window.cpp` měl 29 076 řádků.
Po rozdělení obsahuje 52 řádků: konstruktor a destruktor hlavního okna.
299 metod, společné pomocné funkce a místní widgety jsou rozděleny do
39 samostatně překládaných `.cpp` souborů v `cpp/app/workspace` plus původního
souboru s životním cyklem okna. Deklarace třídy a její datové členy zůstávají
v `cpp/app/assembly_workspace_window.hpp`.

Jde o první etapu: přehlednější zdrojové soubory při zachování chování.
Nevzniká nové API příkazů, samostatný proces ani provoz bez GUI. Operace zatím
zůstávají metodami stejné třídy. Rozdělení proto samo neodstraňuje jejich
závislost na stavu okna, dialozích a vieweru.

## Kde hledat změnu

Všechny soubory následující tabulky jsou v
[`cpp/app/workspace`](../cpp/app/workspace).

| Oblast | Soubory |
| --- | --- |
| Konzole a společné příkazy | GUI adaptér `console.cpp`; provádění a datový strom v `modules/command_host`; viz [Konzole CADu](CAD_CONSOLE.md) |
| Akce, nabídky a propojení událostí | `actions.cpp`, `layout.cpp`, `toolbars.cpp` |
| Dokumenty, taby, přepínání druhu dokumentu | `documents.cpp` |
| Parametry, materiál, relace, rodinná tabulka a nastavení | `document_commands.cpp` |
| Ukládání, přejmenování, verze souborů, stav operace a nastavení aplikace | `file_operations.cpp` |
| Import a export | `interchange.cpp` |
| Adaptéry výslovného výpočtu a kontext editace | `calculation.cpp`; výpočty v `modules/workspace/model_calculation` |
| Vložení komponent a regenerace sestavy | `assembly_commands.cpp` |
| Vlastnosti a výběr komponent | `assembly_properties.cpp` |
| Tažení komponent | `assembly_drag.cpp` |
| Vlastnosti těles a Boolean | `body_properties.cpp` |
| Historie Partu, přesuny, potlačení a mazání | `part_history.cpp` |
| Vlastnosti základních prvků | `primitive_properties.cpp` |
| Vlastnosti Sweep a profily drah | `sweep_properties.cpp` |
| Konstrukční objekty, body a osy | `construction_properties.cpp` |
| Výběr referencí a dočasná viditelnost počátků | `reference_selection.cpp` |
| Zaoblení, zkosení, skořepina a cílové plochy vysunutí | `edge_treatment.cpp` |
| Zobrazení parametrických kót a jejich rozložení | `dimension_display.cpp` |
| Přímá editace kóty v pohledu | `dimension_edit.cpp` |
| Natočení pohledu a orientační dialog | `orientation.cpp` |
| Klávesy, převod paprsku do aktivního výskytu, Undo/Redo | `view_events.cpp` |
| Obnova scény a její výběrový kontrakt | `scene.cpp` |
| Strom dokumentů, historie a skici | `tree.cpp` |
| Kontextové nabídky, aktivace a otevření zdroje komponenty | `context_menu.cpp` |
| Vlastnosti skici, spline a textu | `sketch_properties.cpp` |
| Aktivní skica, pracovní kopie, externí reference a ukončení editace | `sketch_document.cpp` |
| Spuštění a ukončení nástrojů skicáře, trim a mirror | `sketch_tools.cpp` |
| Zachytávání bodů a geometrické odvozování | `sketch_snapping.cpp` |
| Vazby geometrie skici | `sketch_constraints.cpp` |
| Tažení bodů, referencí a kót skici | `sketch_drag.cpp` |
| Mazání geometrie, konstrukční režim a odstranění vazby | `sketch_edit.cpp` |
| Vytváření a vlastnosti kót skici | `sketch_dimensions.cpp` |
| Interaktivní tvorba geometrie a její náhledy | `sketch_preview.cpp` |
| Vlastnosti a náhled odsazení | `sketch_offset.cpp` |
| Společné pomocné funkce | `geometry_helpers.cpp`, `tree_helpers.cpp`, `sketch_helpers.cpp`, `edge_preview_helpers.cpp` |

Největší celky zatím zůstávají `layout.cpp` a `scene.cpp`: obsahují původní
rozsáhlé metody vytvoření rozhraní a obnovení scény. Jejich další dělení uvnitř
metod je samostatná úprava, aby se nepomíchalo s ověřitelným přesunem kódu.

## Hranice a pravidla pro další práci

- `workspace_internal.hpp` je soukromá implementační hlavička: společné
  závislosti, deklarace pomocných funkcí, typy a šablony. Nepatří do veřejného
  API ani do modelových a kernelových modulů. Soustředěné závislosti jsou
  mezikrok; hlavička zatím není minimálním rozhraním nezávislých modulů.
- Pomocné funkce používají `zima::app::workspace_detail` a mají právě jednu
  definici. Výchozí argumenty jsou v deklaracích; do `.cpp` se neopakují.
- Místní widgety jsou v anonymním namespace souboru, který je používá:
  například inline editory v `dimension_edit.cpp`, dialog nového dokumentu
  v `documents.cpp` a průběh operace v `layout.cpp`.
- Soubory jsou explicitně uvedeny v `sources.cmake`, které načítá hlavní
  `cpp/CMakeLists.txt`. Nepoužívat glob ani vkládání `.cpp` přes `#include`.
- Novou funkci přidávat k odpovídajícímu tématu. Do souboru životního cyklu
  nevracet obsluhu jednotlivých příkazů.
- Změny modelování, referencí, transakcí a regenerace dělat odděleně od
  mechanických přesunů. Platí všechna pravidla projektu v `AGENTS.md`.

Formáty dokumentů, start šablony, konfigurační hodnoty, pravidla umístění,
identita geometrie a výpočetní algoritmy se v této etapě nemění.

## Ověření přesunu

Výchozí stav je commit `35f7509`. Kontrola porovnala všech 386 těl metod,
pomocných funkcí, šablon a místních typů v původním a rozděleném kódu.
Po sjednocení konců řádků jsou totožná; žádné nechybí ani nepřibylo.
Samostatná kontrola signatur potvrdila stejné parametry a návratové typy
(výchozí argumenty se pouze přesunuly do deklarací).
Windows Release se sestavuje standardním skriptem projektu. Úplné ověření:

```powershell
./tools/build-windows.ps1 -Configuration Release -RunTests
```

Výsledek ověření: Windows Release sestaven. První úplný běh měl 47/49
úspěšných testů. Dvě selhání byla v testovacích předpokladech a jejich oprava
je oddělená od přesunu aplikačního kódu:

- Test počátku očekával anglické `Point` po zavedení českého `Bod`. Nyní
  kontroluje překládaný název i stabilní sémantický klíč `origin:point`.
- Test JPEG porovnával snímek s velikostí pohledu až po změně stavové zprávy,
  která mohla rozšířit okno. Nyní měří pohled při potvrzení exportu; zachovává
  přesnou kontrolu rozměrů obrázku i nezměněné kamery.

Oba opravené testy následně prošly (pracovní okno 87,91 s, obnova sestavy
3,14 s). Všech 49 scénářů je tak ověřeno; ostatních 47 se po úpravě pouze
těchto dvou testovacích podmínek neopakovalo.

Další etapa může oddělovat konkrétní operace od oken a připravovat společné
příkazy pro GUI, konzoli a AI. Tato mapa není autorizací k implementaci celé
budoucí architektury ani k plošnému auditu Undo/Redo.

První oddělené operace jsou popsány v [DOCUMENT_OPERATIONS.md](DOCUMENT_OPERATIONS.md):
nativní ukládání a dokumentové Undo/Redo nyní sídlí v modulu workspace bez Qt.

Načítání `.prtz`/`.asmz`/`.drwz` a tvorba podle config šablon nyní používají
`modules/workspace/native_documents`. V `documents.cpp` zůstává interakce a
aktivace; v `geometry_helpers.cpp` pouze převod aplikačních settings pro továrny.

Výslovná regenerace nyní sídlí v `modules/workspace/model_calculation` a
čisté pomocné funkce uložených referencí v `modules/workspace/part_references`.
`calculation.cpp` převádí stav editace na datovou politiku výpočtu;
`view_events.cpp` a `assembly_commands.cpp` zajišťují uživatelskou interakci
před/po zavolání společné operace. Podrobnosti a ověření jsou v
[DOCUMENT_OPERATIONS.md](DOCUMENT_OPERATIONS.md).


Katalog a vykonávání příkazů nyní sídlí v `modules/command_host/src/host.cpp`.
`model_tree.cpp` čte datovou hierarchii bez widgetů a bez výpočtu geometrie.
`workspace/console.cpp` zajišťuje panel, snímek interakce a obnovu zobrazení;
`documents.cpp::finish_document_switch` sdílí obnovu po GUI/konzolovém Open/New.
Host a jeho testovací program nelinkují Qt. Následující etapa přidává samostatný
`zima-cad-cli`: `cli/main.cpp`, `cli/runner.cpp`, `cli/settings.cpp`; viz
[CAD_COMMAND_LINE.md](CAD_COMMAND_LINE.md).

První modelovací transakci sdílí okno kvádru a příkazy `box.create/get/set`.
`modules/workspace/box_operations` validuje a atomicky vypočítá/uloží kvádr;
`modules/command_host/src/box_commands.cpp` převádí argumenty a výsledek.
V `primitive_properties.cpp` zůstává dialog, rollback a náhled; OK pro kvádr
volá společnou operaci. Ostatní druhy primitivů zatím používají dosavadní cestu.
Solver a smlouva umístění nejsou změněny. Podrobnosti: [CAD_CONSOLE.md](CAD_CONSOLE.md).
