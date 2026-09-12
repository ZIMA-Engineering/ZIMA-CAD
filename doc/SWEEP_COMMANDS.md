# Příkazy tažení

Etapa vlastností přidává `sweep2d.get/set`, `sweep3d.get/set` a
`helical.get/set`. GUI potvrzení nového i existujícího tažení a příkazová
změna sdílejí `workspace::commit_sweep`: validace, explicitní výpočet a jeden
záznam Undo. CLI tvorba tažení, zadání stanic a správa vložené dráhy ještě
zůstávají další etapou; tyto příkazy upravují existující vypočitatelný prvek.

## Použití

```json
{"command":"sweep2d.get","arguments":{"container":"<ID>"}}
{"command":"sweep3d.set","arguments":{"container":"<ID>","result_type":"thin","thin_mode":"symmetric","thickness_mm":0.5}}
{"command":"helical.set","arguments":{"container":"<ID>","pitch_mm":10,"left_handed":true}}
```

Každý `get` vrací identitu dokumentu, kontejneru, prvku a vlastnícího tělesa,
jméno, operaci `add/subtract`, zámky, platnost referencí a revizi. Dotaz čte
uložený model, nevolá OCCT a nemění skici ani cache. Volitelný `document`
umožňuje číst jiný již otevřený díl. Dotaz při otevřených Vlastnostech čte
potvrzený model, nikoli rozpracovaný návrh.

2D a 3D tažení navíc vracejí `result_type` (`solid/thin`), `thin_mode`
(`one_side/other_side/symmetric`), `thickness_mm` a profily s jejich identitou,
ID stanice, příznakem příchozí větve, ID vlastněné skici a počátkem korespondence.
2D vrací `path_sketch` a případnou referenci `path_plane`; 3D vrací ID své dráhy.
Šroubovicové tažení vrací `pitch_mm`, `left_handed`, `circle`, `start_point`,
`guide_start_point` a ID tří vlastněných skic v pořadí kružnice, radiální dráha,
průřez. Jejich geometrii zpřístupňují stávající příkazy skic.

## Změny a ochrany

`set` přijímá `container`, volitelně `document` jako pojistku cílového aktivního
dílu, `name`, `combine` a objekt `placement` se stávajícími číselnými parametry
umístění. 2D/3D přijímá tři parametry Thin výše; šroubovicové tažení přijímá
stoupání, smysl, vybranou kružnici a počáteční bod v základní skice.
Tloušťka smí být 0,001–1 000 000 mm, stoupání 0,0001–1 000 000 mm.
Neznámé volby, nečíselné hodnoty, chybná geometrie či neplatná reference se
odmítnou atomicky, včetně ostatních položek stejného požadavku.

Prvek musí patřit aktivnímu editovatelnému tělesu. Rozpracovaný GUI příkaz,
cizí dokument, odvozené těleso a zamčená hodnota změnu blokují. ID kontejneru,
prvku, počátku a vložených skic se zachovávají. Výpočet používá stávající
umístění a stejnou hranici rollbacku jako Vlastnosti. Nespouští regeneraci
sestav. Příkazy neobcházejí ochranu referencí ani zámků umístění.

## Nalezená chyba nativního načítání

Načítání šroubovicového tažení přerámovalo jeho skici před načtením umístění
kontejneru. U posunutého/natočeného prvku se tak uložení tělesa a načtené rámce
skic rozcházely. Přerámování nyní následuje až po načtení umístění, stejně jako
u 3D tažení. Jde o pořadí čtení současného formátu; struktura souboru ani start
šablony se nemění.

## Ověření

Modelové testy porovnávají objemy s nezávislými vzorci pro přímé plné a tenké
tažení i kruhový průřez šroubovice. Kontrolují zámky, vlastnictví, odmítnuté
transakce, Undo/Redo, posun a rotaci, úplnou shodu historie po uložení a studený
výpočet po načtení. Samostatný proces CLI provádí všech šest příkazů bez Widgets.
GUI regrese porovnává Vlastnosti s konzolí a ověřuje Zrušit, OK, Undo i uložený
objem.

