# Přesouvání položek historie v Tree

Hlavní položku lze přesunout tažením levým tlačítkem. Zelená čára ukazuje
hranici vložení mezi sourozenci; zakázaný kurzor označuje nepovolené místo.
Klik bez tažení dál potvrzuje výběr v Tree a View. Escape přesun zruší.

Part přesouvá kontejnery, samostatné skici a konstrukční prvky ve společném
`history_order`. Assembly zachovává své existující kolekce: díly a podsestavy
lze přesouvat mezi sebou, operace, konstrukční prvky a skici uvnitř příslušné
kolekce. Přesun nikdy nemění vlastníka ani aktivní výskyt. Během otevřeného
editoru nebo příkazu je přesouvání zakázáno.

Kontrola před vložením používá uložené identity a závislosti. Zahrnuje umístění,
zdrojové skici, externí reference skic, cíle „až k“, reference hran/ploch,
konstrukční prvky a vazby mezi výskyty sestavy. Chrání oba směry přesunu.
Stejnou kontrolu používají příkazy posunu Partu nahoru/dolů.

Během tažení neprobíhá OCCT výpočet. Puštění spustí výpočet změněné historie
Partu nebo operací Assembly na pracovní kopii. Ztráta uložených referencí,
původní referenční geometrie nebo selhání výpočtu zabrání potvrzení. Pořadí
komponent Assembly se mění bez přepočtu zdrojových dokumentů. Úspěšná změna je
jedna transakce Zpět/Znovu, uloží se s dokumentem a zachová nastavení pohledu.

Regrese: `zima_cpp_ui_contract_tests` ověřují obousměrné závislosti, externí
reference vlastněných skic a sestav, zelenou čáru, kliknutí, zakázaný drop a
Escape. `--verify-startup` ověřuje přesun skutečných dokumentů, uložení a Undo.

Cílený integrační běh lze spustit s `ZIMA_VERIFY_HISTORY_DRAG_ONLY=1` a
argumentem `--verify-startup`; používá stejnou testovací funkci jako celý běh.
