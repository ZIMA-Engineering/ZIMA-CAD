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
| `body.cursor` | `index`, volitelné `body document` | Bez body ukončí aktivaci tělesa a nastaví hlavní kurzor; s body mění kurzor aktivní větve |
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
reference se stabilním vlastníkem a sémantickým klíčem. Umístění,
pořadí, mazání větví a odvozené kopie se obsluhují samostatnými příkazy.
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


## Přiřazení původní reference tělesu

`body.reference.set` přiřadí jednomu poli existujícího tělesa původní referenci
umístění. Tato etapa pokrývá tělesa Partu; modelovací prvky, konstrukce,
řezy a odstranění reference zůstávají navazující prací.

```json
{"command":"body.reference.set","arguments":{"body":"ID-TELESA","index":0,"reference":{"owner":"ID-ZDROJE","key":"PUVODNI-KLIC"},"offset_mm":2}}
```

- `index`: 0–2 jsou poziční pole, 3 je FRONT, 4 TOP.
- `reference`: povinné řetězce `owner` a `key`, volitelný `instance_path`.
  V Partu je tato cesta místní, tedy prázdná. Jiné položky objektu se odmítnou.
- `offset_mm`: vzdálenost od rovinného zdroje, výchozí 0. U zamčeného
  pozičního pole se převezme naměřená současná vzdálenost a zámek zůstane.
  U bodu, osy a orientačního pole se nenulové `offset_mm` odmítne.
- `flip`: obrácení směru reference, výchozí false.
- `derive_orientation`: stávající automatické doplnění orientačního pole
  při výběru rovinné poziční reference, výchozí true.
- `document`: volitelné ID otevřeného Partu podle společného cílení příkazů.

Zdrojem smí být počátek Partu nebo původní geometrie předchozího tělesa
podle pořadí grafu. Stejné omezení používá okno vlastností tělesa. Vlastní
geometrie, pozdější těleso, neznámý zdroj a cizí cesta instance se odmítnou.
Odvozené těleso nelze přímo měnit. Zdroj se hledá výhradně v uložených
původních datech a geometrii počátků; jeho druh nelze podvrhnout vstupem.

Příkaz používá sdílené přiřazení referenčního pole a stávající
`prepare_body_edit` / `commit_body_edit` jako Body Properties. Úspěšná změna
explicitně přepočítá těleso a tvoří jednu transakci Undo. Aktivní těleso se
zachová. Shodné zadání nevytvoří nový výpočet nebo krok historie. Chyba
nezanechá částečnou změnu grafu ani geometrie. Výstup je `body.get` doplněné
o `changed`; následné čtení `body.get` / `placement.get` nic nepřepočítává.

Modelový test ověřil skutečný posun o 13 mm při zachování objemu 24 mm³,
závislost na předchozím tělese, převzetí zamčené vzdálenosti, reakci na
změnu zdroje, FRONT, chyby bez částečné změny, Undo/Redo a nativní Part.
Původní test mylně zadal globální počet operací do dotazu na lokální hranici
tělesa; po opravě testu na jednu operaci cílového tělesa prošel **1/1 za 0,17 s**
(`build/body-reference-model-fixed-tests.log`). Po sestavení obou aplikací
prošla integrace **10/10 za 105,00 s**, včetně CLI, obousměrného GUI,
překladů, zámků a historie. Doplněná kontrola původní horní plochy kvádru
(výsledná poloha Z=33 mm), bodu a odmítnutí neúčinného offsetu prošla
**1/1 za 0,17 s** (`build/body-reference-topology-final-tests.log`).
Při doplnění testu bylo nutné ponechat výsledek dotazu na plochy naživu po
celou iteraci a zohlednit, že nativní kvádr je vystředěný. Produkční zdroje
ploch se neměnily. Finální sestavení obou aplikací i všech testovacích
programů následovala úplná regrese **135/135 za 551,56 s**, bez chyby
(`build/body-reference-full-build.log`, `build/body-reference-full-tests.log`).


## Sdílená viditelnost a hlavní kurzor (2026-09-15)

`body.set` s jedinou upravovanou vlastností `visible` používá stejnou
operaci `set_part_body_visibility` jako Skrýt/Zobrazit v kontextovém menu
tělesa. Mění pouze příznak viditelnosti. Nepřipravuje graf pro Vlastnosti,
nepočítá tělesa ani vazby a zachovává původní umístění i vypočtené výsledky.
Vytvoření tělesa a úplné Vlastnosti dále používají stávající výpočet.

`body.cursor` bez `body` nastaví hlavní kurzor a v jedné transakci ukončí
aktivaci tělesa. Platí to i tehdy, když je hlavní kurzor již na zadaném
indexu. Se zadaným `body` mění pouze kurzor uvnitř dané aktivní větve.
Neplatný index se odmítne před ukončením aktivace nebo jinou změnou dat.
Při již odpovídajícím indexu i aktivaci nevzniká krok Undo.

Kontextové Vložit před / Vložit za a značka kurzoru ve stromu sdílejí
`set_body_history_cursor` s CLI. Příkazové změny samotné viditelnosti
a kurzoru vracejí `body_calculated=false`. Undo/Redo i nativní uložení
používají běžnou session. Formát ani šablony se v této etapě nemění.

Modelový test kontroluje zachování geometrie, fingerprintů, ostatních
vlastností těles a původních referencí, jeden krok Undo, no-op, chybný
index a vlastnictví Partu aktivovaného v sestavě. GUI test vyvolává
skutečné kontextové menu i callback značky kurzoru a porovnává celé
uložené soubory s ekvivalentním CLI příkazem.

První modelový průchod prošel **1/1 za 0,32 s**. Obě aplikace a všechny
testovací programy jsou sestavené; související regrese prošla
**10/10 za 231,89 s**. Zahrnuje historii, vícetělesový Part, původní
reference těles, historii Assembly odečtů, command host, konzoli a
úplný průchod pracovním oknem.

Protokoly: `build/body-display-model-tests.log`,
`build/body-display-verified-build.log` a
`build/body-display-verified-tests.log`.
