# Export přes společnou příkazovou vrstvu

Menu GUI a konzole/CLI sdílejí `workspace::export_file` pro model/skici
a `drawing_render` pro PDF a DXF výkresů. Exportuje se poslední
vypočtený stav otevřeného dokumentu. Operace sama nevyvolává Regenerate,
nenačítá změněné zdrojové party a nevytváří modelovou změnu ani krok Undo.

## Příkazy

- `export.step path [overwrite] [document]`
- `export.stl path [overwrite] [document]`
- `export.dxf path [sketch] [overwrite] [document] [sheet]`
- `export.pdf path [overwrite] [document]`
- `export.image` s JSON argumenty `path`, `sheet`, volitelně `dpi`, `crop_mm`,
  `quality`, `overwrite`, `document`

`document` volitelně ověřuje aktivní dokument. `overwrite` je boolean,
výchozí `false`. Relativní cesta patří pracovnímu adresáři konzole.
Přípona musí odpovídat příkazu; STEP přijímá `.step` i `.stp`.
Adresář musí existovat. GUI používá potvrzení přepsání ze svého dialogu.

```json
{"command":"export.step","arguments":{"path":"výsledky/sestava.step"}}
{"command":"export.stl","arguments":{"path":"výsledky/díl.stl","overwrite":true}}
{"command":"export.dxf","arguments":{"path":"výsledky/obrys.dxf","sketch":"SKETCH_ID"}}
```

Výsledek vrací `document`, absolutní UTF-8 `path`, `source_revision`,
`bytes` a `model_changed:false`. Geometrie používá milimetry; DXF zapisuje
`$INSUNITS=4`. STL nemá jednotkovou hlavičku, jeho souřadnice jsou v mm.
Dosavadní STL síť se vytváří s odchylkou 0,1 mm a úhlovou mezí 0,5 rad.
Zobrazení modelu ani jeho uložená síť se tím nemění.

## Rozsah a omezení

STEP zachovává produktovou strukturu Partu i vnořených sestav. Čte již uložené
výsledky konkrétních výskytů v otevřeném dokumentu; export tedy odpovídá
poslední explicitní regeneraci. Změna zdrojového Partu v jiném tabu sama
neaktualizuje export jeho nadřazené sestavy.

STL podporuje Part a plochou sestavu. Vnořené sestavy zatím výslovně odmítá,
stejně jako je nepodporovala původní exportní cesta. DXF podporuje úsečky,
kružnice a kruhové oblouky vybrané skici. Skicu lze určit i ve vlastněném
profilu. B-spline, elipsy, text, offsety, trimy, rohová zaoblení a samostatné
body zatím odmítá: dosavadní DXF zapisovač by je tiše vynechal. Tyto formáty
se ještě musejí rozšířit samostatně; příkaz nehlásí neúplný soubor za úspěch.

Výkres podporuje `export.pdf` pro všechny listy a `export.dxf` s povinným
`sheet` pro jeden list. V tomto režimu se nepřijímá `sketch`; v modelovém
režimu je naopak `sketch` povinný a `sheet` se nepřijímá. Staré pořadí
pozičních argumentů exportu skici zůstává zachované. Pro výkres použijte
pojmenované argumenty v JSON. Rozsah výkresového DXF se liší od skici:
je to obraz uložených průmětů v milimetrech na papíře, s obrysy převedenými
na úsečky. Podrobnosti: [DRAWING_COMMANDS.md](DRAWING_COMMANDS.md).
PNG/JPEG listu nebo jeho výřezu poskytuje `export.image` s výslovným DPI.
PNG/JPEG aktuálního interaktivního View zatím zůstávají adaptéry GUI;
příkazový ekvivalent skutečného 3D snímku vyžaduje explicitně definovanou
kameru. GUI výkresový JPEG sdílí s příkazem atomický obrazový kodér.

## Zápis a chyby

Pracovní úloha vlastní snímek vstupních dat. Hotový soubor zapisuje do
soukromého dočasného podadresáře vedle cíle a teprve po úspěchu jej zveřejní
pod požadovaným názvem. Neúspěch zachová původní cílový soubor. Bez
`overwrite` je odmítnuta i konkurenční tvorba cíle během exportu.
Dočasné soubory se po úspěchu i chybě uklidí; nevzniká trvalý vedlejší formát.

STL používá streamové rozhraní OCCT a `filesystem::path`, protože jeho
původní filename varianta v OCCT 8 otevírala úzký `std::ofstream` a ve Windows
selhávala na českých názvech adresářů. STEP/STL/DXF mají regresi českých cest.
Výpisy OCCT směřují ve skutečném CLI na stderr, JSON protokol zůstává na stdout.

## Ověření

Modelové testy nezávisle kontrolují objem STEP po opětovném importu a objem
uzavřeného STL integrací jeho binárních trojúhelníků: kvádr 10×20×30 mm
má 6000 mm³. Ověřují původní i explicitně regenerovaný stav, vložené sestavy,
DXF kružnice/oblouky, odmítnutí neúplné geometrie a konflikt cílového souboru.
Procesové testy ověřují export po zavření a opětovném načtení Partu s neplatnou
Qt platformou. GUI test spouští konzoli i skutečnou akci menu a dialog souboru.

Závěrečný úplný Windows Release běh: **72/72**, 397,87 s,
`build/export-full-tests.log`. Přeložené GUI i CLI odpovídají tomuto stavu.
Katalog této etapy má 108 příkazů.
