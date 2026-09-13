# Zrcadlo a Pole v příkazové vrstvě

Příkazová vrstva zpřístupňuje `derived_copy.sources`, `mirror.create/get/set`
a `pattern.create/get/set`. GUI Vlastnosti používají stejné zdroje, načtení
parametrů, přípravu a potvrzení jako CLI. Katalog má **205 příkazů**.
Následující popis tvorby a změn je součástí právě ověřované etapy.

```json
{"command":"derived_copy.sources","arguments":{}}
{"command":"derived_copy.sources","arguments":{"object":"ID-EDITOVANE-KOPIE"}}
{"command":"mirror.get","arguments":{"object":"ID-ZRCADLA"}}
{"command":"pattern.get","arguments":{"object":"ID-POLE","document":"ID-OTEVRENEHO-DOKUMENTU"}}
```

Všechny dotazy přijímají volitelné `document`. Čtou i jiný otevřený Part
nebo Assembly bez aktivace, ukládání, změny historie či přepočtu. Nekopírují
triangulaci ani B-Rep a nevolají OCCT, solver umístění nebo regeneraci kopií.
Při otevřených Vlastnostech vracejí uložené hodnoty dokumentu; rozpracované
hodnoty dialogu zůstávají pouze v dialogu.

## Dostupné zdroje

`derived_copy.sources` vrací `document`, `object`, `boundary`, `revision`
a `items`. Položka obsahuje `id`, `name`, `kind` (`body`, `boolean`,
`component`), `visible` a lokální `instance_path` pro komponentu sestavy.
Jméno není identita. Dvě vložení stejného zdrojového souboru jsou dva
nezávislé výskyty.

Bez `object` je v Partu hranice bezprostředně za aktivním Tělesem, případně
u kořenového kurzoru, pokud žádné Těleso aktivní není. Při editaci je před
zadanou kopií. Boolean spotřebuje svoje vstupní tělesa a nabídne vlastní
výsledek. Zrcadlo ani Pole zdroj nespotřebují. Pořadí je pořadím historie.

V Assembly se nabízejí jen vlastní bezprostřední komponenty před kopií;
bez `object` jde o všechny komponenty. Potlačené se vynechají, pouze skryté
zůstávají platnými zdroji. Vnitřní díl podsestavy se nestává komponentou
vlastněnou vyšší sestavou. `boundary` je pozice v uloženém kořenovém seznamu
včetně potlačených komponent; není to pořadí OCCT objektu.

Výpis popisuje dostupnost podle historie a vlastnictví. Sám nevytváří
chybějící výpočtová data a neslibuje, že prázdné těleso má geometrii.
GUI k náhledu použije už existující vypočítaný výsledek. Odstraněna je
nepoužívaná náhrada starého Partu bez historie Těles jedním fiktivním Tělesem.

## Uložené vlastnosti

`mirror.get` a `pattern.get` vracejí `object`, `name`, `kind`, `source`,
`origin`, `visible`, `placement`, `reference`, `reference_valid`,
`value_locks`, `document` a `revision`. U komponenty jde o skutečné
`copy_placement`, nikoli běžnou polohu vloženého komponentu.

`placement` zachovává existující datový kontrakt umístění, jeho zámky a
reference. `reference` osy/roviny používá `owner`, `key`, `instance_path`,
a `offset_mm`. Identita zdroje
ani reference se neodvozuje z pořadí ploch, jména ve stromu nebo geometrické
podobnosti. Zrcadlo navíc vrací poslední uloženou `resolved_plane` s `point`
a `normal`. Neplatná reference zůstane neplatná i po pouhém dotazu.

Pole vrací objekt `pattern`: `mode` (`linear`/`circular`), uložené `count`,
`full_circle`, `angle_degrees`, poslední `resolved_origin` a `resolved_axis`
a všechny tři uložené řádky `linear`. Řádek obsahuje `axis` (`x/y/z`, nebo
`null` pro nepoužitý směr), `spacing_mm`, `count`, `reverse_count`,
`distribution` (`forward/reverse/both/symmetric`) a `resolved_direction`.
Uchovají se i právě neaktivní řádky a nastavení druhého režimu.

