# Příkazy tažení

Etapa vlastností přidává `sweep2d.get/set`, `sweep3d.get/set` a
`helical.get/set`. GUI potvrzení nového i existujícího tažení a příkazová
změna sdílejí `workspace::commit_sweep`: validace, explicitní výpočet a jeden
záznam Undo. `sweep2d.create` a `sweep3d.create` vytvářejí tažení z nativních vstupů.
Tvorba šroubovicového tažení zůstává navazujícím krokem. `sweep2d.set`
a `sweep3d.set` už spravují celý seznam profilů a jejich stanice.

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

## Celý seznam profilů 2D a 3D tažení

`profiles` v `sweep2d.set` a `sweep3d.set` je úplný nový seznam profilů.
Vynechaný existující profil se odstraní. Záznam s `profile` zachovává jeho
skicu i identitu; lze mu změnit `point`, `incoming` a `start_point`. Záznam
se `sketch` a `point` přebírá samostatnou skicu podle stejných pravidel jako
nové 3D tažení. Seznam musí mít 1–5000 položek; platná první stanice musí
mít použitelný profil. Není možné uložit prvek bez vypočitatelného začátku.

```json
{"command":"sweep2d.set","arguments":{"container":"<TAZENI>","profiles":[{"profile":"<PRVNI_PROFIL>"},{"sketch":"<SAMOSTATNA_SKICA>","point":"<KONCOVY_BOD>","incoming":true}]}}
{"command":"sweep3d.set","arguments":{"container":"<TAZENI>","profiles":[{"profile":"<PRVNI_PROFIL>"},{"profile":"<DALSI_PROFIL>","point":"<JINY_BOD>","incoming":false,"start_point":"<BOD_OBVODU>"}]}}
```

ID bodu a příznak `incoming` se berou z položek `stations` v `get`. Také
2D tažení nyní vrací tyto stanice, jejich polohu, tečnu a vlastní profil.
Souřadnice 2D stanic patří tělesu (`station_coordinate_owner`); 3D stanice
jsou lokální vůči vložené dráze. Jde o čistý dotaz nad ZIMA geometrií bez
OCCT, změny rámce či cache. Neplatná dráha vrátí `stations_valid:false`
a diagnostiku, aniž by skryla uložené parametry.

`start_point` určuje skutečný nativní bod párování obvodu; prázdný řetězec
vrací automatickou volbu. U kružnice se používají její body C, tedy body
skici se skutečnou vazbou `point_on_circle`. Nevytvářejí se umělá pořadová ID.
Samotné křivky vlastněného profilu zůstávají dostupné příkazy skicáře.

Nová samostatná skica může ležet před tažením i za ním, ale nesmí záviset
na tomto tažení nebo na pozdější historii. Převzetí nesmí odstranit vstup
používaný jiným objektem. Po převzetí se hranice validace hledá znovu podle
ID tažení, protože odstranění staršího kontejneru skici mění index historie.
Geometrie, odstranění vstupní skici a seznam profilů se potvrzují jedním
záznamem Undo; chyba nic z toho částečně neuloží.

Procesové a modelové regrese obou druhů tažení prošly **2/2** (17,19 s,
`build/sweep-profiles-process-tests.log`). Ověřují převzetí skici, nezávislý
objem komolého kužele, obnovení samostatných vstupů přes Undo, původní ID,
odmítnutí duplicit a neplatných stanic, odebrání profilu, skutečné C body,
uložení párování a ochranu před závislostí na vlastním tažení.

Závěrečné ověření správy profilů:

- **15/15** souvisejících modelových, CLI a GUI testů (128,34 s),
  `build/sweep-profiles-gui-tests.log`; sestavení obou programů
  `build/sweep-profiles-gui-build.log`. Přesun profilu R3 do poloviny 20mm
  dráhy zachová jeho ID a dává nezávisle ověřený objem 460π/3 mm³
  (komolý kužel na první polovině a válec R3 na druhé). GUI otevírá převzatou
  kružnici v původní skice a potvrzení nezdvojuje vstupy.
- Dodatečná regrese prokázala, že 3D profil mohl být přesunut na neexistující
  příchozí větev prvního bodu a při výpočtu se ignoroval
  (`build/sweep-profile-station-red-tests.log`). Nové nebo přesunuté vazby
  nyní vyžadují aktivní stanici, rovněž při `sweep3d.create`. Již uložené
  neaktivní profily zůstávají zachované při změně zaoblení dráhy.
