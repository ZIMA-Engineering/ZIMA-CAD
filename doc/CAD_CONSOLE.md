# Konzole CADu a společné příkazy

## Použití

Konzole se otevírá přes **Zobrazení → Konzole CADu** nebo **Ctrl+Shift+C**.
Je to zavíratelný spodní panel hlavního okna, nikoli samostatné systémové okno.
Po spuštění aplikace je skrytý. Enter spustí příkaz, šipky nahoru/dolů procházejí
posledních 100 příkazů. Historie je pouze v paměti. Tlačítko Vyčistit výpis
odstraní zobrazený protokol; dokumenty nemění.

```text
help
documents
context
tree
new part konzole_zkouska
save
regenerate
undo
redo
fit
open "C:/CAD/moje sestava.asmz"
```

Názvy příkazů a argumentů jsou stabilní anglické identifikátory. Ovládací prvky,
popisy příkazů a aplikační hlášky používají aktuální jazyk. Diagnostika parseru
má stabilní kódy a technický anglický detail. Mezery v cestě/názvu uzavřete do
dvojitých uvozovek. Zpětná lomítka Windows cest se zachovávají; uvnitř uvozovek
lze vložit znak uvozovky pomocí `\"`. Nejde o shell ani interpret Pythonu.

## Dostupné operace

| Příkaz | Argumenty v pořadí pro textovou konzoli | Výsledek |
| --- | --- | --- |
| `help` | žádné | Katalog příkazů, popisy, argumenty a příznak změny stavu |
| `documents` | žádné | Otevřené dokumenty, ID, cesty, aktivní/zobrazený stav; dirty, revision a needs_save pro všechny tři typy |
| `context` | žádné | Aktivní a zobrazený dokument, aktivní výskyt/skica a potvrzený výběr |
| `tree` | volitelné `document` | Datový strom zadaného nebo zobrazeného dokumentu; nejvýše 2000 položek a příznak truncated |
| `new` | `type name` | Nový Part/Assembly/Drawing ze společné továrny a start šablon |
| `open` | `path` | Otevře `.prtz`, `.asmz` nebo `.drwz`; již otevřený dokument aktivuje |
| `save` | volitelné `document` | Uloží aktivní dokument do jeho existující cesty |
| `save_as` | `path`, volitelné `document` | Nezávislá kopie s novými ID včetně navázaných výkresů; existující cíl se nepřepíše |
| `activate` | `document` | Zobrazí otevřený dokument jako hlavní; nepočítá model |
| `close` | volitelné `document discard` | Zavře dokument; neuložené změny vyžadují explicitní boolean `discard: true` |
| `pwd` | žádné | Aktuální pracovní adresář |
| `cd` | `path` | Změní pracovní adresář na existující složku |
| `regenerate` | volitelné `document` | Výslovná regenerace Partu nebo Assembly |
| `undo`, `redo` | volitelné `document` | Společná historie změn s GUI |
| `fit` | žádné | Přizpůsobení modelu pohledu |
| `box.create` | `length_mm width_mm height_mm`, volitelné `document` | Vytvořit a vypočítat kvádr v aktivním tělese |
| `box.get` | `container`, volitelné `document` | Přečíst uložené rozměry, zámky a identitu kvádru |
| `box.set` | `container`, volitelné `length_mm width_mm height_mm document` | Změnit zadané rozměry a vypočítat Part |

`new` přijímá typy `part`, `assembly`, `drawing`. Název je základ jména souboru
v pracovním adresáři; příponu přidá CAD. Soubor se skutečně zapíše až při `save`.
`save_as` odpovídá současnému GUI Uložit jako: vytvoří samostatnou kopii,
zatímco původní dokument zůstane otevřený na stejné cestě a se stejnými
neuloženými úpravami. Cílová přípona musí odpovídat typu dokumentu.
Kopii lze otevřít příkazem `open`. Běžné `save` zapisuje původní dokument;
pokud nemá přiřazenou cestu, konzole vrátí `path_required`.

`documents.needs_save` zahrnuje neuložené úpravy i dosud nezapsaný nebo
chybějící soubor. `close` v takovém případě vrátí `unsaved_changes`.
Zahození lze požadovat jednoznačně přes JSON:

```json
{"command":"close","arguments":{"discard":true}}
```

