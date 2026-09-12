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
| Regenerate, Undo/Redo | Základ Part/Assembly hotov | Výkresové operace a jejich historie |
| Kvádr | Hotovo | Společná tvorba, čtení a rozměrový patch; včetně zámků a přesnosti |
| Válec, koule, kužel, jehlan, klín | Hotovo | Společné create/get/set, zámky, přesnost, GUI/CLI a 59/59 regresí |
| Historie Partu | Hotovo | Společný přesun, ověření závislostí, potlačení, odstranění a kurzor; včetně historie těles a Booleanů |
| Tělesa a Boolean | Základ hotov | Tvorba, čtení, aktivace, název/viditelnost, kurzory a Boolean create/get/set; pořadí/mazání řeší historie; zbývá příkazové umístění a odvozené kopie |
| Umístění a původní reference | Čtení původních referencí hotovo | Part/Assembly, přesné výskyty a geometrická data; zbývá zadání do existujícího společného řešení umístění |
| Konstrukční geometrie | Zbývá | Body, osy, roviny, 3D křivky |
| Skicář: geometrie | Základ hotov | 21 příkazů: samostatné a vložené skici, body, úsečky, kružnice, oblouky, elipsy, B-spline, obdélníky, mnohoúhelníky, posun a pomocná geometrie; text create/get/set s nativním písmem a spline get/set hotovy; zbývá import a kontrola dalších variant podle GUI |
| Skicář: vazby a operace | Vazby/kóty/solver/offset/trim/mirror hotovy | Offset create/get/set/free, úplný podklad a zachování intervalů, trim podle průsečíků, mirror, orientovaný obdélník, tečny a zaoblení rohu; všech 15 druhů vazeb, odstranění a solver; 16 druhů kót včetně vlastností, popisků a mazání; uvolnění externích referencí hotovo |
| Externí reference skici | Part a kořenová Assembly hotovy | Původní geometrie, přesná projekce, aktualizace, odpojení a zachování trimu; zbývá příkazový kontext Partu aktivovaného v sestavě |
| Vytažení a rotace | Zbývá | Vlastněný profil, thin, zakončení, více směrů |
| Tažení | Zbývá | Sweep 2D/3D, loft a helical včetně profilů a drah |
| Otvory a závity | Zbývá | Hole, Thread, ShaftThread, DrillPoint a reference |
| Zaoblení, zkosení, skořepina | Zbývá | Výběr skutečného vstupního tělesa a sdílené transakce |
| Zrcadlo a pole | Zbývá | Odvozená tělesa a komponenty |
| Sestavy | Zbývá | Komponenty, přesné výskyty, vazby, aktivace a řezy |
| Výkresy | Správa neuložených změn hotova | Příkazy pro listy, pohledy, kóty, anotace, šablony, BOM, Show/Erase |
| Řezy, měření a vzhled | Zbývá | Datové operace a uložené výsledky |
| Parametry, relace a materiál | Zbývá | Jednotky, fyzikální údaje a rodinné tabulky |
| Import a export | Zbývá | STEP/IGES/DXF, přesnost, cílový Part/Assembly a podporované exporty |

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
