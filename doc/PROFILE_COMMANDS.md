# Vytažení a rotace přes konzoli a CLI

Příkazy `extrusion.create/get/set` a `revolution.create/get/set` používají
stejné potvrzení profilu jako OK ve Vlastnostech. V Partu tvorba převádí samostatnou
skicu na profilový prvek: zachová ID kontejneru, jeho počátek, umístění,
vlastníka v tělese a pozici historie. Nová operace získá nové ID prvku;
její skica zůstane v témže kontejneru. Nejde o kopii skici.

## Zadání

Nejprve vytvořte skicu pomocí `sketch.create` a doplňte uzavřený profil
skicovými příkazy; pro Thin může být profil i otevřený. Následující ID pocházejí z výsledků příkazů, nejsou to
názvy ve stromu ani pořadová čísla hran.

```json
{"command":"extrusion.create","arguments":{"sketch":"<sketch-ID>","length_forward_mm":20,"name":"Vytažení"}}
{"command":"extrusion.get","arguments":{"container":"<container-ID>"}}
{"command":"extrusion.set","arguments":{"container":"<container-ID>","extent":"two_sides","length_forward_mm":30,"length_reverse_mm":5}}
{"command":"revolution.create","arguments":{"sketch":"<sketch-ID>","axis":"<centerline-ID>","angle_degrees":90}}
{"command":"revolution.get","arguments":{"container":"<container-ID>"}}
{"command":"revolution.set","arguments":{"container":"<container-ID>","angle_degrees":180}}
```

Rotace používá zelenou konstrukční osu ve vlastní skice. Úsečku na ni změní
`sketch.segment.centerline`. Bez argumentu `axis` se použije jediná dostupná
osa; při více osách je nutné zadat její přesné ID. Chybné výslovně zadané ID
se nenahrazuje jinou osou.

Číselné argumenty jsou skutečná čísla JSON: délky v mm, úhly ve stupních,
nezávisle na zobrazovaných jednotkách dokumentu. `name`, `combine` (`add`,
`subtract`), `extent` (`one_side`, `two_sides`, `symmetric`), `direction`
(`forward`, `reverse`) a `profile_offset_mm` jsou společné pro oba prvky.
`placement` přijímá objekt číselných polí ze společného `placement.set`,
včetně existujících omezení a zámků.

Vytažení používá `length_forward_mm` a `length_reverse_mm`, rotace
`angle_degrees` a `angle_reverse_degrees`. Rozsah délky je 0,001 až
1 000 000 mm; úhel každé strany 0,001 až 360°. Skutečný součet rotace musí
splnit omezení modelového jádra. Symetrická varianta nastaví obě strany
na hodnotu vpřed; souběžné zadání rozdílné zadní hodnoty je chyba.

`end_forward` a `end_reverse` mají hodnoty `length`, `through_all` a `up_to`.
Průchozí zakončení je určeno pouze pro odečtení. Jednostranný průchozí řez
zasahuje od roviny profilu ve zvoleném směru; pro řez oběma směry nastavte
`two_sides` a obě zakončení na `through_all`.

`get` vrací identitu dokumentu, kontejneru, prvku, skici a tělesa, parametry,
zámky, platnost umístění a uložené cíle zakončení. Může číst i jiný již otevřený
Part přes `document`, rovněž během otevřených Vlastností. Neprovádí výpočet,
řešení referencí, změnu historie ani načítání závislostí.

## Tenkostěnný výsledek

`result_type` volí `solid` nebo `thin`. Thin používá `thin_thickness_mm`
a `thin_mode` (`one_side`, `other_side`, `symmetric`). Změna typu či strany
počítá skutečné těleso a vstupuje do výpočetního otisku; dřívější chyba,
kdy se přes Thin náhled ukládal plný objem, je odstraněna z modelového výpočtu.

```json
{"command":"extrusion.set","arguments":{"container":"<container-ID>","result_type":"thin","thin_thickness_mm":1,"thin_mode":"symmetric"}}
```

Uzavřená kontura tvoří stěnu mezi dvěma odsazenými obrysy. Otevřená kontura
se uzavře na svých dvou původních koncových bodech. Zakončení mají vlastní
identitu odvozenou od těchto bodů, boky od původních křivek. Vnější a vnitřní
strana uzavřeného profilu si zachovávají identitu při změně tloušťky i strany.
Náhled používá stejné pořadí profilu a počáteční bod jako výpočet, bez OCCT.

