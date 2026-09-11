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

## Offsety, ořezávání a další křivky

Druhá etapa přidává příkazy nad stejnými metodami skicáře, které používají
kreslicí nástroje a vlastnosti offsetu. Všechny mění jen skicu, mají společnou
transakci Undo/Redo a přijímají `sketch`, případně `document` jako výše.

| Příkaz | Argumenty a výsledek |
| --- | --- |
| `sketch.offset.create` | `source`, kladné `distance_mm`, `flipped=false`; vrací ID křivky i společné operace |
| `sketch.offset.get` | `geometry`; zdroj, vzdálenost, směr, tolerance, viditelný interval, kotvy průsečíků, `broken` |
| `sketch.offset.set` | `geometry`; volitelně `source`, `distance_mm`, `flipped`; upraví celou operaci včetně jejích oříznutých částí |
| `sketch.offset.free` | `geometry`; odpojí offset, zachová jeho aktuální křivku a ID |
| `sketch.curve.get` | `geometry`, `limit=4096` (1–100000); úplná podkladová spline a viditelný interval |
| `sketch.curve.retain` | `geometry`, `intervals` jako pole `[start,end]`; ponechá zadané úseky, zachová původní podklad |
| `sketch.trim.pieces` | volitelné `geometry`, `include_axes=true`, `offset=0`, `limit=500` (1–5000); aktuální části mezi průsečíky |
| `sketch.trim` | `pieces`, `include_axes=true`, `snap_mm=0.0000001`; odstraní přesně určené aktuální části |
| `sketch.mirror` | `entities` (pole ID), `axis`, `snap_mm`; vrací nová ID bodů a křivek |
| `sketch.oriented_rectangle.create` | `first`, `guide`, `axis`, `snap_mm`; obdélník podle osy symetrie |
| `sketch.tangent_arc.create` | `start_point`, `end`, `tangent`, volitelné `reverse`, `construction`, `snap_mm` |
| `sketch.common_tangent.create` | `first`, `second` (křivky), `first_hint`, `second_hint` (body určující požadovanou větev tečny) |
| `sketch.corner_fillet.create` | `first`, `second` (úsečky), `radius_mm`, `snap_mm`; nativní nedestruktivní zaoblení rohu |

Offset vzniká z **naší křivky skici**. Externí referenci nelze vydávat za nativní
zdroj. Oříznutí zdroje zachová celou původní podkladovou geometrii; existující
offset se tím nezkrátí. Nový offset vytvořený až z oříznutého zdroje převezme
jeho aktuální interval. Části jednoho oříznutého offsetu sdílejí `operation`;
změna jeho vzdálenosti, směru nebo zdroje aktualizuje všechny tyto části.
Cyklické závislosti se odmítají. Uvolnění zachová geometrii i návazné intervaly
podle společných pravidel skicáře.

`sketch.curve.retain` přijímá 1–1024 nepřekrývajících se intervalů. Parametry
0 až 1 označují právě viditelnou křivku. Pořadí intervalů se zachovává, takže
lze zachovat i úseky přes šev uzavřené křivky, například `[[0.75,1],[0,0.25]]`.
První ponechaná část má původní ID, ostatní dostanou nová. Interval `[0,1]`
je no-op. Úplný podklad je dostupný přes `sketch.curve.get`; `start/end` v jeho
výsledku jsou rozsah viditelné části v tomto úplném podkladu.

U dotazu na křivku se `limit` vztahuje na součet počtů pólů, vah a uzlů spline.
Příliš velký podklad se nezkrátí na neplatnou spline: odpověď uvede
`geometry_omitted_by_limit=true`, stupeň a počty, ale `support` vynechá.
Počítá se pouze matematika křivek skici, nikdy těleso OCCT.

Pro trim nejprve načtěte `sketch.trim.pieces` a předejte z každého vybraného
řádku pouze `geometry`, `start` a `end`:

```json
{"command":"sketch.trim","arguments":{"sketch":"SKETCH_ID","include_axes":false,"pieces":[{"geometry":"CURVE_ID","start":0,"end":0.5}]}}
```

Před odstraněním se části znovu ověří proti aktuálním průsečíkům. Pokud
požadovaný interval již neexistuje, přijde `stale_geometry` bez změny dokumentu.
Příkaz nepřijímá pořadové číslo části ani libovolné vzorkované body. Limit je
2048 částí na požadavek; opakování stejné části se odmítne. Dotaz vrací také
krajní body pro orientaci. `include_axes` musí odpovídat zamýšlenému rozdělení.

Kotvy konců oříznutých křivek sledují menší změny průsečíku. Pokud průsečík
zmizí nebo přejde na jinou větev, zachová se nativní stav opravy (`broken`),
nevymýšlí se jiná reference. Příkaz `offset.get` tento stav výslovně vrací.

