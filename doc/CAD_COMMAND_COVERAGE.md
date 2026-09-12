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
| Tělesa a Boolean | Základ hotov | Tvorba, čtení, aktivace, název/viditelnost, kurzory a Boolean create/get/set; pořadí/mazání řeší historie; zbývá příkazové umístění a odvozené kopie |
| Umístění a původní reference | Čtení původních referencí hotovo | Part/Assembly, přesné výskyty a geometrická data; zbývá zadání do existujícího společného řešení umístění |
| Konstrukční geometrie | Dotazy hotovy | `construction.list/get`: body, osy, roviny, 3D křivky a jejich vlastní body; zbývá tvorba/editace/mazání, reference a vložené dráhy modelovacích prvků |
| Skicář: geometrie | Základ hotov | 21 příkazů: samostatné a vložené skici, body, úsečky, kružnice, oblouky, elipsy, B-spline, obdélníky, mnohoúhelníky, posun a pomocná geometrie; text create/get/set s nativním písmem a spline get/set hotovy; DXF do vložených profilů hotov; zbývá kontrola dalších variant podle GUI |
| Skicář: vazby a operace | Vazby/kóty/solver/offset/trim/mirror hotovy | Offset create/get/set/free, úplný podklad a zachování intervalů, trim podle průsečíků, mirror, orientovaný obdélník, tečny a zaoblení rohu; všech 15 druhů vazeb, odstranění a solver; 16 druhů kót včetně vlastností, popisků a mazání; uvolnění externích referencí hotovo |
| Externí reference skici | Part a kořenová Assembly hotovy | Původní geometrie, přesná projekce, aktualizace, odpojení a zachování trimu; zbývá příkazový kontext Partu aktivovaného v sestavě |
| Vytažení a rotace | Zbývá | Vlastněný profil, thin, zakončení, více směrů |
| Tažení | Zbývá | Sweep 2D/3D, loft a helical včetně profilů a drah |
| Otvory a závity | Zbývá | Hole, Thread, ShaftThread, DrillPoint a reference |
| Zaoblení, zkosení, skořepina | Zbývá | Výběr skutečného vstupního tělesa a sdílené transakce |
| Zrcadlo a pole | Zbývá | Odvozená tělesa a komponenty |
| Sestavy | Dotazy včetně překážek odstranění, vložení a otevření zdrojů komponent hotovy | Uložená hierarchie a přesné výskyty, sdílené vložení a otevření zdroje; zbývají vlastnosti/mazání komponent, vazby, vnořená aktivace a řezy |
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

### Čekající schválení společného umístění

Automatická schvalovací kontrola 2026-09-12 zamítla přesun číselné editace
umístění a společné výpočetní transakce z GUI do modelové vrstvy. Vyhodnotila
jej jako širší zásah podle ochrany umístění v AGENTS.md; změna se neprovedla.
Konkrétní žádost o souhlas čeká v tomto úkolu. Do odpovědi tuto refaktorizaci
neprovádět ani neobcházet. Pokračovat nezávislými operacemi geometrie skicáře,
které používají existující model a nemění pravidla umístění.

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
společné výpočetní transakce stále podléhá výše uvedenému čekajícímu
schválení ochrany umístění; v této etapě se neprovedl. Nativní formáty
ani start šablony se nemění. Běžící CAD nebyl ukončen; testovací GUI
je `zima-cad-construction-validation.exe`.