`activate` a `close` přijímají skutečné ID otevřeného dokumentu. U ostatních
změnových příkazů argument `document` nadále kontroluje aktivní dokument.
`cd` ovlivňuje následné relativní cesty; nemění pracovní adresář procesu.

Čtecí příkazy nespouštějí OCCT. Uložení také nezavádí implicitní regeneraci.
V katalogu je také tvorba a změna šesti základních primitiv. Ostatní modelovací prvky zůstávají
dostupné přes stávající nástroje GUI.

## JSON rozhraní

Stejný dispatcher přijímá JSON. Žádná druhá implementace operací pro AI není:

```json
{"command":"context","arguments":{}}
```

U změnových příkazů volitelný argument `document` chrání volajícího před použitím příkazu v jiném
dokumentu po přepnutí tabu. ID získá z `documents` nebo `context`:

```json
{"command":"save","arguments":{"document":"ID-AKTIVNIHO-DOKUMENTU"}}
```

Výsledek na rozhraní `Result::json()`:

```json
{
  "protocol": "zima-cad.commands/1",
  "ok": true,
  "code": "ok",
  "message": "",
  "data": {}
}
```

Přijímají se pouze známé příkazy, argumenty odpovídající deklarovaným typům a přesná
pole `command`/`arguments`. Chyba validace nikdy nespustí operaci. Textový vstup
má limit 64 KiB. Výpis panelu je omezený; strojový výsledek nepřichází o data
kvůli zkrácení textu v panelu. Strom sám má explicitní limit 2000 položek.

`context.selection` je potvrzená volba, nikoli odhad podle hoveru. Obsahuje
`owner_id`, `semantic_key`, `instance_path`, textový `kind` a `geometry`
(`display` nebo `original_reference`). Nepoužívat popisek stromu jako identitu
objektu. U výkresu zatím není výběr vystavený. Potvrzené úpravy výkresu sleduje
`DrawingState`; `dirty` a `revision` jsou dostupné stejně jako u modelů.

## Transakce a chyby

Příkazy měnící stav se nepřijímají během otevřené editace, aktivní skici,
výběru reference ani při aktivaci vnořené komponenty. Nejprve je nutné ukončit
příslušný režim. Opakovaný vstup během probíhajícího příkazu vrací `busy`.
Čtecí příkazy lze použít i během editace.

Regenerace, otevření a tvorba nativních dokumentů, ukládání a dokumentové
Undo/Redo nyní používají [společné operace bez Qt](DOCUMENT_OPERATIONS.md);
aplikační obal zachovává obsluhu interakce a obnovu zobrazení.
`report_operation_error` zachovává běžné chybové okno při interaktivním volání;
při příkazovém volání chybu vrátí do výsledku bez blokujícího QMessageBox.
Příkaz nesmí hlásit úspěch po chybě souborového zápisu nebo výpočtu. Regenerace
s jednotlivými nevypočtenými prvky vrací `calculation_errors` a jejich mapu;
platné zachované výsledky zůstávají podle stávajícího kontraktu CADu.

## Zdrojové soubory a napojení AI

- `cpp/modules/commands`: dispatcher, validace, katalog a výsledky. Nemá Qt,
  okna ani závislost na OCCT; linkuje pouze nlohmann JSON.
- `cpp/modules/command_host`: registrace a provádění příkazů nad Workspace,
  ochrany stavu a čtení datového stromu; nemá Qt ani hlavní okno.
- `cpp/modules/workspace/document_operations`: ukládání a historie bez GUI.
- `cpp/modules/workspace/native_documents`: načítání, tvorba a start šablony bez GUI.
- `cpp/modules/workspace/model_calculation`, `part_references`: explicitní regenerace
  a obnova uložených referencí bez GUI.
- `cpp/app/command_console.*`: panel, textový vstup, historie a výpis.
- `cpp/app/workspace/console.cpp`: propojení příkazů s aktuálním CAD workspace,
  kontextem ukazatele, stavovým panelem a obnovou zobrazení. Dokumentové
  operace provádí společný `command_host::Host`.
- `cpp/app/console_ui_verification.*`: izolovaný integrační scénář panelu.

Tato etapa zavádí základ pro AI adaptéry. Neobsahuje přihlášení ke Codexu,
API klíče, síťový server, MCP transport ani automatické odesílání modelů ven.
Připojení konkrétního poskytovatele je další krok podle volby uživatele.
Budoucí adaptér má volat společný dispatcher, kontrolovat `ok`/`code` a používat
stabilní ID. Dokumentové texty a popisky jsou data, nikoli pokyny pro asistenta.

