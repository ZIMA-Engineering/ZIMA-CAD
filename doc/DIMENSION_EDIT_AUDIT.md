# Audit editace kót (2026-09-10)

## Doplnění 2026-09-14: zvětšení kót obdélníku opřeného o osy

V `part02.prtz` šly kóty vnitřní skici posledního Protažení zmenšit,
ale zvětšení se odmítalo jako konflikt. Stejná chyba se projevila přes
CLI i při editaci ve View; nešlo o horní mez rozměru ani o výpočet tělesa.

Zkratka řešiče pro bod na přímce hledala průsečík přímky s kružnicí
požadované vzdálenosti. Při zvětšení posunula bod po ose a porušila jeho
vodorovnou nebo svislou vazbu k druhému bodu. Následující iterace jej vrátila,
a řešení tak oscilovalo. Při zmenšení takový průsečík neexistoval a řešič
správně použil pohyb celé skupiny bodů se společnou souřadnicí.

Průsečíková zkratka nyní odmítne kandidáta, který rozdělí společnou souřadnici
spojenou vodorovnými/svislými vazbami, včetně tranzitivních vazeb. Použije se
stávající řešení pohybu příslušných skupin. Skutečně volný bod nadále může
klouzat po své přímce; skutečně nemožná změna se odmítne bez zápisu.

Ověření:

- Regrese před opravou selhala při prvním zvětšení šířky. Po opravě prošly
  všechny čtyři kvadranty, obě pořadí bodů, přímé i tranzitivní vazby,
  opakované zvětšení/zmenšení, načtení uložené skici a atomické odmítnutí
  změny zablokované pevným bodem. Samostatný případ chrání volný posuv
  po přímce bez vodorovné/svislé vazby.
- Na pracovní kopii `part02.prtz` prošly obě kóty na 20, 32, 40 a 50 mm
  přes `sketch.dimension.set`, vždy i s explicitní regenerací tělesa.
  Testy nepřepisují původní uživatelský soubor.
- `ZIMA_VERIFY_PROFILE_DIMENSION_FILE` umožňuje stejný GUI test spustit na
  uloženém Partu: změna přes picker a dvojklik ve View, uložení, Zpět,
  otevřené Vlastnosti, přechod do vlastněné skici a transakce OK/Zrušit.
  Na kopii `part02.prtz` prošel; prošla také běžná matice
  `ZIMA_VERIFY_PROPERTY_SKETCH_ONLY` pro Part a Assembly.
- Prošly `sketcher_contract_tests`, `sketch_dimension_command_tests`,
  `profile_sketch_command_tests`, `contract_tests` a
  `dimension_layout_contract_tests`. GUI zkoušky ověřují události a data;
  offscreen běh nenahrazuje obrazovou kontrolu OpenGL vykreslení.

## Doplnění 2026-09-11: odsazení od počátku tělesa a rovina kóty

Na uživatelském modelu se změna kóty 16 mm po Enteru vracela. View nabízelo
odvozenou souřadnici X, protože podklady kót neobsahovaly počátek tělesa.
Editor přepsal X, ale uložená reference k rovině YZ stále požadovala odsazení
16 mm; řešení umístění proto souřadnici správně obnovilo.

Zobrazení a přímá editace nyní používají uložené reference v souřadnicích
vlastníka včetně počátků těles. Kóta adresuje odsazení příslušné reference.
Zobrazené kóty umístění se převádějí do polohy vlastnícího tělesa. Sdílený
výpočet umístění ani jeho pravidla se nemění.

Kóta odsazení roviny profilu má vynášecí čáry podél lokální osy X profilu;
měření probíhá podél jeho normály. Leží tak v rovině vlastního originu,
nikoli v rovině odvozené od globálního diagonálního vektoru `{5,5,0}`.
Tatáž kóta se nevkládá znovu přes vlastněnou skicu. U Rotace se již vyřešený
směr profilu nepřevádí podruhé rotací kontejneru.

Ověření tohoto doplnění:

- `ZIMA_VERIFY_BODY_REFERENCE_DIMENSION_ONLY=1`: editace 16 → 17 mm,
  uložení/načtení a shoda otisku vypočteného tělesa; také posunuté a otočené
  vlastnící těleso a prostorová poloha jeho kóty.
- `ZIMA_VERIFY_PROFILE_OFFSET_PLANE_ONLY=1`: natočené profily XY/XZ/YZ,
  zakončení na délku i Až k, směr měření a vynášecích čar, normála roviny
  kóty a právě jeden výskyt odsazení. Kontrola zobrazení nevyžaduje výpočet
  tělesa ani zvolený cíl rozpracovaného Až k.
- Obě kontroly jsou součástí `ZIMA_VERIFY_DIMENSION_EDITS_ONLY=1`.
  Prošla celá matice editace, Properties, Cancel, OK a persistence.
- Na načteném uživatelském modelu prošla změna reference 16 → 17 mm
  a geometrická kontrola roviny kóty odsazení profilu 20 mm.

Cílené běhy používaly `QT_QPA_PLATFORM=offscreen` a `--verify-startup`.
Ověřují události editoru a geometrická data kót; offscreen zde neposkytuje
OpenGL kontext, takže nejde o obrazovou kontrolu vykreslených pixelů.

## Původní rozsah auditu

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
