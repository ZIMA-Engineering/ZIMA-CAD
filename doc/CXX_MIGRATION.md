# Budoucí migrace ZIMA-CAD do C++

## Stav rozhodnutí

Řízená migrace byla zahájena. Python verze je ve feature freeze: nepřidávají se
do ní další velké funkční oblasti, ale přijímá opravy blokujících chyb a změny
nutné k přesnému zachycení referenčního chování. Nedokončené oblasti jako nový
STEP/DXF framework, Drawing a povrchové modelování se dotáhnou v cílové C++
architektuře.

Python verze zůstane během migrace referenční implementací. C++ část nesmí být
považována za náhradu, dokud nad stejnými vstupy neposkytne stejné persistované
výstupy, geometrické výsledky, chybové stavy a uživatelské chování.

První vertikální řez je v `cpp/`: textový prototyp Part dokumentu, přímý OCCT
výpočet kvádru, převod na ZIMA `ViewerMesh`, Qt viewer a deterministický test
objemu, plochy, meshe a save/load. Prototyp používá vlastní příponu `.zcp.json`,
aby předčasně nepředstíral úplnou kompatibilitu se současným `.prtz`.

Společný C++ `PropertiesSubWindow` a jeden `BoxPropertiesDialog` pro tvorbu i
editaci zavádějí interní `SubWindow`, pouze OK/Cancel, transakční Cancel a
potvrzení dvojklikem prostředního tlačítka i nad hlavním oknem. GUI kontrakt je
automaticky testovaný; OCCT výpočet kvádru proběhne až po úspěšném OK.

Part prototyp nyní vlastní obecnou uspořádanou historii kontejnerů místo
jednoho speciálního kvádru. Kontejnery mají stabilní unikátní ID a explicitní
operaci Add/Subtract; první Subtract, duplicitní ID a neplatné rozměry jsou
deterministicky odmítnuty. Celá historie se vyhodnotí jediným explicitním
požadavkem adaptéru a UI obdrží pouze výsledný ZIMA mesh a metriky.

Každý kontejner má také persistované posunutí v milimetrech a natočení kolem
lokálních os X, Y a Z ve stupních. Transformace se aplikuje na operand před
jeho Boolean operací, takže nejde o zobrazovací zkratku. Test oddělených a
natočených kvádrů ověřuje, že výsledný objem odpovídá skutečné OCCT geometrii.

`DocumentSession` sjednocuje transakce dokumentu. Každé úspěšné OK vytváří
právě jednu revizi, Cancel žádnou, Undo/Redo obnovují celý persistovaný stav a
savepoint určuje indikaci neuložených změn. Nový commit po Undo ruší starou
Redo větev. Nahrazení nebo zavření změněného dokumentu vyžaduje explicitní
Save/Discard/Cancel; během otevřeného Properties okna nelze změnit revizi pod
rozpracovaným dialogem.

První GPU viewer používá OpenGL 3.3 core přes `QOpenGLWidget`. Pozice, normály,
trojúhelníky a hrany se nahrávají do GPU bufferů; orbit, pan a zoom mění pouze
kamerové matice. Jeden ray test vytváří společný ordered candidate list pro
oranžový hover, LMB potvrzení přesné geometrie azurovou barvou a RMB cycling
před potvrzením. Trojúhelníky stejné persistované plochy se seskupí do jednoho
kandidáta a zvýrazní se celá přesná plocha. Deterministický test ověřuje pořadí
překrývajících se zásahů zepředu dozadu, deduplikaci ploch a cyklický návrat ve
stejné kandidátní sadě.

Plochy kvádru mají sémantické klíče `x_min`, `x_max`, `y_min`, `y_max`,
`z_min`, `z_max` vlastněné stabilním ID kontejneru. OCCT adaptér propaguje
jejich původ přes Boolean `Modified/Generated` historii. Rozporný původ
výsledné plochy se nenahrazuje náhodnou volbou; taková plocha zůstane
nenabízená, dokud nebude reference jednoznačně opravitelná.

