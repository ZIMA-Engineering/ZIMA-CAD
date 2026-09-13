# Pokrytí CAD operací příkazovou vrstvou

Uživatel 2026-09-11 rozšířil zadání na úplnou příkazovku pro všechny podporované
CAD operace a povolil pokračovat po samostatně testovaných a commitovaných krocích.
Tento seznam zachycuje skutečný stav; přítomnost CLI procesu sama neznamená
úplné pokrytí modelování. Napojení AI následuje až po společných příkazech.

## Kritéria dokončení

- Operace nad dokumentem používá stejnou datovou transakci z GUI i konzole/CLI.
- Objekty a geometrické reference se zadávají stabilními ZIMA ID a přesnou cestou
  výskytu, nikoli pořadovým číslem plochy OCCT nebo textem položky stromu.
- Čtení dat nepočítá tělesa. Výpočet je výslovný, se společným řešením referencí,
  vlastnictvím těles a sestav a odpovídající historií Undo/Redo.
- Příkazy mají ověřitelné argumenty, chyby, výsledek a dokumentované jednotky.
- Existují testy skutečných modelových výsledků a vstupu přes CLI; převáděná
  GUI cesta se ověřuje rovněž. Zamítnutý požadavek nesmí zanechat částečnou změnu.
- Exporty a ukládání ověřují skutečné soubory. Nezavádějí se povinné vedlejší
  soubory mimo nativní `.prtz`, `.asmz` a `.drwz`.

Čistě myší interakce (hover, tažení kamery, dialogový náhled) zůstávají adaptérem
GUI. Jejich modelový výsledek musí jít zadat příkazem. CLI bez View nevymýšlí
výběr ani kameru; k takové interakci používá explicitní reference a parametry.

## Postup a stav

| Oblast | Stav | Zbývající rozsah |
| --- | --- | --- |
| Proces CLI, UTF-8, skripty, stdin, config | Hotovo | Rozšiřovat testy nových příkazů |
| Katalog, kontext, datový strom | Hotovo pro současné příkazy | Doplňovat popisy a dotazy podle domén |
| Typované argumenty | Hotovo | Řetězce, čísla, celá čísla, boolean, objekty a pole; validace před mutací |
| New/Open/Save, Save As, aktivace/zavření, pracovní adresář | Hotovo | Přejmenování souborů a správa archivů jsou další samostatné operace |
| Regenerate, Undo/Redo | Společné pro Part/Assembly/Drawing | Doplňovat regrese dalších editačních operací |
| Kvádr | Hotovo | Společná tvorba, čtení a rozměrový patch; včetně zámků a přesnosti |
| Válec, koule, kužel, jehlan, klín | Hotovo | Společné create/get/set, zámky, přesnost, GUI/CLI a 59/59 regresí |
| Historie Partu | Hotovo | Společný přesun, ověření závislostí, potlačení, odstranění a kurzor; včetně historie těles a Booleanů |
| Tělesa a Boolean | Základ hotov | Tvorba, čtení, aktivace, název/viditelnost, kurzory a Boolean create/get/set; pořadí/mazání řeší historie; odvozené kopie pokrývá řádek Zrcadlo a pole; zbývá zadání referencí umístění |
| Umístění a původní reference | Původní reference a číselná editace umístění hotovy | `placement.get/set`: tělesa a prvky Partu, konstrukce Partu/Assembly a jejich body; `value_lock.list/set` sdílejí číselné zámky s GUI; zbývá příkazové přidávání/výměna referencí, vložené dráhy; umístění komponent řeší `component.set` |
| Konstrukční geometrie | Částečně hotovo | `construction.list/get/create/set/delete`: dotazy, tvorba a mazání kořenových bodů/os/rovin/3D křivek, vlastnosti, umístění, úplné seznamy bodů, tečny a zaoblení; dotazy zahrnují vložené 3D dráhy; jejich editaci potvrzuje příkaz tažení; zbývá zadání referencí |
| Skicář: geometrie | Základ hotov | 21 příkazů: samostatné a vložené skici, body, úsečky, kružnice, oblouky, elipsy, B-spline, obdélníky, mnohoúhelníky, posun a pomocná geometrie; text create/get/set s nativním písmem a spline get/set hotovy; DXF do vložených profilů hotov; zbývá kontrola dalších variant podle GUI |
| Skicář: vazby a operace | Vazby/kóty/solver/offset/trim/mirror hotovy | Offset create/get/set/free, úplný podklad a zachování intervalů, trim podle průsečíků, mirror, orientovaný obdélník, tečny a zaoblení rohu; všech 15 druhů vazeb, odstranění a solver; 16 druhů kót včetně vlastností, popisků a mazání; uvolnění externích referencí hotovo |
| Externí reference skici | Lokální i kontextová tvorba, obnova a odpojení | Přesná projekce, trim, společné potvrzení Partu a závislostí, vlastněné profily, Part/Assembly Undo/Redo a souhrny zavřených nativních vlastníků při explicitní regeneraci |
| Vytažení a rotace | Profily, Thin, směry a cíle zakončení Partu hotovy | `extrusion/revolution.create/get/set`, vlastněná skica, původní plochy a dvě nezávislé meze, společné OK; zbývají sestavové řezy |
| Tažení | Tvorba a geometrické vlastnosti hotovy | `sweep2d/sweep3d/helical.create/get/set`, společné GUI potvrzení, celá 3D dráha, stanice a úplná správa profilů/párování, reference roviny 2D dráhy a odsazení základní skici H-tažení; generické rozšíření umístění patří do řádku Umístění |
| Otvory a závity | Katalog, současný Otvor a vnější závit částečně hotovy | `thread.catalog`, `opening.create/get/set`: hladký/závitový otvor, rozměry, sražení, špička, směr a průchozí otvor; `shaft_thread.create/get/set` včetně původních referencí; zbývají cílové reference Otvoru Až k, samostatný Hole a operace vnořených částí otvoru; `drill_point.create/get/set` pokrývají samostatnou vrtací špičku |
| Zaoblení, zkosení, skořepina | Hotovo | `shell.faces/create/get/set`, `fillet.create/get/set`, `chamfer.create/get/set`; `edge_treatment.edges/route/remove`: skutečný vstup, společná tečná trasa a odebrání člena/trasy/posledního prvku podle stromového kontraktu |
| Zrcadlo a pole | Hotovo pro Part a bezprostřední komponenty Assembly | `derived_copy.sources`, `mirror.create/get/set`, `pattern.create/get/set`: společné zdroje a potvrzení GUI/CLI, roviny/osy, lineární i kruhové režimy, umístění, zámky, neuložené zdroje a Undo/Redo; vnořená aktivace patří do řádku Sestavy |
| Sestavy | Dotazy, vložení, otevření zdrojů, vlastnosti, odstranění a přesná aktivace hotovy | `component.set`: název, viditelnost, potlačení, uzemnění, umístění a všechny čtyři druhy vložených vazeb s mezemi/zámky; `component.remove` sdílí kontrolu závislostí a atomické mazání s GUI; `component.activate/deactivate` sdílejí přesný zdrojový kontext s GUI; souhrny referencí jsou společné pro Assembly Undo a explicitní regeneraci; zbývají řezy, oprava řetězce vazeb čeká na konkrétní souhlas |
| Výkresy | Listy, šablony, historie, tvorba/vlastnosti/dotazy/mazání pohledů, regenerace, modelové anotace, Show/Erase a měřené kóty (dotazy, tvorba, editace, řetězec, mazání), razítko, zdrojové parametry BOM, PDF, DXF a PNG/JPEG listu/výřezu hotovy | Další anotace, zdrojové styly šraf a příkazový snímek interaktivního View |
| Řezy, měření a vzhled | Zbývá | Datové operace a uložené výsledky |
| Parametry, relace a materiál | Společné tabulky a transakce hotovy | Parametry, jednotky, přesnost, relace, materiál včetně přímého načtení knihovny a uložené varianty; řízení rozměrů relacemi a generování variant nejsou dosud zavedené ani v GUI |
| Import a export | Import Partu/Assembly STEP/IGES/DXF a základní exporty hotovy | Společný STEP včetně vnořených sestav, STL Part/vnořená Assembly, DXF úsečky/osy/body/kružnice/oblouky/elipsy/spline/trimy/offsety; DXF do vložených profilů hotov; zbývá DXF text/rohová zaoblení, import POINT/neohraničených spline a snímek interaktivního View |

Každá další etapa aktualizuje tabulku a uvádí ověřené testy. Neobcházíme
chybějící operaci nevalidovanou změnou serializovaného dokumentu ani voláním
widgetů ze samostatného CLI. Plošný audit Undo/Redo zůstává samostatným úkolem;
regrese historie potřebné pro právě převáděnou operaci jsou součástí etapy.

Etapa správy dokumentů: katalog měl **34 příkazů**. Kompletní Windows Release
sada ověřila 58/59 testů; nová regrese odhalila chybějící přesměrování vlastního
řádku kusovníku v kopii výkresu. Po opravě prošlo všech **9/9 dotčených testů**
(16,71 s), včetně původně selhávající regrese. Podrobnosti a logy jsou v
[DOCUMENT_OPERATIONS.md](DOCUMENT_OPERATIONS.md).

Etapa těles přidala devět příkazů; katalog měl **43 příkazů**. Sdílené
vlastnosti těles a Booleanů, aktivace a kurzory prošly kompletní sadou **60/60**
(383,35 s) a závěrečnou sadou **6/6** po sjednocení výběru (12,28 s).
Podrobnosti jsou v [BODY_COMMANDS.md](BODY_COMMANDS.md).

Etapa historie přidala šest příkazů; katalog měl **49 příkazů**. Celá
Windows Release sada prošla **61/61** (386,26 s), včetně nových modelových,
procesových i GUI scénářů. Podrobnosti jsou v [HISTORY_COMMANDS.md](HISTORY_COMMANDS.md).
Další krok: dotazy na původní reference a příkazové zadání stávajícího umístění.

Etapa původních referencí přidala `reference.list/get`; katalog má nyní
**51 příkazů**. Dotčené čtyři modelové, hostitelské, GUI a procesové testy prošly
**4/4** (10,14 s). [REFERENCE_COMMANDS.md](REFERENCE_COMMANDS.md) popisuje data,
přesné výskyty, jednotky a limity. Další krok je příkazové zadání umístění.

