# Přejmenování nativních souborů

## Příkaz a společné chování (2026-09-14)

`rename_file name [document]` přejmenuje uložený soubor otevřeného Partu,
Assembly nebo Drawing. `document` je stabilní ID; výchozí je aktivní dokument.
`name` je nový název souboru ve stejném adresáři. Chybějící přípona se doplní,
změna nativního typu se odmítne. Příkazy zůstávají anglické, zprávy jsou
přeložené do cs/en/de/fr/ru.

```json
{"command":"rename_file","arguments":{"name":"nový název.prtz","document":"existing-document-id"}}
```

GUI Přejmenovat používá stejnou `FileRenameJob` jako CLI a nadále jedno
vnitřní okno PropertiesSubWindow s OK/Zrušit a společným potvrzením prostředním
tlačítkem. Neplatný název se zobrazí v okně; Zrušit nic nezmění.
Přejmenování ani mazání aktuálního souboru nesmí přerušit jiné otevřené
editační okno, včetně vlastností materiálu, ani aktivní skicování či výběr.

Úspěšný výsledek obsahuje `document`, `from`, `path`, `changed`,
`updated_paths` a `recovery_paths`. Při chybě může obsahovat `failed_path`.
Stejný název vrací úspěch s `changed=false`. Operace není modelovým Undo:
následné Undo rozměru nesmí vrátit neexistující starou cestu.

## Identity, neuložená práce a závislosti

`document::FileRelocation` obsahuje stávající ID a původní/novou absolutní
cestu. `FileRelocationEdits` připraví všechny nové řetězce a cesty předem.
`apply` provede výměnu bez další alokace a jednou aktualizuje runtime generace.

PartSession, AssemblySession a DrawingState zahrnou aktuální stav i celou
historii Undo/Redo do společné krátkodobé dávky. Zachovají modelové revize,
dirty stav, rozměrové identifikátory, ID výskytů, reference a vypočtené těleso.
Platné sdílené zdrojové snímky Partu se kvůli přejmenování nevytvářejí znovu.
Dávka vzniká až po pracovní I/O části na vlákně vlastnícím Workspace.

- Part mění vlastní název. Externí skicové reference nesou ID dokumentů
  a výskytů. Importní původ STEP/DXF se nepřepisuje jako nativní závislost.
- Assembly mění zdrojové cesty bezprostředních komponent. Zachovává jejich
  aliasy, výskyty, umístění, vazby a sdílená tělesa.
- Drawing mění zdroj dokumentu, zdroje pohledů, zdroje kusovníku,
  zdrojový název a `file_stem`. Ruční obsah a jiné modely zůstávají stejné.

Reference se určují ID. Rozpor cesty a ID se odmítne; chybějící ID se
nedohaduje z názvu. Relativní cesta patří k vlastnímu nativnímu dokumentu.

Soukromé `PreparedNativeDocument` přepisují pouze uložené snímky.
Přejmenování neukládá rozpracované parametry či geometrii otevřených dokumentů.
Jejich metadata aktualizuje až po úspěchu celé souborové dávky.

Výkres stejného základu názvu se automaticky přejmenuje pouze tehdy, pokud
skutečně patří přejmenovávanému Partu nebo Assembly. U otevřeného výkresu
rozhoduje jeho aktuální vlastník v paměti; samotná shoda názvu nestačí.

Závislosti zahrnují aktuální soubory `.asmz` a `.drwz` v adresáři zdroje,
v pracovním adresáři včetně podadresářů a otevřené uložené Assembly/Drawing
mimo ně. Operace nehledá na celém disku; neznámé soubory mimo tento rozsah
nelze automaticky přepsat. Číslované archivy se nepřejmenovávají.
Symbolické souborové odkazy se do prohledávání nezařazují.

## Příprava, zveřejnění a chyby

`prepare_document_file_rename` zachytí identity otevření, revize, generace,
cesty a rozměrové alokace. `stage` může běžet na pracovním vlákně bez
ukazatelů na živé dokumenty. Přečte uložené soubory, prověří identity,
připraví potřebné přepsané nativní soubory a ověří jejich nové načtení.
Originály zůstávají nedotčené.

`commit` znovu kontroluje živé dokumenty, velikosti/časy vstupních souborů,
množinu nalezených závislostí a dostupnost cílů. Nečitelný současný nativní
dokument se nesmí tiše přeskočit. Konflikt identit přejmenovávaných dokumentů
nebo konflikt až ve starším Undo stavu odmítne dávku před první změnou originálu.

