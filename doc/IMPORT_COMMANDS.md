# Import přes společnou modelovou transakci

## Rozsah etapy

Menu importu do Partu a příkazy `import.step`, `import.iges`, `import.dxf`
používají `workspace::import_part`. Geometrický převod zůstává ve stávajících
nativních funkcích `interchange::import_step_part`, `import_iges_part`
a `import_dxf_part`; nemění se jejich pravidla těles ani umístění.

Tato etapa pokrývá běžný aktivní Part. Import sestavy, aktivní výskyt uvnitř
sestavy, import do rozpracovaného GUI náhledu, výkresy a exporty jsou další
oblasti přehledu pokrytí. Importované skici ve vložených profilech zatím
nemají tento příkazový vstup; DXF přijímá samostatnou nebo vlastněnou skicu
z hlavního seznamu Partu.

## Příkazy a jednotky

- `import.step path [mesh_deflection_mm]`
- `import.iges path [mesh_deflection_mm]`
- `import.dxf path [sketch] [unitless_scale_mm] [maximum_entities]`

Všechny příkazy mají volitelný argument `document` pro kontrolu identity
aktivního dokumentu. Přípona musí odpovídat příkazu (`.stp/.step`,
`.igs/.iges`, `.dxf`, bez ohledu na velikost písmen). Relativní cesty se
vztahují k pracovnímu adresáři příkazovky. Cesty a uložené názvy jsou UTF-8,
také na Windows.

`mesh_deflection_mm` musí být kladné konečné číslo. Je to odchylka
zobrazovací sítě v milimetrech, ne změna přesné geometrie. Bez argumentu
se převezme `mesh_deflection` z přesnosti dokumentu; nový dokument ji
dostává z běžné konfigurace/šablony. Hodnoty 1, 2 nebo více mm jsou možné.

DXF bez `sketch` vytvoří novou vlastněnou skicu v aktivním tělese, případně
standardní těleso, pokud žádné není aktivní. S ID skici přidá samostatný
importovaný blok do její geometrie. Vlastněné těleso musí být aktivní
a nesmí jít o odvozenou kopii. `unitless_scale_mm` (výchozí 1) platí pouze
pro soubor bez určených jednotek; hlavička `$INSUNITS` má přednost.
`maximum_entities` je celé číslo 1–1000000, výchozí 100000. Počet zdrojových
DXF entit zahrnuje i nezpracované typy; jejich varování jsou ve výsledku.

```json
{"command":"import.step","arguments":{"path":"import/šroub.step","mesh_deflection_mm":2}}
{"command":"import.dxf","arguments":{"path":"import/obrys.dxf","sketch":"SKETCH_ID","maximum_entities":10000}}
```

## Výsledek a transakce

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
modelového importu. Katalog má 105 příkazů.
