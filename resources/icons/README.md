# ZIMA-CAD Icons — základní sada

První sada čistých SVG ikon pro ZIMA-CAD.

## Styl

- viewport: `0 0 24 24`
- hlavní kresba: `stroke="currentColor"`
- tloušťka čáry: `1.75`
- konce a spoje: `round`
- pozadí: průhledné
- zelený akcent: `#80AA1A`
- bez gradientů, stínů a plastických efektů

Ikony používají `currentColor`, proto se automaticky přizpůsobí světlému a tmavému motivu aplikace.

### Doporučené barvy

- světlý motiv: `#1E1E1E`
- tmavý motiv: `#F2F2F2`
- aktivní/zvýrazněný stav: `#80AA1A`
- zakázaný stav: `#909090`

## Obsah

Referenční geometrie:
`origin.svg`, `point.svg`, `axis.svg`, `plane.svg`

Modelování:
`sketch.svg`, `sketch-3d.svg`, `box.svg`, `pyramid.svg`, `wedge.svg`,
`cylinder.svg`, `sphere.svg`, `protrusion.svg`, `revolve.svg`,
`sweep.svg`, `fillet.svg`, `chamfer.svg`, `shell.svg`, `blend.svg`

Dokumenty:
`part.svg`, `assembly.svg`, `drawing.svg`, `drawing-format.svg`,
`title-block.svg`

Dokumentové ikony používají jednu společnou významovou sadu v záložkách,
stromech, dialogu Nový dokument a v Qt file-dialogu:

- Part je modrá krychle se třemi odstíny ploch a kontrastním obrysem.
- Assembly má všechny tři plochy vyplněné odstíny žluté.
- Drawing používá list s pohledy a kótovací čárou.
- Drawing Format používá list s vnitřním rámečkem.
- Title Block používá tabulkové razítko.

Základní GUI:
`new.svg`, `open.svg`, `save.svg`, `undo.svg`, `redo.svg`,
`delete.svg`, `view-fit.svg`, `measure.svg`, `settings.svg`

Skicář používá mimo jiné `sketch-common-tangent.svg`: dvě křivky, jejich
společnou tečnou úsečku a dva zelené body dotyku. Ikona označuje nástroj tvorby
nové parametrické geometrie, nikoliv samotnou dodatečnou vazbu **Tečná**.

## Qt

SVG lze barvit pomocí stylu nebo při renderování. Zelený významový akcent je součástí některých ikon a zůstává zachován.