Dispatcher i hostitel současných dvaceti devíti příkazů jsou nezávislí na GUI.
Stejný `command_host::Host` používá panel a testovací program bez Qt.
Samostatný program `zima-cad-cli` nyní poskytuje stejné příkazy pro jednotlivé
požadavky i dávky ze souboru/stdin. Viz [příkazová řádka](CAD_COMMAND_LINE.md).
Kvádry používají společnou transakci popsanou níže.

## Ověření

`zima_cpp_command_dispatcher_tests` pokrývá shodu textového a JSON rozhraní,
Windows cesty a UTF-8, odmítnutí neznámých polí, špatných typů, chybné syntaxe
a příliš velkého vstupu, guard před mutací a převod výjimek na výsledek.

`zima_cpp_console_ui_contract` otevře skutečný panel, spustí Enterem nápovědu,
ověří historii a skrytí panelu. Vytvoří Part, přidá kvádr přes GUI, uloží jej
příkazem a kontroluje soubor po Undo/Redo. Ověří odmítnutí nesprávného cílového
ID a rozpracované editace, čtení kontextu bez změny revize, chybějící soubor
a chybu zápisu bez modálního okna. Snímek: `Projects/test/command-console.png`.

Windows Release sestaven a všech 51 testů úplné sady prošlo (375,68 s).
Panel byl ověřen i vizuálně na snímku skutečného okna.

## Kompaktní panel a kontext ukazatele (2026-09-11)

Panel lze stáhnout na jeden řádek výstupu a řádek zadávání. Textový `help`
vypisuje každý příkaz na samostatném řádku s povinnými argumenty v `<…>`
a nepovinnými v `[…]`; JSON katalog zůstává strukturovaný.

`context` přidává okamžik pořízení `captured_at_unix_ms`, `camera`, `pointer`
a `hover`. Kamera obsahuje osm hodnot: quaternion (w, x, y, z), měřítko,
posun v pixelech (x, y) a referenční měřítko. Ukazatel používá logické pixely
pohledu, jeho rozměry a paprsek (`origin`, `direction`) v modelových souřadnicích.
Paprsek se získává z existující kamery bez výpočtu tělesa nebo dalšího pickeru.

Hover přebírá přesně kandidáta nabízeného pohledem. Pokud je ukazatel mimo
pohled, nad překrývajícím oknem nebo ještě neodpovídá poslední zpracované pozici
pickeru, `hover` je `null`. Potvrzený výběr je nezávislý údaj `selection`.
Výkresový kontext zatím neposkytuje kameru ani geometrii ukazatele.

Budoucí hlasový adaptér musí zachytit kontext při ukazování/vyslovení pokynu,
ne až po dokončení přepisu. Tento příkaz sám historii ukazatele ani zvuk
nezaznamenává. Před provedením změny musí adaptér ověřit dokument a platnost
referencí; nejednoznačné „tady“ nesmí převést na odhadnutou geometrii.

Projekt zůstává GPL-3.0-or-later. Hlasový a AI adaptér mají používat společné
příkazové rozhraní. Před distribucí konkrétního přepisovače nebo modelu je nutné
ověřit jeho licenci a zachovat vyžadovaná oznámení. V této etapě není přidána
hlasová knihovna, mikrofon ani poskytovatel AI.

Ověření této úpravy: Windows Release sestaven, test parseru a integrační test
konzole prošly; stabilita testu byla ověřena třemi po sobě jdoucími průchody.
GUI test kontroluje zmenšení panelu, čas a kameru, paprsek a převzetí hoveru
podle skutečného překrytí oken. Při automatizaci může být CAD překrytý jinou
aplikací; tehdy se ověřuje prázdný hover, nikoli vynucený zásah geometrie.
Kompaktní panel byl také zkontrolován na snímku
`Projects/test/command-console-compact.png`.


## Společný hostitel a strom modelu (2026-09-11)

Vstupem `command_host::Host` je Workspace, kernel, pracovní adresář a volitelné
adaptéry nastavení, překladu a interakce. Text i JSON procházejí stejným katalogem,
validací a ochranami. Host přímo volá společné dokumentové operace; nepředává
jejich provedení zpět hlavnímu oknu.

