# ZIMA-CAD – nezávazné architektonické náměty

> Tento dokument je pracovní poznámka z neformální diskuse. Nejde o schválenou
> specifikaci ani závazný plán implementace.

## Jednotný kontejnerový model

- Základní abstrakcí ZIMA-CADu je kontejner.
- Kontejner má stabilní ID, název, parametry, vlastnosti, data a podřízené kontejnery.
- Skica, geometrická operace, solid, díl, sestava i výkres mohou využívat společný
  kontejnerový mechanismus; liší se obsahem dat a povolenými operacemi.
- Kontejnery se mohou vnořovat.
- Výsledná geometrie může vznikat skládáním a odečítáním výstupů kontejnerů.
- Společný mechanismus může později obsloužit kopírování, historii, verzování,
  vlastnosti a reference.

## Souřadný systém kontejneru

Každý prostorový kontejner může mít vlastní:

- počátek,
- osy X, Y a Z,
- roviny XY, YZ a XZ,
- transformaci vůči nadřazenému kontejneru.

Podřízená geometrie se vyhodnocuje v lokálním souřadném systému kontejneru.

## Interpretace ploch OpenCascade

OpenCascade rozlišuje zejména:

- `TopoDS_Face` – aktuální ohraničenou topologickou plochu,
- `Geom_Surface` – podkladový matematický povrch,
- `Wire` a `Edge` – hranice plochy,
- `TopLoc_Location` – umístění,
- `TopAbs_Orientation` – orientaci.

Plocha OpenCascade není automaticky stabilní trvalý kontejner ZIMA-CADu. Po změně
parametrů nebo booleovské operaci může vzniknout nová topologie a jiné instance
`TopoDS_Face`.

### Důsledky pro reference

- Nepoužívat pořadí typu `Face1`, `Face2` nebo `faces[4]` jako trvalou identitu.
- Oddělit dočasnou OCC topologii od trvalé reference ZIMA-CADu.
- Trvalá reference by měla popisovat zdrojový kontejner, zdrojovou operaci a význam
  plochy.
- Příklady významu: `StartFace`, `EndFace`, `LateralFace`,
  `GeneratedFromEdge` nebo u boxu `x_min`, `x_max`, `y_min`, `y_max`, `z_min`,
  `z_max`.
- Pokud referenční plocha při přepočtu zmizí, závislý kontejner má zachovat poslední
  platnou transformaci a reference se má označit jako neplatná.

## Budoucí analyzátor ploch

Pro aktuální `TopoDS_Face` může být užitečná analytická vrstva, která zjistí:

- typ povrchu,
- umístění a orientaci,
- UV rozsah,
- přibližný střed,
- normálu,
- obsah,
- počet hran.

Vhodné OpenCascade nástroje:

- `TopExp_Explorer`,
- `BRepAdaptor_Surface`,
- `BRepTools.UVBounds`,
- `BRepGProp.SurfaceProperties`,
- `GeomAbs_SurfaceType`.

Analýza popisuje pouze právě existující výsledek. Sama o sobě není stabilním
pojmenováním plochy.

## Sketch, Drawing a výměnné formáty

- Sketch může být společnou 2D geometrickou vrstvou pro modelování i výkresy.
- DXF může sloužit jako import/export 2D geometrie Sketch.
- Drawing je samostatný dokument `.drwz`; jeho rozpracovaný základ obsahuje
  více listů, formát papíru, vazbu na zdrojový model a promítnuté pohledy.
- Později může být kontejnerem obsahujícím také rámeček, razítko, řezy,
  kóty, poznámky, tabulky a Sketch.
- Cílem je jeden obecný 2D editor používaný v různých kontextech, nikoli několik
  nesouvisejících editorů.

## Témata k pozdějšímu rozhodnutí

- Přepočet závislostí kontejnerů a dependency graph.
- Stabilní reference mezi kontejnery.
- Historie, undo/redo a verzování kontejnerů.
- Obecné topologické pojmenování mimo jednoduché parametrické tvary.
- Přesná hranice mezi kontejnerem, feature a výsledným body/solidem.

## Aktuální prototyp sestav

- Sestava používá samostatný dokument `.asmz`, ale sdílí metadata, jednotky,
  přesnost a uživatelské parametry s dokumentem Part.
- Vložený díl je instance odkazující relativní cestou na zdrojový `.prtz`.
  Transformace instance patří sestavě a nesmí měnit zdrojový díl.
- Strom instance zobrazuje strom zdrojového dílu. Po aktivaci dílu v kontextu
  sestavy se jeho podsložky chovají jako v Partu; rozdíl aktivní/neaktivní
  instance je především ve view a dostupnosti modelovacích nástrojů.