Kružnice zůstává analytická; u samostatné spline se její matematický offset
převede na jednu B-spline s kontrolovanou odchylkou nejvýše 1e-7 mm.
Nepřijatelný výsledek aproximace je
odmítnut. Hrana si uchová původní identitu a ve vypočteném dokumentu se uloží
její křivková data, nikoli jen zobrazovací lomená čára.

Thin používá kontury bez vnitřních otvorů; příliš silná stěna, zborcený obrys,
nesouvislá dráha nebo odsazení měnící podporovanou topologii vrací chybu.
Další omezení odsazování navazujících křivek jsou stejná jako u společného
Thin výpočtu pro tažení. Příkaz chybnou geometrii nevynechá ani nenahradí
plným výsledkem.

## Transakce a současné hranice

Mutace vyžaduje aktivní Part a aktivní vlastnící těleso. Během GUI editace je
odmítnuta, stejně jako editace odvozeného tělesa. Nepodařený výpočet ani
neplatné argumenty nesmějí změnit skicu, historii, revizi či vypočtená tělesa.
Úspěšná změna je jedna Undo/Redo transakce. OK i `.set` výslovně počítají profil
rovněž při stejných číselných hodnotách, protože se mohla změnit jeho skica.
Závislé sestavy se automaticky neregenerují.

- Řezy vlastněné Assembly zatím používají svou dosavadní samostatnou cestu.
  Vnořená aktivace a příkazové sestavové řezy jsou další část celkové CLI.

Nativní formát se touto etapou nemění, start šablony zůstávají platné.
Všechny potřebné údaje nadále žijí v `.prtz`, `.asmz` a `.drwz`.

## Ověření

Modelové testy porovnávají objemy s nezávislými výpočty: obdélníkové vytažení,
mezikruží vytvořené rotací, jednostranné a oboustranné průchozí odečtení.
Ověřují také skicu s nezměněnými parametry prvku, identitu kontejneru,
zámky, chyby, Undo/Redo a uložení vypočteného tělesa. Samostatné CLI procesy
pracují bez dostupného Qt platformního pluginu. GUI konzole vytváří oba prvky,
edituje je jejich skutečnými Vlastnostmi a porovnává uložený objem.

Úplná Windows Release sada prošla **90/90** (420,90 s),
`build/profile-full-tests.log`. Následná oprava bezpečného uchování první
reference před mazáním staré orientační položky a ochrana před převzetím
skici jiného prvku mají samostatné regresní testy. Závěrečný běh jejich
modelových a GUI cest prošel **9/9** (112,75 s),
`build/profile-final-tests.log`. Tato předchozí etapa ještě Thin odmítala;
navazující etapa jej ověřuje jako skutečné stěny. Oba běžné programy jsou sestavené;
`build/profile-final-build.log`.


Navazující test Thin porovnává objemy válcové stěny, obdélníkové stěny,
otevřené úsečky, oblouku, spline a rotace s nezávislým výpočtem. U spline
používá numerický integrál délky podkladové kubiky. Ověřuje změny tloušťky,
strany a směru, shodu náhledu s tělesem, nativní uložení, otisk cache a původ
plošných referencí. Společné geometrické a 3D Sweep regrese prošly **3/3**
(17,70 s), `build/thin-profile-spline-tests.log`.

Úplná Windows Release sada po zpřístupnění Thin v GUI i CLI ověřila **90/91**
testů (427,39 s), `build/thin-profile-full-tests.log`. Oba programy a všechny
testy byly sestaveny (`build/thin-profile-full-build.log`). Jediné selhání byl
10sekundový timeout automatického výběru DXF při editaci profilu. Samostatný
běh stejného sestavení prošel **1/1** (9,07 s),
`build/thin-profile-dxf-recheck.log`. Po doplnění diagnostiky timeoutu prošly
**tři opakované běhy** (27,75 s), `build/thin-profile-dxf-repeat-tests.log`.
Příčina ojedinělého timeoutu nebyla reprodukována; nepovažujeme ji za opravenou
chybu importního algoritmu. Další úplná sada ji musí nadále pokrývat.

Zpřísněná kontrola spline a obráceného směru prošla **1/1** (0,52 s),
`build/thin-profile-precision-tests.log`: odchylka objemu oproti nezávislému
integrálu byla **1,990028e-9 mm³**, povolená mez je 1e-4 mm³. Odsazená hrana
obsahuje uloženou přesnou B-spline; změna směru zachová rodiče jejích ploch.
Závěrečné GUI/testovací sestavení: `build/thin-profile-final-build.log`.


## Výpočet dvou cílových mezí (navazující etapa)