Před zveřejněním se originály přesunou do vlastních dočasných záloh.
Pak se připravené soubory přemístí na cílové cesty. Teprve při úplném úspěchu
se vymění živá metadata, zachová kamera a aktualizují názvy tabů.
GUI ukládání i titulky tabů používají UTF-8 také na Windows.

Při chybě se již zveřejněné soubory a originály vracejí zpět. Pokud se
vše podaří obnovit, výsledek nehlásí změnu. Když selže i obnova, operace
vrátí `file_rename_recovery_required`, přesné zbývající cesty a adresáře
s daty pro obnovu. Poslední zachované kopie se v takovém případě nemažou.

Běžné chyby mají stabilní kódy: `invalid_filename`,
`document_type_mismatch`, `document_not_found`, `path_required`,
`destination_exists`, `destination_open`, `stale_document`, `stale_file`,
`duplicate_document_identity` a `file_io_error`. Další validační chyby
se vracejí jako `rename_rejected`. Otevřené vlastnosti chrání
`editing_in_progress` ještě před I/O.

Dočasné adresáře `.zima-rename-<ID>` patří jediné transakci a úklid
ověřuje jejich rodiče i přesný název. Nejde o povinné úložiště pro otevření
dokumentu. Vícesouborové zveřejnění není atomická transakce operačního systému
proti pádu procesu či souběžnému zásahu cizí aplikace; při neúplné obnově
musí zůstat výslovně dostupná data pro ruční zotavení.
Na Windows je přejmenování pouze velikosti písmen odmítnuto jako obsazený cíl.

Operace nevolá OCCT, neřeší vazby a nemění schéma nativních souborů.
Start šablony zůstávají platné. Veškerá povinná data jsou stále v nativních
`.prtz`, `.asmz` a `.drwz`.

## Ověření

`zima_cpp_file_relocation_state_tests` ověřuje datovou dávku pro všechny
tři typy, relativní reference, skutečné vypočtené těleso, sdílení,
historické konflikty a zachování neuložených parametrů.

`zima_cpp_file_rename_command_tests` ověřuje fyzické přejmenování,
skutečné zavřené/vnořené závislosti i otevřené dokumenty mimo pracovní
adresář, výkres a kusovník, stejný název, Unicode, změnu vstupů během I/O,
identity a zachování cache/Undo/Redo. Na Windows zamyká originál,
pozdější závislost i připravený soubor a ověřuje obnovu původních bajtů.

Procesový CLI test používá skutečné `.prtz`, `.asmz` a `.drwz`.
GUI ověřuje chybný název, Zrušit, potvrzení, následné příkazové přejmenování,
skutečné zdrojové reference, modelovou historii, kameru, titulky tabů
a Uložit po přejmenování všech tří typů s českými znaky.

Závěrečné sestavení Windows Release prošlo pro obě aplikace a všechny
testovací cíle. Ověřeno je všech **13 dotčených testů**, postupně v několika
bězích; nejde o novou úplnou regresi všech 154 testů.

- První cílený běh: **11/13 za 161,47 s**
  (`build/native-file-rename-targeted-tests.log`). Prošly modelové, procesní
  CLI, katalogové, překladové a souborové testy včetně obnovy při selhání.
  Dvě chyby byly v GUI přípravcích: číselný argument místo výrazu a
  nedokončené Vlastnosti vložení komponenty.
- Po opravě vstupu prošel test GUI konzole za **116,32 s**
  (`build/native-file-rename-ui-tests.log`).
- Doplněná kontrola neplatné zavřené závislosti prošla v testu přejmenování
  za **1,63 s** (`build/native-file-rename-isolated-tests.log`).
- Konečný test pracovního okna prošel **1/1 za 105,84 s** bez zásahu člověka
  (`build/native-file-rename-unattended-tests.log`).

Starší společný adresář GUI přípravků obsahoval i nepodporované dokumenty
z jiných běhů. Test pracovního okna nyní používá vlastní podadresář.
Očekávané otázky při zavírání a mazání obslouží opakovaný časovač podle
viditelného okna. Nečekané potvrzení po 10 sekundách vypíše do logu,
odmítne a označí běh za neúspěšný. Úklid pomocných oken výslovně zahazuje
pouze jejich testovací dokumenty. Běžné potvrzení uživatelského ukládání
se nemění. Běhy, které vyžadovaly ruční kliknutí, nejsou úspěšným ověřením.

V jednom meziběhu selhala dřívější GUI kontrola úhlové kóty při výběru dvou
úseček. Konečný kompletní test pracovního okna touto kontrolou prošel;
případná opakovaná nestabilita tohoto scénáře tím není samostatně vyloučena.
Poslední sestavení: `build/native-file-rename-unattended-build.log`.