Stejný viewer packet nyní obsahuje původní sémantické hrany a vrcholy kvádru.
Vrchol je určen kombinací `x_min/x_max : y_min/y_max : z_min/z_max`; hrana je
kanonicky seřazená dvojice svých koncových vrcholů. Dvanáct hran a osm vrcholů
proto nezávisí na pořadí OCCT traversal. Boolean historie propaguje přeživší a
rozdělené původní hrany/vrcholy. Ray-edge a ray-vertex kandidáti vznikají pouze
z hotového ZIMA viewer packetu a jsou řazeni zepředu dozadu bez dotazu do OCCT.

Viewer nyní nejprve vytvoří jediný společný ordered `ViewerCandidate` seznam
pro Container, Face, Edge a Vertex. Explicitní selection contract pouze
odfiltruje povolené druhy z tohoto již existujícího seznamu a nikdy nespouští
druhý picker. Běžný Part nabízí pouze původní kontejner; aktivní příkaz může
povolit plochu, hranu nebo vrchol. Hover, LMB potvrzení a RMB cycling stále
spotřebují stejné pořadí. Zvýraznění respektuje přesnou úroveň kandidáta:
všechny vlastněné plochy kontejneru, jednu sémantickou plochu, jednu ZIMA
polyline hrany nebo jeden persistovaný vrchol.

Editace existujícího kontejneru nyní používá obecnou hranici historie podle
stabilního ID. Po otevření Properties viewer explicitně vyhodnotí pouze
operace před editovaným kontejnerem, aktivní položka zůstane v celém stromu
zelená a následující položky jsou po dobu editace potlačené. První kontejner
má korektně prázdný vstup. OK vytvoří jednu dokumentovou revizi a Cancel žádná
data nezmění; po zavření se v obou případech obnoví normální výsledek celé
historie.

Běžný potvrzený výběr kontejneru je obousměrně synchronizovaný mezi viewerem a
stromem přes stejné stabilní ID. LMB ve vieweru předá již potvrzeného kandidáta
ze společného candidate listu; strom nespouští nový ray picker. Výběr položky
ve stromu smí vytvořit zvýraznění pouze tehdy, když se její vlastník skutečně
nachází v aktuálním persistovaném viewer packetu.

RMB nyní respektuje stav stejného výběru. Před LMB potvrzením pouze posune
aktivní index ve společném candidate listu. Nad již potvrzeným kontejnerem
otevře jeho běžné kontextové menu místo dalšího cyklování. Menu nabízí
`Vlastnosti` a první obecný krok `Vybrat nadřazený`, který přesune výběr z
kontejneru na Part a současně odstraní kontejnerové zvýraznění ve vieweru.

Potvrzený běžný výběr se uchovává jako stabilní ID, nikoliv jako ukazatel na
položku stromu nebo index trojúhelníku. Přestavba stromu, explicitní výpočet a
ukončení rollbacku proto výběr obnoví proti novému viewer packetu. Během
rollbacku může aktivní kontejner zůstat vybraný ve stromu bez falešného
zvýraznění, pokud ještě není součástí zobrazeného vstupu. Neplatný nebo po
výpočtu nedostupný vlastník se ve vieweru nezvýrazní.

Strom historie používá stejné kontextové akce jako potvrzený objekt ve
vieweru. RMB na položce ji nejprve nastaví jako aktuální stabilní výběr a pak
otevře společné menu; `Vlastnosti` tak vstupují do stejného rollback editačního
kontraktu bez ohledu na to, odkud uživatel příkaz vyvolal.

Výpočet tělesa je nově oddělený od `rebuild()` uživatelského rozhraní.
Explicitní OK nebo příkaz `Regenerovat` vytvoří v jednom průchodu OCCT také
ZIMA viewer výsledky pro hranice historie a uloží je do cache konkrétní revize.
Obnovení stromu, výběr, otevření Properties, rollback, Undo/Redo ani zavření
dialogu už OCCT nevolají; pouze čtou existující vypočtený packet. Revize bez
výsledku zobrazí jednoznačnou výzvu k regeneraci místo skrytého výpočtu.