Výsledek je stále `Result` s protokolem `zima-cad.commands/1`. Poslední změnu
navíc popisuje `Change` (Open, New, Save, Regenerate, History a ID dokumentu).
GUI podle ní obnoví taby/pohled. Nové provedení předchozí změnu smaže; odmítnutý
opakovaný vstup během operace vrací `busy` bez přepsání probíhajícího stavu.
Regenerace vrací změnu i při částečném selhání, aby pohled ukázal platný výsledek
a chyby po skutečně provedeném výpočtu.

Host se volá na vlákně vlastnícím Workspace. Adaptér `run_io` smí přesunout
čtení nebo zápis odděleného snímku na pracovní vlákno, ale musí před návratem
počkat na dokončení a předat chybu. Samotná pracovní úloha nemá přístup do
živého Workspace. GUI při čekání zachovává dosavadní obsluhu událostí bez
uživatelského vstupu. Nový modul explicitně požaduje UTF-8 i při MSVC sestavení
bez Qt, aby cesty a překlady nezávisely na systémové znakové stránce.

`tree [document]` vrací `projection: "model"`. Čte skutečnou datovou hierarchii,
nikoli řádky QTreeWidgetu. Nezahrnuje dočasné řádky otevřené editace, ikony ani
lokalizované dekorace. Part zahrnuje tělesa v pořadí historie, kontejnery,
konstrukce, jejich skici a uloženou geometrii skic/referenční identity, počátky
a řezy. Assembly zahrnuje vlastní objekty a uloženou hierarchii výskytů.
Drawing zahrnuje listy, pohledy a výkresové kóty.

Řádek obsahuje `id`, `parent_id`, `document_id` (vlastník), `instance_path`,
`parent_instance_path`, `depth`, `type`, `label` a `semantic_key`; podle typu
přidává např. zdrojový dokument, potlačení a viditelnost. Identita výskytu je
cesta, ne název ani samotné ID zdrojového dílu. Dva stejné šrouby proto mají
odlišné cesty i při shodném zdroji. Sestavový strom čte poslední uložený/vypočtený
snímek, neotevírá závislosti ani do něj nevnáší novější obsah zdrojového tabu.
Konkrétní otevřený zdroj lze číst jeho `document` ID bez aktivace a regenerace.
Názvy geometrických typů jsou stabilní identifikátory; nejsou překladem UI.

Bez adaptéru interakce zůstávají `selection`, `hover` a `camera` prázdné a
ukazatel je mimo View. Příkaz `fit` bez adaptéru pohledu vrátí `view_unavailable`.
Formáty dokumentů a config šablony se touto etapou nemění.

`zima_cpp_command_host_tests` provádí bez Qt skutečné New/Open/Save všech tří
nativních typů, Undo/Redo a regeneraci kvádru s nezávislou kontrolou objemu.
Ověřuje UTF-8, shodu textu/JSON, pracovní I/O, zákaz opakovaného vstupu,
zachování neuloženého dokumentu, chyby bez změny stavu a datový strom včetně
vlastnictví skic, opakovaných výskytů, výkresů a limitu počtu položek.
Test panelu navíc ověřuje, že dekorace přidaná pouze do widgetu stromu není
ve výsledku `tree`, zatímco skutečný prvek vytvořený přes GUI tam je.


Ověření této etapy: Windows Release, **56/56 testů prošlo** (363,51 s),
`build/command-host-full-tests.log`. `dumpbin /dependents` nad
`zima_cpp_command_host_tests.exe` potvrdil nepřítomnost Qt DLL;
protokol je `build/command-host-dependencies.log`. Snímek skutečného panelu
`Projects/test/command-console.png` byl také vizuálně zkontrolován.


## Spouštění bez hlavního okna

`zima-cad-cli` používá zde popsaného hostitele bez GUI a Qt. Katalog, modelové
operace, ochrana cílového dokumentu i datový strom jsou společné. Vstup/výstup,
config, návratové kódy a hranice dávkového provedení popisuje
[CAD_COMMAND_LINE.md](CAD_COMMAND_LINE.md).

## Společná operace kvádru (2026-09-11)

```text
new part prvni_kvadr
box.create 10 20 30
save
```

