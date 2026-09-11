# Příkazy skicáře

Konzole GUI a samostatný `zima-cad-cli` sdílejí transakci geometrie skici
v `workspace/sketch_operations`. GUI používá stejnou operaci při kreslení,
editaci a v pracovních kopiích vložených profilů. Nová skica v Partu má vlastní
kontejner Sketch a patří do aktivního tělesa; běžná skica sestavy je samostatná.
Tvorba z GUI i CLI používá stejné vložení nativního kontejneru.

## Souřadnice, cíle a výsledky

Geometrické příkazy přijímají povinné `sketch` se stabilním ID skici. Volitelné
`document` při změně musí odpovídat aktivnímu dokumentu. Čtení může mířit do
jiného otevřeného dokumentu bez jeho aktivace. Body se zadávají jako JSON pole
`[x, y]` v lokálních souřadnicích skici a v **milimetrech**, bez ohledu na
zobrazovací jednotky dokumentu. `snap_mm` je kladná tolerance sloučení bodů,
výchozí `0.000001`. Nevytváří se identita z pořadí vykreslovaných segmentů.

Úhly uložených kruhových/eliptických křivek jsou v radiánech; úhlové kóty a
výslovné pole textu `angle_degrees` jsou ve stupních. `sketch.get` tyto konvence
uvádí zvlášť. Vytvořená kružnice zůstává kružnicí, B-spline zůstává přesnou
nativní spline; jejich vykreslení není zdrojem geometrie příkazů.

Změna vrací `document`, `sketch`, `revision`, `changed`, případně `point` nebo
`geometry` s vytvořenými ID. Seznam nových křivek vrací obdélník a mnohoúhelník.
Neplatný vstup se odmítá před publikací změny; stejná hodnota a nulový posun
nevytvářejí další krok historie. `undo` a `redo` používají historii dokumentu.

## Dostupné operace

| Příkaz | Argumenty kromě cílové skici a dokumentu |
| --- | --- |
| `sketch.list` | `offset=0`, `limit=100` (nejvýše 1000); seznam metadat |
| `sketch.get` | metadata a počty prvků |
| `sketch.entities` | `offset=0`, `limit=500` (nejvýše 5000); ID a druhy prvků |
| `sketch.entity.get` | `entity`; jeden uložený bod, křivka, text, reference, vazba nebo kóta |
| `sketch.create` | `name`, `plane=XY` (`XY`, `XZ`, `YZ`); bez vstupního `sketch` |
| `sketch.point.create` | `position`, volitelně `construction=false`, `snap_mm` |
| `sketch.point.move` | `point`, `position`; respektuje vazby a pevné body |
| `sketch.point.fixed` | `point`, `fixed` typu boolean |
| `sketch.point.delete` | `point`; běžná pravidla odstranění závislé geometrie |
| `sketch.segment.create` | `first`, `second`, volitelně `construction`, `snap_mm` |
| `sketch.segment.centerline` | `segment`, `centerline` typu boolean |
| `sketch.circle.create` | `center`, `radius_mm`, volitelně `construction`, `snap_mm` |
| `sketch.arc.create` | `center`, `start`, `end`, volitelně `clockwise`, `construction`, `snap_mm` |
| `sketch.ellipse.create` | `center`, `major`, `minor` (konce poloos), volitelně `construction`, `snap_mm` |
| `sketch.elliptical_arc.create` | navíc `start`, `end`, volitelně `reversed` |
| `sketch.bspline.create` | `points`, `degree=3`, `closed=false`, `interpolating=false`, `construction=false`, `snap_mm` |
| `sketch.rectangle.create` | `first`, `second` (protilehlé rohy), `snap_mm`; zachovává nativní vazby obdélníku |
| `sketch.polygon.create` | `center`, `rim`, `sides` (3–1024), `snap_mm` |
| `sketch.geometry.construction` | `geometry`, `construction` typu boolean |
| `sketch.geometry.delete` | `geometry`; běžná pravidla skicáře |
| `sketch.translate` | `delta`, pole ID `points` a/nebo `geometry`; společný solver posunu |

Příkazy s poli se nejpohodlněji zadávají JSON objektem. Například po získání
skutečného ID z `sketch.create`:

```json
{"command":"sketch.bspline.create","arguments":{"sketch":"ID_Z_VYSLEDKU","points":[[0,0],[3,8],[7,-3],[10,0]],"degree":3}}
```

B-spline dovoluje 2–4096 vstupních bodů a stupeň 1–25; konkrétní kombinaci
ověří nativní skicář. `sketch.entity.get` vrací uložená pole pouze pro čtení.
Nepřijímá zpětný nevalidovaný patch. Odpověď nad 64 KiB odmítne jako
`result_too_large`; seznamy jsou stránkované. Dotaz na seznam používá zapůjčené
skici, nehromadí kopie jejich geometrie. Vložené profily se čtou z uložené
ZIMA definice, bez OCCT.

## Výpočet a editace

Změny křivek aktualizují jejich vzájemné závislosti a ověří skicu, ale **nespouští
výpočet tělesa**. Odpověď proto uvádí `body_calculated=false`. Poslední vypočtené
těleso zůstává zachováno do výslovného `regenerate`, stejně jako při kreslení
myší. Samotné vytvoření nového kontejneru skici používá běžný výpočetní postup
potvrzení vlastností. Nic nemění na chráněném společném řešení umístění.

Lze upravit i již uložený vložený profil Sweep/Loft, Helical, Hole nebo Thread
podle přesného ID skici. Vlastnictví nadále určuje jeho kontejner. Skici řezu
lze číst; jejich změny vyžadují samostatnou transakci vlastního řezu a běžný
příkaz je odmítne jako `unsupported_sketch`. Příkazy zatím neovládají skici
výkresové šablony. Změna skici v neaktivním či odvozeném tělese se odmítá.

Otevřená rozpracovaná editace v GUI, včetně aktivního skicáře, zůstává chráněná
před dalšími mutacemi z konzole. Dotazy jsou povolené. Vlastní kreslení myší
používá společnou transakci přímo; nepřepisuje se neukončený návrh příkazem.

## Ověření

Nové modelové testy ověřují přesné křivky, souřadnice, posun jednoho středu
kružnice, pevné body, neplatné vstupy, historii, no-op, původní cache tělesa,
vlastnictví aktivního tělesa, vložený profil a nativní uložení Partu i Assembly.
Procesový test skutečně vytvoří, uloží, otevře a doplní B-spline přes samostatné
CLI. GUI test vytváří skicu přes dialog i konzoli; existující testy kreslení,
úchytů a offsetu procházejí společnou mutací aktivní skici.

Kompletní Windows Release sada prošla **63/63** (388,03 s), včetně všech
modelových, GUI, procesových, referenčních a spline regresí. Log:
`build/sketch-full-tests.log`. Vizuálně byl zkontrolován také snímek konzole.
Katalog nyní obsahuje **72 příkazů**.
