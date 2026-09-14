# Odebrání reference společně pro GUI a CLI – návrh k odsouhlasení

Stav 2026-09-14: pouze rozbor a návrh, chráněný kód nebyl změněn.
Před implementací je nutný konkrétní souhlas podle oddílu
Container placement protection v [AGENTS.md](../AGENTS.md).

## Vstup, prostředky, výstup

Vstup: otevřený dokument, stabilní ID upravovaného objektu a index řádku
stejně jako u `placement.reference.set` (0–2 pozice, 3–4 orientace).

Prostředky: existující `PlacementReferenceRows`, společné modelové transakce
a dnešní obsluha odebrání v `ContainerPlacementSection`.

Výstup: odstraněná konkrétní reference při zachování zbývajících dat,
s totožným modelovým výsledkem po potvrzení GUI i příkazu. Zrušení dialogu
nebo odmítnutí neplatné transakce ponechá původní dokument.

## Ověřené současné chování

V `cpp/modules/ui/src/container_placement_section.cpp`:

- `remove_reference` vyprázdní poziční řádek na místě; zbývající řádky
  rozpracovaného dialogu neposouvá. Uvolní zámek prázdného řádku.
- Pokud stejný původní objekt, geometrický klíč a cesta výskytu jsou také
  v orientačním seznamu, odstraní tuto orientační položku. Zbylé orientační
  položky přeznačí jako FRONT/TOP. To je dnešní chování při odstranění pozice.
- Přímé odstranění orientačního řádku ve `refresh_orientation_table`
  pouze vyprázdní tento řádek; jiný orientační řádek neposouvá.
- Poziční koncové prázdné řádky se odstraní. Vnitřní prázdné řádky zůstávají
  dočasným stavem dialogu.
- `combined_references/populated_references` při předání do modelu prázdné
  položky filtrují. Návrh nemění tento existující způsob ukládání.
- GUI po změně obnoví tabulky, vyvolá společný náhled a následně znovu
  aktivuje výběr v odstraněném řádku. Pořadí je podstatné, protože aktualizace
  View může předchozí filtr výběru zrušit.

## Navržená změna

Vyjmout pouze datovou změnu seznamů a zámku do společné funkce vedle
`assign_placement_reference`. Její výsledek vrátí, zda se data změnila
a zda byla odstraněna navázaná orientační položka. GUI podle výsledku upraví
své popisky a zvýraznění; vlastní tabulky, obnovení výběru a náhled zůstanou
v GUI.

Nad stejnou funkcí doplnit `placement.reference.remove`. Existující doménové
transakce zkontrolují vlastnictví, připraví navrženou hodnotu a potvrdí ji
jedním krokem historie. Odstranění musí fungovat i pro chybějící zdroj;
nesmí vyžadovat opětovné dohledání geometrie reference, která se právě maže.
Ostatní reference a výsledný model musí projít obvyklou validací.

Rozsah zahrnuje uživatele společné sekce: tělesa, konstrukce, primitiva,
profily, tažení, otvory, řezy a importované prvky. Vložené komponenty Assembly
mají vlastní správu vazeb a tento přesun ji nenahrazuje.

Není navržená změna řešiče, formátu, geometrických identit, uchovávání revizí
ani reakce na přepnutí tabu. Výpočet tělesa patří do existujícího výslovného
potvrzení modelové transakce.

## Ověření před uzavřením

- První/prostřední/poslední poziční řádek, obě orientace, neexistující index,
  již prázdný řádek a zámek odstraněného i neodstraněného řádku.
- Navázaná orientace se musí párovat také podle přesné cesty výskytu.
- Stejná rozpracovaná data v GUI a modelové funkci; zachované následné
  zadávání reference po obnově View.
- GUI Cancel, OK, Undo/Redo a uložení/opětovné otevření.
- Skutečná geometrie alespoň primitiva, tělesa a konstrukce před i po změně;
  odmítnutý vstup bez částečné mutace.
- Původní zdroj reference chybí; odstranění této reference nesmí samo
  selhat jen proto, že zdroj nelze načíst.

Samostatně zůstává dříve předložená oprava zastaralých poloh navázaných
bodů 3D křivky. Tento návrh ji neslučuje ani automaticky neschvaluje.