Textový dokument nyní persistuje také vypočtené ZIMA viewer packety všech
hranic historie: mesh, stabilní reference ploch, hran a vrcholů, objem a
plochu. Načtení validního vypočteného dokumentu proto rovnou obnoví poslední
stav bez OCCT. Serializace kontroluje indexy trojúhelníků, zarovnání referencí,
konečnost souřadnic a shodu počtu hranic s historií; poškozená data se
nepoužijí jako reference. Dokument uložený bez výsledku zůstává platný, ale
vyžádá explicitní Regenerovat.

Každá vypočtená hranice navíc nese deterministický otisk přesného prefixu
operací: stabilních ID, Boolean režimů, rozměrů, posunutí a natočení. Save i
Open porovnají otisk s aktuální historií. Syntakticky validní mesh z jiných
parametrů je proto odmítnut místo toho, aby se zobrazil nebo nabídl k výběru
jako aktuální geometrie.

Vypočtené hranice již nevlastní `MainWindow`, ale přímo stav
`DocumentSession`. Každá Undo/Redo revize proto cestuje společně se svými
odpovídajícími viewer packety. Regenerace aktualizuje odvozený stav bez
vytvoření falešné modelové revize, ale označí dokument jako změněný, dokud se
nový vypočtený stav neuloží. UI zůstává pouze konzumentem dokumentového stavu.

Historie a kernel API již nejsou pevně svázané s kvádrem. Společný
`HistoryOperation` nese variantní ZIMA požadavek primitiva a Boolean režim;
OCCT adaptér nyní ve stejném sledu vyhodnotí kvádr i válec. Válec má
persistované parametry poloměru, výšky a umístění, stabilní původní plochy
`z_min`, `z_max`, `side` a sémantické kruhové/seam hrany. Jediný obecný
`PrimitivePropertiesDialog` slouží pro tvorbu i pozdější editaci kvádru i
válce. Typ prvku pouze určí zobrazená parametrická pole; umístění, operace,
rollback, validace, OK/Cancel a MMB potvrzení nemají duplicitní cesty.

První Assembly datový řez zavádí `AssemblyDocument`, bezprostředně vlastněné
`PartOccurrence` a samostatnou `InstancePath`. Každá komponenta drží stabilní
occurrence ID, identitu a cestu zdrojového Partu, vlastní umístění a poslední
explicitně převzatý ZIMA viewer packet. Složení sestavové scény pouze
transformuje persistovaná viewer data a nevolá OCCT. Dva výskyty stejného
zdrojového Partu proto zachovají shodné feature/container vlastníky, ale ve
viewer referencích mají odlišnou délkově kódovanou instance path. Picking,
deduplikace a přesné zvýraznění nyní zahrnují tuto occurrence identitu.

Assembly prototyp má vlastní textový formát `.zca.json`. Persistuje identitu
sestavy, stabilní occurrence ID, zdrojové document ID/cesty, sestavou vlastněné
umístění a poslední explicitně převzatý vypočtený `BodyResult` každého Partu.
Part i Assembly používají jednu sdílenou serializaci a validaci viewer packetu.
Otevření sestavy pouze ověří a složí persistovanou scénu; OCCT nespouští.
Samostatný jednosměrný `DependencyGraph` odmítá self-reference i libovolně
hluboký nepřímý cyklus ještě před vložením závislosti.

`Workspace` nyní drží více otevřených Part/Assembly dokumentů a explicitně
odděluje aktivní zapisovatelný dokument od nejvyššího zobrazovaného dokumentu.
Aktivace zdrojového Partu tedy sama nenahrazuje zobrazenou sestavu. Každá
sestava má `AssemblySession` s revizemi, Undo/Redo a savepointem. Umístění
komponenty vytváří modelovou revizi; explicitní převzetí nového vypočteného
stavu závislosti pouze označí odvozený stav k uložení.

