# Archivní verze přes GUI a CLI

Správa číslovaných záloh používá společné operace workspace a společné
číslování s nativním ukládáním. Pracuje se soubory vedle dokumentu, například
`díl.prtz.2` a `díl.prtz.10`; žádný nový typ povinného úložiště nevzniká.

| Příkaz | Argumenty |
| --- | --- |
| `file.archives.list` | `path` |
| `file.archives.prune` | `path`, `keep` |
| `directory.archives.list` | volitelně `path`, jinak pracovní adresář |
| `directory.archives.prune` | `keep`, volitelně `path`, jinak pracovní adresář |

Relativní cesty vycházejí z pracovního adresáře hostitele. Operace nevyžadují
otevřený dokument. Cesta souboru označuje základní nativní dokument, nikoli
jednu zálohu. Podporované přípony jsou `.prtz`, `.asmz`, `.drwz` a současné
šablony `.frmz`, `.tblz`; velikost písmen přípony nerozhoduje. Samotný
základní dokument nemusí existovat. Jméno základního souboru se porovnává
přesně včetně velikosti písmen.

## Výpis a řazení

Výpis obsahuje `path`, `scope`, `groups`, `count` a `total_size_bytes`.
Každá skupina vrací `document_path` a `archives`. Archiv obsahuje absolutní
UTF-8 `path`, přesný řetězec `version` a `size_bytes`. Číslo verze zůstává
řetězcem, aby JSON neztratil přesnost dlouhých čísel. Pořadí je číselné,
od nejstaršího: 2 před 10. Počáteční nuly jsou povolené; shodné číselné
verze se deterministicky řadí podle cesty.

Adresářový výpis zpracuje pouze jeho přímé soubory. Podadresáře, symbolické
odkazy na soubory, nenativní přípony a nečíselné koncovky vynechá. Výpis
neotevírá nativní obsah, nenačítá model a nevolá OCCT.

## Odstranění starších verzí

`keep` je povinné nezáporné celé číslo. Hodnota 0 odstraní všechny vybrané
zálohy, 1 ponechá nejnovější, N ponechá nejnovějších N **pro každý dokument**.
Počet větší než počet záloh je bezezměnový požadavek. Aktuální soubor
se neodstraňuje.

Výsledek vrací `selected_count`, `removed_paths`, `removed_bytes`, `keep`
a `changed`. Souborové mazání nemá modelové Undo; nemění revizi, neuložené
editace, geometrii ani výběr otevřeného dokumentu. Host po skutečné změně
obnoví dostupnost souborových akcí GUI. Během otevřené editace je příkazové
mazání blokované stejným kontraktem jako ostatní mutace; výpis zůstává dostupný.

Společná operace nejprve ověří celý vybraný seznam: platný číslovaný nativní
název, jedinečnost cest, běžný soubor, velikost a čas poslední změny. GUI
uchovává tento snímek přes existující potvrzovací okno. Nově vzniklá záloha
se tím automaticky nepřidá do dříve potvrzeného seznamu.

Chybný nebo již změněný snímek se odmítne před prvním odstraněním.
Pokud operační systém selže až během mazání, výsledek obsahuje
`archive_io_error`, `failed_path` a přesné `removed_paths` již odstraněných
souborů. GUI i textové hlášení konzole uvádějí chybu a skutečný počet odstraněných souborů. Operace
netvrdí, že lze souborovou dávku vrátit modelovým Undo.

## Sdílená implementace

`archive_operations` nahrazuje vlastní mazací smyčky čtyř akcí menu pro
staré verze. Jejich potvrzení Ano/Ne a volby zachovat poslední verzi zůstávají.
`versioned_file.hpp` poskytuje společné rozpoznání a číselné řazení pro
GUI, CLI i nativní ukládání. Číslo nové zálohy se inkrementuje jako desetinný
řetězec bez přetečení `int`/`unsigned long long`; přípona se připojuje
k nativní cestě a nepřevádí český název přes systémové ANSI kódování.

## Testy

Modelový a hostitelský test ověřují všech pět přípon, české názvy, číselné
pořadí 001/2/10, třiceticifernou verzi a skutečnou kopii při uložení.
Kontrolují velikost souborů, seskupení, zachování nejnovějších, chybné typy,
chybějící cesty, maximální nezáporný počet, aktivní editaci, no-op a neměnnost
otevřeného modelu. Samostatné případy ověřují změněný snímek, dvojí cestu,
odmítnutí aktuálního dokumentu a částečné selhání mazání na Windows.

Procesní regrese spouští skutečný CLI program se vstupem JSONL a ověřuje
vzniklé soubory i návratové kódy. GUI regrese používá všechny čtyři akce
menu, odpovědi Ano/Ne, nativní zálohy vzniklé uložením a následný příkaz
konzole, který musí obnovit stav menu.

Obnova dostupnosti položek menu čte pouze názvy záloh. Velikosti a časy
se načítají až pro výpis nebo snímek konkrétní mazací operace.


Závěrečné ověření Windows Release: obě aplikace a všechny testovací cíle
jsou sestavené. Úplný běh ověřil **150/151 za 591,33 s**
(`build/archive-full-tests.log`); nový GUI přípravek chybně předával
nepodporovaný argument `path` příkazu `save`. Přípravek nyní nastaví pracovní
adresář, vytvoří dokument a používá běžné `save`.

Po opravě přípravku a dokončení viditelného hlášení částečné chyby konzole
prošla závěrečná sada **7/7 za 164,31 s**: archivní operace, nativní dokumenty,
ukládání a historie dokumentů, skutečný proces CLI, katalog, překlady a GUI
konzole. Ověřené sestavení a výsledky:
`build/archive-final-build.log` a `build/archive-final-tests.log`.
Test skutečně zamčeného souboru Windows ověřuje chybu sdílení, přesné
odstraněné cesty v JSON a počet odstraněných záloh v textovém hlášení.
