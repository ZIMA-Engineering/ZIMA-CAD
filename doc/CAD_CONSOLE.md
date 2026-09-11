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
| `documents` | žádné | Otevřené dokumenty, ID, cesty, aktivní/zobrazený stav; u modelů dirty a revision |
| `context` | žádné | Aktivní a zobrazený dokument, aktivní výskyt/skica a potvrzený výběr |
| `tree` | volitelné `document` | Datový strom zadaného nebo zobrazeného dokumentu; nejvýše 2000 položek a příznak truncated |
| `new` | `type name` | Nový Part/Assembly/Drawing ze společné továrny a start šablon |
| `open` | `path` | Otevře `.prtz`, `.asmz` nebo `.drwz`; již otevřený dokument aktivuje |
| `save` | volitelné `document` | Uloží aktivní dokument do jeho existující cesty |
| `regenerate` | volitelné `document` | Výslovná regenerace Partu nebo Assembly |
| `undo`, `redo` | volitelné `document` | Společná historie změn s GUI |
| `fit` | žádné | Přizpůsobení modelu pohledu |

`new` přijímá typy `part`, `assembly`, `drawing`. Název je základ jména souboru
v pracovním adresáři; příponu přidá CAD. Soubor se skutečně zapíše až při `save`.
Dokument bez přiřazené cesty vyžaduje nejprve GUI příkaz Uložit jako.

Čtecí příkazy nespouštějí OCCT. Uložení také nezavádí implicitní regeneraci.
Vytváření jednotlivých modelovacích prvků a změny jejich parametrů zatím nejsou
v katalogu; zůstávají dostupné přes stávající nástroje GUI.

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

Přijímají se pouze známé příkazy, deklarované argumenty typu string a přesná
pole `command`/`arguments`. Chyba validace nikdy nespustí operaci. Textový vstup
má limit 64 KiB. Výpis panelu je omezený; strojový výsledek nepřichází o data
kvůli zkrácení textu v panelu. Strom sám má explicitní limit 2000 položek.

`context.selection` je potvrzená volba, nikoli odhad podle hoveru. Obsahuje
`owner_id`, `semantic_key`, `instance_path`, textový `kind` a `geometry`
(`display` nebo `original_reference`). Nepoužívat popisek stromu jako identitu
objektu. U výkresu zatím není výběr vystavený a jeho dirty stav v seznamu je
`null`, nikoli falešné potvrzení, že je dokument uložený.

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

Dispatcher i hostitel současných jedenácti příkazů jsou nezávislí na GUI.
Stejný `command_host::Host` používá panel a testovací program bez Qt.
Samostatný program `zima-cad-cli` nyní poskytuje stejné příkazy pro jednotlivé
požadavky i dávky ze souboru/stdin. Viz [příkazová řádka](CAD_COMMAND_LINE.md).
Modelovací příkazy zatím nejsou v katalogu.

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
