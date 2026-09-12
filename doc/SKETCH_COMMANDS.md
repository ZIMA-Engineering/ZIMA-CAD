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


## Kóty a jejich vlastnosti

`sketch.dimension.create/get/set/delete` přijímají `sketch` a volitelné
`document`. Create přijímá `kind`, uspořádané `points` a `geometry`;
get/set/delete používají vrácené stabilní `dimension`. Používají nativní
factory a solver skicáře, stejnou validaci čísel jako dialog Vlastnosti kóty
a společnou transakci dokumentu. Nemění pravidla umístění kontejnerů.

| `kind` | Reference v pořadí |
| --- | --- |
| `distance`, `distance_x`, `distance_y` | jedna úsečka v `geometry`, nebo dva body v `points` |
| `distance_x`, `distance_y` k ose | jeden bod a opačná základní osa: X kóta k `sketch_axis:y`, Y kóta k `sketch_axis:x` |
| `point_line` | jeden bod a jedna přímka/osa |
| `symmetric` | jeden nebo dva body a osa |
| `line_distance` | referenční a řízená rovnoběžná přímka |
| `radius`, `diameter` | jedna kružnice, oblouk nebo záznam zaoblení rohu |
| `angle` | jedna úsečka |
| `three_point_angle` | první bod, vrchol, druhý bod |
| `angle_between` | dvě přímky; nebo čtyři body dvou přímek; nebo dva body a referenční přímka |
| `symmetric_angle`, `symmetric_line_distance` | osa a jedna nebo dvě přímky |
| `ellipse_major`, `ellipse_minor`, `ellipse_rotation` | jedna elipsa |

Délkové hodnoty jsou v **mm**, úhlové ve **stupních**. Výsledek uvádí `unit`.
Zadání reference nepředpokládá její výběr myší. Nativní solver ověřuje vhodnost,
řešitelnost i případné zdvojení řídicího rozměru.

Create i set přijímají nepovinné vlastnosti:

- `value`, `driving`, `locked`: zámek chrání geometrii proti tažení. Záměrná
  číselná změna přes vlastnosti je možná i při zamčené kótě, stejně jako v GUI.
  Referenční kóta (`driving=false`) zachová naměřenou hodnotu, ignoruje změnu
  `value` a nemůže zůstat zamčená.
- `position=[x,y]`: původní poloha popisku ve skice; `solution_side` je -1/1,
  `angle_sector` je -1/0/1 podle nativního řešení.
- `limits={lower,upper}`: číselné meze; `null` konkrétní mez odstraní.
- `text`: řetězce `prefix`, `suffix`, `text_override`, `tolerance_mode`,
  `symmetric_tolerance`, `single_tolerance`, `upper_tolerance`, `lower_tolerance`.
  Každé pole má nejvýše 2048 bajtů. Režimy tolerance jsou prázdný řetězec,
  `symmetric`, `single_deviation`, `deviations`.
- `layout`: `plane_quarter_turns` 0–3, nezáporný `envelope_offset` nebo null,
  `text_along`, `text_outward`, `line_offset`, `radius_rotation_degrees`,
  přepínače `arrows_reversed`, `radius_center_line_hidden`. Úhlová kóta má
  rovinu danou měřenými rameny a nepovoluje nenulové `plane_quarter_turns`.

Neznámá pole se odmítají. Hodnota, text a rozmístění popisku se potvrzují
atomicky jedním Undo; odmítnutá hodnota neuloží ani platnou část popisku.
Samotná změna popisku nemění geometrii. Prázdná či totožná editace nevytváří
novou revizi. `get` vrací zvlášť `document_layout` a `sketch_layout`, protože
jde o dvě existující uložené vrstvy. Příkazové vlastnosti ukládají dokumentovou
vrstvu stejně jako GUI vlastnosti kóty mimo aktivní skicář.

```json
{"command":"sketch.dimension.create","arguments":{"sketch":"SKETCH_ID","kind":"radius","geometry":["CIRCLE_ID"],"value":12,"locked":true,"layout":{"text_along":3}}}
```

Těleso zůstává při změně skici v posledním vypočteném stavu. Teprve explicitní
`regenerate` zpracuje upravený profil. Regrese ověřuje změnu délky profilu
10 × 5 mm vytaženého 2 mm: původní objem 100 mm³ zůstane až do Regenerate,
poté je při délce 20 mm objem 200 mm³. Nativní soubory ani šablony nepotřebují
změnu formátu. Katalog nyní obsahuje **93 příkazů**.


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


## Externí reference a promítnuté profily

Čtyři příkazy používají původní referenční data uložená při výpočtu tělesa.
Projekce je společná s GUI výběrem externí reference a nevolá OCCT. Skica,
reference a případná profilová křivka se mění jednou transakcí. Parametry
`sketch` a volitelné `document` mají stejný význam jako u ostatních příkazů.