Workspace umí vložit aktuální vypočtený stav otevřeného Partu a explicitně
regenerovat přímé Part závislosti z autoritativních in-memory dokumentů.
Obyčejná změna Partu parent Assembly nemění. Nový Assembly Workspace GUI
prototyp `zima-cad-workspace-cpp` nabízí taby otevřených dokumentů, vytvoření a
otevření sestavy, otevření Partu, vložení aktivního Partu, uložení, explicitní
Regenerovat, occurrence strom a leaf Part occurrence picking. Samostatný
`ComponentPropertiesDialog` mění pouze sestavou vlastněný název a placement;
zdrojová identita a geometrie zůstávají read-only.

Workspace shell nyní obsahuje také ověřené Part modelovací příkazy: nový a
otevřený Part, kvádr, válec, společné Properties, explicitní Part Regenerate,
Save a revizní Undo/Redo. Aktivní Part lze editovat při zachování zobrazeného
Assembly snapshotu; parent se změní teprve explicitním Assembly Regenerate.
Tím je funkční obsah původního samostatného Part shellu přenesen do společného
Workspace směru, přestože starý target zatím zůstává jako porovnávací reference.

Rollback editace aktivního Partu funguje také uvnitř stále zobrazené sestavy.
Workspace vyžaduje přesný occurrence kontext; u opakovaného zdrojového Partu
nikdy výskyt nehádá. Pouze tento occurrence je v transientní scéně nahrazen
persistovaným boundary packetem před editovaným kontejnerem. Ostatní výskyty
stejného Partu a celá sestava zůstávají jako pasivní kontext. První operace má
prázdný vstup, aktivní položka je zelená a downstream historie potlačená.

Assembly occurrence nyní odděleně persistuje `suppressed` a `visible`.
Potlačení vyřazuje komponentu z aktivní sestavové scény, zatímco skrytí je
čistě zobrazovací stav; ani jedno nemaže zdrojový packet, placement nebo
occurrence identitu. Společné RMB menu ve stromu i nad potvrzeným viewer
occurrence nabízí Vlastnosti, Skrýt/Zobrazit a Potlačit/Obnovit. Závislosti se
nikdy nehádají z geometrie.

Sestava nyní persistuje explicitní orientované vazby komponent pro reference
uložení a externí reference skici. Ruční potlačení předpokladu se tranzitivně
promítne jen do komponent, které na něm podle tohoto grafu závisejí; skrytí se
nešíří. Obnovení předpokladu automaticky obnoví jen odvozeně potlačené
komponenty, zatímco jejich vlastní ruční potlačení zůstává zachované. Cyklus
je odmítnut už při vložení vazby.

Occurrence může nově odkazovat také na Assembly. Snapshot podsestavy uchovává
vnitřní instance paths a samostatný rekurzivní strukturální strom se stabilními
ID, názvy, typy zdrojů a stavy komponent. Parent při skládání přidá vlastní
stabilní segment; opakované vložení stejné podsestavy proto nesloučí identity
listových Partů. Umístění podsestavy vlastní výhradně její bezprostřední parent.
Explicitní Regenerate top-level sestavy projde otevřené podsestavy až k
autoritativním in-memory Partům, aniž by editace zdroje sama změnila parent.
Vložení, které by uzavřelo přímý nebo nepřímý dokumentový cyklus, je odmítnuto.

`InstancePath` má striktní obousměrný délkově kódovaný formát. Workspace podle
celé cesty určí konkrétní occurrence, jeho source dokument i bezprostředně
vlastnící Assembly. Strom GUI zobrazuje persistovaný strukturální snapshot a
nese stejnou plnou cestu jako viewer candidate; výběr se proto synchronizuje i
pro opakované hluboce vnořené zdroje. Pouze přesná aktivovaná occurrence
podsestavy nahradí svou větev živým editovatelným stavem, zatímco ostatní
výskyty a parent zůstávají pasivní. Vlastnosti, Skrýt a Potlačit se zapisují do
bezprostředního owneru. Explicitní Regenerate atomicky obnoví geometrický i
strukturální snapshot. Rollback vnořeného Partu nahradí v cached top-level
scéně jen geometrii přesné listové cesty a zachová všechny pasivní sourozence
bez implicitní regenerace parentu.