### Schválený přesun společného umístění

Uživatel v tomto úkolu výslovně schválil přesun společné editace umístění a
výpočetní transakce z GUI do modelové vrstvy pro CLI odpovědí „ano povoluji.“
na konkrétní žádost. Dřívější automatické zamítnutí kvůli chybějícímu souhlasu
je tím vyřešené. Přesun má zachovat současné řešení referencí, vlastnictví,
orientace, offsetů, náhledů a perzistence; změny budou ověřeny pro ostatní
kontejnery, které stejný kód používají. Tento souhlas není změnou obecných
pravidel ochrany umístění v AGENTS.md.

Etapa geometrie skicáře přidala **21 příkazů**; katalog má **72 příkazů**.
Kompletní Windows Release sada prošla **63/63** (388,03 s),
`build/sketch-full-tests.log`. GUI kreslení a CLI sdílejí transakci geometrie
bez implicitního výpočtu tělesa; seznam skic nekopíruje všechny jejich křivky.
Nativní posun nyní přijímá také jediný střed kružnice a respektuje jeho vazby.
Podrobnosti: [SKETCH_COMMANDS.md](SKETCH_COMMANDS.md). Další etapa: operace
offset, trim a mirror, následně vazby/kóty a externí reference skic.

Etapa operací nad křivkami přidala **13 příkazů**, celkem **85**.
Integrační sada prošla **9/9** (27,80 s), včetně reálného CLI, konzole GUI,
skicáře, offsetů a přesných spline. Log: `build/sketch-curve-integration-tests.log`.
Nativní jádro operací zůstalo společné s GUI. Samostatný Extend se v současném
skicáři nevyskytuje; jeho případné nové chování je další modelovací úkol.
Další etapa: vazby a kóty skici, poté externí reference.

Etapa vazeb zpřístupnila všech **15 nativních druhů** prostřednictvím
`sketch.constraint.create/delete` a přidala `sketch.solve/solve_status`.
Katalog má **89 příkazů**. Integrační sada prošla **7/7** (11,69 s),
`build/sketch-relation-integration-tests.log`, včetně matematických kontrol,
reálného CLI a konzole GUI. Další etapa: kóty skicáře.


Etapa kót přidala `sketch.dimension.create/get/set/delete`, celkem **93 příkazů**.
Všech 16 druhů používá nativní factory a solver, hodnoty sdílejí validaci s GUI.
Celá Windows Release sada prošla **66/66** (395,03 s),
`build/sketch-dimension-full-tests.log`. Po doplnění odmítnutí otočené roviny
úhlové kóty a testu nativního uložení kót sestavy prošla závěrečná sada **6/6**
(15,83 s), `build/sketch-dimension-final-tests.log`; odpovídající sestavení
GUI i CLI je v `build/sketch-dimension-final-build.log`. Testy ověřují také
společné Undo hodnoty a popisku, číselný zámek, referenční měření, neplatné
vstupy, zachování posledního tělesa a jeho výslovný Regenerate. Další etapa:
externí reference a projekce křivek ze STEP.


Etapa externích referencí přidala čtyři příkazy, celkem **97**. Celá Windows
Release sada prošla **67/67** (389,69 s), `build/sketch-reference-full-tests.log`.
Po omezení průchodů historií a doplnění profilu Helical prošla závěrečná sada
**6/6** (18,92 s), `build/sketch-reference-final-tests.log`; finální GUI i CLI
odpovídají `build/sketch-reference-final-build.log`. Testy zahrnují původní
hrany, body, osy a plochy, chybné/dvojí zdroje, dopředné závislosti, racionální
spline se dvěma zobrazovacími body (odchylka kružnice pod 1e-12 mm²), ořezaný
podklad a offset po posunu o 0,01 mm (odchylka pod 1e-8 mm), zachování geometrie
při zmizení/odpojení zdroje, nativní soubory, dvě vnořené occurrence bez načtení
zdrojových souborů a skutečné CLI/GUI cesty. Následuje text a zbývající editační
operace skicáře, poté modelovací prvky podle tabulky pokrytí.


Etapa textu přidala `sketch.text.create/get/set`, celkem **100 příkazů**.
Text používá v GUI i CLI stejné zabudované OSIFONT, FreeType a HarfBuzz;
čtení a otevření souboru pouze převezme uložené obrysy. Celá Windows Release
sada prošla **69/69** (392,17 s), `build/sketch-text-full-tests.log`. Po
sjednocení UTF-8 překladu skicáře prošla závěrečná sada **6/6** (21,09 s),
`build/sketch-text-final-tests.log`; GUI a CLI jsou sestaveny podle
`build/sketch-text-final-build.log`. Ověřeno porovnání metrik s Qt, čeština
i skládání diakritiky, zarovnání, převrácení, natočení, uložení, Undo a
vytažení číslice s otvorem včetně nezávislé kontroly objemu. Modelování textu
normalizuje směr profilových smyček pro správné vnitřní otvory. Následují
zbývající parametrické editace skicáře a modelovací prvky.


Etapa vlastností B-spline přidala `sketch.bspline.get/set`, celkem **102 příkazů**.
Nativní editace je společná s dialogem a respektuje přesnou parametrizaci,
původní ID, pevné body, vazby i zdrojové řízení. Integrační sada prošla
**10/10** (31,45 s), `build/sketch-spline-integration-tests.log`; celé GUI a CLI
jsou přeložené v `build/sketch-spline-integration-build.log`. Matematické testy
ověřují kubický Bernsteinův výsledek a racionální kružnici (odchylka rovnice
pod 1e-12 mm²), aktualizaci offsetu, současný posun navázaných bodů, odmítnutí
neplatné změny, Undo a nativní uložení. Následují příkazy importu/exportu;
současný DXF writer podporuje pouze LINE/CIRCLE/ARC a nesmí přes konzoli
tvrdit úplný export ostatní geometrie.


Etapa importu Partu přidala `import.step`, `import.iges`, `import.dxf`, celkem
**105 příkazů**. Menu a CLI sdílejí jednu transakci s kontrolou revize,
generace, identity otevření a aktivního tělesa po doběhu úlohy. Opravena je
UTF-8 cesta i metadata STEP/IGES/DXF na Windows. Úplná sada ověřila **70/71**
testů (401,71 s), `build/part-import-full-tests.log`; jediná chyba byla
v novém testu výběru souboru z menu. QFileDialog s proxy modelem při načítání
adresáře zahodil `selectFile()`. Po opravě testu na zadání do skutečného pole
názvu souboru prošel i tento scénář **1/1** (9,01 s),
`build/part-import-gui-tests.log`. Finální GUI odpovídá
`build/part-import-gui-build.log`, ostatní binární soubory úplnému sestavení.
Ověřeny reálné STEP/IGES objemy, zvolená síť, DXF jednotky, zachování původní
topologie po smazání zdroje, native save/load, Undo/Redo, neplatné soubory,
zastaralý výpočet, nové otevření téhož dokumentu a čistý JSON protokol CLI.
Podrobnosti: [IMPORT_COMMANDS.md](IMPORT_COMMANDS.md). Dále exporty a import
Assembly.

Etapa exportu přidala `export.step/stl/dxf`, celkem **108 příkazů**.
GUI a CLI sdílejí export vypočteného snímku a dokončený soubor zveřejňují
atomicky; chybný výstup nepřepíše původní soubor. Regrese odhalila a opravila
STL zápis do české cesty na Windows. Podrobnosti a omezení jsou v
[EXPORT_COMMANDS.md](EXPORT_COMMANDS.md).
Celá Windows Release sada exportní etapy prošla **72/72** (397,87 s),
`build/export-full-tests.log`.

Importní příkazy rozšířeny na Assembly se společnou GUI transakcí. Katalog
zůstává na **108 příkazech**. STEP zachovává hierarchii a sdílení zdrojů,
IGES/DXF vytváří jeden Part. Opožděný výpočet nebo chyba zápisu nezanechá
částečné vložení do cíle. Podrobnosti: [IMPORT_COMMANDS.md](IMPORT_COMMANDS.md).
Integrační testy importu sestav **7/7** (17,00 s); závěrečné hraniční, CLI
a GUI regrese **3/3** (15,52 s). Logy jsou v dokumentaci importu.

Etapa parametrů a nastavení přidává `document.parameters.get/set`
a `document.settings.get/set`, celkem **112 příkazů**. GUI i CLI sdílejí
validaci, jazykové varianty a transakce. Pouhá změna jednotek zachovává
geometrii; změna skutečné výpočetní přesnosti potvrdí také potřebný místní
výpočet, aby cache zůstala uložitelná. Rodiče se neobnovují. Podrobnosti:
[METADATA_COMMANDS.md](METADATA_COMMANDS.md).
Celá Windows Release sada této etapy: **74/74** (405,58 s),
`build/metadata-full-tests.log`.

Etapa technických metadat přidává šest příkazů `document.relations.get/set`,
`document.material.get/set`, `document.family.get/set`, celkem **118 příkazů**.
GUI a CLI sdílejí validaci a transakce, zachovávají B-Rep i rodičovské sestavy.
Cílené modelové, CLI a GUI testy prošly **5/5** (16,77 s). Podrobnosti:
[ENGINEERING_METADATA_COMMANDS.md](ENGINEERING_METADATA_COMMANDS.md).
Celá Windows Release sada: **75/75** (406,56 s). Závěrečná kontrola omezení
nativního textového zápisu: **6/6** (24,23 s). Logy jsou uvedeny v dokumentaci etapy.

Přímé přiřazení `.matz` sdílí nativní čteč a potvrzení s GUI:
`document.material.load`, celkem **119 příkazů**. Ověřeno všech 62 dodávaných
materiálů a celá cílená integrační sada **6/6** (25,05 s),
`build/material-library-integration-tests.log`.

Komponenty: `component.list/get/insert`, celkem **122 příkazů**. Vložení sdílí
GUI operaci a odmítá cykly i chybu relace před transakcí; dotazy čtou uložené
výskyty bez zdrojových souborů. Model/Workspace/import **3/3** (1,39 s),
CLI/GUI integrace **4/4** (17,30 s). Podrobnosti:
[COMPONENT_COMMANDS.md](COMPONENT_COMMANDS.md).
Celá Windows Release sada komponentové etapy: **77/77** (402,70 s),
`build/component-full-tests.log`.

