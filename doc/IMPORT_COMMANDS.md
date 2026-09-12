# Import přes společnou modelovou transakci

## Rozsah etapy

Menu importu a příkazy `import.step`, `import.iges`, `import.dxf` sdílejí
`workspace::import_part` pro Part a `workspace::import_assembly` pro sestavu.
Cílený DXF do existující skici používá `workspace::import_sketch`; GUI návrh
a uložený příkazový cíl sdílejí `prepare_sketch_dxf`.
Geometrický převod zůstává ve stávajících nativních funkcích interchange;
nemění se pravidla umístění těles ani komponent.

Tato etapa pokrývá běžný aktivní Part a kořenovou Assembly. Příkazový import
do aktivního výskytu uvnitř sestavy nebo rozpracovaného GUI náhledu je další
oblast přehledu pokrytí. DXF nyní přijímá také ID vloženého profilu a
samostatné skici nebo vloženého profilu v kořenové Assembly. Exporty popisuje
[EXPORT_COMMANDS.md](EXPORT_COMMANDS.md).

## Příkazy a jednotky

- `import.step path [mesh_deflection_mm] [output_directory]`
- `import.iges path [mesh_deflection_mm] [output_directory]`
- `import.dxf path [sketch] [unitless_scale_mm] [maximum_entities] [output_directory]`

Všechny příkazy mají volitelný argument `document` pro kontrolu identity
aktivního dokumentu. Přípona musí odpovídat příkazu (`.stp/.step`,
`.igs/.iges`, `.dxf`, bez ohledu na velikost písmen). Relativní cesty se
vztahují k pracovnímu adresáři příkazovky. Cesty a uložené názvy jsou UTF-8,
také na Windows.

`mesh_deflection_mm` musí být kladné konečné číslo. Je to odchylka
zobrazovací sítě v milimetrech, ne změna přesné geometrie. Bez argumentu
se převezme `mesh_deflection` z přesnosti dokumentu; nový dokument ji
dostává z běžné konfigurace/šablony. Hodnoty 1, 2 nebo více mm jsou možné.

DXF do Partu bez `sketch` vytvoří novou vlastněnou skicu v aktivním tělese, případně
standardní těleso, pokud žádné není aktivní. S ID skici přidá samostatný
importovaný blok do její geometrie. Cíl může být i serializovaný profil
Sweep/Loftu, Helical nebo otvoru; jeho ID zjistíte přes `sketch.list`.
Sourozenecké profily a vlastník zůstávají beze změny. Vlastněné těleso musí být aktivní
a nesmí jít o odvozenou kopii. `unitless_scale_mm` (výchozí 1) platí pouze
pro soubor bez určených jednotek; hlavička `$INSUNITS` má přednost.
`maximum_entities` je celé číslo 1–1000000, výchozí 100000. Počet zdrojových
DXF entit zahrnuje i nezpracované typy; jejich varování jsou ve výsledku.

```json
{"command":"import.step","arguments":{"path":"import/šroub.step","mesh_deflection_mm":2}}
{"command":"import.dxf","arguments":{"path":"import/obrys.dxf","sketch":"SKETCH_ID","maximum_entities":10000}}
```

## Výsledek a transakce Partu

Výsledek vrací `document`, `source`, nová ID `bodies` a `containers`,
`sketch`, `import_block`, `source_entities`, `imported_entities`, `warnings`,
`body_calculated`, `changed` a novou `revision`. Položky pro DXF jsou u 3D
importů prázdné/nulové.

Výpočet pracuje s vlastním snímkem vstupního Partu. GUI jej spouští přes
stávající běh úlohy na pozadí, CLI synchronně; pracovní úloha nemění Workspace.
Po úspěchu se na hlavním vlákně ověří identita otevření dokumentu, revize,
generace dat a aktivní těleso. Teprve poté vznikne jeden společný commit
dokumentu a vypočteného výsledku a jeden krok Undo. Chyba, zavření/znovuotevření
nebo souběžná změna cíle ponechá aktuální data beze změny.

DXF zachovává poslední vypočtené těleso. STEP/IGES jsou explicitní výpočty
importované geometrie. Žádná z těchto operací neregeneruje nadřazenou sestavu
ani sama neukládá nativní dokument. `save` zůstává výslovné. Geometrie
a původní topologická identita STEP/IGES jsou uloženy uvnitř `.prtz`, takže
po uložení není zdrojový STEP/IGES nutný pro otevření ani regeneraci.

CLI odděluje výpisy OCCT od JSON protokolu: diagnostika jde na stderr,
na stdout zůstává právě jeden výsledkový JSON řádek na příkaz.

## Ověření

`zima_cpp_import_command_tests` ověřuje kvádr 10×20×30 mm (6000 mm³)
ze STEP i IGES, uloženou přesnost, Undo/Redo, odstranění zdrojového STEP
a následnou regeneraci, DXF obdélník 20×10 mm, autoritativní jednotky
hlavičky, české názvy, chybný vstup a pozdě dokončenou úlohu.
Procesové testy spouštějí skutečné CLI s neplatnou Qt platformou a ověřují
všechny tři formáty v české cestě, nativní soubory a čistý protokol.
GUI regrese spouští příkaz i skutečnou akci Importovat přes QFileDialog.

Celá sada: 70/71 v `build/part-import-full-tests.log`; opravený nový GUI
test následně 1/1 v `build/part-import-gui-tests.log`. Oprava se týkala
předvolení souboru testem během načítání proxy modelu dialogu, nikoli
modelového importu. Katalog měl v této etapě 105 příkazů.