První C++ řez Sketcheru zavádí samostatný modul bez Qt a OCCT. Persistovaná
skica vlastní stabilní body a úsečky, konstrukční stav, rovinu XY/XZ/YZ a její
podepsané odsazení. Bodový model podporuje horizontální, vertikální a shodnou
vazbu a řídicí kóty vzdálenosti, podepsané vzdálenosti X a vzdálenosti Y.
Kóta může mít nezávislou absolutní dolní a horní mez; hodnota mimo zapnutou mez
se odmítne ještě před solverem.

Solver mění body transakčně. Pevný nebo nekonvergující konflikt obnoví přesné
vstupní souřadnice a neponechá částečně vyřešenou skicu. Zbývající stupně
volnosti se určují hodností numerického Jacobiánu, takže redundantní rovnice
nepředstírají plně zavazbený model. Testy ověřují řešení, konflikt, meze,
stabilní save/load a stav nedostatečného zavazbení.

Part dokument persistuje skici přímo vedle historie tělesa. Změna skici je
jedna běžná revize DocumentSession a sama nespouští OCCT. Skica promítá své
body a úsečky do ZIMA viewer packetu se stabilním vlastníkem a sémantickým
klíčem; zobrazení a společný picker proto zůstávají na aplikační straně hranice
kernelu. Workspace nabízí stejné interní Properties SubWindow pro vytvoření i
editaci názvu, roviny a odsazení skici.

První interaktivní příkaz Sketcheru vytváří úsečku dvěma LMB body. Oba body
vznikají projekcí stejného kamerového paprsku vieweru do persistované roviny
skici; nevzniká druhý picker ani dotaz do OCCT. První bod a pohyb kurzoru jsou
jen transientní čárkovaný náhled. Druhý bod atomicky vloží geometrii jako jednu
Part revizi, Escape náhled zahodí a změna dokumentu příkaz ukončí. Navazující
konce v modelové toleranci znovu použijí stejné stabilní ID bodu, takže profil
je topologicky propojený a ne pouze vizuálně překrytý. Kamera se mezi
navazujícími úsečkami neresetuje.

Body a úsečky skici mají samostatné druhy viewer kandidátů `SketchPoint` a
`SketchSegment`. Aktivní skica nabízí pouze tyto přesné persistované objekty,
zatímco běžný režim Partu dál nabízí kontejnery a nepropouští result-topology
solidu. Potvrzené úsečce lze přidat vodorovnou nebo svislou vazbu; vazba a
vyřešené souřadnice tvoří jednu Part revizi. Duplicitní vazba nebo kombinace,
která by zkolabovala úsečku do bodu, se transakčně odmítne.

Potvrzená úsečka může vytvořit první řídicí kótu délky. Tvorba i pozdější
editace ze stromu nebo kontextové cesty používají tutéž interní třídu
`SketchDimensionPropertiesDialog`. Okno obsahuje jmenovitou hodnotu a nezávislé
absolutní dolní a horní meze. Při prvním zapnutí se dolní mez předvyplní nulou
a horní aktuální jmenovitou délkou. OK validuje meze, vyřeší skicu a vytvoří
jednu Part revizi; Cancel, hodnota mimo rozsah nebo konflikt nepřenesou žádnou
změnu. Editace zachová stabilní ID kóty a kóta zůstává pod vlastní skicou ve
stromu.

Kóty skici jsou nyní plnohodnotná ZIMA viewer data, nikoliv jen řádky stromu.
Packet kóty obsahuje dva vynášecí body, odsazenou kótovací čáru, číselnou
hodnotu, stabilního vlastníka skici, sémantický klíč kóty a instance path.
Viewer vykreslí vynášecí/kótovací čáry a hodnotu v milimetrech na tři desetinná
místa. Kótovací čára přidává vlastní `SketchDimension` kandidát do společného
ordered pickeru; dvojklik otevře stejnou Properties třídu jako strom.
Viewer-packet JSON kóty validuje a persistuje, zatímco sestava transformuje
všechny jejich body a prefixuje přesnou identitu vnořeného výskytu bez OCCT.