Etapa otevření zdroje přidala `component.open`, celkem **123 příkazů**.
CLI i kontextové menu GUI používají stejnou kontrolu přesného výskytu, identity
souboru a souběžných změn dokumentu. Zdroj se otevře bez regenerace rodičů;
rozpracovaný otevřený dokument má přednost. Integrační sada prošla **5/5**
(27,35 s), `build/component-source-integration-tests.log`.
Podrobnosti jsou v [COMPONENT_COMMANDS.md](COMPONENT_COMMANDS.md).
Následují nezávislé výkresové operace; chráněná refaktorizace umístění nadále čeká.

Etapa výkresových listů přidala devět příkazů, celkem **132**. Společné
operace pokrývají listy, parametry, import/odstranění formátu a razítka;
`DrawingState` nově podporuje Undo/Redo se zachováním přidělených čísel kót
a ochranou uložení během změn historie. GUI používá stejné operace.
Cílená sada prošla **7/7** (21,42 s), `build/drawing-sheet-integration-tests.log`.
Podrobnosti: [DRAWING_COMMANDS.md](DRAWING_COMMANDS.md). Následují výkresové
pohledy a jejich uložená data.

Celá Windows Release sada této etapy prošla **79/79** (407,77 s),
`build/drawing-sheet-full-tests.log`, včetně obou výsledných programů GUI a CLI.

Etapa pohledů přidala `drawing.view.list/get/references/delete`, celkem
**136 příkazů**, a rozšířila `regenerate` na výkresy. Načítání anotací,
řezových údajů a kusovníku přešlo z GUI do Workspace; oba vstupy sdílejí
regeneraci a odstranění pohledů. Opraven je rozdíl otevřeného a zavřeného
zdroje (skici, konstrukce, původní reference) i Unicode cesty v kusovníku.
Integrační sada prošla **12/12** (36,66 s), `build/drawing-view-final-tests.log`.
Podrobnosti a limity: [DRAWING_COMMANDS.md](DRAWING_COMMANDS.md). Následuje
tvorba a parametrická editace pohledů.

Závěrečná kontrola zdrojů a vstupů prošla **3/3** (17,21 s),
`build/drawing-view-source-tests.log`: navíc pouze skica bez tělesa, přípona
`.PRTZ`, český název a čitelně rozmístěné pohledy v GUI. Finální sestavení
odpovídá `build/drawing-view-source-build.log`.

Etapa vytvoření a vlastností pohledů přidává `drawing.view.create/set`, celkem
**138 příkazů**. GUI a CLI sdílejí výpočet projekce, atomický návrh změny,
aktualizaci potomků a měřených kót. Pokryté jsou orientace, vlastní kamera,
měřítko, papírová poloha, styly, řezy a trasy. Opraveno je načtení kusovníku
neuloženého otevřeného dílu na Windows. Celá Windows Release sada prošla
**80/80** (410,05 s), `build/drawing-edit-full-tests.log`; oba výsledné programy
jsou sestavené. Testy zahrnují skutečné GUI i CLI, Undo, zachování přesných
referencí, chybu pozdějšího potomka bez částečného zápisu, zámky, měřítka,
neuložené zdroje, řez i nativní uložení. GUI je vizuálně ověřeno na
`Projects/test/command-drawing-views.png`.


Etapa modelových anotací přidává `drawing.annotation.list/show_erase`, celkem
**140 příkazů**. Dotazy čtou uložená data a sdílejí nabídku GUI bez kopie
geometrie. GUI i konzole potvrzují viditelnost stejnou atomickou operací;
více pohledů je jeden krok Undo/Redo. Přesné reference zahrnují identitu
zdrojového dokumentu, vlastníka, sémantický klíč a cestu výskytu. Neplatné
reference jsou čitelné pro diagnostiku, ale nejsou nabízené pro Show/Erase.
Příkazy nevyvolávají OCCT ani otevření zdrojů a nemění nativní formát.
Podrobnosti: [DRAWING_COMMANDS.md](DRAWING_COMMANDS.md).

Sestavení GUI i CLI a integrační ověření prošlo **6/6** (21,27 s),
`build/drawing-annotation-tests.log`: nativní model, přesné výskyty, atomická
dávka, Undo/Redo, skutečný CLI proces, GUI konzole, blokace zápisu během
náhledu a stávající dialog Show/Erase včetně více pohledů a Cancel.


Etapa dotazů a mazání měřených kót přidává `drawing.dimension.list/get/delete`,
celkem **143 příkazů**. Dotazy zahrnují lineární, radiální, průměrové,
řetězové a úhlové kóty; stav a poslední platnou hodnotu neplatné kóty rozlišují
od aktuálního měření. Čtou přesná napojení a prezentační údaje z `.drwz`
bez načítání zdrojů. GUI Delete a kontextové menu sdílejí operaci mazání
s CLI, včetně historie a zachování přidělených čísel.
Následuje tvorba a editace kót se stejnou nativní validací jako jejich dialog.

Ověřeno sestavením GUI i CLI a cílenou sadou **6/6** (23,53 s),
`build/drawing-dimension-command-tests.log`. Testy zahrnují hodnotu 60°,
původní výskyt, poslední hodnotu neplatné kóty, skrytý průmět rádiusu,
filtry, mazání přes CLI i skutečnou klávesu Delete v GUI, Undo/Redo,
uchování přidělených čísel a nativní uložení.


Etapa tvorby a vlastností měřených kót přidává `drawing.dimension.create/set/extend`,
celkem **146 příkazů**. Potvrzení dialogu i CLI sdílí atomické ověření referencí
a měření. Pokryté jsou všechny současné druhy měřených kót, napojení, směry,
styly/tolerance, rozložení a umístění; řetězec se rozšiřuje z obou konců se
zachováním původních identit. Neplatnou referenci lze opravit přesným nahrazením,
bez hledání podobné hrany. Výsledek nevytváří nové zdrojové soubory a nevolá OCCT.

Integrační sada nových příkazů prošla **6/6** (21,36 s),
`build/drawing-dimension-edit-integration-tests.log`; následně celá
Windows Release regrese **82/82** (376,97 s),
`build/drawing-dimension-edit-full-tests.log`. GUI i samostatné CLI jsou
sestavené. Testy ověřují tvorbu, 60°/120°, opravu reference, oba konce
řetězce, identity, parametry a atomické zamítnutí, Undo/Redo, nativní
uložení a skutečné zobrazení kóty vytvořené z konzole. Snímek
`Projects/test/command-drawing-views.png` prošel vizuální kontrolou.


Etapa razítka a zdrojových parametrů kusovníku přidává
`drawing.bom.list`, `drawing.title.get/set`, celkem **149 příkazů**.
GUI a CLI sdílejí ověření zapisovatelných polí, původních hodnot a přesné
identity zdroje. Modelové parametry se mění ve zdrojovém dokumentu, lokální
hodnoty ve výkresu; uložení i Undo zdrojového dokumentu zůstávají samostatné.
Nevyvolává se OCCT ani regenerace nadřazené sestavy. Podrobnosti a příklady:
[DRAWING_COMMANDS.md](DRAWING_COMMANDS.md). Kompletní příkazovka dosud hotová
není; zbývají další modelovací funkce a dosud nepokryté výkresové operace.

Ověření etapy: **70/70** nezávislých testů (112,97 s),
`build/drawing-title-independent-tests.log`, a **13/13** scénářů hlavního GUI
(269,82 s), `build/drawing-title-gui-tests.log`. Dohromady všech **83 testů**.
Závěrečná cílená sada po doplnění kontroly duplicitních polí prošla **6/6**
(9,83 s), `build/drawing-title-tests.log`. Ověřené jsou přesné zdroje řádků,
opakované výskyty, vypočítané parametry, odmítnutí zastaralých a rozporných
hodnot, výsledky parametrických vztahů v kusovníku, Undo/Redo, explicitní
uložení, UTF-8 a obousměrná editace mezi konzolí a dialogem. Dialog razítka
s reálnou šablonou prošel také vizuální kontrolou.

Běžící uživatelský CAD zamykal `zima-cad-cpp.exe`. CLI a samostatné testy byly
sestaveny běžným CMake postupem. Hlavní GUI bylo pro tuto regresi slinkováno ze
stejných aktuálních CMake objektů a knihoven do `zima-cad-title-validation.exe`
ve stejném build adresáři; dočasná kopie CTest definic změnila pouze tuto cestu.
Nejde o distribuční balíček. Původní spouštěcí soubor nebyl přepsán a běžící
program nebyl ukončen; jeho běžné sestavení je potřeba dokončit po zavření CADu.


Etapa diagnostiky komponent přidává `component.dependencies`, celkem
**150 příkazů**. CLI a GUI mazání sdílejí dosavadní kontrolu blokujících vazeb,
závislých komponent a externích referencí skic. Výsledek rozlišuje jednotlivé
výskyty a jejich bezprostřední vlastníky; je bez čtení zdrojových souborů,
OCCT, řešení vazeb a zápisu historie. Samotné příkazové mazání a změny
společného umístění tím ještě nejsou zavedené.

Ověřeno **4/4** cílených testů (7,76 s) a **2/2** scénářů hlavního GUI
(18,48 s): `build/component-dependencies-tests.log` a
`build/component-dependencies-gui-tests.log`. Dokumentace:
[COMPONENT_COMMANDS.md](COMPONENT_COMMANDS.md).


Etapa PDF přidává `export.pdf`, celkem **151 příkazů**. Plátno, GUI PDF i CLI
sdílejí neinteraktivní renderer listu. Export obsahuje všechny listy, nativní
průměty, kóty, řezy, šrafy a razítka, bez OCCT a historie. Příkazové Qt běží
v režimu `offscreen`; žádné okno se nevytváří. Dokončený soubor se publikuje
atomicky stejným mechanismem jako STEP/STL/DXF. Viz
[DRAWING_COMMANDS.md](DRAWING_COMMANDS.md) a [CAD_COMMAND_LINE.md](CAD_COMMAND_LINE.md).