- Po opravě prošlo **5/5** závěrečných příkazových, procesových, GUI
  a překladových testů (53,86 s), `build/sweep-profiles-final-tests.log`;
  oba programy a všechny testy jsou sestavené v
  `build/sweep-profiles-final-build.log`. Katalog zůstává na **171 příkazech**;
  jde o rozšíření existujících `get/set`. Formáty a šablony se nemění.

## Rovina dráhy 2D tažení

`sweep2d.set path_plane` přijímá původního `owner`, sémantický `key`, volitelný
prázdný `instance_path` a `offset_mm` v rozsahu ±1 000 000 mm. Prázdný objekt
`{}` odstraní explicitní referenci a obnoví místní rovinu dráhy podle běžných
Vlastností. U stejné reference vynechané odsazení zachová aktuální hodnotu;
nová reference má výchozí odsazení nula.

```json
{"command":"sweep2d.set","arguments":{"container":"<TAZENI>","path_plane":{"owner":"<POCATEK_TAZENI>","key":"origin:plane:xy","offset_mm":7}}}
{"command":"sweep2d.set","arguments":{"container":"<TAZENI>","path_plane":{"owner":"<PUVODNI_OBJEKT>","key":"<KLIC_ROVINNE_PLOCHY>"}}}
{"command":"sweep2d.set","arguments":{"container":"<TAZENI>","path_plane":{}}}
```

Volba smí mířit na jednu ze tří rovin vlastního počátku, dostupný hlavní
počátek/počátek tělesa nebo na dřívější původní rovinu či rovinnou plochu
tohoto Partu. Výsledné těleso, cizí výskyt, vlastní výsledná plocha a pozdější
historie nejsou přípustné zdroje. Platnost pořadí kontroluje společný commit
pro CLI i GUI. Typ roviny a její skutečnou geometrii určuje stávající nativní
řešení roviny dráhy; CLI nezavádí jiný picker ani procházení OCCT.

Konstrukční rovina se používá včetně svého vlastního odsazení. Další
`offset_mm` patří rovině dráhy. Referenci i odsazení lze změnit ve stejné
transakci jako umístění kontejneru; profily se přerámují do výsledné dráhy.
Změna zdroje se do tělesa promítne při explicitním výpočtu. Dotaz `get`
čte uloženou referenci a stanice, bez regenerace. Částečný chybný požadavek
neprovede ani ostatní současně zadané změny.

První modelový běh prošel **1/1** (6,47 s),
`build/sweep-path-plane-model-final-tests.log`: konstrukční rovina z=10 mm
s vlastním odsazením 3 mm a odsazením dráhy 2 mm dává z=15 mm. Po změně
zdroje a Regenerate dává z=18 mm. Vlastní rovina natočená o 30° a odsazená
o 4 mm byla ověřena také přímo na vykreslovací geometrii vypočteného tělesa.
Uložení zachovává původní ID, odsazení i rámy skic. Formát reference zůstává
stejný: původní vlastník, klíč, výskyt a odsazení; nevznikají nová persistentní
pole ani změny start šablon.

Rozšířená sada ověřila **14/15** případů (126,33 s,
`build/sweep-path-plane-gui-tests.log`), včetně GUI a samostatného CLI.
Poslední modelový scénář nejprve odhalil chyby přípravy testu: textové
rozměry příkazu `box.create`, záměnu osy analytické roviny za orientaci
konkrétní plochy a pokus měnit zavazbené Z tělesa přímo. Test nyní volí
skutečně nejvyšší vodorovnou plochu a těleso posouvá jeho existujícím
offsetem vazby XY. Následně prošel celý modelový test **1/1** (6,73 s),
`build/sweep-path-plane-body-tests.log`. Ověřuje původní plochu jiného
tělesa, kompenzaci jeho 10mm posunu v lokálních souřadnicích, odmítnutí
hrany místo roviny, zachovaný objem a přesnou nativní perzistenci.

Oba programy a celá sada testovacích programů jsou sestavené
(`build/sweep-path-plane-gui-build.log`); poslední změny byly pouze
v uvedeném testovacím scénáři (`build/sweep-path-plane-body-build.log`).
Katalog zůstává na **171 příkazech**.