`instance_count` je počet kombinací vypočtený pouze z číselných parametrů;
zahrnuje původní zdroj. Při nevyhodnotitelném počtu je `null`. Tento údaj
není geometrická validace. Uložené `count` zůstává nastavením kruhového počtu i při lineárním režimu;
počet lineárních kombinací určuje `instance_count`. Například testované
lineární pole má 12 pozic, ale zachovává kruhové nastavení `count:4`. Při `full_circle:true` je skutečný kruhový krok 360° / `count`; uložené
`angle_degrees` zachovává nastavení pro vlastní krok. Výsledné těleso Pole
obsahuje pouze nové kopie, tedy
`instance_count − 1` kopií zdroje. Všechny délky a body jsou v mm, úhly
ve stupních; vektory jsou bezrozměrné.

Neexistující nebo cizí objekt vrátí `object_not_found`, jiný typ objektu
`wrong_feature`, nepodporovaný dokument `unsupported_document`. Zadaná
hranice musí být skutečné Zrcadlo nebo Pole v daném dokumentu. Dotaz nikdy
nedohledává nejednoznačný výskyt podle názvu.

Formát, přípony a start šablony se nemění. Potřebná data jsou již uložená
uvnitř `.prtz` nebo `.asmz`.

## Ověření

První modelový běh prošel **1/1** (0,49 s),
`build/derived-copy-query-model-build.log` a
`build/derived-copy-query-model-tests.log`. Nezávisle ověřuje objem zdroje
48 mm³, stejný objem zrcadla a 11 × 48 mm³ u pole s 12 pozicemi včetně
zdroje. Kontroluje oboustranný/symetrický směr, neaktivní řádek, zámky,
původní identity a nativní uložení.

Dále ověřuje hranice vytvoření/editace, Boolean zdroj a spotřebované
vstupy, chybné typy a cizí ID, dvě vložení stejného dílu, skrytý versus
potlačený výskyt a explicitně neaktualizovanou geometrii sestavy. Při
čtení se nemění revize, aktivace, graf historie ani adresy vypočítaných
výsledků/sdílených snapshotů. Testy skutečného CLI a GUI Vlastností jsou
součástí navazujícího sestavení a ověření.

První související sada měla **8/9** úspěšných testů (80,94 s),
`build/derived-copy-query-related-tests.log`. Nová GUI kontrola odhalila,
že editace ze stromu předává pouze ID; parametr druhu platí jen při
vytváření. Přidaná kontrola druhu byla z GUI odstraněna, takže při editaci
opět rozhoduje uložený objekt. Příkazové `mirror.get` a `pattern.get`
nadále explicitně kontrolují požadovaný druh. Dokumentace a modelový test
navíc rozlišují uložený kruhový počet a celkový počet lineárních kombinací.

Po opravě jsou oba programy sestavené a celá dotčená sada prošla **9/9**
(77,04 s), `build/derived-copy-query-final-build.log`,
`build/derived-copy-query-final-tests.log`. GUI test je nyní trvale
zaregistrovaný v CTest; ověřuje Zrcadlo a Pole v Partu i Assembly,
znovuotevření stejných Vlastností, rollback/Cancel, směry mřížky, zdrojový
výběr a potvrzení prostředním tlačítkem. Konzole navíc čte uložené hodnoty
během rozpracované editace bez výměny rollback geometrie.


## Vytváření a změny

```json
{"command":"mirror.create","arguments":{"source":"ID-ZDROJE","local_plane":"yz","placement":{"x":-2}}}
{"command":"mirror.set","arguments":{"object":"ID-ZRCADLA","reference":{"owner":"ID-PUVODNIHO-OBJEKTU","key":"KLIC-PLOCHY","instance_path":"","offset_mm":1}}}
{"command":"pattern.create","arguments":{"source":"ID-ZDROJE","linear":[{"axis":"x","spacing_mm":30,"count":3,"distribution":"symmetric"},{"axis":"y","spacing_mm":20,"count":2,"reverse_count":2,"distribution":"both"}]}}
{"command":"pattern.set","arguments":{"object":"ID-POLE","mode":"circular","count":6,"full_circle":true,"local_axis":"z"}}
```