Ověřeno **84/84 testů** (381,82 s), `build/drawing-pdf-full-tests.log`.
Cílená sada PDF/exportů/CLI a výkresového GUI předtím prošla **7/7** (11,91 s).
Testy zahrnují skutečný CLI proces s vynuceným offscreen režimem, více listů,
UTF-8 cesty, neuložené zdrojové parametry, nezměněnou historii a atomické
zachování původního PDF při chybě na druhém listu. Nezávislé čtení PDF ověřilo
počet stran, český text a rozměry A4/A3 s tolerancí 0,25 mm pro zaokrouhlení
Qt na tiskové body; rastrové náhledy obou listů a řezu prošly vizuální kontrolou.
`dumpbin /dependents` potvrzuje Qt Core/Gui/Svg bez Qt Widgets v CLI.

Běžící uživatelský CAD zůstal otevřený. Hlavní GUI bylo pro úplnou regresi
slinkováno z aktuálních CMake objektů a knihoven do
`zima-cad-pdf-validation.exe`; kopie definic CTest měnila pouze cestu tohoto
programu. CLI, samostatný výkresový harness a všechny testovací programy byly
sestaveny běžným CMake postupem. Běžný `zima-cad-cpp.exe` lze přepsat novým
sestavením po zavření uživatelského CADu. Nejde o distribuční balíček.


Etapa DXF rozšiřuje stávající `export.dxf` o výkres s výslovným ID listu;
katalog zůstává na **151 příkazech**. GUI a CLI sdílejí výkresový zapisovač,
renderer listu, kontext zdrojových parametrů i atomické zveřejnění souboru.
Modelový export skici má zachovaný rozsah a pořadí pozičních argumentů.
Formát nativních dokumentů ani šablon se nemění. Podrobnosti:
[DRAWING_COMMANDS.md](DRAWING_COMMANDS.md) a [EXPORT_COMMANDS.md](EXPORT_COMMANDS.md).

Ověřeno **10/10 cílených testů** (29,03 s),
`build/drawing-dxf-verified-tests.log`: překlady, skutečný CLI proces,
DXF/PDF příkazy, dosavadní modelové exporty, katalog příkazů, hlavní GUI
konzole, Show/Erase, měřené kóty a výkresové GUI. Test čte souřadnice DXF
nezávisle na rendereru a ověřuje měřítko 2:1, směr os, kruhový obrys,
tloušťky/typy čar, češtinu, konkrétní druhý list, živé neuložené parametry,
nezměněnou historii, ochranu aktivního kontextu a atomické chyby.
GUI a konzole vytvořily bajtově shodný DXF. Nový test rozšiřuje celou sadu
na 85 testů; v této etapě byla znovu spuštěna dotčená sada deseti testů,
nikoli všech 85. Předchozí úplná regrese PDF měla 84/84.

CLI a výkresový harness jsou sestaveny. Uživatelský CAD stále běžel;
GUI regrese proto použila `zima-cad-dxf-validation.exe`, slinkovaný
z aktuálních CMake objektů a knihoven. Dočasná kopie definic CTest změnila
jen cestu k hlavnímu GUI. Běžný zamčený `zima-cad-cpp.exe` nebyl přepsán;
po zavření CADu zbývá běžný link tohoto programu. Nejde o distribuční build.


Etapa obrazového exportu přidává `export.image`, celkem **152 příkazů**.
PNG/JPEG obsahuje jeden list nebo výslovný výřez papíru při zadaném DPI,
se stejným rendererem jako GUI/PDF. GUI JPEG snímek sdílí atomický kodér
při zachování původního zobrazení. Nadlimitní obrázek se odmítne před alokací;
formát nativních dokumentů a šablon se nemění. CLI nasazuje vlastní JPEG
plugin stejným CMake postupem jako GUI. Podrobnosti:
[DRAWING_COMMANDS.md](DRAWING_COMMANDS.md).

Ověřeno **11/11 cílených testů** (30,73 s),
`build/drawing-image-final-tests.log`. Samostatný test příkazu kontroluje
PNG/JPEG dekódování, velikost i DPI, přesnou shodu výřezu s odpovídajícími
pixely celého listu, mez velikosti před alokací, chybové vstupy a zachování
souboru/modelu. Skutečný CLI proces exportuje oba formáty bez okna.
GUI JPEG je bajtově shodné s dosavadním snímkem plátna. Prošly také
PDF/DXF, modelové exporty, konzole, výkresové kóty a překlady.

Nezávislé načtení knihovnou Pillow potvrdilo 2100×2970 pixelů pro celý list
A4 a 600×400 pro výřez 60×40 mm, vše při 254 DPI. Náhled českého textu prošel
vizuální kontrolou. JPEG kvalita nezasahuje do PNG komprese: kontrolní řídký
list má bezztrátově 30 843 bajtů místo 18 743 977 při vypnuté kompresi.
Test chrání i toto nastavení. Celá sada má nyní 86 testů; tato etapa spustila
výše uvedených 11 dotčených testů.

CLI a výkresový harness jsou sestavené; hlavní GUI regrese používá aktuální
objekty v `zima-cad-image-validation.exe`. Běžící uživatelský CAD nebyl
ukončen a jeho zamčený `zima-cad-cpp.exe` nebyl přepsán. Po zavření zbývá
běžný link hlavního programu. Jde o místní vývojové sestavení.


Etapa cíleného DXF doplňuje `import.dxf` do vložených profilů Partu a
existujících skic kořenové Assembly. Katalog zůstává na **152 příkazech**.
Převod GUI návrhu a uloženého příkazového cíle je společný. GUI import
uvnitř vlastněného skicáře respektuje OK/Cancel vlastníka; CLI zachovává
vlastníka, jiné profily, vypočtené těleso a jednu Undo transakci.
Sdílené umístění kontejnerů ani nativní formát se nemění. Viz
[IMPORT_COMMANDS.md](IMPORT_COMMANDS.md).

Ověřeno všech **9 dotčených scénářů**: nativní import, překlady, skutečný
CLI proces, import sestav, cílený import, editace skic, katalog, GUI konzole
a vlastněný profil v GUI. V `build/sketch-dxf-checked-tests.log` prošlo osm
scénářů; poslední modelový test po opravě porovnání vůči inicializovanému
profilu samostatně prošel **1/1** (0,43 s),
`build/sketch-dxf-target-tests.log`. Výsledný kód je ve všech těchto bězích
shodný, závěrečná změna upravila jen výchozí stav a diagnostiku testu.

Regrese ověřují přesný druhý vložený profil, nezměněného sourozence,
žádný nový samostatný objekt/komponentu, zachovaný vypočtený tvar,
Undo/Redo, explicitní regeneraci a nativní uložení, neaktivní těleso,
novější editaci a zavření/znovuotevření během importu. GUI test importuje
kružnici do návrhu Protrusion přes skutečné menu a ověřuje Cancel i OK.
CLI proces uloží a znovu načte DXF uvnitř serializovaného profilu Sweep/Loftu.

GUI je slinkované jako `zima-cad-sketch-dxf-validation.exe` z aktuálních
CMake objektů; CLI a testy jsou přeložené běžně. Běžící uživatelský CAD
zůstal otevřený a jeho hlavní spouštěcí soubor zůstává zamčený.


STL export nyní podporuje také vnořené sestavy, opakované výskyty a jejich
složené transformace. Používá existující skládání vypočtených komponent,
včetně hotového výsledku řezu; nemění umístění ani historii modelu.
Katalog zůstává na **152 příkazech**. Integrační sada prošla **7/7**
(27,02 s), `build/nested-stl-integration-tests.log`, a regrese STEP **1/1**
(1,31 s), `build/nested-stl-step-regression.log`. Podrobnosti a limity jsou
v [EXPORT_COMMANDS.md](EXPORT_COMMANDS.md).


Přesný skicový DXF export nyní zahrnuje elipsy a jejich oblouky, racionální,
interpolované a periodické spline, uložené trimy/offsety, samostatné body a osy.
Zapisovač je oddělen od importního parseru; počet příkazů zůstává **152**.
Integrační sada prošla **8/8** (26,16 s), `build/dxf-curves-integration-tests.log`,
a výměnné kontrakty **1/1** (0,09 s), `build/dxf-curves-interchange-tests.log`.
Nezávislé načtení a geometrické ověření přes ezdxf 1.4.4 prošlo bez chyb a oprav.
Text, rohová zaoblení a rozšíření zpětného DXF importu zůstávají další etapou;
podrobnosti v [EXPORT_COMMANDS.md](EXPORT_COMMANDS.md).


Navazující DXF import přijímá elipsy, jejich oblouky, přesné ohraničené spline
a nekonečné osy. Nový blok nesdílí body se staršími importy, včetně kružnic
a oblouků. Ověřeno **10/10** integračních testů (38,20 s),
`build/dxf-import-curves-integration-tests.log`, a dodatečné modelové testy
objemu **1/1** (0,45 s) a jednotek **1/1** (0,10 s). Příkazů je stále **152**.
Samostatné POINT a jiné než ohraničené řídicí reprezentace spline zůstávají
nepodporované. Podrobnosti: [IMPORT_COMMANDS.md](IMPORT_COMMANDS.md).


Etapa konstrukčních dotazů přidává `construction.list/get`, celkem
**154 příkazů**. Body, osy, roviny, 3D křivky a jejich body se čtou přímo
z modelu v příslušném lokálním rámci; dotazy zachovávají cache, historii
a diagnostiku chybějících referencí. Kompletní dotčená sada prošla **6/6**
(26,86 s), `build/construction-query-final-tests.log`. GUI, skutečný CLI,
nativní Part/Assembly a stránkování jsou ověřené. Podrobnosti a zbývající
rozsah: [CONSTRUCTION_COMMANDS.md](CONSTRUCTION_COMMANDS.md).

Navazující tvorba/editace konstrukcí má sdílet stávající potvrzovací
transakci dialogu a nativní řešení umístění. Přesun číselné editace a
společné výpočetní transakce se v etapě dotazů neprovedl; následný výslovný
souhlas uživatele je zaznamenán výše. Nativní formáty
ani start šablony se nemění. Běžící CAD nebyl ukončen; testovací GUI
je `zima-cad-construction-validation.exe`.


