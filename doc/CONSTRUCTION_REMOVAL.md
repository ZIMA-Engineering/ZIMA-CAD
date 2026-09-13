# Mazání samostatných konstrukcí

`construction.delete` přijímá `construction` jako stabilní ID samostatného
konstrukčního kontejneru a volitelný aktivní `document`. Nepřijímá pořadí bodu,
název objektu ani ID jeho zobrazované entity. Používá stejnou operaci
`workspace::delete_construction` jako odstranění konstrukce ze stromu GUI.

```json
{"command":"construction.delete","arguments":{"construction":"<construction-id>"}}
```

Výsledek obsahuje `document`, `construction`, `removed`, `changed`, `revision`
a `body_calculated`. U Partu obsahuje také chyby navazujícího výpočtu, pokud
existuje vypočtená hranice. Neúspěšná validace nic neodstraní. Úspěšná operace
má jeden společný krok Undo/Redo a ukládá se do dosavadního `.prtz` nebo `.asmz`.

## Vlastnictví a reference

- Part používá stávající mazání historie: zachová pravidla aktivního tělesa,
  znovu vyhodnotí model a případnou nedostupnou navazující geometrii označí
  dosavadním způsobem. Tímto příkazem se nemění pravidlo mazání použitých prvků
  v Partu. Když pozdější prvek nelze vypočítat, zůstává dostupná předchozí
  platná geometrie podle běžného kontraktu historie.
- Assembly před odstraněním ověří, zda konstrukci nebo její vlastněné body,
  entity a počátky nepoužívá jiné uložené umístění, konstrukce, skica nebo řez.
  Zahrnuje komponentové vazby a umístění i cíle sestavových odečtů. Samotné
  shodné ID na jiné cestě výskytu neznamená závislost na místní konstrukci.
- Kontrola externích skic v otevřených Partech zahrnuje i vložené profily a
  skici řezů. Prochází vypůjčené stavy; nekopíruje geometrii a historii všech
  otevřených dokumentů. Nejde o index všech libovolných dokumentů na disku.
- Vlastněný bod 3D křivky se upravuje úplným seznamem bodů nadřazené křivky.
  Vložená dráha tažení se upravuje příkazem vlastnícího tažení. Odstranění
  dítěte tímto příkazem se odmítne; nesmaže skrytě celého rodiče.

Assembly používá dosavadní následné řešení konstrukcí a komponentového
umístění. Kontrola závislostí nevolá OCCT. Solver umístění, zdrojové identity,
sériový formát ani šablony se touto etapou nemění.

## Chyby

`construction_not_found` označuje neexistující ID; `owned_construction`
a `embedded_construction` chrání vlastněné body a dráhy. `construction_in_use`
chrání používanou konstrukci Assembly. Part zachovává také dosavadní chyby
`inactive_body` a `read_only_body`. Obecné kontroly hostitele nadále odmítají
editaci neaktivního dokumentu, výkresu či otevřeného rozpracovaného dialogu.

## Ověření

Výchozí modelový test potvrdil chybějící příkaz (`unknown_command`),
`build/construction-removal-baseline-tests.log`. Testy ověřují:

- Part: objem kvádru 24 mm³, vlastnictví historie, nativní uložení a Undo/Redo;
- odmítnutí bodu patřícího 3D křivce, vložené dráhy a neaktivního tělesa;
- Assembly: odkazy jiného bodu, bodu v křivce, skici, řezu, vlastností a cíle
  odečtu, komponentové vazby a externích profilů/řezů Partu;
- zachování revize, geometrie a historie při odmítnutí a rozlišení cizí cesty;
- skutečný CLI proces s uložením Partu i Assembly a opakováním Undo/Redo;
- nabídku stromu GUI a odstranění stejných objektů přes konzoli;
- úplnost všech pěti lokalizací nových textů.

Obě aplikace a všechny testovací programy se sestavily
(`build/construction-removal-integration-build.log`). Integrační běh ověřil
**9/10 testů za 168,86 s**, včetně skutečného CLI (20,43 s), konzole s nabídkou
stromu (51,81 s) a celého startovního GUI testu s překlady (94,71 s).
Zbývající nový test odhalil chybu svého nastavení: aktivoval další Part, ale
ponechal zobrazený původní dokument, takže obecná ochrana správně odmítla
nesouhlasící kontext dříve než kontrola vložené dráhy.

Po opravě pouze tohoto testovacího nastavení prošly **4/4 cílené regrese
za 0,89 s** (`build/construction-removal-final-tests.log`), včetně všech devíti
scénářů používané konstrukce. Produkční kód se po integračním běhu neměnil.
Všechny příslušné modelové, procesové a GUI scénáře tak prošly.
Katalog příkazů se rozšiřuje z 209 na 210 položek.