Zrcadlení přijímá rovněž stabilní základní osy `sketch_axis:x` a
`sketch_axis:y`. Zaoblení rohu vrací identitu uloženého záznamu zaoblení;
nevrací dočasná ID vyhodnocených tečných bodů. Původní úsečky zůstávají
zachované. Volání pro stejný pár úseček také upraví existující poloměr.

Integrační sada prošla **9/9** (27,80 s),
`build/sketch-curve-integration-tests.log`; odpovídající GUI i CLI jsou
přeložené v `build/sketch-curve-integration-build.log`. Modelové testy měří
odchylku spline offsetu v 1025 bodech (méně než 0,00001 mm), přesnost trimu,
navazující průsečíky, stale odmítnutí, větve tečen, symetrii a nativní
uložení. Předchozí kompletní etapa prošla **63/63**. Katalog má nyní
**85 příkazů**.

## Vazby a solver skici

`sketch.constraint.create` přijímá `kind` a výslovně uspořádaná pole ID
`points` a `geometry`. Nepotřebné pole vynechte nebo předejte prázdné.
Výsledek běžné vazby obsahuje `constraint`; shodnost bodů vrací přeživší
`point`, protože jde o sloučení topologie, nikoli další rovnici.

| `kind` | `points` v pořadí | `geometry` v pořadí |
| --- | --- | --- |
| `horizontal`, `vertical` | dva body | prázdné; alternativně žádné body a jedna úsečka |
| `coincident` | přeživší, pohlcený bod | prázdné |
| `point_reference` | nativní bod, referenční bod | prázdné |
| `parallel`, `perpendicular`, `equal_length` | prázdné | referenční, řízená úsečka |
| `equal_radius`, `concentric` | prázdné | referenční, řízená kruhová geometrie |
| `point_on_circle` | bod | křivka podporovaná nativní vazbou |
| `point_on_line` | bod | úsečka nebo osa |
| `midpoint` | bod | úsečka |
| `midpoint_on_line` | prázdné | úsečka, přímková reference pro její střed |
| `symmetric` | zdrojový, zrcadlený bod | osa |
| `tangent` | volitelný bod dotyku | dvě křivky |

Například ukotvení existujícího nativního bodu na počátek skici:

```json
{"command":"sketch.constraint.create","arguments":{"sketch":"SKETCH_ID","kind":"point_reference","points":["POINT_ID","sketch_origin"]}}
```

`sketch.get` nyní vrací i stabilní jména základního počátku a os:
`sketch_origin`, `sketch_axis:x`, `sketch_axis:y`. Pro jiné reference se použijí
jejich skutečná uložená ID. Pořadí vstupů nenahrazujeme odhadem z jejich polohy.
U tečnosti nativní solver kontroluje platný kontakt a doménu křivek. Kontakt
konce úsečky s kružnicí lze nejprve zajistit vazbou `point_on_circle` a potom
na stejném bodě vytvořit tečnost. Pouhá číselná shoda polohy nevytváří vztah.

`coincident` přepojí závislosti na přeživší bod a pohlcený bod odstraní. Nelze
jím vyrobit neplatnou či zkolabovanou závislou geometrii. `undo` obnoví původní
body i jejich vztahy. Číselné vyhodnocení konfliktní nebo neplatné vazby se
nepublikuje. Nativní hlášení nadbytečnosti se vrací jako `redundant_constraint`.

`sketch.constraint.delete` přijímá `constraint`, odstraní vazbu a ověří zbývající
rovnice. Geometrická poloha může zůstat stejná, ale změní se počet volností.

`sketch.solve` výslovně vyřeší geometrii skici a uloží její případnou změnu.
Nespouští výpočet tělesa. `sketch.solve_status` provede stejné vyhodnocení na
dočasné kopii a skutečný dokument, jeho revizi ani cache nezmění. Oba příkazy
přijímají `iterations=100` (1–10000) a vracejí `status`,
`remaining_degrees_of_freedom`, `maximum_residual`. Stav `under_constrained`
je platný výsledek; `conflicting` a `invalid` jsou u mutujícího příkazu chyba
`constraint_conflict` bez commitu. Dotaz může tyto stavy vrátit jako informaci.
Výchozí obecný zákaz přepsání rozpracované GUI editace platí i pro `solve`.

Integrační sada prošla **7/7** (11,69 s),
`build/sketch-relation-integration-tests.log`; GUI i CLI byly přeloženy
z téhož zdroje. Test ověřuje všech patnáct druhů vztahů a obě formy H/V,
nezávislé geometrické rovnice, volnosti, sloučení topologie, odmítnutí chybných
vstupů, Undo a nativní uložení. Katalog má nyní **89 příkazů**.
