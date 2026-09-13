# Příkazy řezů

## Čtení uložených definic

`section.list/get/components` čtou otevřený Part nebo Assembly. Volitelné
`document` dovoluje přečíst jiný otevřený dokument bez jeho aktivace. Dotazy
neotevírají závislé soubory, nemění historii a nespouštějí OCCT, řešení vazeb
ani výpočet řezu nad sítí tělesa. Používají stejné `sections_for_part` a
`sections_for_assembly` jako GUI a výkresové zdroje.

```json
{"command":"section.list","arguments":{"offset":0,"limit":100}}
{"command":"section.get","arguments":{"object":"<section-id>"}}
{"command":"section.components","arguments":{"object":"<section-id>","offset":0,"limit":100}}
```

- `list` vrací ID řezu (`object`), název, ID vlastní skici a počátku,
  příznaky zobrazení/obrácení a uloženou platnost referencí umístění.
- `get` navíc vrací úplné uložené umístění včetně referencí, rámec skici,
  `path_mm`, řezové `frames`, `valid` a `error`. Rozměry jsou v mm a úhly
  ve stupních. Řezové soustavy vycházejí pouze z uložené ZIMA skici a rámce;
  jejich výpočet nepotřebuje těleso ani síť. `path_mm` slučuje sousední
  kolineární úsečky stejně jako zobrazení. Původní body a ID poskytuje
  `sketch.get` s vráceným ID skici.
- `valid` popisuje platnost otevřené řezové čáry, její soustavy a uloženého
  příznaku referencí umístění. Není kontrolou průniku s aktuálním tělesem
  ani novým ověřením existence každé reference. Neplatný řez zůstává
  čitelný se svými identitami a hlášením chyby.
- `components` vrací klíč `component` (ID tělesa Partu nebo přesná cesta
  výskytu Assembly), jméno, `available`, `stored`, `mode`, `custom_hatch`
  a výsledné `hatch`. Režimy jsou `cut_hatch`, `cut_only`, `uncut`; vzory
  `parallel`, `cross`, `dashed`. Šrafování zahrnuje `angle_degrees`,
  `spacing_mm` a `offset_mm`. Výchozí střídání úhlu je společné s GUI.
- Dříve uložené nastavení již chybějící komponenty se vrací s
  `available:false`. Opakované výskyty téhož zdroje mají různé cesty a
  různá nastavení. V Assembly vychází seznam z její uložené hierarchie.
- `list` a `components` mají `offset >= 0` a `limit` od 1 do 10 000
  (výchozí 2 000). Výsledky obsahují `items`, `total`, vlastnící dokument
  a jeho `revision`.

Chybějící řez vrací `section_not_found`; chybějící otevřený model
`unsupported_document`; neplatné stránkování `invalid_arguments`.

## Stav implementace a ověření

Čtení doplňují společné příkazy aktivace a odstranění popsané níže.
Tvorbu a vlastnosti včetně šrafování doplňují níže uvedené příkazy.
Dávková změna celé řezové skici ještě zbývá. Obyčejné příkazy
skicáře nadále odmítají přímou změnu skici řezu: celý návrh musí potvrdit
transakce řezu po kontrole jeho otevřené souvislé čáry. Společné umístění,
formát dokumentů a start šablony se nemění.

Výchozí test selhal na dosud chybějícím příkazu (0/1 za 0,10 s).
Po implementaci prošly modelové regrese a katalog **2/2 za 0,53 s**.
Ověřují uložená ID, řezovou čáru a směr, vlastní šrafování, chybějící těleso,
stránkování, neplatný řez, neaktivní dokument, nativní Assembly, opakované
vnořené výskyty i zachování historie a cache. Logy:
`build/section-query-baseline-tests.log`, `build/section-query-first-tests.log`.
Obě aplikace a všechny testovací programy se sestavily. Integrační sada
prošla **4/5 za 96,46 s**; procesní test odhalil chybu svého vstupu, kde se
cesta s diakritikou převáděla systémovým kódováním Windows. Po použití
společného `path_to_utf8` prošel tento test **1/1 za 21,32 s**. Produkční
kód se po integrační sadě nezměnil. Start aplikace a překlady prošly za
92,58 s. Logy: `build/section-query-integration-tests.log`,
`build/section-query-utf8-tests.log`. Katalog má **221 příkazů**, sada 123 testů.

