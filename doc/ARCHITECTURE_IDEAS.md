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
- Drawing může být kontejner obsahující formát, rámeček, razítko, pohledy, řezy,
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
- Strom instance zobrazuje read-only strom zdrojového dílu a samostatný lokální
  počátek instance použitelný pro budoucí vazby.
- První prototyp ustavení používá až tři dvojice rovinných ploch, Offset a Flip.
- Aktuální `Face N` je pouze dočasná topologická reference. Před rozšířením
  sestavových vazeb je nutné zavést stabilní sémantické pojmenování ploch a
  diagnostiku ztracených či konfliktních vazeb.
