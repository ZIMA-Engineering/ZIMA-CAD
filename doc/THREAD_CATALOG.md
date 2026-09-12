# Společný katalog závitů

`thread.catalog` čte stejná data jako vlastnosti Otvoru a Vnějšího závitu.
Nevyžaduje otevřený dokument, nepočítá geometrii a nemění historii ani Undo.

```text
thread.catalog metric M10
```

```json
{"command":"thread.catalog","arguments":{"standard":"whitworth","designation":"W 1/2"}}
{"command":"thread.catalog","arguments":{"standard":"metric","offset":100,"limit":100}}
```

`standard` je povinně `metric`, `whitworth` nebo `pipe`. Nepovinné
`designation` hledá přesný uložený název; neexistující název vrátí prázdné
`items`. Jemné metrické závity používají znak `×`, například `M10×1`.
Desetinná čárka v některých názvech zůstává součástí označení z původní tabulky.

`offset` je nezáporné celé číslo nejvýše 100000000; výchozí je 0.
`limit` je celé číslo 1–1000; výchozí je 100. Výsledek obsahuje `standard`,
`items`, `total`, `more` a `next_offset`. `total` počítá záznamy po filtrování.
Stránka za koncem tabulky je prázdná a má `more: false`.

Každá položka obsahuje:

- `designation`: původní označení;
- `nominal_diameter_mm`: jmenovitý průměr;
- `pitch_mm`: stoupání;
- `internal_root_diameter_mm`: tabulkový malý průměr vnitřního závitu;
- `external_root_diameter_mm`: tabulkový malý průměr vnějšího závitu;
- `preferred`: stejné zvýraznění běžných velikostí jako v GUI.

Všechny číselné rozměry jsou v milimetrech, i u palcových označení. Například
`W 1/2` má jmenovitý průměr 12,7 mm. `G 1/2` má podle stávající tabulky
jmenovitý průměr 20,955 mm; označení potrubí se nepřevádí jako průměr 12,7 mm.

## Zdroj a ověření

`document_core` nyní načítá neměnné katalogy bez Qt. CMake do programů vloží
stejné verzované TSV pod `resources/data/threads`; běžící aplikace nehledá
externí tabulky. GUI používá jen převod textu do QString. Hodnoty a pravidla
původního katalogu se nemění, včetně odvození stoupání G z tabulkových průměrů.
Jde o sjednocení dosavadních dat, ne o nový audit závitových norem.

Nativní test ověřuje všech 392 metrických, 28 Whitworthových a 24 trubkových
záznamů, jedinečnost názvů, rozměrový rozsah a konkrétní M10, M10×1, W 1/2,
G 1/2. Host testuje stránkování, hledání a odmítnutí chybných argumentů.
Procesový test spouští skutečné CLI mimo projekt; GUI test ověřuje převzetí
M10 do dialogu a zachování vlastního průměru při editaci jiného rozměru.

Tvorba a editace otvorů/závitů jsou navazující etapa. Nativní formát dokumentů
ani start šablony se tímto čtecím příkazem nemění.

Sestavení obou programů a testů prošlo (`build/thread-catalog-full-build.log`).
Související sada prošla **8/8** (62,70 s), `build/thread-catalog-related-tests.log`.
Po odstranění druhého zabalení tabulek v Qt a doplnění GUI dotazu i chybných
stránek prošlo závěrečné sestavení a **5/5** (61,14 s),
`build/thread-catalog-final-build.log`, `build/thread-catalog-final-tests.log`.
