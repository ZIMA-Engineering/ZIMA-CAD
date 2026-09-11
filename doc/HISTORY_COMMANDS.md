# Historie Partu z GUI, konzole a CLI

Stav 2026-09-11. Šest příkazů používá společné operace v
`workspace/history_operations.hpp`; strom už neobsahuje druhou implementaci
potlačení, přesunu, mazání ani kurzoru Partu. Čisté kontroly pořadí a závislostí
jsou v `workspace/history_policy.hpp`. Sestava je zatím používá ze svého GUI;
tato etapa nepřidává její mutační příkazy.

## Příkazy

| Příkaz | Argumenty | Význam |
| --- | --- | --- |
| `history.list` | `[document]` | Pořadí prvků, stabilní ID, jména, druhy, vlastnící tělesa, potlačení, kurzor a chyby posledního výpočtu |
| `history.suppress` | `object suppressed [document]` | Výslovně nastavit boolean potlačení, vypočítat a zapsat historii |
| `history.delete` | `object [document]` | Smazat prvek, samostatnou skicu, konstrukční objekt, těleso nebo Boolean |
| `history.move` | `object [before document]` | Přesunout před objekt ve stejném rozsahu historie; bez before na konec |
| `history.can_move` | `object [before document]` | Ověřit pořadí, závislosti a vlastnictví bez výpočtu geometrie |
| `history.cursor` | `index [document]` | Nastavit nulový index vložení v celkovém pořadí historie Partu |

Příkazy přijímají stabilní ZIMA ID, nikoli jméno položky nebo index hrany OCCT.
Volitelný dokument u změn musí být aktivní; dotazy mohou číst jiný otevřený Part.
Změna uvnitř tělesa vyžaduje jeho aktivaci. Samotná tělesa a Booleany se přesouvají
v hlavním pořadí těles, prvky vždy v pořadí svého tělesa. Přesun mezi tělesy není
změnou pořadí a tento příkaz ho nepovoluje.

`history.cursor` čísluje celkovou historii Partu a respektuje rozsah aktivního
tělesa. `body.cursor` naproti tomu nabízí lokální index uvnitř tělesa, nebo index
v hlavním pořadí těles a Booleanů. Ani jeden kurzor nepřepočítává geometrii.

```json
{"command":"history.suppress","arguments":{"object":"<ID prvku>","suppressed":true}}
{"command":"history.can_move","arguments":{"object":"<ID prvku>","before":"<ID následujícího prvku>"}}
{"command":"history.cursor","arguments":{"index":0}}
```

## Transakce a reference

Výpočet změny proběhne nad pracovní kopií a historie se zapíše jedním commitem.
Neplatný typ argumentu, cizí rozsah, porušení pořadí závislostí nebo výjimka před
commitem ponechají dokument a jeho historii beze změny. Stejná operace z GUI a
příkazovky sdílí Undo/Redo. Opakované nastavení stejné hodnoty nepřepočítává ani
nevytváří další revizi.

Přesun chrání existující správně uspořádané závislosti, vazby těles a vstupy
Booleanů. Po skutečném výpočtu kontroluje také zachování uložených referencí,
původní referenční geometrie a platnosti konstrukčních, kontejnerových a externích
skicových referencí. `history.can_move` kontroluje pouze datové podmínky; jeho
úspěch není zárukou budoucího geometrického výpočtu. Odpověď obsahuje `allowed`
a `would_change`; zamítnutí vrací konkrétní chybový kód.

Mazání zachovává dosavadní opravu navazujících Fillet/Chamfer referencí: hranu
lze přepojit na vstup před odstraněným prvkem pouze při jediné jednoznačné shodě
uložené geometrie. Nová, změněná nebo nejednoznačná hrana se neodhaduje.

Potlačení nebo smazání může podle dosavadního chování GUI zanechat navazující
prvek nevypočitatelný. Dokument pak uchová změnu i platnou předcházející geometrii.
Příkaz vrátí `ok:false`, `code:calculation_errors` a **`data.changed:true`** spolu
s mapou chyb. Tento výsledek výslovně odlišuje provedenou změnu s chybami výpočtu
od zamítnuté transakce. Je možné použít Undo nebo opravit reference. Běžná CLI
dávka se na něm zastaví; `--keep-going` pokračuje. Uložení je vždy výslovné.

Žádný dotaz, kurzor ani náhled možnosti přesunu nevolá OCCT. Operace neregenerují
nadřazené sestavy. Řešení umístění, formát dokumentů a startovní šablony se nemění.

## Ověření

Modelová regrese porovnává skutečné objemy kvádrů, stabilní reference před a po
přesunu, potlačení, mazání, správné vložení na kurzor, Undo/Redo, ochranu neaktivního
tělesa a Booleanů, neplatné argumenty a uložení/načtení. Samostatný scénář se dvěma
zkoseními ověřuje, že po smazání prvního funguje druhé nad přeživší hranou.
Další zkosení na nově vzniklé hraně ověřuje reportovanou obnovu platného
předchozího výsledku při ztrátě zdroje, mapu chyb a návrat přes Undo.
GUI regrese používá skutečný strom, přetažení, kurzor a nabídky Potlačit/Odstranit.
Procesová regrese spouští samostatné CLI a čte výsledný nativní soubor.

Kompletní Windows Release regrese: **61/61 prošlo**, 386,26 s,
`build/history-full-tests.log`. Obsahuje modelové testy bez Qt, skutečné CLI
procesy, historii v GUI, již existující test přetažení stromu i scénáře
modelování, sestav, výkresů, spline křivek a offsetů. GUI a CLI byly přeloženy
ze stejného konečného zdroje. Samostatná rozšířená regrese hran prošla rovněž
(`build/history-edge-tests.log`, 0,47 s).
