# Tělesa a Boolean ve společné příkazové vrstvě

`cpp/modules/workspace/body_operations` obsahuje společné potvrzení vlastností
těles a Booleanů, aktivaci tělesa a posun kurzoru. Stejné funkce volají existující
okna vlastností, strom a konzole/CLI. Okna zůstávají interní s OK/Zrušit a
stávajícím ovládáním referencí.

## Kontrakt operací

`prepare_body_edit` a `prepare_body_boolean_edit` vytvářejí malý přechodný snímek
grafu historie bez vypočtené geometrie. Nové objekty dostanou identitu před
výpočtem. Těleso používá dosavadní `create_origin_bound_body`: tři polohové a
dvě orientační reference k počátku Partu. Nezavádí se další řešení umístění.

Potvrzení porovná původní graf se současným, ověří identitu upravovaného objektu,
spočítá pracovní kopii a teprve po úspěchu provede session commit. Zastaralý
návrh nemůže přepsat novější úpravu jiného tělesa. Zrušení pracuje pouze s náhledem.
Nezměněné hodnoty nevytvářejí položku Undo ani výpočet. Po změně vlastností tělesa
zůstává zachován dosavadní závěrečný průchod pro přesné fingerprinty normalizovaných
parametrů. Boolean používá dosavadní průchod s řešením referencí.

Aktivace odstraní potvrzený výběr stejně jako aktivace ve stromu.
Aktivace a kurzor jsou čisté dokumentové transakce bez OCCT; přebírají již
vypočtené výsledky. Kurzor uvnitř větve lze posouvat jen v aktivním tělese.
Uložení, Undo a Redo používají stávající session. Nadřazené sestavy se automaticky
nepřepočítávají. Nativní formáty a start šablony se nemění.

## Příkazy

| Příkaz | Argumenty pro textovou konzoli | Význam |
| --- | --- | --- |
| `body.list` | volitelné `document` | Tělesa a Booleany v pořadí historie, aktivní těleso, kurzor a dostupné výsledky |
| `body.get` | `body`, volitelné `document` | Uložené vlastnosti, původní reference, obsah větve a její kurzor |
| `body.create` | `name`, volitelné `visible active document` | Nové těleso na počátku Partu, standardně viditelné a aktivní |
| `body.set` | `body`, volitelné `name visible active document` | Patch názvu, viditelnosti nebo aktivace; alespoň jedna vlastnost |
| `body.activate` | volitelné `body document` | Aktivuje těleso; bez ID tělesa jeho aktivaci ukončí |
| `body.cursor` | `index`, volitelné `body document` | Bez body mění kurzor pořadí těles, s body kurzor uvnitř aktivní větve |
| `body.boolean.get` | `boolean`, volitelné `document` | Vlastnosti operace a ID jejích vstupních výsledků |
| `body.boolean.create` | `operation target tool`, volitelné `name document` | Výpočet ze dvou různých dostupných předcházejících výsledků |
| `body.boolean.set` | `boolean`, volitelné `operation target tool name document` | Změna zadaných vlastností a výpočet |

`operation` přijímá `add`, `subtract`, `intersect`. `index` je nezáporné celé
číslo od nuly; `visible` a `active` jsou boolean. Ostatní argumenty jsou řetězce.
ID vrací `body.list`, `body.get` nebo vytvoření objektu. Pro přeskočení volitelných
argumentů používejte JSON, například:

```json
{"command":"body.set","arguments":{"body":"ID-TELESA","visible":false}}
{"command":"body.cursor","arguments":{"index":0,"body":"ID-TELESA"}}
{"command":"body.boolean.set","arguments":{"boolean":"ID-OPERACE","operation":"intersect"}}
```

Každá úspěšná změna vrací `changed`; dotazy model neaktivují ani nepočítají.
`placement` v dotazu je uložený datový model: délky v mm, rotace ve stupních a
reference se stabilním vlastníkem a sémantickým klíčem. Přímé příkazové úpravy
umístění, pořadí/mazání větví a odvozené kopie jsou další samostatné etapy.
`body.set` nepřijímá libovolný JSON grafu nebo nevalidovanou serializaci dokumentu.

## Ověření

Modelový test bez Qt používá kvádry 10 × 10 × 10 a 4 × 4 × 4 mm. Očekávané
objemy ověřuje nezávisle: rozdíl 936 mm³, průnik 64 mm³ a sjednocení 1000 mm³.
Po posunu nástroje o 20 mm přes původní rovinnou referenci je sjednocení
1064 mm³. Testuje také vlastnictví prvků, stabilní ID, kurzory a vložení do správné
větve, neplatné vstupy, zastaralý návrh, Undo/Redo a skutečné uložení/načtení.

Integrační test tvoří tělesa a Boolean z konzole, mění je skutečnými okny
vlastností a kontroluje uložený objem. Procesový test provádí stejnou tvorbu
samostatným CLI. Dosavadních šest cílených regresí prošlo (11,58 s,
`build/body-commands-integration-tests.log`); samostatný modelový test také
(0,26 s, `build/body-command-model-tests.log`).

Kompletní Windows Release regrese: **60/60 prošlo**, 383,35 s,
`build/body-commands-full-tests.log`. Po doplnění shodného zrušení potvrzeného
výběru při aktivaci tělesa prošlo znovu **6/6** dotčených testů, 12,28 s,
`build/body-commands-final-tests.log`. GUI i CLI byly přeloženy z konečného
stavu (`build/body-commands-final-build.log`).
