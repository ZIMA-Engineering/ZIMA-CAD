# Relace, materiál a tabulka variant přes společné příkazy

GUI konzole i samostatná CLI používají stejné operace jako potvrzení stávajících
oken Relace, Materiál a Family Table. Podporován je otevřený Part a Assembly.
Všechny změny mají jednu transakci Undo; shodné výsledné hodnoty žádnou.
Čtení, výpočty relací a změny materiálu používají uložené fyzikální hodnoty,
bez výpočtu B-Rep a bez obnovy rodičovských sestav.

## Příkazy

| Příkaz | Argumenty | Význam |
| --- | --- | --- |
| `document.relations.get` | `[document]` | Pořadí relací, uživatelské parametry, dostupné fyzikální hodnoty a přesnost |
| `document.relations.set` | `relations:Array`, `[document]` | Úplná náhrada a vyhodnocení relací |
| `document.material.get` | `[document]` | Vlastnosti, jednotky, jazykové popisy a nabídka jednotek |
| `document.material.set` | `properties:Array`, `[document]` | Úplná náhrada materiálových dat |
| `document.family.get` | `[document]` | Uložená tabulka variant |
| `document.family.set` | `table:Object`, `[document]` | Úplná náhrada tabulky variant |

Volitelné `document` označuje ID otevřeného dokumentu. Změny musí cílit na aktivní
samostatný dokument; během rozpracovaného GUI dialogu nebo vnořené aktivace se
příkaz odmítne. Výsledek obsahuje `document`, `revision` a při změně `changed`.
Uložení do `.prtz` nebo `.asmz` zůstává výslovné příkazem `save`.

```json
{"command":"document.material.set","arguments":{"properties":[{"key":"MATERIAL_NAME","value":"Hliník","descriptions":{"cs":"Název materiálu"}},{"key":"MASS_DENSITY","value":"2700","unit":"kg/m^3"}]}}
{"command":"document.relations.set","arguments":{"relations":[{"target":"grams","expression":"model.mass * 1000"},{"target":"double_grams","expression":"grams * 2"}]}}
{"command":"document.family.set","arguments":{"table":{"columns":["NUMBER","LENGTH"],"instances":[{"name":"Varianta A","values":{"NUMBER":"ZE-100","LENGTH":"20"}}]}}}
```

Relace se vyhodnocují v pořadí a navazující výrazy používají nezaokrouhlený
mezivýsledek. Podporují současný nativní jazyk aritmetiky a funkcí, nikoli Python
nebo shell. Cíle jsou jedinečné identifikátory ASCII. Názvy dostupných hodnot
jsou ve `model_values`; nedostupná hmotnost se nevymýšlí. Odstranění relace
ponechá poslední hodnotu jejího cílového parametru. Prázdný seznam odebere všechny
relace; otevření prázdného dialogu nevytvoří novou relaci samo.
Relace zatím ovládají uživatelské parametry, ne rozměry modelovacích prvků.

Materiálový řádek obsahuje textové `key`, `value`, volitelné `unit` a mapu
`descriptions` podle jazyka. Číselná hustota je konečná a kladná; podporuje
`kg/mm^3`, `kg/m^3`, `g/cm^3`, `lb/in^3`. Další povolené jednotky určuje daná
vlastnost stejně jako GUI. Hmotnost sestavy se stále počítá ze snímků jednotlivých
komponent; přiřazení materiálu sestavě nepřepíše jejich hustoty. Aktualizaci změn
zdrojového dílu do sestavy volí uživatel explicitní regenerací.

Tabulka variant uchovává stejné nativní údaje jako dosavadní GUI. Názvy sloupců
a instancí musí být jedinečné; instance se nesmí jmenovat jako základní dokument.
Chybějící buňka se doplní prázdným textem. Tabulka sama nevytváří geometrii
variant; takový výpočet není v současném modelu zaveden.

Validace proběhne nad soukromou kopií před změnou dokumentu. Dělení nulou,
neznámé jméno, neplatná jednotka, duplicita nebo chybná struktura zachovají
revizi i generaci dat. Parser omezuje rekurzi u závorek, unárních operátorů
i mocnin, včetně volání mimo konzoli. Limity: 4096 relací, 16384 bajtů výrazu,
256 aktivních vstupů do rekurzivních pravidel, 4096 materiálových vlastností,
512 sloupců a 4096 variant. GUI při chybném OK ponechá rozpracované hodnoty
v původním interním dialogu; Cancel je nezapíše.

## Ověření

Nezávislá kontrola: kvádr 10 × 20 × 30 mm při hustotě 2700 kg/m³ má 16,2 g.
Modelové testy kontrolují navazující relace, zachování B-Rep, Undo/Redo,
chyby bez částečné změny, dlouhé rekurzivní výrazy, jazykové popisy,
normalizaci tabulky, uložení Part/Assembly a neobnovení rodičů.
Procesové testy spouštějí CLI, ukládají a znovu otevírají nativní soubor.
GUI test potvrzuje stejná data přes původní dialogy, včetně chybného OK a Cancel.
Cílená sada prošla **5/5** (16,77 s), `build/engineering-metadata-tests.log`.
Katalog obsahuje **118 příkazů**. Celá Windows Release sada prošla **75/75**
(406,56 s), `build/engineering-metadata-full-tests.log`. Po doplnění validace
jednořádkového zápisu prošla závěrečná modelová, CLI a GUI sada **6/6**
(24,23 s), `build/engineering-metadata-final-tests.log`.

Texty parametrů, materiálových vlastností a jejich popisů jsou jednořádkové,
bez okolních mezer či tabulátorů, aby se zachovaly v nativním INI zápisu.
Klíče nesmí obsahovat `\\`, čárku, `=`, `[` nebo `]` ani začínat `#` či `;`.
Víceřádkové hodnoty a rezervované oddělovače se odmítnou před transakcí.
Hodnoty buněk tabulky variant jsou uvnitř JSON, proto toto omezení řádků nemají.
