# Otevření posledního vypočteného stavu

## Zadání a rozsah kontroly (2026-09-14)

Uživatel navrhuje při otevření zobrazit poslední uložený vypočtený stav
a historii znovu vyhodnocovat až příkazem Regenerovat. Toto je audit aktuálního
kódu a návrh dalšího měření; neoznamuje hotovou optimalizaci načítání.

Vstupem je nativní dokument včetně parametrů, historie, původních referencí
a vypočtených dat. Výstupem má být rychle dostupný model pro View i editaci.
Prostředkem jsou již existující uložené výsledky a sdílené zdrojové snímky.
Čtení stromu jako dat není totéž jako nové provedení jeho modelových operací.

## Ověřené cesty

- Host `open` v `cpp/modules/command_host/src/host.cpp` používá
  `read_native_document` a `insert_native_document`. Nevolá `evaluate_history`
  ani regeneraci. Přidání do Workspace převezme načtené výsledky.
- `PartDocument::load` v `cpp/modules/document_core/src/part_document.cpp`
  přesto sestaví `kernel_operations(false, true)` pro celou historii,
  obnoví všechny uložené hranice a validuje jejich otisky. Příprava operací
  mimo jiné zpracovává zdrojové profily. Nejde o nové vytváření těles v OCCT.
- Pro každou hranici se volá `history_fingerprint(operations, index + 1)`.
  Implementace v `cpp/modules/kernel_api/include/zima/kernel/geometry_kernel.hpp`
  pokaždé znovu prochází celý příslušný prefix. Pro n operací se tak zpracuje
  nejméně n(n + 1)/2 položek; 1000 operací znamená 500 500 návštěv.
  Skutečná cena závisí na velikosti parametrů jednotlivých operací.
  Jde o nezávislou kontrolu počtu průchodů, nikoli naměřený čas.
- Obnova `original_references_mode=append` kopíruje dosavadní referenční
  geometrii a připojí přírůstek u každé hranice. Následuje validace referencí
  uložených hranic. Také tato práce může růst s délkou historie.
- `AssemblyDocument::load` obnoví uložené zdrojové snímky, konkrétní výskyty,
  polohy a vlastní operace. Na konci sestaví `build_scene()` pouze pro
  validaci a výsledek zahodí. Tato scéna využívá vypočtená data, nevytváří
  nová tělesa v OCCT.
- Session při vytvoření obnoví fyzikální odvozené hodnoty a synchronizují
  rozměrové identifikátory. Nejde o úplnou regeneraci geometrie.
- GUI `refresh_scene` volá `Workspace::refresh_source_geometry`.
  Otevřený zdrojový Part je autoritativní; zavřené zdroje se načítají
  z nativních souborů a opakovaná čtení omezuje cache. Vnořené sestavy se
  procházejí kvůli aktuálním zdrojům a připravují scénu pro zobrazení.
  Vlastní odečty a odvozené kopie sestavy zachovávají vypočtený výsledek
  do výslovné regenerace.

## Doporučený postup

Nejdříve odděleně změřit rozbalení a dekódování souboru, obnovu hranic,
přípravu operací a jejich otisků, validaci dat, aktualizaci zdrojů sestavy
a přípravu View/pickeru. Ze statického auditu nelze určit jejich podíl
na konkrétním pomalém souboru.

Cílem je jedna cesta: otevření přečte strukturu a poslední vypočtený stav,
starší mezivýsledky připraví podle potřeby editace. Výpočet těles, vazeb
a vlastních operací sestavy náleží explicitní regeneraci nebo potvrzení
modelové změny. Potřebné mezivýsledky a původní reference se nemají
zahodit; editace staršího prvku je musí stále umět použít.

Kontrola struktury, identity a bezpečnosti načtených dat musí zůstat
zachovaná. Nákladné kontroly lze přeorganizovat tak, aby neopakovaly stejné
průchody; poškozený soubor se nesmí přijmout jen kvůli rychlosti.
Změna způsobu ukládání otisků či indexu mezivýsledků by vyžadovala odpovídající
aktuální schéma a aktualizaci start šablon v config.

Assembly nesmí držet starou revizi Partu proti současným pravidlům:
aktuální vypočtený stav zdrojového Partu se musí projevit bez regenerace
Assembly. Výpočet vazeb a vlastních odečtů Assembly zůstane výslovný.
Zastaralý či chybějící výsledek má být rozpoznatelný; případný poslední
platný náhled nesmí předstírat aktuální vypočtený výsledek.

Všechna povinná data nadále patří do `.prtz`, `.asmz` a `.drwz`.
Nevzniká povinné externí úložiště cache ani historických revizí.
