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

STL podporuje Part i vnořené sestavy včetně opakovaných výskytů. Sestavu
převede do jedné sítě; STL neuchovává jména ani produktovou hierarchii.
DXF podporuje úsečky, osy, samostatné body, kružnice, kruhové a eliptické
oblouky, elipsy i B-spline vybrané skici, včetně uložené geometrie offsetů
a trimů. Skicu lze určit i ve vlastněném profilu. Text a rohová zaoblení
zatím odmítá před zápisem: jejich úplný viditelný tvar není v této etapě
převeden. Příkaz nehlásí neúplný soubor za úspěch.

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


## STL vnořených sestav

Příkaz `export.stl` i exportní menu používají společný snímek uložených
komponent. Pracovní úloha sestaví dočasné těleso existující operací
`assembly::calculate_component_body`; číselné umístění ani řešení vazeb se
nemění. Transformace se skládají od dílu přes všechny jeho vlastníky až
k cílové sestavě. Opakované výskyty stejného zdroje zůstávají samostatnými
kopiemi v síti. Skrytá, potlačená a závislostí potlačená větev se vynechá.

Pokud komponenta obsahuje vlastní hotové těleso po řezu nebo odvozené kopii,
exportuje se toto těleso, nikoli její původní neodečtení potomci. Jinak se
použijí uložená dětská tělesa podle ID výskytu. Chybějící viditelná geometrie
vrací `calculation_required`; prázdný viditelný výsledek vrací `empty_geometry`.
Hloubka průchodu má stejnou mez 256 jako existující skládání komponent.

Export neotevírá zdroje, neřeší vazby ani nepřepočítává historii modelu.
OCCT v pracovní úloze pouze skládá vypočtená tělesa a trianguluje export.
Dočasné těleso se neukládá do Assembly a nemění její historii ani sdílené
snímky. Výstup může být výpočetně náročný podle složitosti B-Rep; tato etapa
nemění dosavadní exportní odchylku 0,1 mm a úhlovou mez 0,5 rad.

Regrese nezávisle čte binární STL a ověřuje dva kvádry 10×20×30 mm ve třech
úrovních s otočením postupně kolem X, Y a Z. Kontroluje všech 16 rohů,
24 trojúhelníků, orientaci normál a podepsaný objem 12 000 mm³. Stejnou
geometrii ověřuje příkazový proces i skutečná exportní akce GUI. Další
scénáře zahrnují skryté a potlačené zdroje bez tělesa, chybějící viditelné
těleso, uzavření zdrojového dokumentu během pracovní úlohy, výsledný řez
s objemem 3 000 mm³ a odmítnutí přepsání platného souboru při chybě.

Integrační sada prošla **7/7** (27,02 s),
`build/nested-stl-integration-tests.log`; samostatná regrese původního STEP
sestavení prošla **1/1** (1,31 s), `build/nested-stl-step-regression.log`.
GUI používá ověřovací EXE `build/cpp-windows-release/zima-cad-nested-stl-validation.exe`
ze stejných aktuálních CMake objektů, protože běžný uživatelský CAD zůstává
spuštěný. Katalog se nemění: rozšířil se existující `export.stl`.


## Přesné křivky skici v DXF

Zapisovač `interchange::export_dxf` je oddělen od importního parseru do
`dxf_export.cpp`. GUI a CLI používají jeho společnou validaci. Soubor uvádí
verzi `AC1015` a mm; čísla zapisuje s 17 platnými číslicemi a desetinnou tečkou
nezávisle na prostředí uživatele.

Elipsy a eliptické oblouky jsou entity `ELLIPSE`. Zapisovač normalizuje delší
osu a při jejím prohození posune parametrický interval; obrácený směr zachová
normálou. Spline zapisuje jako `SPLINE` s řídicími body, uzly a případnými
váhami. Rozložení polí vychází z dokumentace Autodesk
[ELLIPSE](https://help.autodesk.com/cloudhelp/2025/DEU/AutoCAD-DXF/files/GUID-107CB04F-AD4D-4D2F-8EC9-AC90888063AB.htm)
a [SPLINE](https://help.autodesk.com/cloudhelp/2016/ENU/AutoCAD-DXF/files/GUID-E1F884F8-AA90-4864-A215-3182D47A9C74.htm).

Běžná, interpolovaná i uzavřená periodická skicová spline používá existující
nativní převod na přesnou B-spline. U přesné externě získané spline zůstávají
její uzly a váhy. Nejde o vzorkovaný řetěz úseček a převod nevolá OCCT.
Offsety a trimy exportují jejich současnou uloženou viditelnou křivku;
nezapisuje se další kopie podpůrné geometrie ani vazba na zdroj. Export
neobnovuje externí reference a nemění parametry nebo závislosti skici.

Samostatné body jsou `POINT`; středy a řídicí body křivek se jako další
entity nezapisují. Nekonečná osa je `XLINE`, konečná pomocná úsečka zůstává
`LINE`. Pomocná geometrie používá vrstvu `CONSTRUCTION`, ostatní `PROFILE`.
Text a rohové zaoblení nadále vracejí `unsupported_geometry`, včetně ochrany
již existujícího cíle. Importní parser tato etapa nerozšiřuje: vstupní
`ELLIPSE`, `SPLINE`, `POINT` a `XLINE` zatím hlásí jako nepodporované entity.

Test čte skutečné skupinové kódy a nezávisle kontroluje analytický tvar
elipsy s prohozenými osami a opačně orientovaný oblouk, racionální kružnici,
Bernsteinův polynom kubické křivky, interpolační body a periodické uzavření.
Ořezaná kružnice používá svůj racionální parametr, který obecně není přímo
úhlem. U offsetu se ověřují přesné konce oříznutého úseku a absence další
kopie úplného podkladu. Stejnou sadu křivek exportuje skutečné CLI i konzole GUI.

Integrační sada prošla **8/8** (26,16 s),
`build/dxf-curves-integration-tests.log`; původní výměnné kontrakty prošly
**1/1** (0,09 s), `build/dxf-curves-interchange-tests.log`. Nezávislý parser
**ezdxf 1.4.4** načetl 12 entit bez chyby nebo opravy a ověřil racionální
kružnici, kubický polynom, interpolaci, uzavření a ořezaný offset. Největší
odchylka kontrolovaných interpolačních bodů byla 1,12e-9 mm; ezdxf při
vyhodnocování zaokrouhlil uložený uzel 0,3333333333333333 na 0,3333333333.
Protokol: `build/dxf-curves-ezdxf-validation.json`. Parser je jen dočasná
ověřovací závislost pod `build`, není součástí aplikace ani runtime.

GUI bylo ověřeno samostatným EXE
`build/cpp-windows-release/zima-cad-dxf-curves-validation.exe`; uživatelský
běžící CAD zůstal spuštěný. Počet příkazů zůstává 152. Nativní formát ani
startovací šablony se touto změnou nemění.