Výsledek tvorby obsahuje `document`, `container`, `feature`, `body`, `name`,
`length_mm`, `width_mm`, `height_mm`, `value_locks`, `revision` a `changed`.
Pro další změny použijte skutečné ID `container` z odpovědi nebo ze stromu.
`box.get ID` čte jen uložené parametry; volitelné `document` umožňuje číst jiný
otevřený Part bez jeho aktivace. Výsledek neobsahuje přechodné hodnoty rozpracovaného dialogu.

```json
{"command":"box.set","arguments":{"container":"ID_Z_ODPOVEDI","width_mm":"40"}}
```

Argumenty uvedených rozměrových příkazů jsou řetězce. Rozměry jsou výslovně v **mm**, nezávisle na
zobrazovaných jednotkách dokumentu; používají desetinnou tečku. Přípustný rozsah
je stejný jako v okně kvádru: **0,001 až 1 000 000 mm**. `box.set` vyžaduje alespoň
jeden rozměr. U textového příkazu jsou hodnoty poziční; pro změnu samotné šířky
nebo výšky použijte JSON. Nezadané rozměry zůstanou zachované.

`box.create` vkládá prvek na aktuální kurzor aktivního tělesa. `box.set` mění
existující kvádr podle ID a nepřesouvá jej do aktivního tělesa. Zachovává jméno,
umístění, reference, režim kombinace, potlačení, zámky a identity původních ploch.
Zamčená hodnota vrací `value_locked`; odemknutí je zatím přes GUI. Změnové příkazy
podléhají stejným ochranám rozpracované editace a aktivované komponenty jako ostatní
příkazy konzole. Odvozené těleso není přímo editovatelné.

GUI OK i příkazy používají `workspace::commit_primitive` v `primitive_operations.cpp`:
validace, kopie dokumentu, existující vyřešení umístění nad uloženými referencemi,
výslovný výpočet, obnova externích referencí a jeden společný commit do historie.
Zrušit v GUI nevolá commit; shodné hodnoty nevytvářejí Undo krok ani výpočet.
Při chybě validace nebo výpočtu zůstává dokument i jeho cache beze změny.
Editace kontroluje chybu u upravovaného prvku a zachovává dosavadní pravidlo,
že již chybné následující prvky lze opravit samostatně. Nadřazené sestavy se
automaticky neregenerují. Formáty a start šablony se nemění.

`zima_cpp_box_command_tests` ověřuje objemy, identity ploch při změně rozměrů,
zámky, atomické odmítnutí, kurzor a vlastnictví těles, uložení a Undo/Redo.
GUI scénář střídá konzoli a stejné okno vlastností včetně Zrušit a historie;
procesový CLI test vytváří i mění skutečný uložený kvádr.

Parametrický patch má v `workspace::set_primitive_dimensions` společnou kontrolu zámků;
GUI předává celé potvrzené vlastnosti, takže lze během jedné editace hodnotu
odemknout, změnit a znovu zamknout. Samotný výpočet a commit zůstávají společné.
Okno kvádru zachovává přesné hodnoty nedotčených polí i při menším počtu zobrazených
desetinných míst. Zaokrouhlení pro zobrazení nemění model ani nezablokuje změnu
jiného rozměru. Tyto případy včetně Undo ověřuje test konzolového GUI.

Aktuální rozsah a zbývající práce: [mapa pokrytí CAD příkazů](CAD_COMMAND_COVERAGE.md).

Ověření kvádru: Windows Release, úplná sada **58/58** prošla (382,07 s,
`build/box-full-tests.log`). Po doplnění zachování přesných hodnot a kontroly
parametrických patchů prošlo všech **7/7** dotčených modelových, CLI a GUI
scénářů (199,07 s, `build/box-final-tests.log`), včetně pracovního okna,
profilů a úprav kót. Finální překlad je v `build/box-final-build.log`.
CLI nadále nelinkuje Qt (`build/box-cli-dependencies.log`).

## Všechna základní primitiva

Tvorbu, čtení a parametrický patch nyní sdílí šest druhů prvků. Textové příkazy
tvorby přijímají rozměry v tomto pořadí; poslední volitelný argument je `document`.

| Příkaz | Povinné rozměry v mm |
| --- | --- |
| `box.create` | `length_mm width_mm height_mm` |
| `cylinder.create` | `radius_mm height_mm` |
| `sphere.create` | `radius_mm` |
| `cone.create` | `bottom_radius_mm top_radius_mm height_mm` |
| `pyramid.create` | `length_mm width_mm height_mm` |
| `wedge.create` | `length_mm width_mm height_mm top_offset_mm` |