Schválená etapa společného umístění přidává `placement.get/set`, celkem
**156 příkazů**. GUI inline editace těles, prvků a konstrukcí používá
společné číselné přiřazení; konstrukční Properties a inline editace sdílejí
potvrzení a Part parametry společnou výpočetní transakci. Zůstává nativní
řešení referencí a atomická historie. Příkaz respektuje i samostatné zámky
korekčních úhlů a lokální rámec bodu v otočené křivce.

Úplná sada prošla **87/88** (382,41 s), `build/placement-full-tests.log`;
nový GUI test hledal souřadnicové pole pod nesprávným názvem. Po opravě
sémantického výběru pole, odstranění nadbytečného kopírování dokumentu a
doplnění korekčního zámku prošla závěrečná dotčená sada **9/9** (81,10 s),
`build/placement-verified-tests.log`. Podrobnosti, argumenty a zbývající
rozsah: [PLACEMENT_COMMANDS.md](PLACEMENT_COMMANDS.md). Následuje tvorba
a obecné vlastnosti konstrukcí a zadávání referencí. Úplná CLI ještě hotová
není; nativní formát a start šablony se v této etapě nemění.

Po zavření uživatelského CADu je dokončený běžný Windows Release build obou
programů. Start instancí, GUI konzole a skutečný CLI proces prošly **3/3**
(29,51 s), `build/placement-normal-tests.log`. Tím je uzavřeno dříve odložené
slinkování běžného EXE; ověření nevyžaduje alternativní testovací program.


Etapa tvorby a vlastností konstrukcí přidává `construction.create/set`,
celkem **158 příkazů**. Body, osy a roviny se tvoří v aktivním vlastníku;
obecné vlastnosti a umístění sdílejí transakci s GUI. Byla sjednocena
příprava směru osy, obnovena odvozená poloha entity roviny při nativním čtení
a přenesena kontrola celé 3D dráhy také na přímou editaci jejího bodu.

Oba běžné programy a všechny testovací programy jsou sestavené. Úplná
Windows Release regrese prošla **88/88** (389,12 s),
`build/construction-edit-full-tests.log`. Podrobnosti:
[CONSTRUCTION_COMMANDS.md](CONSTRUCTION_COMMANDS.md). Nativní formáty a
start šablony se nemění. Další rozsah jsou konstrukční reference, tvorba
a vlastnosti 3D křivek a další modelovací příkazy; úplná CLI ještě hotová není.


Etapa samostatných 3D křivek rozšiřuje `construction.create/set`; katalog
zůstává na **158 příkazech**. Part i Assembly podporují přesnou lomenou čáru,
zaoblení, interpolační spline, úplné seznamy bodů se zachováním ID, tečny
a lokální umístění. Úplná sada prošla **89/89**; po dodatečné opravě přípravy
rámce prošlo **8/8** dotčených regresí a doplňkový test vlastního počátku.
Podrobnosti a logy jsou v [CONSTRUCTION_COMMANDS.md](CONSTRUCTION_COMMANDS.md).
Další etapa převádí vytažení a rotaci včetně vlastněných profilů; zadání nových
referencí umístění a ostatní řádky tabulky nadále zůstávají otevřené.


Etapa vytažení a rotace přidává šest příkazů `extrusion/revolution.create/get/set`,
celkem **164 příkazů**. Samostatná skica se mění na vlastněný profil při zachování
kontejneru, počátku a historie; CLI i Vlastnosti používají společné potvrzení.
Objemové testy kontrolují délky, úhly, směry, řezy, Undo/Redo a nativní uložení.
Přesunutý pomocný kód bezpečně uchovává první rovinnou referenci před odstraněním
staré orientační položky. Potvrzovaný návrh nemůže převzít skicu jiného prvku.

Úplná Windows Release sada prošla **90/90** (420,90 s),
`build/profile-full-tests.log`; po doplnění uvedených ochran a překladu chyb
prošlo **9/9** dotčených regresí (112,75 s), `build/profile-final-tests.log`.
Oba běžné programy i testy jsou přeložené. Formáty ani start šablony se nemění.

Testy zároveň prokázaly starou chybu Thin: plný válec místo stěny. Nové společné
OK/CLI v této předchozí etapě Thin výslovně odmítalo bez změny dokumentu, dokud nebude opraven jeho
skutečný výpočet. Ten je následující krok; dále zbývá zadávání cílů zakončení
a ostatní řádky tabulky. Úplná CLI tedy ještě hotová není. Podrobnosti:
[PROFILE_COMMANDS.md](PROFILE_COMMANDS.md).


Etapa Thin opravuje skutečný výpočet stěn pro vytažení a rotaci. Sdílí přesné
odsazení profilů s tažením, podporuje otevřené konce i uzavřené kontury,
zachovává původní identitu křivek/bodů a souhlas náhledu s vypočteným tělesem.
Samostatná spline se aproximuje s kontrolovanou odchylkou nejvýše 1e-7 mm a
uloží jako B-spline, ne pouze zobrazovací lomená čára. Katalog zůstává na **164**.

Úplná sada ověřila **90/91** (427,39 s); ojediněle vypršel limit automatického
souborového dialogu DXF. Stejný scénář následně prošel samostatně a po doplnění
diagnostiky třikrát za sebou. Příčina timeoutu není potvrzena. Všechny nové
Thin modelové, CLI i GUI kontroly prošly; zpřísněná kontrola spline naměřila
odchylku objemu 1,990028e-9 mm³. Podrobnosti, omezení a logy:
[PROFILE_COMMANDS.md](PROFILE_COMMANDS.md). Nativní formát ani start šablony
se nemění. Pokračujeme cílovými referencemi vysunutí a dalšími řádky tabulky.


Navazující kernelová etapa připravila nezávislé dvě meze vysunutí a kombinaci
cílové plochy s průchozím řezem. Kontroluje celý profil vůči šikmé rovině a
při výpočtu používá aktuální původní plochu. Stávající související regrese
prošly, nová objemová zkouška prošla po opravě jejího vlastnictví zásoby.
Nativní adaptéry, náhled a příkazové zadání cílů zůstávají právě dokončovaným
krokem; katalog má nadále **164 příkazů**. Podrobnosti:
[PROFILE_COMMANDS.md](PROFILE_COMMANDS.md).


Etapa cílových referencí vysunutí doplňuje `targets_forward/targets_reverse`
do `extrusion.create/set`, nativní obnovu cílů, dvě nezávislé meze a symetrii.
GUI vybírá stejné původní plochy jako CLI. Vazby mezi posunutými/natočenými
tělesy se počítají v jejich vlastních souřadnicích; pozdější řez nezamění
původní plochu za výsledný fragment. Hrubé vykreslení nesmí definovat rovinnost.
Katalog má nadále **164 příkazů**, formát ani start šablony se nemění.

Úplné sestavení a Windows Release sada prošly **92/92** (421,62 s),
`build/extrusion-target-complete-tests.log`. Po závěrečné opravě rozpoznání
hrubě vykreslené plochy a doplnění nápovědy/překladů prošlo **9/9** souvisejících
modelových, CLI a GUI regresí (47,02 s), `build/extrusion-target-final-tests.log`.
Podrobnosti jsou v [PROFILE_COMMANDS.md](PROFILE_COMMANDS.md).
Další etapa pokrývá vlastnosti a tvorbu tažení včetně vložených profilů a drah;
sestavové řezy a ostatní otevřené řádky zůstávají součástí celkového cíle CLI.


Etapa vlastností tažení přidává šest příkazů `sweep2d/sweep3d/helical.get/set`,
katalog má **170 příkazů**. GUI tvorba i editace používá stejné explicitní
potvrzení s validací, výpočtem a Undo. Test uložení odhalil předčasné přerámování
šroubovicových skic při načítání; nyní nastává až po načtení umístění prvku.

Cílené regrese prošly **9/9** (120,57 s), `build/sweep-command-gui-tests.log`.
Oba programy jsou sestavené a úplná Windows Release sada prošla **93/93**
(432,54 s), `build/sweep-command-full-tests.log`. Ověřené jsou nezávislé objemy,
Thin, posun/rotace, zámky, atomické chyby, GUI/CLI, Undo/Redo, nativní uložení
i studený výpočet. Formát ani start šablony se nemění. Podrobnosti:
[SWEEP_COMMANDS.md](SWEEP_COMMANDS.md). Další část tažení zahrnuje příkazovou
tvorbu, správu profilů, stanice a vloženou dráhu; ostatní otevřené řádky zůstávají.


Navazující etapa přidává úplnou vloženou 3D dráhu do `sweep3d.set path`:
pořadí, přidání/odebrání bodů, rádiusy, tečny, polyline i interpolovanou spline.
Přeživší body zachovají ID; změna jde přes stejný Sweep commit jako hlavní OK.
`construction.list/get` nyní čte i dráhu a její body s vlastnícím prvkem;
`sweep3d.get` vrací aktivní/příchozí stanice. Parametry 3D křivek sdílí jeden
validátor pro samostatnou i vloženou dráhu.

Ověřeno **7/7** souvisejících regresí (61,86 s), `build/sweep-path-gui-tests.log`,
a závěrečná geometrická zkouška rámců tělesa a neplatné dráhy **1/1** (4,99 s),
`build/sweep-path-frame-tests.log`. Reálný GUI test kontroluje také vnořené
Vlastnosti bodu, hlavní OK/Cancel, Undo a uložený objem. Oba programy jsou
sestavené; katalog zůstává na **170**, formát ani start šablony se nemění.
Podrobnosti: [SWEEP_COMMANDS.md](SWEEP_COMMANDS.md). Pokračuje správa profilů,
reference roviny 2D dráhy a příkazová tvorba tažení.

### Tvorba 3D tažení z nativních vstupů

`sweep3d.create` přebírá samostatnou 3D dráhu a profilové skici do jednoho
kontejneru. Zachovává jejich ID a původní umístění dráhy, nevytváří druhé
kopie a vrací vstupy přes Undo. Katalog má **171 příkazů**. Profily se předávají
stabilními ID skic a bodů, včetně příchozí větve a počátku korespondence.
Kontroly odmítají cizí stanice, neaktivní nebo sdílené vstupy i chybný výpočet
bez částečné změny. Společná historie zároveň nově rozpoznává vlastníka
vložených skic a bodů, takže přesun neobejde závislosti na tažení.
Podrobnosti a nezávislé geometrické kontroly: [SWEEP_COMMANDS.md](SWEEP_COMMANDS.md).
Formáty ani start šablony se nemění.