## Vytvoření 2D tažení ze skic

`sweep2d.create` přebírá samostatnou skicu dráhy (`source_path`) a pole
`profiles` se stejným kontraktem jako 3D tažení. Bod stanice je původní ID
bodu skici, které lze získat přes `sketch.entities` / `sketch.entity.get`.
Stejně jako v GUI musí 2D dráha tvořit souvislou otevřenou křivku začínající
v lokálním počátku `(0, 0)`. První profil patří této stanici s `incoming: false`;
na konci úseku je větev `incoming: true`. Po vytvoření vrací `sweep2d.get`
všechny stanice s těmito příznaky.

```json
{"command":"sweep2d.create","arguments":{"source_path":"<SKICA_DRAHY>","profiles":[{"sketch":"<SKICA_PRUREZU>","point":"<BOD_DRAHY>"}],"name":"Tažení"}}
```

Volitelně přijímá `name`, `combine`, `placement`, `result_type`, `thin_mode`,
`thickness_mm`, `path_plane` a pojistku cílového `document`. Reference
`path_plane` má stejná pravidla jako při editaci; výchozí rovina zachovává
fyzickou rovinu a odsazení zdrojové skici. Bez explicitního přepsání se
přebírá umístění kontejneru zdrojové dráhy včetně jeho živých referencí.

Všechny vstupy musí být samostatné, aktivní a v právě upravovaném tělese
před kurzorem historie. Nesmí je potřebovat jiný prvek ani nesmějí záviset
na spotřebovaném kontejneru skici. Převzetí odstraní kořenové kontejnery
vstupů, zachová identitu skic i jejich lokální geometrie a vytvoří nové ID
vlastnícího tažení. Profily se orientují podle stanice stejně jako v GUI.
Chyba nezmění dokument; Undo obnoví původní vstupy včetně historie.

Referencovaná skica používá čtvrtotáčku kolem lokální osy Y. Běžný kontejner
používá Z. Při vytvoření tažení se tato lokální čtvrtotáčka přepočte do
korekce nového prvku; původní reference zůstanou živé. Společný solver
umístění se nemění. Odsazení roviny dráhy se zapíše do již existující
reference vlastní roviny počátku. Formát a start šablony se nemění.

První modelová kontrola prošla **1/1** (10,70 s),
`build/sweep2d-create-model-tests.log`: 72 kombinací XY/XZ/YZ, žádná/jedna/tři
rovinné reference, FRONT/BACK a čtvrtotáčky 0–3, korekce i natočené těleso.
Před převzetím a po něm se porovnávají skutečné počátky a osy dráhy;
nezávislý objem válce je `π × 2² × 20 = 80π mm³`. Ověřeno je přesné Undo,
atomické odmítnutí nesprávných, sdílených a neaktivních vstupů i nativní
uložení a nový výpočet. Navazující ověření doplňuje obloukovou dráhu,
živé odsazení, skutečný CLI proces a GUI Vlastnosti.


Závěrečné ověření této etapy:

- Oba programy a všechny testovací programy sestavené:
  `build/sweep2d-create-full-build.log`.
- Související regrese **14/15** (133,92 s),
  `build/sweep2d-create-gui-tests.log`. Úspěšně proběhl skutečný CLI proces,
  GUI vytvoření a následné Vlastnosti (OK/Cancel), ostatní druhy tažení,
  historie, umístění, překlady i nativní dokumenty.
- Nový obloukový test nejprve zadával koncový bod proti směru dráhy jako
  výchozí větev. Po opravě vstupu podle skutečné nativní stanice prošel
  úplný modelový test **1/1** (11,72 s),
  `build/sweep2d-create-final-tests.log`; produkční kód se mezi běhy neměnil.
- Modelový test ověřil i změnu živého referenčního odsazení po převzetí.
  Pro čtvrtkružnici R10 a průřez R2 je nezávislý objem `20π² mm³`;
  pro symetrický Thin 0,5 mm je `10π² mm³`. Kontrola zahrnuje původní
  oblouk, vlastnictví, uložení a studený výpočet.

Katalog obsahuje **172 příkazů**. Další krok je vytvoření šroubovicového tažení.
