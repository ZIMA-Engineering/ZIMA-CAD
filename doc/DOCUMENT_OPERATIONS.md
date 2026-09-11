# Dokumentové operace bez GUI

## První oddělená etapa (2026-09-11)

`cpp/modules/workspace/document_operations` obsahuje společné ukládání Partu,
Assembly a Drawing a dokumentové Undo/Redo. Veřejné rozhraní je v
`include/zima/workspace/document_operations.hpp`, implementace v
`src/document_operations.cpp`. Nepoužívá Qt, okna, picker ani aplikační smyčku.
GUI i konzole používají tutéž implementaci, původní kopie logiky byly odstraněny.

Vstupem operace je Workspace, přesné ID dokumentu a cílová cesta, případně
směr historie. Výstupem je uložený nativní dokument nebo změněný stav historie.
Zobrazení, dialogy, lokalizované zprávy a stavový panel zůstávají v aplikaci.

## Ukládání

1. `prepare_document_save` na vlákně vlastnícím Workspace pořídí snímek
   dokumentu a již vypočtených dat. Nic nezapisuje a nemění dirty stav.
2. `DocumentSave::write` pracuje jen se snímkem. Může běžet na pracovním vlákně
   nebo synchronně v programu bez GUI. Používá stávající nativní serializaci.
   Až po úspěšném zápisu vrací `SavedDocument`.
3. `complete_document_save` na vlákně Workspace dohledá dokument znovu podle
   identity. Neuchovává ukazatel do vektoru otevřených dokumentů přes pracovní
   úlohu. Teprve zde aktualizuje cestu a případně označí dokument za uložený.

Revize parametrů sama nestačí: explicitní přepočet může změnit vypočtená data
bez nové položky historie. Dokončení kontroluje revizi, generaci dat a počet
přidělených rozměrových identifikátorů. Novější změny zůstávají dirty.
AssemblySession proto stejně jako DocumentSession vystavuje runtime generaci
svých dat; tato hodnota není součástí souborového formátu.

Každý otevřený stav má runtime identitu odlišnou od trvalého ID dokumentu.
Dokončení staré úlohy nesmí změnit dokument, který byl mezitím zavřen a znovu
otevřen ze stejného souboru. Odmítne také mezitím změněnou cílovou cestu.
Pracovní snímek není revize připnutá k sestavě a nevytváří žádný sidecar.

Výkres zatím nemá dokumentovou session s dirty revizemi. Uložení zachovává
stávající chování: zapíše snímek a aktualizuje cestu, novější obsah v paměti
nepřepisuje. Rozšíření správy výkresové historie není součástí této etapy.

## Historie

`can_step_document_history` a `step_document_history` používají existující
Part/Assembly session. Part po úspěšném kroku synchronizuje závislosti externích
skic stejně jako dříve. Operace nemění aktivní ani zobrazený dokument.

V GUI zůstávají zákazy během editace, lokální historie řezu/skici, zrušení
rozpracovaného segmentu a obnova pohledu. Tyto interakční kroky nemají patřit
do dokumentového jádra. Není to plošný audit všech transakcí Undo/Redo.

## Ověření a další hranice

`zima_cpp_document_operations_tests` běží bez QApplication. Ověřuje:

- skutečný zápis a opětovné načtení všech tří nativních typů;
- identitu dokumentu, geometrii a reference Partu a komponent sestavy;
- neměnný snímek zapisovaný na pracovním vlákně;
- novější editace a vypočtená data během ukládání;
- selhání zápisu bez změny cesty, revize a dirty stavu;
- odmítnutí chybějícího dokumentu a prázdné cesty;
- Undo/Redo Partu a Assembly, prázdnou historii a nepodporovaný typ;
- změnu cesty, zavření a opětovné otevření během ukládání;
- zachování aktivního a zobrazeného dokumentu.

Integrační test konzole nadále ověřuje kombinaci GUI editace, konzolového
uložení a Undo/Redo proti skutečnému souboru. Celá regresní sada se spouští
přes `tools/build-windows.ps1 -Configuration Release -RunTests`.

Otevírání a vytváření podle config šablon jsou popsány v další etapě níže.
Regenerace je oddělena ve třetí etapě popsané níže. Společný hostitel příkazů
bez GUI je nyní v `modules/command_host`; viz [CAD_CONSOLE.md](CAD_CONSOLE.md).
Formát souborů se nemění; stávající přípony a config šablony zůstávají platné.

## Výsledek ověření