Závěrečné sestavení obou programů a všech testů prošlo
(`build/sweep-create-full-build.log`). Úplný běh měl **91/93** úspěšných testů
(478,27 s, `build/sweep-create-full-tests.log`): inspektor měření překročil
90s limit a GUI potvrzení sestavového profilu jednou selhalo. Se stejnými
binárními soubory prošlo samostatné opakování profilu (89,51 s) a následně
oba dotčené testy **2/2** (90,14 s, `build/sweep-create-ui-recheck-tests.log`).
Příčina nepravidelného GUI selhání není prokázána; nejde o tvrzení, že původní
úplný běh prošel celý. Geometrické, CLI a GUI testy nového tažení prošly.

### Profilové stanice 2D a 3D tažení

`sweep2d/sweep3d.set profiles` nyní přebírá další samostatné skici, zachovává
existující profily, odstraňuje vynechané, mění jejich stanice a počátky
párování obvodu. `get` vrací stanice i u 2D dráhy. Ochrana historie brání
závislosti nového profilu na vlastním nebo pozdějším prvku; nová vazba musí
patřit aktivní stanici. Existující neaktivní profily se při změně dráhy
neztrácejí. Katalog má stále **171 příkazů**, nativní formát se nemění.

Oba programy a testy jsou sestavené. Související sada prošla **15/15**
(128,34 s); po dodatečné opravě neexistující větve stanice prošla závěrečná
sada **5/5** (53,86 s), včetně skutečného CLI procesu a GUI skicáře.
Logy, nezávislé objemy a přesný kontrakt: [SWEEP_COMMANDS.md](SWEEP_COMMANDS.md).

### Původní rovina 2D dráhy

`sweep2d.set path_plane` nastavuje, mění a odstraňuje referenci roviny
dráhy včetně odsazení. Zdrojem je původní dostupná rovina/plochy tohoto
Partu nebo rovina vlastního počátku. Společný commit kontroluje vlastnictví
a pořadí historie; nevytváří nový solver ani OCCT picker. Testy ověřily
konstrukční rovinu s nenulovým odsazením, natočený vlastní počátek, původní
plochu jiného tělesa, změnu tělesového rámce, CLI, GUI OK/Cancel a uložení.

Související sada prošla **14/15** (126,33 s); po opravách přípravy posledního
modelového scénáře prošel tento test **1/1** (6,73 s). Produkční kód se
mezi těmito běhy neměnil. Podrobnosti: [SWEEP_COMMANDS.md](SWEEP_COMMANDS.md).
Oba programy jsou sestavené; katalog zůstává na **171**, formát a šablony
se nemění. Z tažení zbývá příkazová tvorba 2D a šroubovicové varianty.


### Tvorba 2D tažení ze samostatných skic

`sweep2d.create` přebírá skicu dráhy a profily do jednoho nového prvku.
Zachovává původní ID geometrie, rovinu, odsazení, živé reference a fyzickou
orientaci dráhy. Převod respektuje zvláštní čtvrtotáčku referencované skici;
společný solver umístění se nemění. Jeden Undo obnoví samostatné vstupy.
Katalog má **172 příkazů**, formát a start šablony se nemění.

Oba programy a všechny testy jsou sestavené. Související sada prošla **14/15**
(133,92 s); po opravě stanice v přípravě nového obloukového scénáře prošel
celý modelový test **1/1** (11,72 s), beze změny produkčního kódu mezi běhy.
Ověřeno je 72 prostorových kombinací, přesné Undo, atomické chyby, živé
odsazení, obloukový Solid/Thin, nativní uložení, skutečný CLI proces a GUI
Vlastnosti. Podrobnosti a logy: [SWEEP_COMMANDS.md](SWEEP_COMMANDS.md).
Z tažení zbývá příkazová tvorba šroubovicové varianty.


### Tvorba šroubovicového tažení

`helical.create` přebírá základní skicu, radiální dráhu a průřez. Zachovává
jejich původní ID a rámec základní skici. Vlastní odsazení základní roviny
se přebírá ze zdroje, čte a mění přes CLI i stejné Vlastnosti H-tažení.
Neposouvá kontejner; ostatní tažení a obecné řešení umístění se nemění.
Katalog má **173 příkazů**, formát a start šablony se nemění.

Oba programy a testy jsou sestavené. Související sada prošla **15/15**
(142,88 s), `build/helical-create-gui-tests.log`. Ověřuje 72 kombinací
rámce H-tažení, nezávislé objemy, smysl vinutí, převzetí skic, přesné Undo,
atomické chyby, zámky, vlastní odsazení, nativní uložení a studený výpočet,
skutečný CLI proces a GUI OK/Cancel. Podrobnosti: [SWEEP_COMMANDS.md](SWEEP_COMMANDS.md).
Další oblastí jsou otvory a závity; ostatní otevřené řádky zůstávají součástí
celkového úkolu CLI.


Dodatečně prošel návrat ze skicáře základní kružnice do rozpracovaných
Vlastností **1/1** (38,31 s), `build/helical-create-sketcher-tests.log`:
View skutečně ukazuje kružnici v odsazené rovině a návrat zachová pending
hodnotu i OK/Cancel. Produkční kód se po 15/15 neměnil.


### Společný katalog závitů

`thread.catalog` zpřístupňuje přesné označení, průměry, stoupání a stránkování
bez dokumentu nebo výpočtu. GUI i CLI čtou jediný zabudovaný katalog;
TSV data se nemění. Katalog příkazů má **174 položek**. Nativní formát ani
start šablony se nemění. Tvorba a editace otvorů/závitů následují.

Oba programy a testy jsou sestavené. Související regrese prošly **8/8**
(62,70 s) a po závěrečné úpravě balení a testů **5/5** (61,14 s).
Ověřené jsou všechny tabulkové řádky, konkrétní metrické i palcové rozměry,
chyby stránkování, samostatné CLI mimo projekt, GUI konzole a dialog závitu.
Kontrakt a logy: [THREAD_CATALOG.md](THREAD_CATALOG.md).


### Současný Otvor: tvorba a vlastnosti

`opening.create/get/set` ovládají hladký a závitový Otvor (nativní Thread),
předvrtání, vstupní sražení, špičku, směr a délkové/průchozí zakončení.
GUI a CLI sdílejí výběr katalogových rozměrů, existující normalizaci FRONT
a explicitní potvrzení. Katalog má **177 příkazů**. Formát a šablony se nemění.
Přidání/výměna referencí Až k a další druhy otvorových operací následují.

Oba programy a všechny testy jsou sestavené. Související sada prošla **11/11**
(66,97 s), doplněné kontroly **3/3** (16,96 s) a po závěrečném přesunu volání
normalizace FRONT znovu **5/5** (57,25 s). Ověřeny jsou nezávislé objemy pro
čtyři druhy otvorů a tři úhly sražení, vrtací špička, směr, průchozí otvor,
reference původního počátku, přímé/nepřímé zámky, atomické chyby, Undo/Redo,
identity, uložení, studený výpočet a GUI/CLI. Kontrakt, překlady a logy:
[OPENING_COMMANDS.md](OPENING_COMMANDS.md).


### Vnější závit hřídele

`shaft_thread.create/get/set` zpřístupňují existující ShaftThread přes
společné potvrzení Vlastností, původní válec/počáteční plochu, vstupní
sražení a koncovou rovinu/válec/kužel. GUI i CLI sdílejí katalogový výběr.
Katalog má **180 příkazů**. Nativní formát a start šablony se nemění.
Kontrakt a ověření: [SHAFT_THREAD_COMMANDS.md](SHAFT_THREAD_COMMANDS.md).

Oba programy i testy jsou sestavené; související sada prošla **9/9**
(70,03 s). Po sjednocení kontroly zámků při potvrzení prošly znovu
modelové, procesní a GUI testy **3/3** (57,52 s), včetně tří norem,
nezměněného objemu, sražení, hranice historie, Undo a nativního uložení.
Následuje samostatná vrtací špička a další zbývající řádky tabulky.


### Samostatná vrtací špička

`drill_point.create/get/set` sdílejí potvrzení s GUI Vlastnostmi, ověřují
původní dna před hranicí editace a podporují celý seznam referencí i úhel.
Identity kuželových ploch a hran jsou odvozené od zdrojového dna, takže
odebrání či přeuspořádání výběru nepřejmenuje zbývající geometrii.
Katalog má **183 příkazů**. Kontrakt: [DRILL_POINT_COMMANDS.md](DRILL_POINT_COMMANDS.md).


Oba programy a všechny testy jsou sestavené. Úplná sada prošla **97/97**
(458,94 s) bez opakování. Doplněný test dna současného Otvoru prošel **1/1**
(0,53 s); ověřuje nezávislý objem a původ kuželové plochy. Pokračují zbývající
modelovací operace z tabulky; celá CLI tím ještě není dokončena.


### Skořepina a její skutečný vstup

`shell.faces/create/get/set` zpřístupňují tloušťku, celý seznam otevření a
společné potvrzení GUI/CLI. Dotaz vrací plochy před Shellem nebo u kurzoru
aktivního Tělesa, bez kopie triangulace a bez OCCT. Prázdný seznam vytvoří
uzavřenou dutinu. Nové testy odhalily a opravily kolizi identit pomocných
ševů/pólů při Shellu koule; skutečné kruhové okraje zůstávají zachované.
Katalog má **187 příkazů**, nativní struktura a start šablony se nemění.

Oba programy a všechny testy jsou sestavené. Úplná sada prošla **98/98**
(463,81 s) bez opakování, `build/shell-command-final-build.log` a
`build/shell-command-full-tests.log`. Ověřuje nezávislé objemy kvádru,
válce, koule, otevřené polokoule i tělesa po zaoblení, reference, zámky,
Undo/Redo, nativní uložení, skutečné CLI a GUI OK/Cancel. Kontrakt a průběh
ověření: [SHELL_COMMANDS.md](SHELL_COMMANDS.md). Pokračuje Fillet/Chamfer.