Druhý interaktivní příkaz vytvoří obdélník dvěma protilehlými rohy. Pohyb
kurzoru vykresluje čtyři transientní hrany náhledu. Potvrzovací klik atomicky
vloží čtyři sdílené body, čtyři uzavřené úsečky, dvě vodorovné a dvě svislé
vazby jako jednu Part revizi. Nulová šířka nebo výška se odmítne bez změny,
Escape náhled zahodí a přepnutí nástroje odstraní předchozí transientní stav.
Po úspěchu zůstává příkaz aktivní pro kreslení dalších profilů.

Kružnice je další nativní geometrie Sketcheru. Vlastní stabilní ID, jeden
stabilní středový bod, kladný poloměr a konstrukční stav. Dvoukrokový příkaz
zadá střed a bod obvodu, během pohybu kreslí 96segmentový transientní náhled a
jednu Part revizi vytvoří až po potvrzení nenulového poloměru. Persistovaná
viewer data používají samostatný `SketchCurve` kandidát a klíč `circle:<id>`,
takže kružnici nelze zaměnit za hranu solidu.

Potvrzená kružnice může vytvořit řídicí kótu poloměru přes stejnou Properties
třídu a kontrakt absolutních mezí jako délka úsečky. Kóta odkazuje přímo na ID
kružnice, nevytváří pomocný bod obvodu, transakčně řídí uložený poloměr,
přispívá stupněm volnosti do Jacobiánu a ve vieweru má prefix `R`. Kružnice i
její kóta poloměru se ukládají bez OCCT.

Tříbodový nástroj oblouku zadává střed, začátek a konec. Oblouk persistuje
stabilní středový bod, poloměr a protisměrný úhlový interval. Adaptivní viewer
křivka i picking používají klíč `arc:<id>` a samostatný `SketchCurve` kontrakt,
nikoli OCCT hranu výsledného tělesa.
Vybraný oblouk používá stejnou interní Properties třídu kóty poloměru jako
kružnice. Kóta řídí persistovaný poloměr v absolutních mezích, vstupuje do
solveru a zobrazuje se radiálně přes střed úhlového intervalu oblouku.

Příkaz Shodnost bodů vlastní explicitní výběrový kontrakt pouze pro Sketch body.
Dva potvrzené kandidáty `point:<id>` vytvoří jednu transakční vazbu; solver
pohybuje jen nefixovanými souřadnicemi, odmítá duplicitu i konflikt a Escape
vrátí běžný výběr geometrie skici.

Běžně potvrzený Sketch bod lze jednou modelovou operací fixovat nebo uvolnit.
Fixace je persistovaná vlastnost stabilního bodu, odebere jeho dvě souřadnicové
proměnné ze stupňů volnosti solveru a uloží se jako jedna Part revize. Vybraná
úsečka nabízí také podepsané projekční kóty X a Y. Používají společné Properties
okno kóty a zobrazují osově orientované čáry s prefixem `X` nebo `Y`; nemíchají
se se skutečnou délkou ani s výkresovou tolerancí.

Properties kóty dovolují přepnout řídicí a měřený stav. Tažení potvrzeného
nefixovaného bodu vychází ze stejného seřazeného viewer kandidáta jako hover a
klik. Pohyb se počítá nad transientním Part dokumentem, solver zachová vazby,
měřené kóty se přepočítají a absolutní meze odmítnou neplatnou polohu. Puštění
LMB vytvoří nejvýše jednu Part revizi.

První řez sestavových vazeb persistuje dva přesné konce reference
(`InstancePath`, owner kontejneru a sémantický klíč), typ vazby, offset, stav
výpočtu a odpovídající orientovanou dependency edge. Rovinná geometrie se
řeší výhradně z persistovaných trojúhelníků ZIMA viewer packetu a ověřuje se
rovinnost celé sémantické plochy; OCCT se při výběru ani výpočtu reference
nevolá.

