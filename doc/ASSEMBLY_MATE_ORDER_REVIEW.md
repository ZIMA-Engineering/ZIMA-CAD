# Opraveno: pořadí výpočtu vazeb komponent

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

## Schválený a provedený zásah

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

## Ověření

- Řetězec A/B/C musí po založení mít 5/3/0 mm a po změně C na 10 mm 15/13/10 mm.
- Jediné Undo/Redo změny C vrátí/obnoví celý řetězec.
- Všech šest pořadí tří komponent dá stejnou geometrii, bez přeskládání stromu.
- Cyklus v nativním výpočtu bude odmítnut bez úniku dílčích poloh.
- Dosavadní modelové, procesové CLI a skutečné GUI testy vazeb a odvozených kopií.

Uživatel po vysvětlení dopadu na společné umístění výslovně povolil opravu
zprávou „povluji opravu.“ dne 2026-09-13. Dřívější blokace této konkrétní
opravy je tím vyřešena. Nativní solver nyní používá frontu závislostí nad
soukromým kandidátem a odmítá cyklus před publikováním výsledku.

Aktuální reprodukce nejprve selhala: očekáváno 5 mm, skutečnost 2 mm
(`build/mate-order-baseline-tests.log`, 0/1 za 0,28 s). Po opravě prošly
modelové testy **2/2 za 0,88 s** (`build/mate-order-first-tests.log`).
Kromě řetězce a všech šesti pořadí ověřují duplicitní cíl, zachování pořadí
stromu a sdílené geometrie, opakovaný výpočet, explicitní regeneraci,
nativní uložení, cyklus bez dílčích změn a původní pravidlo uzemnění.

Obě aplikace a všechny testovací programy se sestavily. Integrační sada
prošla **9/9 za 34,98 s** (`build/mate-order-integration-tests.log`):
Assembly, komponenty, vnořená aktivace, umístění, odvozené kopie včetně GUI
a skutečný CLI proces. Navazující GUI sada prošla **3/3 za 117,99 s**
(`build/mate-order-ui-tests.log`): vlastnosti komponenty, start a překlady
a aktualizace zobrazení sestavy. Nová chyba cyklu má všech pět překladů.

Rozsah opravy je pořadí existujících sestavových vazeb. Nemění rovnice,
zámky, identity, uložené pořadí stromu ani formát dokumentů. Nezavádí OCCT
při přepínání tabů. Samostatný výpočet odvozených kopií zůstává beze změny.