Kernel má nyní nezávislou dopřednou a zpětnou mez vysunutí. Obě mohou omezovat
profil šikmou rovinou; zpětná mez podporuje také původní přesnou plochu.
Průchozí řez může být kombinován s cílovou mezí na opačné straně. Změna mezí
zachovává identitu obou čel a boků odvozenou od původního profilu.

Kontrola šikmé roviny používá přesné meze celého profilu v souřadnicích roviny.
Odmítne tedy také rovinu protínající kružnici mimo její švový bod. Rovinná
plocha původního tělesa při explicitním výpočtu poskytuje svou aktuální rovinu;
starý číselný snímek cíle ji nemůže přepsat. Kernel ověřuje existenci přesného
původního vlastníka i u sdružených profilových prvků.

Tato předchozí etapa připravila kernelový základ. Navazující nativní adaptéry,
druhý konec náhledu, obnova cílových referencí a CLI zadání jsou popsány níže.
Nepřidává příkaz ani nové pole nativního dokumentu.

První kontrola základních kontraktů a Thin prošla **3/3** (7,31 s),
`build/extrusion-limits-kernel-tests.log`. Rozšířená sada ověřila všechny
stávající geometrické kontrakty, Sweep 2D/3D, ShaftThread, Hole a Thin;
nová řezová zkouška nejprve chybně nezařadila zásobu do tělesa (**6/7**, 22,48 s,
`build/extrusion-limits-related-tests.log`). Po opravě přípravy testu prošla
**1/1** (0,23 s), `build/extrusion-limits-fixture-tests.log`. Objemové testy
zahrnují dvě šikmé roviny, obě kombinace průchozího řezu, Thin, původní rovinu
se záměrně zastaralým snímkem, přesnou povrchovou mez, chybnou stranu,
chybějící zdroj, identitu ploch a změnu otisku cache.


## Původní cíle zakončení v Partu

`extrusion.create/set` přijímá `targets_forward` a `targets_reverse`: pole
s nejvýše jednou referencí. Každá obsahuje `owner` a `key` z původní ZIMA
geometrie; volitelně `label` a `kind` (`plane` nebo `face`). Číselné souřadnice
se nezadávají, společná modelová transakce je získá z původních dat dokumentu.
Prázdné pole smaže uložený cíl. Aktivní `up_to` vždy vyžaduje právě jeden cíl.

```json
{"command":"extrusion.set","arguments":{"container":"<container-ID>","extent":"two_sides","end_forward":"up_to","targets_forward":[{"owner":"<original-owner-ID>","key":"<original-face-key>"}],"end_reverse":"length","length_reverse_mm":4}}
```

Konstrukční rovina používá své `entity` a klíč `plane`, nikoli ID kontejneru.
Roviny počátku používají původní ID počátku a klíč `origin:plane:xy`, `xz`
nebo `yz`. Vybrat lze předcházející původní plochu nebo dostupný počátek;
pozdější objekt, neznámé ID a jiná occurrence jsou odmítnuty bez transakce.
Vazby ze sestavy náleží samostatné sestavové cestě, nikoli tomuto Part příkazu.

Rovinnost původní plochy se určuje z její uložené analytické geometrie.
Samotná koplanarita zobrazovacích trojúhelníků nestačí: hrubě vykreslená
zakřivená plocha nesmí být považována za rovinu. Konstrukční roviny a roviny
počátků jsou určeny svým nativním typem.

Obě strany mohou mít nezávislou rovinu nebo plochu, lze je kombinovat s délkou
či průchozím řezem. `symmetric` zrcadlí dopřednou mez přes rovinu profilu;
nevyhodnocuje nepoužívaný zadní cíl. Náhled řeší obě meze z uložených ZIMA dat.
GUI vybírá cíl společným seznamem kandidátů a potvrzuje přes stejnou transakci.

Při výpočtu se původní plocha uchovává před následujícími booleovskými
operacemi. Zakončení proto zůstává navázáno i po oříznutí její viditelné části.
Mezi tělesy se předávají jen požadované plochy v příslušných lokálních
souřadnicích. Cache bere v úvahu geometrii i umístění zdroje a cílového tělesa.
Runtime řetězec původních ploch sdílí OCCT objekty; nevytváří kopii celého
B-Repu pro každý historický krok. Po studeném načtení se při explicitním
výpočtu potřebná původní topologie znovu sestaví z nativní historie.

Změna původní geometrie při výpočtu obnoví uložený snímek cíle. Ztracená
reference si uchová své ID a poslední data pro opravu; aktivní zakončení
se tím nepovažuje za platné. Samotné čtení či přepnutí tabu kernel nespouští.
Nativní formát a start šablony se nemění, katalog zůstává na 164 příkazech.


