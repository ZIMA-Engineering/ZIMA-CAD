# Příkazy skořepiny (Shell)

`shell.create/get/set` ovládají současný nativní Shell. Vlastnosti GUI i CLI
potvrzují operaci přes `workspace::commit_shell`, používají stejnou tloušťku,
původní identity ploch, výpočet jádra a jeden krok Undo.

## Vstup a příklad

Shell odebírá materiál **směrem dovnitř**. Vybrané plochy otevře; prázdný
seznam vytvoří uzavřené duté těleso. Vstup musí být jedno souvislé vypočtené
těleso. Geometrické podmínky výpočtu OCCT se touto etapou nemění.

```json
{"command":"shell.faces","arguments":{}}
{"command":"shell.create","arguments":{"thickness_mm":2,"faces":[{"owner":"ID-ZDROJE","key":"KLIC-PLOCHY"}]}}
{"command":"shell.get","arguments":{"container":"ID-SHELLU"}}
{"command":"shell.faces","arguments":{"container":"ID-SHELLU"}}
{"command":"shell.set","arguments":{"container":"ID-SHELLU","thickness_mm":1}}
{"command":"shell.set","arguments":{"container":"ID-SHELLU","faces":[]}}
```

`shell.faces` vrací jedinečné identity skutečného vstupního tělesa. Bez
`container` čte hranici u kurzoru historie aktivního Tělesa; s `container`
čte vstup **před** daným Shellem. Proto při editaci nabízí i plochu, kterou
Shell v konečném výsledku otevřel. Nezahrnuje plochy jiného Tělesa ani
pozdějšího kontejneru. Identita je dvojice `owner`, `key` s prázdnou
`instance_path`; číselné pořadí ploch slouží jen k prohlížení seznamu.

Dotaz podporuje volitelné `owner`, `offset` (0–100000000), `limit` (1–5000,
výchozí 500) a `document`. Vrací `items`, `total`, `offset`, `limit`,
`has_more`, ID dokumentu, případného kontejneru a revizi. Seznam má smysl
pouze pro tento operační vstup; nepředstavuje obecnou referenci umístění.

Čtení vypočtené hranice pouze zapůjčí uložený výsledek, bez kopie celé
triangulace. Dotazy nespouštějí OCCT, regeneraci ani změnu historie.

## Parametry a potvrzení

- `thickness_mm`: JSON číslo 0,001–1000000 mm, výchozí 1 mm. Příliš velkou
  tloušťku, pro kterou geometrie nemůže vzniknout, odmítne skutečný výpočet.
- `faces`: celý nový seznam otevíraných ploch. Při tvorbě je volitelný;
  při editaci vynechání zachová stávající seznam. Prázdné pole zavře všechna
  otevření a ponechá dutinu. Nejvýše 10000 referencí, každá pouze jednou.
- `name`: volitelný neprázdný název; `document` musí při zápisu odpovídat
  aktivnímu Partu. Editace vyžaduje aktivní vlastnící Těleso.
- `container`: povinné stabilní ID pro `get/set`; `set` potřebuje také alespoň
  jeden měněný parametr. `get` vrací i ID prvku a Tělesa, zámky a revizi.

Reference obsahuje `owner`, `key` a volitelně prázdné `instance_path`.
Náhradní analytická geometrie se nepřijímá. Vybrané plochy se ověřují proti
vypočtenému vstupu; neexistující, cizí, duplicitní či pozdější reference
nezanechá částečnou změnu. Výpočet dále odmítá nejednoznačné identity.

Zámek tloušťky má stejný klíč `thickness` jako GUI. Shodný patch nemění
revizi ani cache. OK vypočítá a potvrdí změnu; Cancel ji zahodí. Seznam ploch
lze měnit i odebíráním ve stávajícím okně Vlastností. Operace nevystavuje
samostatné umístění ani nový solver.

## Nativní data a ověření

Formát Shellu, přípony dokumentů a struktura start šablon se nemění.
Všechny parametry a identity zůstávají v `.prtz`. Nevznikají požadované
vedlejší soubory.

Modelové regrese porovnávají dutý kvádr a různé kombinace otevření s
nezávislými objemy stěn. Kontrolují také válec a kouli, změnu zdroje,
hranici historie, oddělení Těles, prázdný vstup, neplatné reference a rozměry,
zámek, atomické chyby, Undo/Redo a nativní uložení se studeným výpočtem.
Skutečný CLI proces a GUI přidávají tvorbu, editaci, OK/Cancel a odebírání
otevřených ploch. Výsledky sestavení a běhů jsou uvedeny níže.


### Nalezená chyba sférického vstupu

Nový geometrický test a samostatné CLI reprodukovaly kolizi identit při
uzavřeném Shellu koule. Samotná koule záměrně nepersistuje šev a póly OCCT
jako ZIMA hrany/body; Shell jim přesto vytvářel odvozené identity podle
jediné sousední plochy, takže se jejich klíče shodovaly.

Výpočet Shellu nyní rozpoznává tyto pomocné prvky hladké sférické plochy
a nevytváří pro ně nové entity. Předchozí uložené reference zachovává,
skutečné plochy, hrany a vrcholy stále kontroluje na úplnost a jednoznačnost.
Pravidlo se nevztahuje obecně na singularity jiných ploch, například vrchol
kužele. Ostatní Boolean operace se nemění. Otisk odvozeného výpočtu Shellu
má novou verzi; zobrazení uloženého dokumentu tím nevyvolává regeneraci.

První související běh skončil **10/12** (65,00 s): nový modelový test odhalil
výše uvedenou chybu koule a nový procesní scénář chybně převáděl Unicode
cestu testovacího souboru přes systémovou kódovou stránku. Procesní příprava
nyní předává UTF-8 stejně jako ostatní CLI testy. GUI tvorba a editace Shellu
prošly již v tomto běhu. Logy: `build/shell-command-full-build.log`,
`build/shell-command-related-tests.log`, `build/shell-sphere-probe.log`.


Po opravě prošel celý modelový test **1/1** (0,75 s),
`build/shell-command-curved-build.log` a `build/shell-command-curved-tests.log`.
Zahrnuje dva poloměry duté koule, její přesný objem a identity ploch po uložení,
Shell po dvou svislých zaobleních s nezávislým objemem a odmítnutí dvou
nespojených solidů. Doplněný test otevřené polokoule prošel **1/1** (0,78 s),
`build/shell-command-rim-build.log` a `build/shell-command-rim-tests.log`:
objem odpovídá rozdílu polokoulí R10/R9 a oba skutečné kruhové okraje zůstaly
referencovatelné. Produkční kód se mezi těmito dvěma běhy neměnil.


Závěrečné sestavení obou programů a všech testů prošlo. Úplná sada má
**98/98** úspěšných testů (463,81 s), bez opakování:
`build/shell-command-final-build.log` a `build/shell-command-full-tests.log`.
Zahrnuje opravený skutečný CLI proces v adresáři s diakritikou, GUI tvorbu a
editaci, geometrii, historii těles, sestavy, výkresy, skicář, překlady a
nativní ukládání. Katalog obsahuje **187 příkazů**. Následuje Fillet/Chamfer;
celkové CLI má nadále další otevřené řádky v tabulce pokrytí.