| Příkaz | Argumenty a výsledek |
| --- | --- |
| `sketch.reference.create` | `kind` (`edge`, `point`, `axis`, `face`), `owner`, `key`, volitelné `instance_path`, `profile=false`; vrací `reference`, `source_document` a při profilu také `geometry` |
| `sketch.reference.project` | `reference`; přidá naši profilovou křivku a vrátí její `geometry` |
| `sketch.reference.delete` | `reference`; odstraní vazbu na externí zdroj, zachová naši profilovou křivku i její ID |
| `sketch.reference.refresh` | výslovně aktualizuje reference této skici; vrací `broken_references` a počet referencí |

Zdrojové identity získáte přes `reference.list/get`; uložené reference ve
skice najdete přes `sketch.entities` a načtete pomocí `sketch.entity.get`.
`owner`, `key` a `instance_path` tvoří přesný původní zdroj. Zobrazená výsledná
hrana tělesa ani pořadové číslo hrany nejsou náhradou této identity.

```json
{"command":"sketch.reference.create","arguments":{"sketch":"SKETCH_ID","kind":"edge","owner":"SOURCE_OWNER_ID","key":"PERSISTED_EDGE_KEY","profile":true}}
```

V Partu musí zdroj předcházet cílové skice podle existujícího pořadí těles
a kontejnerů; dopředná závislost se odmítá. Ověření vlastníka používá společný
seznam vložených profilů, tedy i profily Helical/Sweep3D/Hole/Thread. Reference
se převádí stávající transformací do souřadnic vlastnícího tělesa a skici.
V kořenové skice Assembly je povinná přesná cesta zdrojového výskytu. Cestu
nevyvozujeme ze jména dílu a nespojujeme ji lomítky; předejte ji beze změny
z dotazu na reference. Tentýž Part lze referencovat ve více různých výskytech.

Kořenová skica sestavy nemá vlastní cestu aktivovaného Partu. Společná
validace nyní tento případ rozlišuje od Partu upravovaného v kontextu sestavy;
ten nadále vyžaduje obě cesty a ID vlastnící sestavy. Oprava odstraňuje stejné
chybné odmítnutí i v GUI. Nativní pole ani přípony souborů se nemění.

Pro příkazovou projekci se ze zapůjčených referenčních dat zkopíruje pouze
vybraná hrana/bod/osa nebo trojúhelníky vybrané plochy. Celá sestava se kvůli
jedné hraně nekopíruje. Přesné uzly, váhy a póly spline se promítnou přímo;
hrubé zobrazovací body nenahradí její přesný matematický podklad. Rovinná
plocha může poskytnout průsečnou přímku; ostatní průseky používají existující
projekci uložených dat plochy. Nejednoznačný zdroj se odmítá.

`profile=true` platí pouze pro hranu. První projekce vytvoří naši křivku
napojenou na referenci; druhá projekce stejného zdroje se odmítne bez změny.
Takto získanou křivku lze ořezávat a offsetovat již zavedenými příkazy. Při
odpojení reference zůstává poslední geometrie i identita křivky zachovaná.
Závislé vazby přímo na odstraněnou referenci zpracuje nativní mazání skicáře.

`refresh` čte poslední vypočtený stav daného dokumentu. Nestahuje novější
otevřený Part do nadřazené sestavy a nepřepočítává tělesa. Pro načtení změn
celého řetězce závislostí slouží výslovný `regenerate`. Chybějící původní zdroj
se označí jako `broken`; poslední platná profilová křivka zůstane zachovaná.
Menší posun přesné spline aktualizuje její podklad a ponechá ořezaný interval
i návazný offset. Konflikt solveru odmítne celou transakci.

Příkazové editování Partu aktivovaného uvnitř sestavy zůstává další etapou
obecného kontraktu aktivního výskytu. Obecná ochrana rozpracované GUI editace
a aktivovaného výskytu zůstává účinná. Zvláštní `reference.refresh/delete`
navíc nepřepisují uložené kontextové závislosti Partu mimo jejich vlastnící
sestavu. Katalog nyní obsahuje **97 příkazů**.


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


## Text skici bez GUI závislostí

`sketch.text.create` vyžaduje `value` a `position=[x,y]`, `sketch.text.set`
vyžaduje stabilní `text`. Oba přijímají `sketch` a volitelné `document`,
`height_mm` (výchozí 10), `angle_degrees` (0), `flipped` (false),
`modeling_geometry` (true), `horizontal` (`left/center/right`),
`vertical` (`bottom/middle/top`) a `color` (`green/white/yellow/red`).
Vynechaná vlastnost při set zůstává beze změny. `sketch.text.get` vrací tyto
vlastnosti, počet obrysů/bodů a skutečné meze `bounds_mm`, aniž by znovu tvořil
znaky. Mazání používá obecné `sketch.geometry.delete` s ID textu.

