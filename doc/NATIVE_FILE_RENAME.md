# Přejmenování nativních souborů

## Stav (2026-09-14)

Ověřená je datová příprava pro otevřené i zavřené dokumenty.
Souborová transakce, napojení menu a příkaz `rename_file` jsou další rozpracovaná
etapa; tento mezikrok ještě nový příkaz nepřidává.

## Problém původní cesty

Původní GUI přepisuje část otevřených odkazů před fyzickým přejmenováním.
Použití `Session::replace` přitom maže Undo/Redo a u Partu může odstranit
vypočtené hranice historie. Výkresové odkazy nejsou přepsané úplně, zejména
zdroj dokumentu a řádky kusovníku. Výkres stejného názvu se přesouvá bez ověření
jeho skutečného vlastníka a některé chyby zápisu závislostí jsou ignorované.

## Ověřená datová vrstva

`document::FileRelocation` obsahuje stávající ID dokumentu a původní/novou
absolutní cestu. Nepřiděluje jiné ID. `FileRelocationEdits` nejdříve připraví
všechny nové řetězce a cesty; `apply` teprve provede jejich výměnu bez další
alokace. Současně zveřejní změnu runtime generace. Opakované `apply` už nic
nezmění.

PartSession, AssemblySession a DrawingState připravují změny ve svém aktuálním
stavu i celé historii Undo/Redo. `prepare_native_file_rebase` umožňuje spojit
několik dokumentů do jedné dávky bez kopírování těles. `rebase_native_files`
poskytuje přímé provedení pro samostatný stav. Dávka je krátkodobá: sběr a
zveřejnění probíhají na vlákně vlastnícím Workspace a mezitím se nesmí přesunout
nebo změnit dotčené session. Souborová transakce ji připraví až po pracovní
části, těsně před zveřejněním souborů.

Zachované zůstávají modelové revize, dirty stav, body/undo/redo, rozměrové
identifikátory, ID výskytů, původní geometrické reference a vypočtená data.
Změní se jen názvy dokumentů a souborové odkazy. Přejmenování není modelová
operace Undo: pozdější Undo parametrů nesmí vrátit již neexistující starou cestu.

- Part mění vlastní název. Externí skicové reference nesou ID dokumentů a
  výskytů, nikoli další zdrojovou souborovou cestu. Importní původ STEP/DXF se
  nepřepisuje jako odkaz na přejmenovávaný nativní soubor.
- Assembly přepisuje zdrojové cesty svých bezprostředních komponent.
  Nemění jejich jména/aliasy, umístění, vazby ani sdílené těleso.
- Drawing přepisuje zdroj dokumentu, zdroje pohledů, zdroje řádků kusovníku,
  zdrojový název a `file_stem`. Jiné modely a ruční obsah zůstávají stejné.

Zdroje se vážou podle ID. Rozpor cesty a ID se odmítá; chybějící ID se
nedohaduje z názvu souboru. Relativní cesta se vyhodnocuje vzhledem k vlastnímu
nativnímu dokumentu, nikoli k aktuálnímu pracovnímu adresáři procesu.

Stejná pravidla používá `PreparedNativeDocument::rebase_native_files`.
Jeho `write` uloží pouze tento soukromý, načtený snímek s původními ID a
vypočtenou geometrií. Nepřebírá neuložené změny z Workspace. Test výslovně
rozlišuje uložený Part bez nové úpravy a otevřený Part se dvěma novějšími
historickými úpravami.

`is_drawing_for` ověřuje skutečnou deklarovanou vazbu výkresu na model.
Shodný název souboru sám o sobě nepotvrzuje vlastnictví.

Žádná z těchto operací nevolá OCCT, neřeší vazby a nemění schéma souborů.
Start šablony proto zůstávají platné. Nevzniká nové povinné vedlejší úložiště.

## Testy

`zima_cpp_file_relocation_state_tests` používá skutečně vypočtené těleso,
dvě instance Partu v sestavě, výkresový pohled a kusovník. Ověřuje:

- názvy s českými znaky, absolutní a relativní zdrojové cesty;
- zachování přesných ID a cache v Partu i sdílení geometrie Assembly/Drawing;
- aktuální, Undo a Redo stavy včetně návratu k čistému uloženému stavu;
- opakování bez změny a jeden přírůstek generace při zveřejnění celé dávky;
- rozpor nalezený až ve starším Undo stavu bez částečné změny;
- odloženou společnou dávku několika otevřených dokumentů;
- nativní uložení a nové načtení Partu, Assembly i Drawing;
- soukromé souborové snímky, vlastnictví výkresu a zachování živých úprav.

Závěrečná cílená regrese prošla **4/4 za 0,85 s**: nové přesměrování,
nativní dokumenty, transakce PartSession a společné dokumentové operace.
Logy: `build/file-relocation-batch-build.log` a
`build/file-relocation-batch-tests.log`. GUI napojení není součástí tohoto
mezikroku a bude ověřeno při dokončení souborové transakce.

## Následující souborová transakce

Připravovaný postup zachová neuložené změny a nepoužije `Session::replace`.
Nejprve načte uložené dokumenty a připraví jejich nové nativní soubory
v soukromých dočasných adresářích. Před zveřejněním znovu ověří identity,
revize, cesty, snímky souborů a množinu prohledaných závislostí.

Staré soubory se odloží pro možnost návratu při chybě. Otevřená metadata se
změní až po úspěšném zveřejnění celé dávky. Při neúspěšné obnově nesmí úklid
smazat poslední zachovanou kopii; výsledek musí uvést cesty pro obnovení.
Dočasná příprava se nikdy nestane součástí načítacího kontraktu aplikace.

Automatický související výkres se určí podle jeho skutečného vlastníka.
U otevřeného výkresu je rozhodující aktuální stav v paměti. Rozsah vyhledání
závislostí navazuje na dosavadní GUI: adresář zdroje, pracovní adresář včetně
podadresářů a otevřené Assembly/Drawing mimo něj. Nečitelná data a konflikty
se nesmějí tiše ignorovat. Archivní číslované verze se nepřejmenovávají.
