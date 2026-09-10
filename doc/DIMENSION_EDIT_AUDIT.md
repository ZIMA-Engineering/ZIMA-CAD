# Audit editace kót (2026-09-10)

Vstup je změna číselné hodnoty existující kóty. Výstupem musí být odpovídající
geometrie a uložený stav, případně odmítnutí změny bez zápisu. Samotné přepsání
popisku nebo hlášení úspěchu není důkazem přepočtu.

## Opravené cesty

- Přímá editace odsazení vnitřního profilu Vytažení/Rotace aktualizuje také
  `Sketch::plane_offset`. Před výpočtem používá existující řešení polohy,
  aby se do tělesa nepromítla stará poloha počátku skici.
- Zpětný úhel oboustranné Rotace má obsluhu i při přímé editaci ve View.
- Otvor má přímé obsluhy hloubky, průměru hladkého otvoru, délky závitu,
  hloubky a úhlu sražení a úhlu vrtací špičky. Katalogový průměr závitu
  zůstává měřenou hodnotou; označení závitu používá katalogový výběr.
- Shell předává editovanou tloušťku do otevřených vlastností. Dialog bez
  aktivní reference pro umístění se již neobchází přímým zápisem do dokumentu.
  Cancel proto zahodí rozpracovanou změnu kóty.

## Regresní pokrytí

| Cesta | Ověření |
| --- | --- |
| Kvádry, válce, koule, kužely, jehlany, klíny; všechny nabízené rozměry | `zima_cpp_dimension_edits_ui_contract` |
| Posun X/Y/Z, Shell, číselné kóty otvoru/závitu | Stejná matice: View → Properties → Cancel → Properties → OK → uložení/načtení |
| Odsazení roviny profilu, reference počátku, zpětný úhel rotace | `zima_cpp_profile_frame_ui_contract`: dvojklik na skutečný popisek, Enter, kontrola polohy skici a mezí vypočteného tělesa |
| Orientace profilu a otevření vlastností | XY/XZ/YZ, Front/Back a čtyři otočení, dostupnost nenulové kóty po opětovném otevření |
| Kóty vnitřní skici | `ZIMA_VERIFY_PROPERTY_SKETCH_ONLY`: skutečný picker a dvojklik, opakované změny, přechod do Skicáře, OK/Cancel, Part i Assembly |
| Zámky, numerické vstupy a další dialogy | `zima_cpp_ui_contract_tests`, `numeric_value_locks`, `numeric_fields`, `sketcher_contract_tests` |
| Výkresové a měřené kóty, prezentace, sestavy | Existující kontraktní testy Drawing, Measurement, DimensionLayout, Viewer a Assembly |

Matice kontroluje vedle uložené hodnoty také shodu otisku vstupů uloženého
výsledku s aktuálními operacemi dokumentu. Test profilů navíc nezávisle
porovnává prostorové meze tělesa s výpočtem očekávaných vstupů.

Grafické testy spouštět postupně v jedné grafické relaci. Současně otevřená
testovací okna si mohou přebírat fokus. Na Linuxu bez funkčního offscreen
OpenGL je potřeba použít `-platform xcb`.

Toto je konečná matice konkrétních interakčních cest, nikoli důkaz všech
možných kombinací geometrie a vazeb. Zmizení přesné kóty 18 mm z uživatelského
06.png není doloženo samotným obrázkem zavřených vlastností; pro tuto konkrétní
kombinaci je potřeba uložený model. V testovacích modelech se kontroluje
viditelnost nenulové reference v natočeném pohledu. Záměrné potlačení kóty,
jejíž měřená čára se při pohledu přesně podél ní promítne do bodu, zůstává
součástí obecné prezentace kót.

## Výsledek běhu

- Matice 26 parametrů prošla: přímá editace, rozpracovaná změna bez zápisu,
  Cancel, následná změna s OK a načtení uloženého výsledku.
- Profilový test prošel včetně skutečného dvojkliku na popisek, odsazení,
  reference počátku a zpětného úhlu oboustranné rotace.
- Proběhl také širší startup test s transakcemi kót vnitřní skici v Partu
  a Assembly a kontraktní sady modelu, výkresů, měření, vieweru a zámků.
- Offscreen test vzhledu nedostal OpenGL kontext; opakování přes XCB prošlo.