## Import do sestavy

Stejné příkazy v aktivní sestavě vytvoří nativní zdroje a do jejího dokumentu
vloží jeden kořenový výskyt. STEP zachovává skutečnou produktovou strukturu,
opakované komponenty sdílejí jeden zdrojový Part či podsestavu a mají vlastní
identity výskytů. Rozdělení solidu a plochy uvnitř jednoho STEP produktu se
nemění: nevytváří se z nich samostatné komponenty.

IGES a DXF bez `sketch` vytvoří jeden Part podle současné Part šablony. IGES obsahuje
importované těleso, DXF nativní vlastněnou skicu. DXF s `sketch` naopak přidává geometrii přímo do existující skici sestavy. Nové zdroje
přebírají přesnost a zobrazovací jednotky vlastnící sestavy. STEP souřadnice
a geometrické výpočty nadále používají mm.

`output_directory` je volitelný pouze pro Assembly. Musí označovat dosud
neexistující adresář pod existujícím rodičem; import žádné původní zdroje
nepřepisuje. Relativní hodnota je vůči pracovnímu adresáři konzole.
Bez argumentu vznikne unikátní `<název_zdroje>_zima`, případně `_1`, `_2`,
vedle cílové sestavy nebo v pracovním adresáři neuložené sestavy. Stejnou
organizaci používá menu GUI pro STEP, IGES i DXF. Obsahuje pouze `.prtz`
a `.asmz`; žádný povinný manifest či další formát.

```json
{"command":"new","arguments":{"type":"assembly","name":"montáž"}}
{"command":"import.step","arguments":{"path":"import/celek.step","output_directory":"celek_native","mesh_deflection_mm":2}}
{"command":"save","arguments":{}}
```

Výsledek sestavy obsahuje `document`, `source`, `directory`, `files`,
`occurrence`, `source_document`, seznamy zdrojových ID `parts` a `assemblies`,
`sketch`, statistiky DXF, `warnings`, `changed` a novou `revision` vlastníka.

Výpočet i zápis pracují s oddělenými připravenými daty. Po obou pracovních
fázích se kontroluje identita otevření, revize a generace dat cílové sestavy.
Úplná změna vlastníka včetně Undo je připravena před vložením zdrojů do živého
Workspace. Chyba či změněný/znovuotevřený cíl nezanechá částečně vložené
dokumenty; odstraní se pouze soubory této nedokončené operace. Žádné cizí
adresáře se rekurzivně nemažou. Po úspěchu jsou zdrojové soubory uloženy,
vlastnící sestavu uživatel ukládá výslovným `save`.

Undo odpojí jediný vložený kořenový výskyt. Nové nezávislé zdrojové dokumenty
a jejich soubory zachová; Redo připojí stejný výskyt se stejnou identitou.
Import nemění aktivní/zobrazený dokument a neregeneruje jiné nadřazené sestavy.

`zima_cpp_assembly_import_command_tests` ověřuje čtyři výskyty kvádru
10×20×30 mm ve dvou výskytech téže podsestavy: celkem 24000 mm³, jeden
zdrojový Part, dvě zdrojové Assembly a sdílená geometrická data. Dále ověřuje
Unicode cesty, jednotky, přesnost, nativní uložení a regeneraci po odstranění
STEP, Undo/Redo, IGES 1000 mm³, DXF a chybové/souběžné dokončení obou fází.
Skutečné CLI ověřuje všechny tři importy do sestavy a následné otevření souborů.
GUI scénář používá příkaz pro STEP a skutečnou akci menu pro DXF.

Katalog zůstává na 108 příkazech; importní příkazy nyní podporují oba typy
dokumentů. Integrační sada **7/7**, 17,00 s,
`build/assembly-import-integration-tests.log`. Závěrečné regrese **3/3**,
15,52 s, `build/assembly-import-final-tests.log`, včetně znovuotevřeného cíle,
pasivního rodiče a zachování cizího souboru při úklidu. GUI i CLI byly
znovu přeloženy v `build/assembly-import-final-build.log`.


## DXF do vlastněného profilu a návrhu GUI

Příkaz s `sketch` v Partu i Assembly vytváří jeden blok přímo v cílové skice,
bez nového kontejneru, komponenty nebo adresáře. `output_directory` se v tomto
režimu odmítá. Bez `sketch` má Assembly dosavadní význam importu nové
komponenty. Sekční skici se nadále upravují přes vlastní operaci řezu.

Pokud má editovaný profil vypočtené těleso, před uložením hotového modelu
proveďte `regenerate`, aby vypočtené hranice historie odpovídaly novým
parametrům podle současného nativního formátu. Import sám toto rozhodnutí
neprovádí.

Příkaz importuje soukromou kopii skici a po načtení ověří identitu otevření,
revizi, generaci dat a u Partu aktivní těleso. Změnu pak uloží společná
skicová transakce s Undo/Redo. Nevypočítává se těleso: nové křivky profilu
se do tělesa promítnou až při explicitním Regenerate.

Pokud uživatel spustí import menu uvnitř GUI skicáře, stejný převod upraví
aktuální návrh přes `mutate_active_sketch`. Profil v otevřených vlastnostech
zůstane přechodný; dokončení skici jej vrátí do návrhu vlastníka. Teprve OK
vlastníka počítá a ukládá model. Cancel importovanou geometrii návrhu zahodí.
Příkaz z konzole nesmí souběžně přepsat otevřený GUI editor.