Windows Release: kompletní sada **52/52 prošla**, 374,17 s. Testovací program
nových operací byl navíc spuštěn samostatně a jeho závislosti byly ověřeny přes
`dumpbin /dependents`: neobsahuje Qt. Úplný protokol je v lokálním
`build/document-operations-full-tests.log`.

Po doplnění lokalizace chybové zprávy a přesného porovnání ID původních ploch
byly znovu přeloženy dotčené cíle. Následně prošly `zima_cpp_ui_contract_tests`,
`zima_cpp_document_operations_tests` a `zima_cpp_console_ui_contract` (3/3,
12,27 s). Geometrický test porovnává vlastníka, sémantický klíč a cestu výskytu
každé uložené původní plochy, ne pouze počet referencí.

## Druhá etapa: otevření a nový dokument

`native_documents.hpp/.cpp` v modulu workspace nyní poskytují:

- `read_native_document`: čtení `.prtz`, `.asmz`, `.drwz` a uložené geometrie
  do odděleného výsledku; může běžet na pracovním vlákně bez Qt;
- `prepare_new_native_document`: nový dokument, nastavení jednotek a přípravu
  ze start šablony; nevytváří soubor ani nemění otevřený Workspace;
- `insert_native_document`: vložení na vlákně vlastnícím Workspace;
- `part_from_template` a `assembly_from_template`: společné továrny také pro
  import, který potřebuje nastavení a počáteční dokument ze stejné šablony.

`NativeTemplateSettings` přenáší cesty ze settings a přeložené jméno prvního
tělesa. Kopírování hodnot mezi Qt settings a těmito daty je malý aplikační
adaptér. Samotné čtení šablony, kontrola jejího obsahu a vytvoření nových ID
nepotřebují Qt. Config šablony se nezapisují ani nemění.

Part i těleso mají novou identitu a těleso se váže na počátek nového Partu
stávající funkcí `create_origin_bound_body`. Její implementace ani kontrakt
umístění se nemění. Assembly rovněž dostává nové ID. Jednotky aplikace se
přenášejí stejně jako dříve; přesnost a ostatní hodnoty zůstávají ze šablony.
Výkres se nadále vytváří svým stávajícím výchozím konstruktorem.

Při Open GUI nejprve kontroluje už otevřenou cestu. Vložení načteného výsledku
kontrolu opakuje: pokud byl mezitím stejný soubor otevřen, převezme jeho ID a
nepřepíše neuložené změny. Shodné trvalé ID pod jinou cestou se odmítá stejně
jako dosud. New odmítá obsazenou cestu před přípravou i před vložením.

Vložení nevyvolává explicitní přepnutí dokumentu. Zachovává základní chování
Workspace, který první vložený dokument nastaví jako výchozí. Přepínání tabů,
aktivaci nástrojů, vymazání dočasného výběru, první aktivní těleso a obnovu
View stále zajišťuje GUI. Tím zůstává obsluha po Open/New stejná.

Formáty `.tblz`/`.frmz` pro grafické šablony a obnova fontových kontur zůstávají
v dosavadním editoru šablon. Nejsou dalšími nativními typy Part/Assembly/Drawing.
Tato etapa nemění žádný uložený formát ani vyžadované soubory dokumentů.

`zima_cpp_native_documents_tests` běží bez QApplication a ověřuje čtení všech
tří typů, data pro View, přesné identity původních ploch, UTF-8 cestu, config
šablony, nové identity včetně vazby tělesa na správný počátek, nastavení jednotek,
kolize cest, obsazení cesty během přípravy, již otevřený změněný dokument,
duplicitní ID, chybějící/poškozený soubor a odmítnutí staré přípony `.prt`.

## Ověření druhé etapy

Windows Release sestaven, **53/53 testů prošlo** (354,86 s), protokol
`build/native-documents-full-tests.log`. Test nových nativních operací navíc
prošel samostatně a kontrola `dumpbin /dependents` potvrdila nepřítomnost Qt DLL.
Config šablony test porovnává před a po tvorbě bajt po bajtu.

## Třetí etapa: výslovná regenerace a reference

Uživatel 2026-09-11 výslovně povolil úpravu chráněného umístění kontejnerů.
Schválený přesun je strukturální: výpočty a reference přecházejí z hlavního
okna do modulu workspace. Řešič umístění, pořadí průchodů, limit
`history.size() + 2`, konvergence i transakce zůstávají stejné.

`model_calculation.hpp/.cpp` poskytují bez Qt:

- `calculate_part`: výpočet se zachováním platné geometrie při chybě;
- `calculate_part_with_resolved_references`: výpočet do ustálení umístění,
  konstrukcí, externích skic a bodů vrtání, včetně polohy řezů;