- Cílené modelové, procesové a GUI regrese: **9/9**, 120,57 s,
  `build/sweep-command-gui-tests.log`.
- Úplná Windows Release sada: **93/93**, 432,54 s,
  `build/sweep-command-full-tests.log`.
- Oba programy a všechny testy sestaveny:
  `build/sweep-command-gui-build.log`.

Katalog obsahuje 170 příkazů. Celkové CLI ještě není dokončené; otevřené oblasti
zůstávají v [CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).


## Vložená 3D dráha a stanice

`sweep3d.set` přijímá objekt `path` s položkami `curve_type`
(`polyline/interpolating_spline`), `rounding_enabled` a úplným polem `points`.
Bod s `construction` zachová své nativní ID; bod bez něj získá nové ID. Položky
bodu mají stejná pravidla jako samostatná 3D křivka: `name`, `values`,
`radius_mm`, `tangent` a `tangent_enabled`. Souřadnice v `values` jsou lokální
vůči dráze. Umístění celého tažení nadále patří položce `placement` kontejneru;
nevkládá se další posun do samotné dráhy.

```json
{"command":"sweep3d.set","arguments":{"container":"<TAZENI>","path":{"curve_type":"polyline","rounding_enabled":true,"points":[{"construction":"<PRVNI_BOD>"},{"values":{"x":0,"y":0,"z":20},"radius_mm":5},{"construction":"<POSLEDNI_BOD>","values":{"x":10,"y":0,"z":20}}]}}}
```

Seznam je celý navržený stav: vynechaný bod se odstraní spolu se svými profily,
stejně jako ve Vlastnostech. Pokud tím vznikne nevypočitatelný prvek (například
zmizí nezbytný první profil), odmítne se celá transakce. Přeživší body a profily
nezmění identitu. Zámky a vazby existujících bodů platí i při výměně celého pole.

`construction.list/get` zahrnuje vloženou 3D dráhu i její body. Pole
`owning_feature` rozlišuje vlastnící tažení, `parent`, `body` a
`coordinate_owner` popisují přesnou hierarchii. Dráha má souřadný systém
`container`; její body `parent_construction`. Čtení zůstává bez výpočtu a bez
vytváření dalšího objektu v dokumentu. Samotné `construction.set` na vložené
objekty odkáže na příkaz vlastnícího tažení, aby změna dráhy vždy potvrzovala
celý platný prvek.

`sweep3d.get` navíc vrací `stations`: nativní ID bodu, příchozí/odchozí větev,
aktivitu, lokální polohu, tečnu a případný vlastní profil. `station_coordinate_owner`
je ID dráhy. `stations_valid` a případné `stations_error` umožňují přečíst i
prvek s poškozenou dráhou; dotaz ho sám neopravuje ani neregeneruje.

Modelové regrese této navazující části kontrolují objem po prodloužení,
interpolovanou přímou spline, kruhové zaoblení s nezávislou délkou oblouku,
identitu vloženého bodu, vlastní rovinnou referenci v současně posunutém a
natočeném tažení i tělese, dotaz na poškozenou dráhu a atomické odmítnutí
duplicit či cizích bodů.

Navazující cílená sada prošla **7/7** (61,86 s),
`build/sweep-path-gui-tests.log`: jádro 3D tažení, překlady, skutečný CLI proces,
konstrukční a Sweep příkazy, katalog a konzole s vnořenými Vlastnostmi bodu.
Doplněná zkouška souřadnic tělesa a dotazu na neplatnou dráhu prošla **1/1**
(4,99 s), `build/sweep-path-frame-tests.log`. Oba programy a testy jsou sestavené,
`build/sweep-path-gui-build.log`. Katalog nadále obsahuje 170 příkazů; nativní
formát a start šablony se nemění.
