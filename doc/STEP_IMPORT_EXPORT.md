# Import a export STEP

## Import do Partu

**Soubor → Importovat → STEP** vloží každý koncový díl / výskyt ze STEP do
samostatného **Tělesa**. Uvnitř tělesa je běžný kontejner importovaného STEP.
Opakovaný výskyt vytvoří další těleso ve své poloze. Existující tělesa Partu
zůstanou zachována; import je automaticky neslučuje Booleanem.

Polohy se převedou z celé zdrojové hierarchie do souřadnic Partu. Zůstanou
zachovány názvy a fyzické rozměry; například rozměr 1 inch se převede na
25,4 mm. Interní geometrie ZIMA-CAD používá milimetry. Přesnost zobrazovací
sítě vychází z nastavení cílového dokumentu.

Kontejner ukládá zmrazený B-Rep a zdrojové identity topologie. Uložení,
znovunačtení a regenerace proto nepotřebují původní STEP soubor.

## Import do sestavy

Vznikne jedna vložená STEP sestava. Zachová vlastní hierarchii podsestav,
jednotlivé díly a jejich místní polohy. Každý unikátní zdrojový díl má jeden
soubor `.prtz`, každý unikátní zdroj podsestavy jeden `.asmz`. Opakované
výskyty odkazují na společný zdroj. Každý importovaný Part obsahuje těleso
s kontejnerem STEP.

Soubory vzniknou v novém podadresáři `<název STEP>_zima` vedle cílové sestavy,
u dosud neuložené sestavy v pracovním adresáři. Další import použije nový
adresář s číselným příponovým označením a nepřepíše předchozí import.
Soubor bez původní hierarchie vytvoří plochou sestavu. Samostatný STEP Part
se také vloží přes kořenovou STEP sestavu.

## Export

**Soubor → Exportovat → STEP** používá aktuální vypočtený stav dokumentu.
Part exportuje viditelná výsledná tělesa samostatně. Tělesa spotřebovaná
Booleanem se neexportují podruhé. Sestava zachová podsestavy, díly, opakované
definice, názvy a polohy. Skryté a potlačené komponenty se vynechají.
Export zapisuje milimetrové jednotky a nijak nemění model ani jeho historii.

Geometrie vnořených komponent je součástí uloženého snímku sestavy. Export
nepřebírá samovolně změny z jiných otevřených dokumentů. Aktualizaci závislostí
si uživatel vyžádá příkazem **Regenerovat** na sestavě. Pokud uložený dokument
potřebné geometrické snímky neobsahuje, export na nutnost regenerace upozorní.

Změny STEP nepřidávají podporu vnořených sestav do exportu STL. Import barev
ploch zůstává samostatným navazujícím úkolem.

## Ověření

Regresní test `zima_cpp_step_model_contract_tests` provádí skutečný zápis a
čtení STEP přes OCCT. Kontroluje dva druhy dílů, opakovanou podsestavu,
rotace kolem všech tří os, globální rozsahy geometrie, objemy, samostatná
tělesa při opakovaném importu, uložení/znovunačtení sestavy, STEP v palcích
a regeneraci Partu po odstranění původního STEP souboru.