Tvorba vyžaduje `source`, editace `object` a alespoň jeden měněný parametr.
Obě podporují `name`, `source`, `placement` a vlastní parametry uvedené níže.
Změna smí cílit jen aktivní Part/Assembly s ukončeným dialogem a skicářem.
Vnořenou aktivaci dosud omezuje společný guard příkazové vrstvy; její
rozšíření zůstává v celkovém seznamu CLI. Nový příkaz si nevymýšlí zdroj
podle názvu ani podle aktuálního hoveru.

Zrcadlo potřebuje `local_plane` (`xy/xz/yz`) vlastního počátku nebo přesnou
`reference`; nesmí být zadány současně. Výslovné `offset_mm` reference
posouvá její rovinu po normále. Zdroj se odráží ve svých skutečných
souřadnicích dokumentu, rovina patří umístění Zrcadla.

Pole je při vytvoření lineární. `linear` je úplný seznam jednoho až tří
směrových řádků; každý vyžaduje `axis` (`x/y/z` nebo `null`). Ostatní pole
řádku jsou volitelná a zachovají dosavadní hodnoty: `spacing_mm`, `count`,
`reverse_count`, `distribution`. Vynechané řádky se deaktivují, ale jejich
číselná nastavení zůstanou uložená. Osy se nesmějí opakovat. Tímto polem
se nezadávají libovolné vektory: používají se osy vlastního počátku Pole.

Rozteč je 0,001 až 1 000 000 mm, počet ve směru 2 až 1000. `forward`,
`reverse` a `symmetric` počítají celkem včetně zdroje; symetrický počet
musí být lichý. `both` má `count` vpřed včetně zdroje a `reverse_count`
(1 až 999) dalších pozic vzad. Celkový součin nesmí přesáhnout 1000 pozic.

Kruhový režim se volí `mode:"circular"`. Používá `count` (2 až 1000),
`full_circle` a `angle_degrees` (−359,999 až 359,999°); vlastní krok musí
rozlišovat výskyty v jedné otáčce. `local_axis` vybírá vlastní `x/y/z`,
alternativní `reference` původní osu či přímou hranu. Nulové odsazení osy
je přípustné; nenulové odsazení reference je vyhrazené rovině Zrcadla.
Výchozí kruhová reference je vlastní osa Z.

Kruhová pole argumentů se zadávají pouze při kruhovém režimu, `linear`
pouze při lineárním. `mode` lze změnit ve stejném příkazu. Přepnutí druhu
Pole zachová neaktivní směry i kruhové nastavení. Druh Zrcadlo ↔ Pole
se touto editací nemění.

`placement` je číselný patch stejného umístění jako ve Vlastnostech:
`x/y/z`, `rotation_x/y/z` a `reference_offset:N`. Jednotky jsou mm/stupně.
Existující pravidla zamčených a referencí řízených polí se nemění.
Nové generické přidávání umísťovacích referencí patří do samostatné etapy.

## Potvrzení, zámky a chyby

Společný `prepare_derived_copy_edit` uchová revizi, hranici historie,
původní parametry a původní viewer reference. Nevolá OCCT. OK/příkaz
použije `commit_derived_copy`, současný solver umístění a současný výpočet
odvozených těles/komponent. Nová geometrie se počítá výslovně při potvrzení.
Cancel nic z pending hodnot neuloží. Změněný dokument odmítne starou
přípravu přes `document_changed`.