## Aktivace a odstranění

```json
{"command":"section.activate","arguments":{"object":"<section-id>"}}
{"command":"section.activate","arguments":{}}
{"command":"section.delete","arguments":{"object":"<section-id>"}}
```

`section.activate` nastaví nejvýše jeden aktivní řez. Vynechané nebo prázdné
`object` přepne na trvalou položku Bez řezu; příznak zobrazení řezových rovin
se tím nemění. Před aktivací zkontroluje uložený příznak referencí umístění
a otevřenou řezovou čáru. Opakovaná aktivace stejného stavu vrací
`changed:false` a nepřidává Undo. Přepnutí na Bez řezu funguje i při
neplatné definici uloženého řezu.

`section.delete` odstraní jen zadanou definici. Neexistující ID vrací
`section_not_found` bez změny dokumentu. Undo obnoví stejnou skicu,
počátek, nastavení komponent i stav aktivace.

Akce vyžadují aktivní zobrazený Part nebo Assembly, stejně jako nabídka
stromu GUI. Cílový dokument musí být současně zobrazený i aktivní. Výsledky
obsahují `document`, `object`, `changed` a `revision`. Oba příkazy i nabídka
stromu používají společné `workspace::activate_section/remove_section`.
Part zachovává společnou aktualizaci souhrnů externích referencí při
odstranění vlastněné skici. Změna ukládá jednu transakci do nativního
modelu; nepočítá tělesa, neposouvá komponenty a nespouští solver vazeb.
Zobrazení řezu následně používá obvyklá data View.

Výchozí regrese akcí selhala na chybějícím `section.activate`
(0/1 za 0,22 s). Po implementaci prošly modelové testy a katalog
**2/2 za 0,53 s**. Ověřují aktivaci/Bez řezu, no-op, neexistující ID,
neplatnou čáru, Undo/Redo, neaktivní dokument a sdílení stejného snímku
geometrie Assembly. Logy: `build/section-action-baseline-tests.log`,
`build/section-action-first-tests.log`.

Obě aplikace a všechny testovací programy se sestavily. Dotčená integrační
sada prošla **6/6 za 129,03 s**: geometrie řezu, nové příkazy, skutečný CLI
proces (20,95 s), katalog, start a překlady (93,20 s) i GUI kontextové akce
(14,06 s). GUI regrese ověřuje také no-op aktivaci, skutečné odstranění
přes nabídku stromu a návrat celého tělesa. Log:
`build/section-action-integration-tests.log`. Katalog má **223 příkazů**.

## Přesnost vlastností šrafování

Otevření a potvrzení vlastností zachovává plnou přesnost uložených hodnot
šrafování i volby momentálně nedostupných komponent. Změna jednoho pole
aktualizuje pouze tento parametr; hromadná změna se přenese na vybrané
řádky. Otočení přičítá 90° k vlastnímu přesnému úhlu každého řádku.

Nová GUI regrese nejprve selhala na zaokrouhlení nezměněných hodnot
(`build/section-hatch-baseline-tests.log`, 0/1). Po opravě se sestavily obě
aplikace a testovací programy. Dotčené testy řezů a výkresů včetně obou GUI
kontraktů prošly **4/4 za 19,40 s**
(`build/section-hatch-verified-tests.log`). Katalog zůstává na 223 příkazech.

## Tvorba a vlastnosti

`section.create` vytváří řez z celé otevřené lomené čáry. `section.set`
upravuje uloženou definici a zachovává ID řezu, vlastní skici i počátku.
Oba příkazy i OK v dialogu vlastností používají společné
`workspace::prepare_section_edit/commit_section`.

```json
{"command":"section.create","arguments":{"path_mm":[[-50,0],[50,0]],"plane":"XY","name":"A–A","show_cut":true}}
{"command":"section.set","arguments":{"object":"<section-id>","reversed":true,"placement":{"y":2}}}
{"command":"section.set","arguments":{"object":"<section-id>","components":[{"component":"<body-id-or-occurrence-path>","mode":"cut_hatch","hatch":{"angle_degrees":12.3456789,"spacing_mm":2.3456789,"offset_mm":0,"pattern":"cross"}}]}}
```