Ověření této etapy:

- Úplná Windows Release sada prošla **92/92** (421,62 s),
  `build/extrusion-target-complete-tests.log`; oba programy i testy jsou sestavené,
  `build/extrusion-target-complete-build.log`.
- Po doplnění ochrany hrubě vykreslené plochy, popisků a překladů prošlo
  **9/9** dotčených regresí (47,02 s), `build/extrusion-target-final-tests.log`.
  Závěrečné sestavení: `build/extrusion-target-final-build.log`.
- Geometrické testy kontrolují dvě nezávislé šikmé meze, Thin, smíšený
  průchozí řez, symetrii, opačný směr, ignorování neaktivní zadní reference,
  plochu odstraněnou z viditelného výsledku pozdějším řezem, natočená tělesa,
  změnu umístění zdroje/cíle, studenou cache a potlačení zdroje. Kulové
  zakončení porovnávají s analytickým objemem a rovnici koule ověřují i na
  uložených bodech obou zrcadlených čel.
- Příkazové testy zadávají cíle na roviny, hlavní počátek a původní plochu
  jiného posunutého tělesa, mění zdroj, ověřují Undo/Redo, uložení a odmítnutí
  neplatných referencí bez změny dokumentu. Samostatný CLI proces ověřuje
  také UTF-8 popisek, dvě meze a symetrické zakončení.
- GUI test skutečně kliká na referenční pole a rovinu ve View, kontroluje
  Cancel, OK, Undo a uložený objem. Cílová konstrukční rovina má nenulový
  offset, takže záměna jejího počátku za skutečnou rovinu neprojde.

První úplný běh odhalil dvě chyby nových testovacích vstupů: parametr určený
ose byl použit u roviny a odmítnuté mazání reference očekávalo jiný kód chyby.
Obě přípravy testů byly opraveny; poslední výsledky výše zahrnují jejich
funkční scénáře. Žádná tato změna nevytváří externí cache či nový formát souboru.


## Profilové odečty sestavy

Stejné příkazy `extrusion.create/get/set` a `revolution.create/get/set`
pracují i v aktivní Assembly. `assembly.cut.list` vypíše její odečty bez
výpočtu a bez otevírání zdrojů. `create` převezme samostatnou skicu sestavy
a přiřadí jí nový vlastnický kontejner odečtu. Part nadále zachovává
původní kontejner při převodu samostatné skici.

```json
{"command":"extrusion.create","arguments":{"sketch":"ID-SKICI","length_forward_mm":4,"targets":["ID-VYSKYTU"]}}
{"command":"extrusion.set","arguments":{"container":"ID-ODECTU","length_forward_mm":2,"targets":["PRVNI-VYSKYT","DRUHY-VYSKYT"]}}
{"command":"assembly.cut.list"}
```

`targets` je pole identifikátorů přímo vložených výskytů Partu v cílové
sestavě. Opakované vložení téhož Partu má samostatné cíle. Nelze zasáhnout
vnitřní díl podsestavy, podsestavu jako celek ani odvozený výskyt. Duplicitní
a neznámé cíle se odmítnou. Při vytvoření bez `targets` se použijí všechny
aktuální nepotlačené přímo vložené editovatelné Party, stejně jako v GUI;
při změně bez `targets` se zachová dosavadní seznam. Prázdné pole ukládá
odečet bez zasažených výskytů. V Partu se tento argument odmítne.

Režim `combine` musí být `subtract`. Ostatní parametry jsou společné
s Partem: rozměry, tenká stěna, rozsah a směr, vlastní konstrukční osa
rotace, číselné umístění a původní koncové reference. U `targets_forward`
a `targets_reverse` určuje `instance_path` přesný výskyt původní plochy;
geometrie se čte z uložených referencí. Výstupy navíc obsahují `targets`
a `suppressed`. Změna tvoří jeden krok Undo, zdrojový Part se nemění.

GUI i CLI volají `commit_assembly_profile`. Kontrola identity, vlastníka
skici a cílů předchází potvrzení. Přepočet závislostí probíhá v připravené
kopii dokumentu a respektuje aktuální neuložené otevřené zdroje. Teprve
po dokončení všech odečtů se potvrdí sestava. Neplatný požadavek nepublikuje
mezistav regenerace. Uložení vstupních těl pro Properties rollback zůstává
součástí nativního dokumentu.

### Úplné uložení profilových parametrů

