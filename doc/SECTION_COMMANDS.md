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

Tato etapa zpřístupňuje čtení. Příkazová tvorba, změna celé řezové skici,
šrafování, aktivace a odstranění řezu ještě zbývají. Obyčejné příkazy
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
