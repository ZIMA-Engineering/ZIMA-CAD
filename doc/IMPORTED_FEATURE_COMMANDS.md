# Vlastnosti importovaného tělesa přes CLI

Příkazy upravují existující nativní prvek importovaný ze STEP nebo IGES.
Nové načtení souboru nadále zajišťují import.step a import.iges.

| Příkaz | Argumenty |
| --- | --- |
| import.get | container, volitelně document |
| import.set | container, volitelně name, combine, placement, document |
| import.reference.set | container, index, reference, volitelně offset_mm, flip, derive_orientation, document |

import.get čte libovolný otevřený Part bez výpočtu. Vrací ID dokumentu,
kontejneru, prvku a Body, název, operaci add/subtract, uložený zdroj a cestu
komponenty, mesh_deflection_mm (nebo null), velikost uloženého B-Rep v bajtech,
počet původních topologických identit, umístění s referencemi, souřadný rámec,
platnost referencí a revizi. Nečte původní STEP/IGES ani nekopíruje B-Rep do JSON.

import.set mění aktivní Part. Musí obsahovat alespoň jednu vlastnost. combine
přijímá add nebo subtract; name podléhá společné validaci nativních názvů.
placement je číselný patch x/y/z, rotation_x/y/z nebo reference_offset:N,
stejný jako ostatní příkazové vlastnosti. Délky jsou v mm, úhly ve stupních;
omezenou nebo zamčenou hodnotu nelze přepsat. Neplatný patch se celý odmítne.

import.reference.set používá stejný kontrakt jako
[ostatní reference umístění](PLACEMENT_REFERENCE_COMMANDS.md): index 0–2
označuje poziční pole, 3 FRONT a 4 TOP. reference obsahuje owner, key
a volitelnou prázdnou instance_path. Vlastní nebo pozdější zdroj, duplicitní
reference, neplatný index a cizí cesta jsou odmítnuty. Příkaz používá
prepare_part_feature_reference a sdílené potvrzení importovaného prvku.
Stejnou operaci nabízí placement.reference.set s argumentem object.

Výsledek mutace odpovídá import.get a obsahuje changed. No-op nepřidává Undo
a nepočítá těleso. Vlastnosti otevřené v GUI blokují mutaci příkazem, ale
dotazy nad potvrzeným dokumentem zůstávají dostupné. Editace vyžaduje aktivní
vlastnící Body; odvozené těleso je pouze ke čtení.

## Společná transakce GUI a CLI

commit_imported_feature přebírá poslední dosud neoddělenou potvrzovací větev
okna PrimitivePropertiesDialog, kterou používá importovaný prvek. Zachovává
přípravu referenční geometrie, řešení před výpočtem, původní editační hranici,
výpočet historie, obnovení externích referencí skic a commit Partu. Společný
řešič umístění ani ContainerPlacementSection se nemění.

Vlastnosti nemohou přepsat zdrojový B-Rep, topologické identity, cestu zdroje
ani importní přesnost. Vybraná geometrie je uložená v nativním .prtz; původní
STEP/IGES není potřebný pro editaci ani regeneraci. Formát ani šablony se nemění.

## Ověření

Modelové testy používají STEP i IGES kvádru 10 × 20 × 30 mm a původní soubor
před editací odstraní. Kontrolují objem 6000 mm³, přírůstek X po přiřazení
roviny YZ, identitu a neměnnost zdrojové geometrie, zámek offsetu, no-op,
atomické odmítnutí, neaktivní Body, Undo/Redo a nativní uložení. Samostatný
odečítací test vloží před import krychli 200 mm a ověří objem
8 000 000 − 6000 mm³, historii i uložený výsledek.

Procesní test spouští skutečné CLI pro STEP i IGES. GUI test otevře skutečné
Vlastnosti, mění offset 3 → 4 mm a kontroluje Cancel/OK, blokování mutace během
editace, Undo/Redo a nativní geometrii. Překlady nových zpráv jsou doplněné
v češtině, angličtině, němčině, francouzštině a ruštině.


## Oprava předání umístění importovanému tělesu

Geometrický test odhalil starší chybu: importovaný kontejner ukládal posun
a natočení, ale StepRequest je vůbec nepřebíral a výpočet používal původní
tvar. Adaptér importu nyní předává již vyřešenou polohu i rotaci do kernelu
a zahrnuje je do fingerprintu výpočtu. Kernel umístí runtime tvar a jeho
původní plochy, hrany a vrcholy stejnou transformací. Zdrojové identity,
původní B-Rep i archivní lokátory zůstávají nezměněné.

Tato oprava spotřebovává existující umístění kontejneru; nemění řešení
referencí, pravidla orientace ani společnou sekci umístění. Souřadnice prvku
jsou v rámci vlastnícího Body a umístění Body se nadále skládá samostatně.
Test kontroluje nejen uložené hodnoty, ale i skutečný posun geometrie,
záměnu rozměrů X/Y při otočení o 90° a původní identity ve výstupní síti.

Import v režimu add nadále skládá původní tvary do compoundu; neprovádí
sjednocení jejich průniků. Test proto rozlišuje součet objemů v režimu add
a skutečný odečet v režimu subtract. Tato stávající pravidla se nemění.


Nativní validace nyní přijímá také existující hodnotu combine=subtract
u importovaného prvku; dřívější kontrola ji odmítala až při ukládání.
Schéma souboru ani jeho pole se nemění. Odečet vyžaduje předchozí vypočtený
vstup ve vlastním Body a jinak se odmítne bez změny dokumentu. Test uloženého
odečtu znovu počítá těleso s čerstvým kernelem bez zdrojového STEP/IGES.


Závěrečné ověření Windows Release: obě aplikace a všechny testovací cíle
jsou sestavené. Úplná regrese prošla **150/150 za 647,59 s**, včetně
modelových, procesních CLI, překladových a skutečných GUI testů.
Logy: `build/import-feature-full-build.log` a
`build/import-feature-full-tests.log`. GUI ověřuje skutečný přírůstek X
1 mm po změně offsetu 3 → 4 mm i totožný výsledek uloženého Partu.
