# Zrcadlo a Pole v příkazové vrstvě

První etapa zpřístupňuje `derived_copy.sources`, `mirror.get` a `pattern.get`.
GUI Vlastnosti používají stejnou funkci pro dostupné zdroje a pro načtení
uložených parametrů. Samotná příkazová tvorba a změny následují; tento krok
je neoznačuje za dokončené. Katalog má **201 příkazů**.

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