- `calculate_resolved_assembly_cuts`: výpočet sestavových odečtů s jejich
  uloženými referencemi, skutečným vstupem pro rollback a odvozenými kopiemi;
- `regenerate_part` a `regenerate_assembly`: výslovná regenerace konkrétního
  otevřeného dokumentu včetně stávajících pravidel historie a závislostí.

`part_references.cpp` obsahuje původní čisté pomocné funkce pro výběr vlastníků,
obnovu externích referencí a skládání uložené referenční geometrie. GUI
používá tytéž funkce přes deklarace v privátním workspace headeru. Žádná z
nich nezískává identitu geometrie novým procházením OCCT topologie.

`PartCalculationPolicy` je pouze kontext explicitního výpočtu: zda zamítnout
chyby a ke kterému dokumentu/hraničnímu indexu patří editace. Při rollbacku
se posuzuje chyba právě editovaného prvku; chyby pozdější historie zůstávají
u svých vlastníků. GUI převádí svůj stav dialogu a rollbacku na tuto datovou
strukturu. Nový modul nepotřebuje okno, viewer ani QApplication.

Regenerace běží synchronně na vlákně vlastnícím Workspace. Nemění aktivní
dokument ani zobrazenou sestavu. GUI nadále řídí rozpracovanou editaci,
obnovu View, výběr a hlášení výsledku. Obnova Partu bez změny definice
aktualizuje vypočtené hranice bez nového Undo kroku. Změněné umístění či
reference se potvrzují stejnou transakcí jako před přesunem.

Explicitní regenerace Assembly dál používá otevřené neuložené zdroje.
Běžné zobrazení ani přepnutí tabu novou regeneraci nevyvolává. Stávající
`Workspace::calculate_assembly_cuts` v obnově závislostí zůstává beze změny:
pracuje s již uloženými definicemi odečtů. Přesunutá aplikační fáze navíc řeší
reference sestavového odečtu a jeho vlastněnou skicu. Sloučení těchto dvou
odlišných fází není součástí tohoto strukturálního přesunu.

Přípony, serializace, config i start šablony zůstávají stejné. Nevznikají
revizní adresáře ani povinné soubory mimo `.prtz`, `.asmz` a `.drwz`.

Nezávislé porovnání proti předchozímu commitu ověřilo totožnost všech osmi
přesunutých pomocných funkcí; u výpočtů a obou regeneračních transakcí
ověřilo totožnost po nahrazení přístupů ke členům okna explicitními parametry.
Lokální protokol: `build/model-calculation-extraction-audit.txt`.

`zima_cpp_model_calculation_tests` běží bez Qt a kontroluje řetězec tří
navázaných kontejnerů, navázaný řez, změnu zdrojového rozměru, zachování ID
původních ploch, Undo/Redo, opakovaný nezměněný výpočet, chyby při editaci a
regeneraci, neuložený Part v Assembly, identitu výskytu a sestavový odečet.
Objem odečtu je ověřen nezávisle jako 2000 − 2 × 10 × 10 = 1800 mm³.

## Ověření třetí etapy

Finální Windows Release prošel **55/55 testy** (365,35 s), včetně kompletních
GUI, konzolových, souborových, sestavových, skicářských a geometrických regresí.
Protokol je v `build/model-calculation-final-tests.log`. První úplný běh
zachytil jediný problém s kompaktností Vlastností osy; oprava rezervy tlačítka
je popsána v [NUMERIC_VALUE_LOCKS.md](NUMERIC_VALUE_LOCKS.md). Následný úplný
běh již neměl chybu. Kontrola `dumpbin /dependents` potvrdila, že nový test
modelových výpočtů neobsahuje Qt DLL (`build/model-calculation-dependencies.txt`).


## Čtvrtá etapa: společné provádění příkazů

`modules/command_host` přímo propojuje katalog textových/JSON příkazů se zde
popsanými operacemi. Main Window již neregistruje vlastní implementace příkazů.
Poskytuje pouze nastavení, lokalizaci, stav interakce, pracovní I/O a akci Fit;
po provedení obdrží popis změny pro obnovu zobrazení. Sdílená pomocná funkce
`finish_document_switch` zachovává stejné vyčištění dočasného výběru a obnovu
stromu/View při Open/New z GUI i konzole.

Modelový strom a seznam dokumentů lze číst bez Qt, bez otevření zdrojových
souborů závislostí a bez OCCT. Kontrakt, datová pole a hranice příkazového
programu jsou v [CAD_CONSOLE.md](CAD_CONSOLE.md).