- `path_mm` je povinné pouze při tvorbě: 2 až 10 000 dvojic konečných
  souřadnic, každá nejvýše ±1 000 000 mm. Sousední body musí být vzdálené
  více než 1e-7 mm. Nativní kontrola odmítá uzavřenou, rozvětvenou nebo
  nesouvislou čáru. Vytvářejí se běžné úsečky a body se stabilními ID.
- Volitelné vlastnosti jsou `name`, `plane` (`XY`, `XZ`, `YZ`), `reversed`,
  `show_plane`, `show_cut`, `placement`, `components` a cílové `document`.
  Prázdný či duplicitní název se odmítá. Bez názvu tvorba najde volné A–A,
  B–B atd. Aktivace nového řezu vypne dosavadní aktivní řez v téže transakci.
- `placement` mění číselná pole `x/y/z`, `rotation_x/y/z` nebo
  `reference_offset:N` podle existujícího kontraktu umístění. Vzdálenosti
  jsou v mm, úhly ve stupních. Zamčené, vázané a neznámé parametry se odmítají.
  Tyto příkazy zatím nepřidávají ani nenahrazují reference umístění.
- `components` je částečný seznam změn nejvýše 10 000 komponent. Každý
  přesný klíč smí být uveden jen jednou. Neuvedené komponenty i jejich
  nedotčená pole zůstávají beze změny; platí to i pro dříve uložené volby
  momentálně nedostupného tělesa nebo výskytu.
- Komponenta přijímá `mode`, `custom_hatch` a částečný objekt `hatch`.
  Vlastní šrafování se při prvním zapnutí odvodí z aktuálního děděného
  stylu. Zadání `hatch` je automaticky zapne; současné `custom_hatch:false`
  se odmítá. Samotné `custom_hatch:false` obnoví dědění bez ztráty uložených
  vlastních hodnot. Rozteč musí být 0,1 až 100 mm. Číselné hodnoty se
  uchovávají s plnou přesností, nezávisle na zaokrouhlení polí GUI.
- Celý soukromý návrh se ověří před jediným zápisem do historie. Kontrola
  používá již vypočtenou síť a původní reference. Nevolá OCCT, nemění
  geometrii těles ani neřeší vazby sestavy. Počítá pouze definovaný řez
  nad sítí, stejným způsobem jako potvrzení vlastností.
- Zastaralý dialog, změněná identita, neplatný vstup nebo nevyřešená
  reference nezmění dokument ani cache. Beze změny výsledného návrhu
  vrací editace `changed:false` a nepřidává historii. Výsledek obsahuje
  data `section.get`, `document`, `revision`, `changed` a `body_calculated:false`.

Formát `.prtz/.asmz` ani start šablony se nemění. Úpravu jednotlivých
entit řezové skici nadále nelze potvrdit běžným příkazem skicáře: pro
přestavbu čáry je nutná validace celé změny jako jedné operace řezu.

Výchozí regrese selhala na chybějícím `section.create` (0/1 za 0,11 s).
Základní modelové testy potom prošly **2/2 za 0,35 s**. Rozšířený test
odhalil vlastní neplatný ukazatel po přidání dokumentů do Workspace;
po opětovném vyhledání Partu podle ID prošel **1/1 za 0,19 s**. Ověřuje
nezávisle spočtenou plochu 300 mm², zachování objemu 6000 mm³, přesnost
šraf, neplatné dávky, jednu aktivaci, zastaralé návrhy, identity, Undo/Redo,
nativní soubory a oddělené volby opakovaných vnořených výskytů.

Obě aplikace i všechny testovací programy se sestavily. Integrační sada
prošla **9/9 za 190,58 s**: modelové kontrakty řezů, nový i dosavadní příkazy,
katalog, skutečný CLI proces (28,22 s), GUI konzole (47,29 s), start a
překlady (98,20 s), GUI vlastnosti řezu (15,08 s) a výkresový kontrakt.
GUI zkouška zahrnuje vytvoření řezu konzolí, otevření jeho vlastností a
potvrzení změny jména bez ztráty přesných hodnot šrafování.
Logy: `build/section-properties-baseline-tests.log`,
`build/section-properties-first-tests.log`,
`build/section-properties-expanded-tests.log`,
`build/section-properties-pointer-tests.log`,
`build/section-properties-full-build.log`,
`build/section-properties-integration-tests.log`.
Katalog má **225 příkazů**, CTest obsahuje 125 testů; tato etapa spustila
uvedených 9 dotčených integračních testů.