```json
{"command":"sketch.text.create","arguments":{"sketch":"SKETCH_ID","value":"Řez Ø10","position":[20,30],"height_mm":3,"modeling_geometry":false}}
```

GUI dialog i příkazovka nyní používají jednu modelovou tvorbu obrysů.
Přibalený OSIFONT se při sestavení vloží přímo do modelové knihovny; není
zapotřebí systémově nainstalované písmo ani spuštěný Qt proces. FreeType
čte vektorové obrysy, HarfBuzz zpracovává Unicode, kerning a skládání znaků.
Nativní text může být modelovou geometrií nebo pouhou anotací. Název písma
zůstává `osifont`, stejně jako u dosavadního dialogu.

Výška je jmenovitá výška verzálek písma. Skutečný inkoust některých znaků
je mírně nižší nebo vyšší: například OSIFONT má cap height 1515 jednotek,
ale obrys H je vysoký 1510. Zarovnání používá skutečné meze obrysů; řádkování
používá metriky fontu. Oba směry osy Y a zrcadlení v šablonách zachovávají
stávající GUI kontrakt. Test porovnává rozměry s původní cestou Qt.

Křivky písma se převádějí na uložené polygonové obrysy s odchylkou nejvýše
menší z hodnot **0,01 mm a 0,1 % jmenovité výšky**. Kontrola používá vzdálenost
řídicích bodů Bézierovy křivky od úsečky. Tím se zabrání stovkám zbytečně
krátkých stěn při vytažení běžného drobného textu. Text má nejvýše 4096
Unicode code pointů; nepodporovaný znak, neplatné UTF-8, prázdný obrys nebo
překročení výpočetního limitu odmítne celou změnu. Tabulátor odpovídá čtyřem
mezerám. Nativní soubor uchovává vlastní obrysy; otevírání je nepřetváří.

Převod textu na modelový profil nově sjednocuje orientaci vstupních smyček.
Jádro pak obrátí smyčku otvoru právě jednou. Tím se opravuje neplatný profil
znaků s otvory; vykreslované obrysy i jejich vlastní orientace zůstávají
zachované. Numerický test ověřuje objem vytažené číslice proti ploše jejího
inkoustu a současně kontroluje, že se otvor nezaplní.

Příkazy mění jen skicu a její historii; přepočet tělesa zůstává výslovný.
No-op nezmění revizi ani cache a Undo vrací přesné uložené obrysy.
Katalog nyní obsahuje **100 příkazů**.


## Parametrické vlastnosti B-spline

`sketch.bspline.get` přijímá `sketch`, `geometry`, volitelně `document` a
`limit` (1–4096, výchozí 256). Vrací stupeň, uzavření, interpolační režim,
pomocnou geometrii, `exact`, `read_only`, počet bodů a jejich stabilní ID,
souřadnice v mm, uzly a váhy. Při překročení limitu vynechá geometrická pole
a nastaví `geometry_omitted_by_limit`. Dotaz nepočítá těleso ani křivku.

`sketch.bspline.set` přijímá `sketch`, `geometry` a volitelně `degree`,
`closed`, `points`. `points` je úplný seznam `[x,y]` v dosavadním pořadí;
počet řídicích bodů se při editaci nemění a maximum příkazu je 4096. Stupeň
je celé číslo 1–25 a musí být menší než počet bodů. Změna neobnovuje ani
nepřevádí křivku: zachovává její ID, ID bodů, uzly, váhy a interpolační režim.
Přesná spline dovoluje změnit volné póly, ale zachovává stupeň i uzavření.
Spline odvozená z externí reference, ořezu nebo offsetu je řízena svým zdrojem.

Vlastnosti v GUI a příkaz používají `Sketch::edit_bspline_properties`.
Zadané souřadnice jsou po dobu výpočtu skici pevné cíle solveru; uložené
příznaky pevných bodů zůstanou zachovány. Konflikt s existující vazbou,
přesun pevného nebo zdrojem řízeného bodu a neplatná geometrie odmítnou celou
změnu. Současná změna více bodů je jedna transakce a jeden krok Undo.
Navazující offsety se aktualizují, výpočet tělesa se provede až výslovným
Regenerate. Beze změny parametrů nevzniká revize.

```json
{"command":"sketch.bspline.set","arguments":{"sketch":"SKETCH_ID","geometry":"SPLINE_ID","points":[[0,0],[3,10],[7,2],[10,0]]}}
```

Katalog po této etapě obsahuje **102 příkazů**. Zbývající modelovací domény
jsou nadále uvedeny v přehledu pokrytí.