Každý prefix má také `.get container [document]` a `.set container ...`.
U `.set` jsou rozměry volitelné, ale musí být zadán nejméně jeden. JSON patch
umožňuje zadat konkrétní pole bez pozičních zástupných hodnot, například:

```json
{"command":"cone.set","arguments":{"container":"ID_KUZELE","top_radius_mm":"0"}}
```

Horní poloměr kuželu a horní odsazení klínu smějí být nulové. Ostatní rozměry
mají rozsah 0,001 až 1 000 000 mm; horní odsazení klínu nesmí překročit jeho délku.
Geometricky neplatný výpočet (například kužel se shodnými poloměry) se odmítne
bez změny dokumentu. Příkaz konkrétního typu nemůže změnit jiný druh kontejneru.

Všech šest oken používá `workspace::commit_primitive`. Parametrické patche
používají `set_primitive_dimensions`, který respektuje zámky a volá tutéž
transakci. Jedna definice parametrů v modelové vrstvě poskytuje čtení, zápis
a rozsahy; GUI zachovává nezměněné přesné hodnoty i při zaokrouhleném zobrazení.

`zima_cpp_primitive_command_tests` porovnává výsledné objemy s nezávislými
vzorci válce, koule, komolého kuželu, jehlanu a klínu. Ověřuje identity ploch,
Undo/Redo, zámky, save/load, nulové horní rozměry a atomické odmítnutí chyb.
Panelový test prochází všechny typy přes CLI tvorbu a GUI editaci; procesový
test je vytváří skutečným samostatným CLI.

Ověření rozšíření primitiv: Windows Release, **59/59 testů prošlo** (369,66 s),
`build/primitives-full-tests.log`. Předtím prošlo všech pět cílených testů
modelu, GUI a CLI (8,74 s, `build/primitives-focused-tests.log`).

## Typy argumentů

Každý argument má v katalogu `help` deklaraci `type`. Dispatcher podporuje
`string`, konečné `number`, `integer`, `boolean`, `object` a `array`.
Dosavadní příkazy včetně rozměrů primitiv nadále deklarují řetězce; samotné
rozšíření dispatcheru jejich syntax měnit nesmí. Nová rozhraní mohou deklarovat
přesné datové typy pro seznamy bodů, parametry a reference.

JSON požadavek předává hodnoty ve skutečném deklarovaném typu. Řetězec `"true"`
nenahrazuje boolean a řetězec s JSON nenahrazuje objekt. Textový vstup převádí
pouze argumenty deklarované jako jiné než `string` pomocí JSON parseru;
řetězce, Windows cesty a jejich escapování zůstávají stejné. Pro složité objekty
a seznamy používejte celý JSON požadavek. Prázdný objekt nebo seznam se považuje
za přítomný argument; jeho obsah dále validuje konkrétní modelová operace.

Nesprávný typ, chybějící povinná hodnota, neznámé pole nebo neplatné číslo
selže před mutací. Deklarace nesmí obsahovat duplicitní názvy argumentů.
Stejná ochrana rozpracované editace platí i pro typované požadavky.

Ověření: přeloženo GUI i CLI; **6/6** cílených testů dispatcheru, hostitele,
primitiv, skutečných CLI procesů a panelu prošlo (8,49 s),
`build/typed-arguments-tests.log`. Překlad: `build/typed-arguments-build.log`.

## Tělesa a operace mezi nimi

Katalog nyní obsahuje také `body.list`, `body.get`, `body.create`, `body.set`,
`body.activate`, `body.cursor` a `body.boolean.create/get/set`. Vlastnosti těles
a Booleanů používají tutéž transakci jako GUI. Aktivace a kurzory nepočítají OCCT.
Syntax, datové typy a ověřované objemy jsou v [BODY_COMMANDS.md](BODY_COMMANDS.md).

## Historie Partu

`history.list`, `history.suppress`, `history.delete`, `history.move`,
`history.can_move` a `history.cursor` sdílejí operace se stromem GUI.
Argumenty, rozsahy těles, chybové výsledky a příklady popisuje
[HISTORY_COMMANDS.md](HISTORY_COMMANDS.md).
