# Pořadí historie sestavy přes GUI a CLI

Příkazy `history.list`, `history.can_move` a `history.move` podporují
Part i Assembly. GUI strom potvrzuje přesun sestavových položek stejnou
operací `workspace::move_assembly_history` jako příkazový hostitel.

## Seznamy a identity

Assembly uchovává čtyři oddělené seznamy:

- `components`: bezprostřední výskyty Partů a podsestav;
- `constructions`: samostatné konstrukční objekty;
- `sketches`: kontejnery samostatných skic;
- `cuts`: vlastní profilové odečty sestavy.

`history.list` vrací `type: "assembly"`, položky `items` a objekt `orders`
s uvedenými čtyřmi seznamy stabilních ID. `suppressed` v položce popisuje
uložený příznak potlačení; účinný stav komponenty včetně závislostí je
součástí příkazu `component.get`.

Cílem přesunu je `object`. U skici jde o ID jejího kontejneru z `orders.sketches`,
nikoli o ID vnitřní skici. U komponenty jde o její ID výskytu v bezprostředně
vlastnící sestavě. ID vnitřního dílu podsestavy nelze použít k jeho přesunu
z nadřazené sestavy.

Volitelné `before` určuje následující položku stejného seznamu.
Vynechání přesune objekt na konec. Přesun před sebe nebo ponechání poslední
položky na konci nevyvolá změnu. Přesun mezi různými seznamy se odmítne.

## Transakce a závislosti

`history.can_move` pouze ověří pořadí, vlastnictví a reference; vrátí
`allowed` a `would_change`. Nemění dokument ani vypočtená data.

Přesun komponent, konstrukcí a skic nepotřebuje OCCT, řešení vazeb
komponent ani regeneraci odečtů. Zachová původní reference, vyřešené
pracovní rámy a sdílené vypočtené výsledky. `history.move` při změně
vytvoří jediný krok Undo/Redo a vrátí `body_calculated: false`.

Kontrola zahrnuje i závislost procházející jiným seznamem, například
skica → konstrukční rovina → další skica. Přesun nesmí porušit dosud správné pořadí
zdroje a závislého objektu. Stávající pravidlo umožňující
nezávislou opravu již porušených návazností zůstává zachované.

Odečty používají existující `move_assembly_cut`, včetně jeho výslovného
výpočtu a kontrol původních referencí. Při skutečné změně jejich pořadí
se vrací `body_calculated: true`. Původní příkazy
`assembly.cut.move/can_move` zůstávají dostupné.

Volitelné `document` může při dotazu určit jiný otevřený dokument.
Změna vyžaduje aktivní vlastnický dokument a respektuje otevřenou editaci
GUI. Aktivace vnořené sestavy zachovává zobrazenou nadřazenou sestavu a
přesnou cestu aktivního výskytu.

## Ověření

Modelový test ověřuje všechny seznamy, odmítnuté a prázdné změny, vazbu
mezi komponentami, nepřímou závislost skic, Undo/Redo, nativní uložení a
vnořenou aktivaci. Při přesunu datových položek kontroluje zachování
sdílených výsledků a umístění všech komponent i revize zdrojového Partu.

Nezávislá kontrola odečtů vychází z kvádru 10 × 10 × 10 mm. Dva oddělené
odečty mají objemy 1 a 2 mm³; po změně pořadí zůstává 997 mm³ a druhý
výskyt Partu má 1000 mm³.

GUI test vyvolá skutečný callback stromu pro komponenty, konstrukce a
skici. Porovná celé uložené `.asmz` s výsledkem stejného příkazu po Undo.
Ověřuje také stejné odmítnutí pořadí porušujícího referenci.

Základní modelová regrese Part/Assembly prošla 2/2 za 0,79 s.
Obě aplikace i všechny testovací programy jsou sestavené. Širší regrese
prošla **13/13 za 257,92 s**: historie Part/Assembly, odečty, komponenty,
reference, skici Assembly, katalog, samostatný proces CLI, překlady,
GUI konzole a úplný průchod aplikací.
Logy: `build/assembly-history-verified-build.log` a
`build/assembly-history-verified-tests.log`.
Katalog má 292 příkazů; CTest registruje 160 testů.