Deterministická vazba plocha–plocha zatím řeší právě ten rozsah, pro který je
prostá transformace správným prostředkem: dvě již rovnoběžné roviny a translaci
pohyblivé bezprostřední komponenty na zadané podepsané odsazení. Jedna
komponenta smí nyní vlastnit jednu vypočítávanou vazbu každého typu;
nerovnoběžné roviny se označí jako nepodporovaná geometrie místo předstírání
obecného solveru. Chybějící nebo nepodporovaná reference zčervená ve stromu a
potlačí závislou větev. Po opravě reference další explicitní Regenerate starý
chybový stav nejprve uvolní a komponentu může obnovit.

GUI příkaz `Vazba plocha–plocha` používá společný viewer candidate list. První
potvrzená plocha určuje pohyblivou komponentu, druhá pevnou referenci. Název a
odsazení se zadávají ve sdíleném interním Properties SubWindow pouze s OK a
Cancel; OK vazbu vypočítá a vytvoří jednu Assembly revizi.

ZIMA viewer packet nyní vlastní také stabilní osy: bod, jednotkový směr,
zobrazovací délku a `AxisReference`. Kvádr při explicitním výpočtu uloží své
lokální `axis:x/y/z`, válec svou hlavní `axis`; jejich umístění se vyřeší při
výpočtu a další UI/persistence/Assembly cesty OCCT nepoužívají. Assembly
transformuje bod i směr osy, prefixuje přesnou instance path a zachovává osu i
při rollbacku jiného occurrence. Společný ordered candidate list obsahuje
`Axis`; během axis selection contractu jsou osy zobrazené a hover/LMB/RMB
pracují se stejnou kandidátní sadou a přesným zvýrazněním.

Vazba osa–osa natočí libovolně orientovanou závislou osu nejkratší rotací na
nejbližší ekvivalentní směr referenční osy a potom odstraní kolmou vzdálenost
mezi přímkami. Správně ponechá volný posun a rotaci podél osy. Vazba
plocha–plocha stejným principem nejprve natočí komponentu kolem vybraného bodu
a potom aplikuje podepsané odsazení. Souhlasná i opačná orientace zůstávají
stabilní bez zbytečného obratu o 180 stupňů.

Osovou a rovinnou vazbu lze kombinovat na stejné komponentě; po výpočtu se oba
výsledky znovu ověří z persistovaných ZIMA dat. Konflikt vrátí přesný placement
komponenty před regenerací a označí všechny její aktivní vazby jako
nepodporované, takže v dokumentu nezůstane částečný pohyb. GUI používá stejný
dvoukrokový výběr a interní Properties okno; u osa–osa se nezobrazuje
editovatelný offset, protože posun podél společné osy zůstává
stupněm volnosti.

Vazby mají nyní celý editační životní cyklus. Stejný `MatePropertiesDialog`
slouží pro tvorbu i dvojklikovou editaci; OK nahradí vazbu, přestaví její
dependency edge, znovu vypočítá sestavu a vytvoří jednu revizi. RMB nabízí
Vlastnosti, Potlačit/Obnovit a potvrzené Smazat. Potlačená vazba zůstává
persistovaná, ale nepočítá placement ani nešíří potlačení přes svou edge.
Smazání odstraní vazbu i její edge atomicky. Undo/Redo obnovuje kompletní stav
včetně placementu, statusu a dependency grafu.

Strom zobrazuje editovatelné vazby jen pro přesnou aktivní Assembly. Při
aktivaci podsestavy jsou pod její zelenou occurrence vloženy její živé vazby;
vazby top-level parentu ani jiných opakovaných výskytů se tím nestanou
zapisovatelné. Chybnou vazbu lze otevřít a upravit bez nuceného úspěšného
výpočtu, protože její přesné reference mohou být opravitelné až následnou
změnou modelu a explicitní regenerací.

## Hlavní cíle