- Ustavení používá až tři dvojice referencí. Podporuje rovinné vazby s offsetem,
  souosost datumových a generovaných os, úhlové vazby a Flip. Nabídka typu se
  omezuje podle geometrie a zbývajících stupňů volnosti; stabilní solver vybírá
  polohu nejbližší současné transformaci.
- Vazby se ve 3D view zobrazují jako klikací kóty. Hodnotové vazby lze editovat
  přímo ve view, nulové a souosé vazby zůstávají viditelné jako stav ustavení.
- Centrální `TopologyRegistry` poskytuje stabilní `FaceRef`, `EdgeRef` a
  `VertexRef` pro Box/Wedge, Extrusion a Revolve. Externí skici i podporované
  operace historie používají tyto identity místo dočasných indexů. Podporovaná
  propagace přes přidání a odečtení zachovává původ a rozlišuje chybějící a
  nejednoznačný výsledek; zbývá ji rozšířit na obecné booleovské kombinace a
  další typy operací.
- Protrusion a Revolve v sestavě jsou pouze odečítací operace. Mohou působit na
  všechny nebo jen vybrané instance, ale nesmějí měnit původní `.prtz`.

## Aktuální základ výkresů

- Jeden `.drwz` obsahuje více listů. Každý list má vlastní formát A4–A0.
- A4 je vždy na výšku, ostatní podporované formáty vždy na šířku.
- List je geometrie ve skutečných milimetrech. Počátek je vpravo dole, kladné X
  směřuje doleva a kladné Y nahoru, aby změna formátu zachovala polohu razítka.
- Pracovní prostor má černé pozadí bez výplně papíru; hranici listu představuje
  bílý obdélník.
- Výkres ukládá relativní cestu a ID zdrojového dílu nebo sestavy. Geometrie
  pohledů se za běhu odvozuje ze skutečné topologie nativního rendereru;
  zastaralá uložená 2D cache promítnutých čar se záměrně nepodporuje.
- Pohledy jsou vybíratelné, přesouvatelné a odstranitelné. Odvozené pohledy
  zachovávají vazbu na rodiče a lze je vytvářet v osmi směrech po 45 stupních
  podle evropské nebo americké projekční metody.
- Každý pohled má vlastní režim zobrazení. Čárové režimy používají společnou
  klasifikaci hran a siluet s 3D modelem; stínované režimy přebírají barvy a
  vyhlazené normály modelu a používají softwarový Z-buffer.
- Pohled může zobrazit samostatně přesouvatelný popisek s názvem a měřítkem.
- Je implementovaný první asociativní lineární rozměr ve výkresu. Zbývá
  dokončit ISO kóty, tolerance, pozice, popisky a technické symboly.
- `.frmz` a `.tblz` lze upravovat jako Sketch dokumenty. Renderer razítka
  `.tblz` čte přímo persistovaný `[Sketch]`; neodvozuje vložení z hranic
  geometrie a nepoužívá skrytý posun. Sketch `(0, 0)` je výkresové `(0, 0)`.
- Text razítka zachovává kotevní bod, obě zarovnání, otočení, převrácení,
  font, barvu a výšku. CAD výška je kapitálková výška fontu, nikoliv výška
  inkoustové stopy konkrétního řetězce.
- Parametrická pole jsou sémantické textové entity s tokeny; renderer přes
  Sketch nedokresluje žádnou pevně naprogramovanou tabulku.
- BOM Repeat Region v razítku včetně Item Number a Quantity je funkční.
  Zbývají řezy, detaily a další produkční výkresové funkce.

## Parametry a relace

- Uživatelský parametr je vždy materializovaná hodnota použitelná beze znalosti
  jejího původu ve featurech, sestavě, rodinné tabulce, razítku a výkresu.
- Relace patří pouze modelovému dokumentu Part nebo Assembly. Ukládá dvojici
  `target + expression`; vyhodnocený výsledek zapisuje do běžného
  `user_parameters[target]` a jazykově sdílené hodnoty parametru.
- Výraz se parsuje přes Python AST, ale vyhodnocuje se vlastním allow-list
  interpretem. Soubor modelu proto nemůže importovat moduly, přistupovat k
  souborům ani volat aplikační Python.
- První systémový kontext poskytuje `model.volume`, `model.area`, `model.mass`
  a `material.density`. Objem a plocha vycházejí z výsledného OCCT shape;
  hustota se normalizuje na `kg/mm^3` a hmotnost se ukládá v kilogramech.
- Drawing relace nevlastní. Dialog Parametry otevřený z Drawingu pracuje se
  zdrojovým Partem nebo Assembly a po uložení obnoví výkresovou geometrii i
  razítko.
- Nový Part se klonuje z nakonfigurovaného `start_part.prtz`, přičemž dostane
  nové ID dokumentu a cílový název. Tím zůstávají výchozí relace součástí
  běžného souborového modelu a nejsou natvrdo zapsané v aplikačním kódu.