Test nové transakce odhalil, že Assembly ukládala jen část parametrů
vysunutí a rotace. Nyní používají Part i Assembly společné
`save_profile_parameters` / `load_profile_parameters`. Zachovají se
oboustranné délky a úhly, tenká stěna, odsazení skici, koncové podmínky
a původní reference. Assembly ukládá také úplné stávající umístění
a číselné zámky přes existující datový formát Placement. Neúplná
sestavová serializační větev byla odstraněna.

Přípony se nemění. Assembly má verzi INI **17** a vnitřní verzi **26**;
starší verze není podporována podle pravidel projektu. Part zůstává na
verzích **19/43**, protože jeho profilová pole mají stejnou podobu.
Startovací Assembly i verzovaná testovací sestava odpovídají nové verzi.
Všechna potřebná data zůstávají pouze v nativních dokumentech.

Ověření této etapy:

- Původní test objevil neúplnou serializaci Assembly; po opravě prošel model
  vysunutí i rotace. Rozsahy, tenká stěna, zdrojové díly a původní koncová
  plocha mají kontrolované objemy, nikoli jen počet objektů.
- Integrační běh **8/9 za 121,68 s** ověřil skutečný CLI proces, obousměrné
  GUI Properties, historii, šablony a původní profilové reference. Jedinou
  chybou bylo očekávání celého průchozího řezu v jednostranném testu ze středu
  kvádru. Test nyní samostatně ověřuje 30 mm³ jednostranně a 60 mm³ oboustranně.
- Opravený model, překlady a obě startovací šablony prošly **4/4 za 13,93 s**
  (`build/assembly-profile-final-model-tests.log`). Obě šablony byly přeuloženy
  nativní aplikací; Part neztratil ani nezměnil žádnou původní hodnotu.
- Doplněná kontrola chyby výpočtu otevřeného profilu, zákazu zásahu dovnitř
  podsestavy a uložených zámků prošla **1/1 za 0,83 s**
  (`build/assembly-profile-atomic-tests.log`).
- Finální sestavení obou aplikací i všech testovacích programů následovala
  **úplná regrese 136/136 za 540,06 s**, bez chyby
  (`build/assembly-profile-full-build.log`, `build/assembly-profile-full-tests.log`).

Správa pořadí, potlačení a odstranění odečtů: [ASSEMBLY_CUT_HISTORY_COMMANDS.md](ASSEMBLY_CUT_HISTORY_COMMANDS.md).


## Dávkové potvrzení profilové skici (2026-09-13)

`extrusion.sketch.edit` a `revolution.sketch.edit` upravují skicu daného
profilového kontejneru a jednou přepočítají jeho těleso. Fungují v Partu
i pro profilový odečet Assembly. Používají stejné potvrzení jako Properties
v GUI; uchovávají vlastníka, identitu prvku a cílové výskyty odečtu.

```json
{"command":"extrusion.sketch.edit","arguments":{"container":"ID-PRVKU","operations":[{"command":"sketch.point.move","arguments":{"point":"ID-BODU-1","position":[12,0]}},{"command":"sketch.point.move","arguments":{"point":"ID-BODU-2","position":[12,8]}}]}}
```

Pole `operations` obsahuje 1 až 1000 dostupných editačních příkazů skici.
Vnitřní příkazy nezadávají `sketch` ani `document`: vždy mění pouze vlastní
pracovní kopii této skici. Nejsou dostupné příkazy dokumentu jako `save`.
Podporována je také práce s původními externími referencemi a přesnými
cestami výskytů. Neúspěšná vnitřní operace vrací `operation_index` počítaný
od nuly. Chyba dávky nebo výpočtu finálního profilu nepublikuje žádné změny.

Úspěch vrací profilová data a `results` v pořadí operací, `changed` a
`body_calculated`. Změněná dávka je jedním krokem Undo/Redo. Dávka se stejným
výsledným stavem nic nepočítá a nepřidává historii. Samostatné `sketch.*`
příkazy nadále mění jen skicu; jejich dosavadní smlouva se nemění.

Ověření: modelové testy **4/4 za 2,27 s**, finální CLI a modelová integrace
**9/9 za 29,29 s** a GUI konzole s překlady **2/2 za 70,63 s** prošly.
Sestaveny obě aplikace a všechny testovací programy. Kontrolovány objemy
všech čtyř kombinací, chyba otevřeného profilu, hranice dávky, neměnná
historie při chybě/no-op, původní reference konkrétního výskytu a nativní
uložení po Undo/Redo. Logy: `build/profile-sketch-batch-integration-build.log`,
`build/profile-sketch-batch-integration-tests.log`, `build/profile-sketch-batch-gui-tests.log`.
