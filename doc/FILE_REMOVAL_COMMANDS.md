# Odstranění aktuálního nativního souboru

## Rozsah

`delete_file` odstraní uložený soubor otevřeného Partu, Assembly nebo Drawing.
Cílem je přesné ID dokumentu a jeho uložená cesta. Bez `document` použije
aktivní dokument. Nejde o obecné mazání libovolné cesty nebo o odstranění
komponenty ze sestavy. Nativní přípony a formát dat se nemění.

| Argument | Typ | Výchozí hodnota |
| --- | --- | --- |
| `document` | řetězec, ID otevřeného dokumentu | aktivní dokument |
| `archives` | boolean | `false` |
| `discard` | boolean | `false` |

`archives: true` zahrne všechny číslované archivy daného souboru, které jsou
v potvrzovaném snímku. Pravidla názvů jsou společná se
[správou archivů](ARCHIVE_COMMANDS.md). Jiný dokument ani podadresáře
do této operace nepatří.

Neuložené změny vyžadují `discard: true`. Příkaz neotevírá dialog,
neukládá rozpracovaný model a nespouští výpočet OCCT. Během otevřených
vlastností nebo editace je zablokovaný společným hostitelem.

```json
{"command":"delete_file","arguments":{"document":"document-id","archives":true,"discard":true}}
```

## Společná operace a potvrzení GUI

`workspace/file_removal_operations` obsahuje přípravu a provedení.
Příprava pouze čte: ID otevření, cestu, revizi, generaci vypočtených dat,
počet přidělených rozměrových identifikátorů, velikost a čas souboru.
Při zahrnutí archivů pořídí i jejich seznam s velikostmi a časy.

GUI akce „Odstranit aktuální soubor“ a „Aktuální soubor a všechny verze“
používají stejnou operaci. Mají jedno potvrzení Ano/Ne, výchozí Ne.
U neuložených změn otázka výslovně uvádí i jejich zahození. Ne nic nemění.
Společná ochrana souborových akcí odmítne mazání také během otevřených
vlastností materiálu nebo jiného editačního okna, nejen kontejneru.

Po potvrzení se na vlákně vlastnícím Workspace znovu ověří dokument a
soubory. Změněný dokument, jiné otevření stejného ID, jiná cesta nebo
změněný snímek souborů se odmítnou před prvním smazáním.

Pořadí provedení:

1. Smazat aktuální soubor. Pokud selže, dokument zůstane otevřený a jeho
   archivy se nemažou.
2. Zavřít přesně tento dokument bez následné otázky Uložit. Původní GUI
   cesta mohla po smazání nabídnout uložení a právě smazaný soubor znovu
   vytvořit, protože neexistující soubor znamená potřebu uložení.
3. Smazat zvolené archivy. Selhání OS v tomto kroku je částečný výsledek,
   nikoli obnovení už smazaného souboru.

Zavření používá společný výběr zbývajícího aktivního a zobrazeného dokumentu.
Při mazání aktivovaného zdrojového Partu v sestavě zůstane rodičovská sestava,
její komponenta, identita zdroje a vypočtený snímek zachovaný. Zdrojová cesta
pak samozřejmě odkazuje na odstraněný soubor. Operace neodstraňuje výskyt.

## Výsledek a chyby

Výsledek obsahuje `document`, `path`, `closed`, `changed`,
`removed_paths` a `removed_bytes`. Při selhání během provádění obsahuje
také `failed_path` a přesné již provedené účinky. Viditelné chybové hlášení
uvádí selhanou cestu a počet odstraněných souborů. I při částečném selhání
konzole aktualizuje GUI podle skutečného zavření dokumentu.

Mezi odmítnutí patří `no_document`, `document_not_found`,
`path_required`, `file_not_found`, `unsaved_changes`, `stale_document`,
`stale_file` a `stale_archive`. Chyby fyzického mazání vracejí
`file_io_error` nebo `archive_io_error`. Šablony nejsou cílem `delete_file`.

Smazání souborů nemá modelové Undo a nepřesouvá soubory do koše.
Kontrola velikosti a času je kontrola snímku, nikoli kryptografické ověření
obsahu. Další proces může soubor změnit i mezi kontrolou a systémovým
voláním; taková souběžnost není transakce napříč procesy.

## Ověření

Modelové testy pokrývají všechny tři nativní formáty, volitelné archivy,
zahození změn, zachování historie při odmítnutí, zastaralé snímky dokumentu
a souborů, nové otevření stejného ID a kontext aktivované komponenty.
Windows testy zamykají skutečný soubor bez sdílení mazání a odlišují
selhání aktuálního souboru od částečného selhání archivu.

Procesní regrese spouští samostatné CLI pro všechny tři formáty, kontroluje
skutečné soubory, JSON, návratový kód a požadavek výslovného `discard`.
GUI regrese používá obě menu, Ano/Ne, čistý i neuložený model a otevřené
vlastnosti. Kontroluje jediné potvrzení a nepřítomnost následného Uložit.

Obě aplikace a všechny testovací cíle jsou sestavené ve Windows Release.
Závěrečná cílená sada prošla **7/7 za 150,02 s**: nové mazání souborů,
archivy, dokumentové ukládání a historie, skutečný proces CLI, katalog
příkazů, překlady a GUI konzole. Logy: `build/file-removal-final-build.log`
a `build/file-removal-targeted-tests.log`.