### Skutečné vstupní hrany a společná tečná trasa

`edge_treatment.edges/route` čtou vstup před zaoblením/sražením a používají
stejné uložené spojení hran jako viewer. Nejednoznačné reference se odmítají;
samostatná kružnice bez dvou konců zůstává jednou hranou s výslovně neúplnou
informací o koncích. Katalog má **189 příkazů**.

Oba programy jsou sestavené. Dotčená sada prošla **10/11** (74,43 s),
po opravě JSON vstupu nového GUI testu prošel jeho celý scénář **1/1**
(45,78 s). Modelové a procesové testy i viewer prošly již v první sadě.
Podrobnosti: [EDGE_TREATMENT_COMMANDS.md](EDGE_TREATMENT_COMMANDS.md).
Tvorba a editace Fillet/Chamfer jsou následující etapa.


### Zaoblení a sražení: společné potvrzení a směr R1

`fillet.create/get/set` a `chamfer.create/get/set` sdílejí modelové potvrzení
s GUI, původní hrany skutečného vstupu, zámky a úplné seznamy tras. Proměnné
zaoblení vyžaduje explicitní R1. Nový test více tras odhalil a opravil
nesprávné přiřazení R1 při rozbalení hrany na runtime použití v jádře.
Nativní identita ani struktura souboru se nemění; otisk Filletu zneplatní
stará vypočítaná data této operace. Katalog má **195 příkazů**.

Oba programy a testovací cíle jsou sestavené; po opravě prošla celá sada
**101/101** (470,10 s), `build/fillet-multiple-routes-full-tests.log`.
Ověřeny jsou nezávislé objemy a koncové poloměry, všechny režimy sražení,
směr/FLIP, více tras, původní identity, nativní výpočet, zámky, atomické
chyby, Undo/Redo a skutečné GUI i CLI. Kontrakt a předchozí neúspěšné
běhy: [EDGE_TREATMENT_COMMANDS.md](EDGE_TREATMENT_COMMANDS.md).


### Odebrání hran a tras

`edge_treatment.remove` sdílí stromovou transakci pro jednotlivou původní
hranu, celou trasu i odstranění kontejneru po poslední trase. Zachovává R1,
aktivní Těleso a jeden krok Undo. Ztrátu navazující reference po odstranění
celého prvku hlásí stejně jako `history.delete`, výslovně s `changed:true`.
Katalog má **196 příkazů**.

Oba programy jsou sestavené; modelový běh **1/1** (2,16 s), související
sada **38/38** (250,10 s) a překlady **1/1** (1,43 s). Skutečné CLI i GUI
ověřují poslední trasu a návrat historie. Kontrakt, příklady a logy:
[EDGE_TREATMENT_COMMANDS.md](EDGE_TREATMENT_COMMANDS.md).


### Společné číselné zámky

`value_lock.list/set` sdílejí uložené zámky s Vlastnostmi a kótami ve View,
včetně nulových hodnot, originových offsetů, lokálních bodů a konkrétních
výskytů v sestavě. Přepnutí zachovává vypočítaná data bez OCCT a má jedno
Undo. Katalog má **198 příkazů**, formát a šablony se nemění.

Oba programy jsou sestavené; úplná sada prošla **102/102** (479,54 s).
Po závěrečném sjednocení adres katalogové velikosti závitu a stoupání
prošla dotčená sada **7/7** (162,86 s). Geometrie, zámky, chyby, nativní
soubory, CLI i skutečné GUI jsou ověřené; podrobnosti a logy:
[VALUE_LOCK_COMMANDS.md](VALUE_LOCK_COMMANDS.md). Následují Zrcadlo a Pole.


### Zrcadlo a Pole: společné zdroje a datové dotazy

`derived_copy.sources`, `mirror.get` a `pattern.get` čtou přesnou hranici
historie, Boolean zdroje, bezprostřední komponenty, umístění kopií a všechny
směry pole. GUI používá stejný výběr zdrojů i uložená data. Dotazy nemění
geometrii, tab ani revizi. Katalog má **201 příkazů**, formát a šablony
se nemění. Tvorba a editace kopií pokračují v další etapě.

Oba programy jsou sestavené; modelová sada **1/1** (0,49 s), první
související sada **8/9** (80,94 s) odhalila chybu nového napojení editace
Pole ze stromu. Po opravě prošla celá dotčená sada **9/9** (77,04 s),
včetně trvale registrované GUI regrese a skutečného CLI procesu. Přesný
kontrakt, rozdíl kruhového/lineárního počtu a logy:
[DERIVED_COPY_COMMANDS.md](DERIVED_COPY_COMMANDS.md).


### Zrcadlo a Pole: tvorba a změny Partu i Assembly

Čtyři mutace `mirror.create/set` a `pattern.create/set` doplňují katalog
na **205 příkazů**. GUI i CLI používají stejnou přípravu a potvrzení;
zdroje jsou před kopií v její vlastní historii. Příkazy podporují původní
roviny/osy, lokální počátek, lineární mřížku a kruhový režim, umístění,
změnu zdroje, zámky roztečí i úhlu a jediný krok Undo/Redo.

Úplná sada prošla **105/105** (496,11 s). Dodatečný test neuloženého zdroje
odhalil chybějící převzetí vypočítaných dat v čistém CLI; příprava Assembly
nyní používá stávající sdílení zdrojů z GUI, bez ukládání/regenerace Partu.
Po opravě, doplnění Undo i GUI zámku a novém sestavení obou programů
prošlo všech **14/14** dotčených testů (85,98 s). Geometrické výpočty,
atomické chyby, nativní soubory, procesové CLI a GUI jsou popsány
v [DERIVED_COPY_COMMANDS.md](DERIVED_COPY_COMMANDS.md). Další etapou jsou
vlastnosti a správa komponent sestavy.


### Vlastnosti a vložené vazby komponent

`component.set` doplňuje katalog na **206 příkazů**. Sdílí potvrzení s GUI
Vlastnostmi a kontextovými akcemi: název, viditelnost, potlačení, uzemnění,
číselné umístění a seznam nejvýše tří původních referencí pro všechny čtyři
druhy vazeb. Ruční souřadnice respektují volnost pohybu i zámky; změna
názvu/viditelnosti neřeší vazby. Kandidát přebírá pouze data vlastněná
výskytem, takže nepřepíše mezitím aktualizovanou geometrii zdroje.

Test odmítnutí fyzikální relace opravil předčasnou změnu interního čítače
`AssemblySession::commit`; selhání nyní zachová revizi, obsah i generaci.
Kompletní sestavení a **107/107** testů prošlo (496,95 s). Po dorovnání
uložených mezí a výchozího zámku shodnosti podle GUI a přidání scénáře
neuloženého zdroje prošlo **16/16** dotčených testů (93,45 s).
Podrobný kontrakt a logy: [COMPONENT_PROPERTY_COMMANDS.md](COMPONENT_PROPERTY_COMMANDS.md).
Následuje kontrola řetězení sestavových vazeb a odstranění komponent.


### Zjištěná chyba společného řešení řetězce vazeb — čeká na souhlas

Nový nezávislý test A → B → C po dokončení `component.set` potvrdil závislost
výsledku na pořadí komponent: očekávané 2 + 3 = 5 mm zůstává 2 mm.
Jde o dosavadní společnou funkci GUI/CLI, která řeší komponenty jedním
průchodem v pořadí stromu. Automatická kontrola odmítla opravu společného
umístění bez konkrétního souhlasu požadovaného AGENTS.md. Produkční kód
nebyl změněn, reprodukce je zachována samostatně a jiné CLI operace pokračují.
Konkrétní návrh, dopady, testy a umístění reprodukce:
[ASSEMBLY_MATE_ORDER_REVIEW.md](ASSEMBLY_MATE_ORDER_REVIEW.md).
Tato chyba zůstává otevřená; nelze tvrdit, že jsou všechny řetězce vazeb ověřené.


### Odstranění komponenty: společné kontroly a atomický výsledek

`component.remove` doplňuje katalog na **207 příkazů**. GUI i CLI sdílejí
kontrolu závislostí, práci s cíli řezů a jediný výsledný commit. Běžné
odstranění používá současné vypočítané zdroje v soukromém kandidátovi;
nevystaví živé sestavě částečnou regeneraci, když další výpočet selže.
Zdrojové dokumenty a ostatní výskyty zůstávají zachované.

Nové modelové testy a obnovená commitovaná regrese vlastností prošly **2/2**
(0,83 s). Po sestavení obou programů prošla související sada **20/20**
(192,48 s), včetně skutečného kontextového menu, CLI procesu a celkového
startu GUI. Ověřuje objem řezu 2800 mm³, aktuální neuložené zdroje,
atomické chyby, blokující reference, nativní soubory a Undo/Redo.
Kontrakt a logy: [COMPONENT_REMOVAL_COMMAND.md](COMPONENT_REMOVAL_COMMAND.md).
Následuje vnořená aktivace a povolení příkazů v přesném zdrojovém kontextu.
Oprava řetězce vazeb zůstává samostatně čekající na konkrétní souhlas.


### Přesná aktivace komponenty a příkazy uvnitř sestavy

`component.activate/deactivate` doplňují katalog na **209 příkazů**.
Workspace spravuje přesnou cestu aktivace společně pro GUI a CLI. Modelové
příkazy mění aktivní zdroj a zachovají zobrazenou hlavní sestavu; podsestava
vlastní své lokální vložení, vlastnosti, odstranění a regeneraci. Otevřený
neuložený zdroj je autoritativní. Aktivace ani návrat neopustí otevřenou editaci.

Modelové testy prošly **3/3** (0,64 s). Úplná sada **108/109** (490,27 s)
odhalila starý startovací test, který aktivoval jinou komponentu během
nedokončených Vlastností vložení. Po doplnění správného GUI dokončení a
ověření blokované aktivace/návratu prošla závěrečná sada **9/9** (174,88 s),
včetně celého startu GUI, konzole, CLI a překladů. Testy také ověřují přesné
opakované výskyty, zavřené mezilehlé karty, aktuální zdroje, objem, Undo/Redo,
lokální vlastnictví a zachování kontextu při ukládání a zavírání.
Podrobnosti: [COMPONENT_ACTIVATION_COMMANDS.md](COMPONENT_ACTIVATION_COMMANDS.md).
Následuje sjednocení původních referencí a příkazové projekce pro Part
aktivovaný v sestavě; při čtení referencí nesmí docházet k výpočtu řezů.


