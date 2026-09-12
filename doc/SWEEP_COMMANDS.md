# Příkazy tažení

Etapa vlastností přidává `sweep2d.get/set`, `sweep3d.get/set` a
`helical.get/set`. GUI potvrzení nového i existujícího tažení a příkazová
změna sdílejí `workspace::commit_sweep`: validace, explicitní výpočet a jeden
záznam Undo. Níže uvedený `sweep3d.create` už vytváří 3D tažení z nativních vstupů.
Tvorba 2D a šroubovicového tažení a úplná správa profilových stanic zůstávají
navazujícími kroky; příkazy `get/set` upravují existující vypočitatelný prvek.

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


## Tvorba 3D tažení z nativních vstupů

`sweep3d.create` převezme samostatnou 3D křivku (`source_path`) a jednu nebo
více samostatných skic (`profiles`). Profil uvádí `sketch`, nativní `point`
z dráhy, volitelně `incoming` a `start_point` korespondence. Vstupy se vytvoří
stávajícími příkazy konstrukcí a skic; není potřeba zapisovat interní serializaci.

```json
{"command":"sweep3d.create","arguments":{"source_path":"<KRIVKA_3D>","name":"Tažení","profiles":[{"sketch":"<SKICA>","point":"<PRVNI_BOD_DRAHY>"}],"result_type":"solid"}}
```

Tvorba přijímá stejné parametry operace, jména, Thin a umístění jako editace.
Nové tažení přebírá umístění dráhy. Profil používá své lokální 2D křivky a
vazby; jeho nové umístění určuje zvolená stanice. Jedno tažení vlastní dráhu
i skici a původní samostatné vstupní kontejnery se při potvrzení odstraní.
ID dráhy, bodů, skic a křivek zůstávají zachovaná. Novou identitu dostává
kontejner tažení, jeho prvek a vazba profilu na stanici. Undo vrací všechny
samostatné vstupy i jejich původní umístění, Redo obnoví stejné tažení.

Vstupy musejí patřit aktivnímu editovatelnému tělesu a ležet před jeho kurzorem.
Každá profilová skica se smí převzít jednou. Pokud na vstupním kontejneru
závisí jiný objekt nebo vstupní kontejnery závisejí jeden na druhém, převzetí
se odmítne. Stejně se odmítne reference profilu na odstraňovaný kontejner skici.
Reference na dřívější nepřebírané objekty zůstávají součástí skici. Kontroly,
výpočet a odstranění vstupů tvoří jednu atomickou transakci; chyba nezanechá
odstraněné skici ani částečně vložený prvek.

GUI potvrzení i tvorba používají společné nastavení vlastněné dráhy, které
ponechá transformaci na kontejneru a lokální body v dráze. Tím se původní posun
nebo rotace neaplikuje dvakrát. Formát a start šablony zůstávají stejné.

Modelová regrese ověřuje skutečný objem a prostorové meze natočeného
tažení, původní ID křivek, blokování závislého objektu, odmítnutí cizí stanice,
přesnou obnovu vstupů po Undo a jediné vlastnictví po uložení. Přechod mezi
kružnicemi R2 a R3 na délce 20 mm odpovídá nezávislému objemu komolého kužele
380π/3 mm³. Otevřená úsečka délky 4 mm se při neplatném Solid neodstraní;
následující Thin s tloušťkou 0,5 mm a délkou 20 mm dává 40 mm³. Kontroly
zahrnují potlačený vstup, neaktivní těleso a zachování reference bodu na vlastní
počátek dráhy.

Skutečný proces CLI vytváří, vrací a znovu ukládá převzaté tažení. GUI regrese
následně otevírá jeho Vlastnosti, mění vložený bod, rozlišuje Cancel/OK rodiče
a ověřuje Undo i uložený objem. Těchto 15 souvisejících testů prošlo (126,44 s,
`build/sweep-create-gui-tests.log`).

Nová regrese zároveň odhalila chybějící převod vlastníka reference z vloženého
bodu/skici na kontejner tažení: `history.can_move` nesprávně dovolil přesunout
závislý objekt před zdroj. Společný sběr závislostí nyní registruje vloženou
dráhu, její body/počátky a všechny vlastněné skici. Dotaz i přesun takové
pořadí odmítají bez změny dat či cache. Regrese nejprve prokázala chybu
(`build/sweep-owned-dependency-red-tests.log`); po opravě a doplnění výše
uvedených geometrických případů prošly oba modelové testy (5,53 s,
`build/sweep-create-profiles-tests.log`). Pravidlo mazání historie se nemění.

Závěrečné sestavení obou programů a všech testů prošlo
(`build/sweep-create-full-build.log`). Úplný běh měl **91/93** úspěšných testů
(478,27 s, `build/sweep-create-full-tests.log`): inspektor měření překročil
90s limit a GUI potvrzení sestavového profilu jednou selhalo. Se stejnými
binárními soubory prošlo samostatné opakování profilu (89,51 s) a následně
oba dotčené testy **2/2** (90,14 s, `build/sweep-create-ui-recheck-tests.log`).
Příčina nepravidelného GUI selhání není prokázána; nejde o tvrzení, že původní
úplný běh prošel celý. Geometrické, CLI a GUI testy nového tažení prošly.