Změna má jeden krok Undo; přesně stejné nastavení je no-op bez nové
revize a výpočtu. Zdroj musí být dostupný před operací. Vlastní, chybějící,
cizí a pozdější zdroje se odmítají před změnou dokumentu. Chyba nového
výsledku nebo jeho zdroje v Partu zabrání commitu. Samostatná chyba
navazujícího prvku může stejně jako v historii zůstat v dokumentu;
příkaz pak výslovně vrátí `calculation_errors` a `changed:true`.

Zámky kopie nyní používají `value_lock.list/set`: její vlastní umístění
v `placement:*` a u Pole `pattern:angle`, `pattern:spacing:0/1/2`.
Komponenta používá `copy_placement`, nikoli běžné umístění komponenty.
Zámek úhlu brání také nepřímé změně úhlu přes počet plného kruhu. Zámek
rozteče patří konkrétnímu řádku Vlastností, včetně dočasně neaktivního řádku.
Odemykání uvnitř dialogu se potvrdí společně s hodnotami; konzole změnu
během pending Vlastností odmítne.

Editace komponentové kopie zachovává její vlastní viditelnost a barevné/
vzhledové přepisy. Geometrie, materiál a zdrojové vlastnosti nadále přicházejí
ze zdroje podle stávajícího výpočtu. Při vytvoření se zachová dosavadní
převzetí vlastností ze zdrojové komponenty. Zdrojový Part se tím neupravuje.

## Průběžné ověření tvorby a editace

Po přesunu potvrzení prošly existující dotazové a skutečné GUI testy
**2/2** (8,01 s), `build/derived-copy-shared-edit-build.log` a
`build/derived-copy-shared-edit-tests.log`.

Nový modelový test prošel **1/1** (0,81 s),
`build/derived-copy-command-model-build.log`,
`build/derived-copy-command-model-tests.log`. Ověřuje nezávislé objemy
48 mm³ u zrcadla, 528 mm³ u pole 12 pozic, 912 mm³ u 20 pozic a 2000 mm³
u sestavového pole tří pozic ze zdroje 1000 mm³. Porovnává ručně spočtené
meze zrcadlení a směrové mřížky, původní rovinu, identity kopií při změně
počtu, neaktivní režimy, zámky a nepřímý úhel, atomické chyby a překročení
počtů, zdroj za hranicí, starou přípravu, Undo/Redo, studený nativní výpočet
a zachování vlastního vzhledu komponenty.

První úplný běh rozšířené sady prošel **105/105** (496,11 s),
`build/derived-copy-command-all-build.log` a
`build/derived-copy-command-full-tests.log`. Následuje dodatečné ověření
aktuálního neuloženého zdroje v čistém CLI bez GUI obnovy scény.

Doplňující scénář neuloženého Partu odhalil stale zdroj čistého CLI:
po zvětšení zdroje z 1000 na 2000 mm³ měla nově potvrzená kopie stále
1000 mm³ (`build/derived-copy-unsaved-repro-tests.log`, 0/1). GUI tuto
obnovu provádí v `refresh_scene`; příprava kopie Assembly proto nyní
přebírá aktuální zdroje existujícím `Workspace::refresh_source_geometry`.
Ten sdílí vypočítaná data a neřeší vazby ani nepočítá staré kopie/řezy.
Datové dotazy tuto přípravu nevolají. Zdrojový Part se neukládá ani
nemění a obnova zdroje nevytváří samostatný krok Undo.


Po opravě aktuálního zdroje prošel modelový test **1/1** (0,75 s),
`build/derived-copy-unsaved-fix-build.log` a
`build/derived-copy-unsaved-fix-tests.log`. Závěrečný test přidal návrat
kopie na starý výsledek pomocí Undo při zachování současných sdílených
zdrojových dat, následné Redo, změnu zdroje ve skutečném CLI procesu
a zamčenou rozteč v GUI Vlastnostech. Oba programy jsou sestavené;
všech **14/14** dotčených testů prošlo (85,98 s),
`build/derived-copy-final-build.log` a `build/derived-copy-final-tests.log`.
Úplná sada 105 testů výše předcházela poslední opravě sdílení zdroje.
Formát dokumentů ani šablony se touto etapou nemění.
