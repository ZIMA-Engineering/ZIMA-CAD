# Ke schválení: pořadí výpočtu vazeb komponent

## Ověřená chyba

V pořadí stromu A, B, C má A plošnou vazbu k B s odsazením 2 mm a B k C
s odsazením 3 mm. C je na z = 0. Po potvrzení vazby B vychází B = 3 mm,
ale A zůstane na 2 mm místo nezávisle spočtených 5 mm.

Důvod: `AssemblyDocument::calculate_placement_references` řeší komponenty
jedním průchodem v uloženém pořadí. A použije předchozí polohu B, která se
změní až později v témže průchodu. Stejnou funkci používá GUI náhled,
GUI potvrzení, CLI a explicitní regenerace.

Reprodukční rozšíření `cpp/tests/component_property_command_tests.cpp` je
uchované ve stashi `953168c8669d7391df429da82c4b187454e3f028`
(`codex pending approval: assembly mate evaluation order`). `build/component-chain-repro-tests.log`: **0/1**, očekáváno
5 mm, skutečnost 2 mm. Výchozí etapa CLI je commit `efbfc58`; její úplná
sada 107/107 a závěrečná sada 16/16 prošly před tímto novým scénářem.

## Konkrétní navržený zásah

Pouze v `cpp/modules/assembly/src/assembly_document.cpp`, ve funkci
`calculate_placement_references`, nahradit přímý průchod kandidátem:

1. Vytvořit mapu existujících ID výskytů na jejich místa v uloženém seznamu.
2. Z `target_reference.instance_path` každé neuzemněné komponenty sestavit
   seznam bezprostředních cílových komponent. Duplicitní cíle započíst jednou.
   Vlastní reference Assembly s prázdnou cestou nemá předchůdce. Chybějící
   výskyt nepřidává novou geometrii; stávající řešení neplatných referencí zůstává.
3. Zpracovat nejdříve komponenty bez předchůdců, pak jejich následovníky.
   Pro každou použít přesně dosavadní `make_placement_system` a
   `solve_placement`, pouze nad již aktualizovanými cíli.
4. Pokud zbude cyklus, odmítnout celý výpočet. Vše probíhá na současném
   soukromém kandidátovi; do živého dokumentu se polohy převezmou až po úspěchu.
5. Uložené pořadí komponent, identity, reference, rovnice, zámky, vzhled,
   zdrojové pakety a formát zůstanou beze změny. Bez nového volání OCCT.

Použije se iterativní fronta, nikoli rekurze nebo opakovaná regenerace.
Změna se projeví také v náhledu Vlastností, při tažení a regeneraci všech
sestavových vazeb; proto jde o společný zásah vyžadující konkrétní souhlas.

## Připravené ověření po schválení

- Řetězec A/B/C musí po založení mít 5/3/0 mm a po změně C na 10 mm 15/13/10 mm.
- Jediné Undo/Redo změny C vrátí/obnoví celý řetězec.
- Všech šest pořadí tří komponent dá stejnou geometrii, bez přeskládání stromu.
- Cyklus v nativním výpočtu bude odmítnut bez úniku dílčích poloh.
- Dosavadní modelové, procesové CLI a skutečné GUI testy vazeb a odvozených kopií.

Automatická kontrola schválení odmítla provedení změny: dopad na sdílené
umístění přesahuje samotné převedení příkazů do CLI a AGENTS.md vyžaduje
konkrétní předchozí souhlas. Produkční funkce tedy zůstává nezměněná.