- vysoká rychlost aplikace, zejména vieweru, pickingu, velkých sestav a práce
  s rozsáhlými datovými strukturami;
- rychlá inkrementální kompilace při běžném vývoji;
- malé moduly s úzkými veřejnými rozhraními;
- zachování oddělení technického záměru, persistovaného modelu, viewer dat a
  dočasné OCCT geometrie;
- možnost postupné migrace a průběžného porovnávání s Python verzí.

## Navržené moduly

```text
Document Core
├── dokumenty a historie
├── parametry a relace
└── serializace

Geometry
├── vlastní geometrická data
├── stabilní reference
└── úzký OCCT adaptér

Viewer
├── mesh a scéna
├── picking
├── hover a potvrzený výběr
└── OpenGL renderer

Sketcher
├── geometrie skici
├── vazby a kóty
└── solver

Assembly
├── stabilní instance paths
├── dependency graph
├── sestavové vazby
└── explicitní regenerace

Drawing
├── listy a pohledy
├── kóty
└── rámečky a razítka

Application
├── Qt okna
├── dialogy
└── příkazy
```

Každý modul vlastní svá interní data. Změna Qt dialogu nesmí vyžadovat
rekompilaci Sketcheru nebo geometrického jádra; změna rendereru nesmí měnit
dokumentový model. Rozhraní mezi moduly používají datové typy ZIMA-CAD, jako
jsou `BodyResult`, `FaceReference` a `ViewerMesh`, nikoliv živé OCCT objekty.

## Hranice OpenCascade

OCCT se nebude kopírovat do zdrojového stromu ZIMA-CAD ani běžně kompilovat se
ZIMA-CAD. Aplikace bude proti předkompilovaným OCCT DLL/SO dynamicky linkovat
přes jeden úzký C++ adaptér. Samostatný OCCT proces a IPC se nezavádí, pokud
pozdější měření neprokáže konkrétní potřebu izolace procesu.

OCCT zůstává solid-modeling kernelem pro explicitní výpočty těles. Dokumenty,
historie, stabilní reference, viewer, picking, hover a UI zůstávají vlastnictvím
ZIMA-CAD. Těžké OCCT hlavičky mají být omezeny na implementaci adaptéru, aby
jejich změny nezpůsobovaly plošnou rekompilaci aplikace.

## Rychlost sestavení

Preferovaný build používá CMake a Ninja. Veřejné hlavičky mají být malé;
použijí se dopředné deklarace, PImpl a podle měření vhodné předkompilované
hlavičky. OCCT a Qt runtime se při běžném buildu aplikace znovu nekompilují.

Měří se odděleně:

- čistý build celé aplikace;
- inkrementální build po změně jednoho dialogu nebo příkazu;
- linkování;
- spuštění cílených a úplných testů;
- start aplikace, regenerace modelu, picking a vykreslení velké sestavy.

Rychlost kompilace ani runtime se nesmí pouze předpokládat; před a během
migrace se zaznamenají reprodukovatelné benchmarky.

## Pořadí migrace

1. Dokončit rozpracované zásadní funkce Python verze.
2. Stabilizovat aktuální dokumentový model a odstranit dočasné duplicitní
   cesty; staré formáty se nemigrují.
3. Doplnit testy, které fungují jako přenositelná specifikace chování.
4. Vyhlásit feature freeze; Python verze poté přijímá pouze opravy nutné pro
   referenční chování.
5. Vytvořit C++ základ s Document Core, Qt aplikací, viewerem a OCCT adaptérem.
6. Migrovat po vertikálních řezech, vždy s načtením dokumentu, výpočtem,
   zobrazením a interakcí potřebnou pro jeden úplný scénář.
7. Porovnávat každý řez s Python verzí na stejných datech a testech.
8. Přepnout hlavní aplikaci až po dosažení definované funkční a datové parity.

Mechanicky přeložené množství C++ kódu není měřítkem dokončení. Rozhodující je
ověřená shoda kontraktů a odstranění potřeby udržovat dvě aktivně se měnící
implementace stejné funkce.