### Původní reference aktivovaného Partu bez skrytého přepočtu

Společný převod používá přesné uložené cesty výskytů a aktuální data
zdrojového Partu. Čtení referencí již nepřipravuje výpočet sestavových
řezů, kopií ani vazeb. Převádí také póly přesné spline a analytické plochy;
sdílí převedenou plochu mezi jejími trojúhelníky a kopíruje jen vybrané
reference. Ověřuje identitu zavřeného nativního zdroje a zachovává
vypočtenou zrcadlenou kopii i počátek přímého Partu.

Modelová sada prošla **4/4** (1,02 s), po sestavení obou aplikací širší
regrese **17/17** (142,93 s), včetně celého startu GUI, projekce, offsetů,
CLI procesu a přesných spline. Katalog zůstává na **209 příkazech**;
následuje společná transakce příkazového vytvoření/odpojení reference
v kontextu sestavy. Kontrakt a logy:
[CONTEXT_REFERENCE_GEOMETRY.md](CONTEXT_REFERENCE_GEOMETRY.md).


### Jedna kontrola cyklů pro vložení a externí reference

Kontrola externí závislosti nyní prochází stejný dokumentový graf jako
vložení komponenty, včetně vlastněných profilů a zavřených vnořených
zdrojů. Neúplná dostupnost se odmítá, otevřená neuložená data jsou
rozhodující a čtení nemění živý Workspace. Modelová sada **3/3** (0,83 s)
a po sestavení všech programů související sada **9/9** (37,09 s) prošly.
Počet příkazů zůstává **209**; společná transakce kontextové skici
pokračuje. Podrobnosti: [DOCUMENT_DEPENDENCY_VALIDATION.md](DOCUMENT_DEPENDENCY_VALIDATION.md).


### Obnovení referencí Partu v přesném sestavovém kontextu

`sketch.reference.refresh` podporuje všechny čtyři druhy původních referencí
aktivovaného Partu, aktuální neuložený zdroj i soukromě načtený nativní
zdroj. Zachovává přesnou spline, trim a navázaný offset, při ztrátě zdroje
ponechá poslední geometrii a nepřepočítává sestavu. Jiný výskyt stejného
Partu se odmítne; změna skici má jeden krok Undo/Redo.

Modelové testy a skutečné CLI prošly **4/4** (18,20 s). Po doplnění fyzicky
chybějícího souboru, pěti lokalizací a sestavení obou programů prošla
**celá sada 113/113** (511,11 s). Katalog zůstává **209 příkazů**.
Kontextová tvorba a odpojení s atomickým potvrzením sestavové závislosti
pokračují; před nimi následuje ověření původních ploch vnořených kopií.
Kontrakt a logy: [CONTEXT_REFERENCE_REFRESH.md](CONTEXT_REFERENCE_REFRESH.md).


### Analytické plochy a přesné cesty vnořených kopií

Nový nezávislý test nalezl odchylku roviny zrcadlené podsestavy až
51,5766 mm od jejích vrcholů a odmítnutou cestu do Pole. Výslovný výpočet
kopie nyní převádí analytické údaje přes správný rámec paketu a vrací
uložený rámec výskytu. Společné cesty zahrnují Pole, zachovávají skutečného
Assembly vlastníka a aktivují správný původní zdroj. Solver umístění,
identity a formát se nemění.

Po opravě prošly modelové a geometrické regrese, včetně válce, více těles,
tří zrcadlových rovin, kopií kopií, nativního znovuotevření a kontextové
projekce. Po úplném sestavení prošla související sada **21/21** (142,57 s),
včetně GUI, CLI a překladů. Katalog má **209 příkazů**. Následuje společné
potvrzení kontextové reference a jejích závislostí.
Podrobnosti: [NESTED_COPY_REFERENCES.md](NESTED_COPY_REFERENCES.md).

## Atomická historie Partu (2026-09-13)

Společná `DocumentSession` odmítne chybnou fyzikální relaci, jednotku či identitu
rozměru bez změny revize, generace, geometrie a Undo/Redo. Stavy historie mají
jednoznačné vlastnictví; její růst nepřekopírovává staré vypočtené hranice.
Nová regrese ověřuje odmítnuté potvrzení, výměnu a přepočet, 24 kroků historie,
identitu uložených alokací a nezávislost výslovné kopie relace.

Sestavení obou aplikací a **115/115 testů prošlo za 502,04 s**. Podrobnosti jsou
v [PART_SESSION_TRANSACTIONS.md](PART_SESSION_TRANSACTIONS.md). Jde o společnou
transakční hranici pro GUI a CLI; kontextová tvorba/odpojení reference se
sestavovými závislostmi stále zbývá a katalog má nadále 209 příkazů.

## Obnova všech vlastněných skic (2026-09-13)

Lokální a kontextová regenerace nově zahrnují všechny profily Sweep2D,
Sweep3D, Helical Sweep, Hole a Thread i skici řezů. Stejný rozsah používá
hledání kontextových závislostí před regenerací Assembly a jejich souhrn
pro otevřené Party. Nová regrese kontroluje přesnou racionální křivku, trim,
offset, chybějící/obnovený zdroj, Undo a nedotčený sousední profil.
Skutečný CLI proces navíc regeneruje a nativně uloží platnou šroubovici
s referencí pouze ve vnitřní základní skici.

**116/116 testů prošlo za 501,40 s** po sestavení obou aplikací. Podrobnosti:
[OWNED_SKETCH_REFERENCE_REFRESH.md](OWNED_SKETCH_REFERENCE_REFRESH.md).
Katalog zůstává na 209 příkazech. Společná transakce kontextové tvorby/odpojení
a bezpečné zachování závislostí sdílených s uzavřenými Party nadále zbývají.

## Publikace stavů otevřených dokumentů (2026-09-13)

Assembly a Drawing nyní sdílejí princip jednoznačně vlastněných stavů s Partem.
Bezvýjimečný přesun `DocumentState` odstranil kopírování geometrie a historie
existujících Partů při růstu seznamu otevřených dokumentů. Cílený test otevírá
48 smíšených dokumentů a sleduje přímo jejich alokace; doplňuje odmítnuté
aktualizace, historie a nezávislost výslovných kopií.

Celé sestavení GUI/CLI a **117/117 testů prošlo za 501,22 s**. Podrobnosti:
[WORKSPACE_STATE_PUBLICATION.md](WORKSPACE_STATE_PUBLICATION.md).
Tato hranice umožňuje bezpečně přidat potřebný zdrojový dokument do připravené
společné transakce. Samotná kontextová tvorba a odpojení reference s atomickou
aktualizací závislostí nadále zbývají; katalog má stále 209 příkazů.

## Transakce kontextových referencí (2026-09-13)

`sketch.reference.create/delete` nyní sdílejí přípravu a potvrzení s GUI.
Změna Partu a souhrnných závislostí společné Assembly se zveřejní společně.
Zavřené nativní Party ve stejné větvi nejsou při odstraňování poslední reference
přeskočeny. Part Undo/Redo připravuje souhrny před přesunem historie a odmítne
nově vzniklý cyklus bez dílčí změny.

GUI návrh vlastněného profilu nic nezapisuje do sestavy před OK. Rozšířený
GUI test rovněž opravil chybějící skicu při rollbacku v sestavě. Po integraci
prošla celá sada **118/118 za 513,98 s**. Následovala regrese všech čtyř druhů
kontextové reference a oprava explicitního cíle dotazu `history.can_move`.
Závěrečné sestavení a **9/9 cílených testů prošlo za 53,72 s**.
Podrobnosti a navazující práce: [CONTEXT_REFERENCE_TRANSACTIONS.md](CONTEXT_REFERENCE_TRANSACTIONS.md).
Katalog zůstává na 209 příkazech.

## Historie a regenerace souhrnů Assembly (2026-09-13)

Assembly Undo/Redo obnovuje souhrn proti aktuálním Partům a připravené hierarchii.
Explicitní regenerace projde i zavřené nativní vlastníky. Zdrojové Party se při
kontrole neotevírají do tabů a jejich vypočtené alokace se nemění. Chybějící
zdroj zachová známé hrany; cyklický kandidát se odmítne před změnou historie.
Obrácení směru reference odstraní nejprve prokazatelně zastaralé hrany.

Modelové regrese prošly **2/2 za 0,69 s**. Po úplném sestavení prošly procesové
CLI a modelové regrese **3/3 za 21,22 s** a rozšířený GUI test **1/1 za 18,57 s**.
Následně prošla **celá sada 119/119 za 528,05 s**, včetně odmítnutého GUI Undo
a úspěšného opakování po opravě vlastního testovacího zdroje.
Podrobnosti: [ASSEMBLY_REFERENCE_SUMMARIES.md](ASSEMBLY_REFERENCE_SUMMARIES.md).
Katalog zůstává na **209 příkazech**. Následuje sjednocení mazání konstrukcí.

## Společné mazání konstrukcí (2026-09-13)

Nový `construction.delete` má stejnou modelovou transakci jako strom GUI.
Assembly kontroluje i vlastněné entity a body, komponentové vazby, řezy,
cíle odečtů a vložené externí profily Partů. Kontrola nekopíruje celý Workspace.
Part zachovává dosavadní historii, aktivní těleso a následný výpočet modelu.

Obě aplikace se sestavily. Z integračních deseti testů prošlo devět včetně
skutečného CLI, GUI nabídky stromu a překladů; poslední odhalil chybu vlastního
nastavení zobrazeného dokumentu. Po opravě tohoto nastavení prošly **4/4 za
0,89 s**. Produkční kód již zůstal stejný. Podrobnosti a logy jsou v
[CONSTRUCTION_REMOVAL.md](CONSTRUCTION_REMOVAL.md). Katalog má **210 příkazů**.
Následuje samostatný nativní Hole, který zatím CLI příkaz nemá.
