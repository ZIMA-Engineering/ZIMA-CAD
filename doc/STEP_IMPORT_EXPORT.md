# Import a export STEP

Další podporované importy: [IGES a DXF do Partu a sestav](IGES_DXF_IMPORT.md).

Převod spline hran do skici: [přesná geometrie a oddělená reference](SKETCH_EXACT_PROJECTION.md).

## Nastavení před importem STEP / IGES

Po výběru souboru se v Partu i sestavě otevře společné interní okno
Nastavení importu. Ukazuje název, velikost a formát souboru; u STEP také
schéma, pokud je dostupné v úvodní hlavičce. Získání těchto informací
nepřevádí geometrii a čte nanejvýš prvních 64 KiB souboru.

Jemnost zobrazení (odchylka v mm) se předvyplní z odpovídající startovací
šablony nastavené v configu, nikoli z případně změněné přesnosti otevřeného
dokumentu. Aktuální šablony používají 0,1 mm. Lze zvolit 1 mm i vyšší
hodnotu. Menší hodnota znamená jemnější síť; přesná geometrie a rozměry
se nezmění. Počet trojúhelníků omezuje také úhlové kritérium OCCT.

OK spustí import se zvolenou hodnotou, Cancel nezahájí výpočet ani zápis
souborů. Volba se uloží do importovaných kontejnerů v nativním Partu a
platí i při regeneraci. Nemění config, přesnost cílového dokumentu ani
předchozí importy. U STEP sestavy ji dostanou všechny nově importované
Party. DXF používá svůj dosavadní importní postup.

### Kontrolní měření jemnosti

Na válci Ø200 × 200 mm importovaném ze STEP vytvořila odchylka 0,1 mm
396 zobrazovacích trojúhelníků, 1 mm 124 a 5 mm 100. Výpočet přesného
objemu se nezměnil. Jde o kontrolu vlivu nastavení na jednoduchý model,
nikoli měření doby importu velké sestavy.

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

Jeden zdrojový STEP produkt zůstává jedním Partem i tehdy, když obsahuje
solid a samostatné plochy (například pomocné plochy závitu šroubu).
Geometrické položky jednoho produktu nevytvářejí falešnou podsestavu.
Skutečné podsestavy se rozlišují podle produktových vazeb v původním STEP,
nikoli podle počtu těles nebo ploch. Toto seskupení platí také pro import do Partu.
Změna se projeví při novém importu; již uložené sestavy automaticky nepřestavuje.

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
