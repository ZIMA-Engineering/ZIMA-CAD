# Vícetělesový Part a booleovské operace

Stav k 2026-09-07: **vícetělesový Part je zapojený do aplikace a ověřený
modulovými i GUI testy**. Každé těleso má vlastní historii, počátek a umístění.
Strom, aktivace, rollback, skicář a reference pracují s vlastnícím tělesem,
včetně editace Partu ve vnořené Assembly. Sjednocení, rozdíl a průnik jsou
samostatné položky historie Partu. Nový Part aktivuje první těleso.

Assembly Boolean, Pattern celých těles a přesun těles mezi Party zůstávají
budoucími rozšířeními. Podrobné zápisy implementačních etap níže zachycují
postup vývoje; aktuální stav shrnuje tento úvod a závěrečné oddíly.

## Účel a model

Vstupem jsou samostatně vytvářené geometrické větve, například polotovar
formy a nástrojový tvar dutiny. Každá větev potřebuje vlastní historii
přičítání, odebírání a dalších úprav. Výstupem je geometrie dokumentu vzniklá
z jejich výslovně určených kombinací. Prostředkem je vícetělesový Part,
parametrické booleovské operace a existující explicitní výpočet přes OCCT.

Typické příklady: licí formy, otisky, razníky, dutiny a elektrody. Kanálky
nejsou hlavním důvodem této změny; jejich organizaci mohou řešit skupiny.
Organizační skupina sama neznamená samostatný geometrický výsledek.

## Part

- Kořenem stromu bude název souboru; před uložením pracovní název dokumentu.
- Pod kořenem bude počátek dokumentu a samostatné kontejnery těles.
- Každé těleso bude mít vlastní lokální počátek a vlastní modelovací historii
  se současnými typy prvků. Nejde pouze o jeden primitivní nebo importovaný solid.
- Aktivní těleso určí, do které větve vzniká nový prvek. Aktivita není
  booleovská operace a samotné přidání tělesa nic automaticky nesjednotí
  ani neodečte.
- Uvnitř tělesa půjde přičítat a odebírat materiál a upravovat jeho výsledek.
  Výsledek celé větve se poté může stát vstupem dalšího Booleanu.
- Boolean bude položkou historie s explicitními vstupy a druhem operace.
  Po jeho vytvoření může práce pokračovat dalšími tělesy i operacemi.
- Kliknutí na název souboru označí celkovou výslednou geometrii dokumentu.
  Nevznikne navíc volně stojící položka „výsledné těleso“ bez jasného vlastníka.
  Boolean přesto má výpočetní výstup, na který mohou navazovat další operace.
- Výsledek dokumentu může obsahovat více nespojených těles. Výběr celého
  dokumentu není automatické geometrické sjednocení.
- Zdrojové větve zůstanou editovatelné. Viditelnost zdrojů a výsledku musí
  zabránit matoucímu překrytí, aniž by mazala zdrojová data.

Příklad struktury stromu:

```text
Forma.prtz                         ← aktuální výsledek dokumentu
├─ Počátek dokumentu
├─ Těleso: polotovar
│  ├─ Počátek tělesa
│  └─ vlastní historie prvků
├─ Těleso: nástroj dutiny
│  ├─ Počátek tělesa
│  ├─ Protrusion
│  ├─ další přičtený a odečtený prvek
│  └─ zaoblení
├─ Boolean: dutina                  ← polotovar minus nástroj dutiny
├─ Těleso: další nástroj
│  └─ vlastní historie prvků
├─ Boolean: dokončení               ← dutina minus další nástroj
└─ Vložit zde
```

## Dohodnuté ovládání

Boolean má vlastní řádek ve stromu na stejné úrovni jako tělesa.
Vlastnosti tělesa obsahují název, aktivaci, viditelnost a umístění; těleso
nemá volbu přičítání nebo odečítání. Vlastnosti Booleanu obsahují název,
Sjednocení / Rozdíl / Průnik a dva explicitní vstupy: cíl a nástroj.
Rozdíl počítá cíl minus nástroj. Operace spotřebuje oba dostupné vstupní
výsledky a zpřístupní nový výsledek pod vlastním stabilním ID Booleanu.
Zdrojová tělesa a jejich historie zůstávají zachované pro editaci.
Další Boolean může použít výsledek předchozího Booleanu a nové těleso.
Přesun Booleanu i tělesa kontroluje pořadí závislostí před změnou dokumentu.

- Bez aktivního tělesa panel nabízí vytvoření tělesa a operace na úrovni těles.
- Po aktivaci tělesa nabízí současné modelovací příkazy pro jeho vlastní historii.
- Aktivní těleso se ve stromu označí zeleně; aktivace je dostupná i v kontextové nabídce.
- Úroveň těles má vlastní „Vložit zde“ a dovoluje přesun těles při zachování závislostí.
- Vlastnosti používají společný interní dialog s OK/Cancel.
- Výsledek dokumentu se označuje kliknutím na název souboru.

Dokumentový počátek a počátky těles mají stejnou zobrazovací velikost jako
současný dokumentový počátek. Lokální počátky prvků zachovají menší velikost.
Vícenásobné použití jedné větve bude vyžadovat explicitní seznam použití;
první implementace nemá skrytě duplikovat již spotřebovaný výsledek.

## Historie, viditelnost a přepočet

Každé těleso má vlastní historii a aktivní místo v ní. Při práci v tělese 1
se nezobrazuje pozdější těleso 2. Při práci v tělese 2 se dřívější těleso 1
zobrazuje jako kontext z poslední vypočítané geometrie. Aktivita tělesa
určuje cíl nových prvků; nepředstavuje booleovskou operaci.

Změna uvnitř tělesa nevyvolává OCCT přepočet nezávislých těles. Invalidace
sleduje skutečné reference a vstupy Booleanů; samotné pořadí pozdějšího tělesa
není závislost. Při explicitním výpočtu se obnovují změněné větve a jejich
závislé výsledky. Běžné zobrazení a aktivace používají uložené výsledky.

## Budoucí Pattern celých těles

Počítat s lineárním a kruhovým Patternem nad výsledkem tělesa, s počtem,
roztečí nebo úhlem. Kopie odkazují na zdroj a neduplikují jeho modelovací
historii. Výsledek lze ponechat samostatný nebo použít jako nástroj Booleanu,
například více otisků odečtených od polotovaru formy.

Datový model musí umožnit více výstupů jedné operace se stabilními identitami
jednotlivých instancí. Pattern je budoucí rozšíření, nikoli nutná součást první
implementace vícetělesového modelu.

## Assembly a Drawing

Kořen stromu Assembly i Drawing bude také název souboru. Pod sestavou budou
její počátek, komponenty a operace sestavy; pod výkresem listy a pohledy.
Drawing tím nezískává booleovské modelování.

V Assembly mohou booleovské operace pracovat s konkrétními umístěnými výskyty
Partů nebo podsestav, například při geometrickém návrhu tvarového vyjiskřování.
Výsledek operace patří sestavě; nesmí nevyžádaně přepsat zdrojový Part ani
zdrojovou podsestavu. Obyčejné vložení komponenty zůstává vložením samostatné
komponenty, nikoli automatickým sjednocením všech komponent.

Vlastnictví umístění zůstává u bezprostřední vlastnící Assembly. Vyšší sestava
nesmí převzít umístění vnitřních komponent podsestavy. Opakované výskyty
rozlišují stabilní instance paths. Aktualizace závislostí zůstává výslovná
přes Regenerate, bez skrytého přepočtu při přepnutí záložky nebo obnovení stromu.

## Import a reference

Požadovaným použitím je také vložit STEP nebo další `.prtz` jako nástrojovou
geometrii a odečíst ji od jiného tělesa. Propojený zdroj versus nezávislá kopie,
výběr těles z vícetělesového zdroje a jejich obsluha jsou ještě k dopracování.
Propojené zdroje mají respektovat explicitní Regenerate a zákaz cyklů.

Identity těles, operací a závislostí musí být stabilní a persistované.
Topologie a reference nadále vycházejí ze ZIMA ancestry, nikoli z pořadí
OCCT ploch nebo hran. Je nutné určit, jak navazující prvky adresují výstup
Booleanu při zachování původu zdrojové topologie.

## Implementační a ověřovací brány

Před implementací dopracovat vlastnictví prvků, adresování vstupů/výstupů,
výpočetní graf, rollback, viditelnost a serializaci. Změna datového modelu je
přijatelná; kompatibilitní větve pro staré dokumenty se nepožadují. Uživatel následně schválil vlastní umístění tělesa a rozšíření hierarchie
dokument → těleso → kontejner. Význam současných vazeb zůstává zachovaný.

Nejdříve vícetělesový Part a operace mezi jeho větvemi, poté Assembly.
Přesné zařazení do širší roadmapy nebylo určeno; dosavadní pořadí prací
včetně pozdějšího komplexního auditu Undo/Redo se tím automaticky nepřepisuje.

Ověřit alespoň nezávislost dvou větví, navazující Booleany, změnu zdrojového
prvku, odmítnutí cyklu, potlačení operace, OK/Cancel a obnovu po chybě výpočtu,
uložení/načtení, více nespojených výsledků a opakované výskyty v Assembly.
Pro formy testovat dutinu vytvořenou nástrojovou větví i importovaným tvarem.
Úspěch architektury nezaručuje úspěch každé geometrické operace; neplatný
nebo degenerovaný vstup musí být odmítnut bez poškození posledního výsledku.

## Technický rozsah prvního zásahu

Kontrola současného C++ kódu potvrzuje tyto závislé oblasti:

- `PartDocument` nyní ukládá jednu historii, pořadí a kurzor. Zavést stabilního
  vlastníka Body pro prvky, vlastní pořadí a kurzor tělesa a explicitně
  adresované výstupy operací mezi tělesy. Aktivní těleso musí být určeno ID.
- `DocumentSession` nyní ukládá lineární vektor vypočítaných hranic. Rozdělit
  cache podle vlastníků a závislostí tak, aby nezměněné těleso neprocházelo
  OCCT ani při úpravě jiné větve. Undo/Redo zachová celý graf a jeho cache.
- `calculate_part` nyní posílá kernelu jednu posloupnost operací. Nový plán
  oddělí samostatné historie od Booleanů a určí přesné závislosti jejich
  výstupů. Více nespojených výsledků dokumentu je agregace, ne implicitní fuse.
- Společné `resolve_constructions` a `construction_reference_geometry_for`
  dnes řeší hlavně dokumentový rámec a lokální rámec bodů Curve3D. Body
  přidá obecnou úroveň mezi dokument a prvky. Vyhodnocení musí skládat
  dokument → těleso → kontejner → vnořený bod/skicu, bez dvojí transformace.
- Uložená reference musí určit vlastníka i dostupný zdrojový rámec. Výběr,
  zvýraznění a kóty musí používat stejné převody jako explicitní výpočet,
  včetně referencí mezi tělesy. Zachovat pravidla nezávislého vlastnictví
  Assembly a stable instance paths.

**Souhlas s rozšířením umístění byl udělen v navazující diskusi.**
Schválený zásah přidává rámec tělesa a převody referencí mezi rámci;
nemění význam FRONT/TOP, Flip, odsazení ani potvrzování.

Ověřovací minimum pro tento zásah: jedno těleso v identickém počátku má
stejný výsledek jako dnešní model; posunutí a natočení tělesa se aplikuje
právě jednou; reference mezi různými rámci se řeší správně; změna tělesa B
nepočítá nezávislé A; cyklus se odmítne; Cancel obnoví původní stav; po
uložení/načtení sedí geometrie, aktivita, identity a reference. Teprve po
ověření této hranice pokračovat celým zapojením UI a Assembly.

## Rozpracovaný výpočetní základ

`HistoryOperation::body` určuje stabilní ID samostatné větve a její umístění.
Samostatný výpočetní krok Boolean má vlastní ID, typ, cílové a nástrojové
vstupní ID. Jeho geometrie se přebírá z vypočítaných vstupů; nevytváří se
pomocný primitiv ani se neopakuje historie nástroje. Operace jedné větve tvoří souvislý úsek výpočetního plánu;
opakované ID větve v nesouvislých úsecích nebo opakovaný vlastník prvku
jsou chyby vstupu. Kernel nejprve kontroluje pořadí závislostí, potom počítá
každou lokální historii samostatně existujícím výpočtem historie.

Dokumentový `BodyResult` nese `body_boundaries` (lokální historie),
`body_inputs` (umístěné samostatné výsledky) a `body_outputs` (výsledky
těles i samostatných Booleanů). Lokální snímky znovu neobsahují dokumentové cache.
Tyto snímky se ukládají přes společný viewer packet. Nezměněná větev se
přebírá podle svého otisku i po načtení, bez potřeby živé OCCT cache.

Geometrie samostatných výsledků se skládá jako compound, nikoli fuse.
Boolean zachovává původní referenční geometrii obou vstupů; nevyrábí
referenční identity výsledku z pořadí OCCT topologie. Umístění převádí
současně zobrazovací geometrii, analytické údaje ploch a směry pro úpravy hran.

Regresní test `zima_cpp_multibody_contract_tests` pokrývá objemy tří Booleanů,
navazující odečet, prázdný výsledek, odmítnutí neplatných závislostí a vlastníků,
rotaci a posunutí, zachování původních referencí a serializaci cache.
Samostatný scénář odstraní zdrojový STEP po prvním výpočtu a změní druhou
větev v nové instanci kernelu: nezměněná importovaná větev musí být převzata
z uloženého výsledku.

Výpočetní základ je připojen k vlastnictví v PartDocument i k UI Partu.
Vlastní kurzory, reference mezi rámci, rollback na hranici konkrétní větve
a zobrazení pasivního kontextu bez výpočtu jsou zapojené. Vnější seznam výpočetních hranic není náhradou
za vlastní historii tělesa; tělesové editování musí čerpat z `body_boundaries`.

### Model vlastnictví a pořadí větví

`BodyHistoryGraph` je nový dokumentový modul s vlastním pořadím těles a Booleanů,
aktivním ID, dokumentovým místem vložení a samostatným kurzorem každé větve.
Položka historie má právě jednoho vlastníka. Změny pořadí a vlastností se
nejdříve ověří v kopii; neplatná změna nepoškodí dosavadní stav.
Explicitní závislosti a cíle Booleanů musí předcházet závislé větvi.
Aktivace, kurzory a výběr kontextu nevolají OCCT.

Modul sestavuje výpočetní plán z lokálních operací dodaných kompilátorem
prvků a ukládá/načítá stav vlastnictví. Testy propojují tento plán s OCCT
odečtem dvou těles. `visible_context` nabízí před aktivním tělesem dostupné
výsledky větví a samotné aktivní těleso; již spotřebované zdroje Booleanu
nevkládá znovu do stejného zobrazení.

**PartDocument nyní ukládá tento modul přímo v `.prtz`** a před výpočtem
kontroluje úplné vlastnictví prvků i shodu pořadí. `set_body_history` promítá
pořadí větví do společného stromového pořadí; vložení při aktivním tělese
aktualizuje pouze jeho historii. `DocumentSession` uchovává graf s transakcí
a vrací skutečný lokální vstup upravovaného prvku z cache jeho vlastní větve.
Resolver nyní převádí reference mezi rámci dokumentu, tělesa a kontejneru.
Testy ověřují bod dokumentového počátku, vlastní počátek tělesa, referenci
na předchozí těleso a převod analytických ploch bez změny zdrojového snímku.
Pasivní kontext v pohledu, strom a kompletní audit ostatních referencí zbývají.
Nové vlastnosti těles musí převzít společné tlačítko `POČÁTEK` přes
`PropertiesSubWindow::ensure_origin_selection_button` a stejné zapojení
jako ostatní modelovací kontejnery.


### Nativní persistence a hranice editace

Part INI má verzi **15** (interní JSON obálka 41) a povinné pole
`Document.body_history`. Starší soubory se nepřevádějí. Nízkoúrovňové dokumentové scénáře mohou mít
prázdný graf větví s neseskupenou historií; běžné UI nového Partu vytváří
a aktivuje první těleso. Nejde o načítací adaptér starého formátu;
i tento aktuální stav zapisuje nové povinné pole.

V dokumentu s tělesy musí každý prvek, samostatná skica a konstrukce patřit
právě jedné větvi. Chybějící vlastník nebo nesouhlas pořadí se odmítne.
První prvek tělesa nesmí odečítat od výsledku jiného tělesa implicitně.
Booleany mezi tělesy jsou samostatné kroky s explicitním cílem a nástrojem.

`calculated_body_boundary` a `rollback_boundary` vybírají lokální vstup
pouze z vlastní historie. První prvek druhého tělesa proto má prázdný vstup;
druhý prvek druhého tělesa dostane jeho první výsledek a jeho původní reference.
Undo/Redo obnovuje graf i uložené výpočty společně.

Uložení nesmí nahradit poslední dokumentový výsledek předchozím snímkem jen
proto, že poslední prvek je potlačený. Výsledek dokumentu obsahuje také cache
všech větví. Nové testy ověřují jejich zachování při uložení/načtení, včetně
potlačeného posledního prvku, a následný rollback bez výpočtu OCCT.

### Změna dohody: samostatný Boolean (2026-09-07)

Definice kombinace byla odstraněna z dokumentového `BodyHistory`.
`BodyBoolean` má vlastní identitu, název, typ, cíl, nástroj a viditelnost.
Společné pořadí obsahuje ID těles i operací. Validace odmítá shodné vstupy,
budoucí vstupy a opakované použití spotřebovaného výsledku. Neplatné
vložení, editace nebo přesun jsou atomicky odmítnuty. Nativní soubor
ukládá oba registry a společné pořadí; žádný převod staršího modelu není zaveden.

Assembly později použije obdobnou samostatnou operaci ve vlastnictví
bezprostřední sestavy. Nesmí tím upravovat zdrojové dokumenty komponent.
Assembly Boolean zatím není zapojený v UI. Základní strom a vlastnosti
těles/Booleanů pro samostatně otevřený Part již jsou připojené.

### První propojení UI Partu (2026-09-07)

Panel nabízí Vytvořit těleso a Boolean. Aktivní těleso zpřístupňuje vlastní
modelovací příkazy; bez aktivního tělesa ve vícetělesovém dílu zůstávají
příkazy na úrovni těles. Kořen stromu Part/Assembly/Drawing ukazuje název
souboru. Part seskupuje existující řádky prvků podle vlastnictví, přidává
počátky těles, samostatné Booleany a dokumentový i tělesový kurzor.
Kontextová nabídka umožňuje aktivovat těleso/díl a určit Vložit před/za.

BodyPropertiesDialog používá společné SubWindow, OK/Cancel, POČÁTEK,
název, aktivitu, viditelnost a společnou sekci umístění/rotace. Boolean používá
jediný dialog pro vytvoření i editaci, s typem a dvěma dostupnými vstupy.
Nové řádky jsou do OK jen dočasné; Cancel obnoví původní strom i pohled.
První těleso v dosud neseskupeném živém dokumentu převezme jeho existující
historii; nejde o podporu načítání starých formátů.

DocumentSession::body_context_mesh čerpá pouze z uložených výsledků.
Aktivní těleso bere vlastní lokální hranici a převádí ji do dokumentu;
předchozí dostupné výsledky jsou kontext. Editace Booleanu ukazuje vstupy
před jeho hranicí. Grafická zkouška ověřuje seskupení stromu, Cancel,
editaci tělesa potvrzenou prostředním dvojklikem, Boolean a nativní uložení.

Navazující etapy níže doplnily Sketcher/modelovací interakce a výběr napříč
rámci, včetně aktivovaného Partu uvnitř Assembly.

### Přesouvání historií ve stromu

Tělesa a Booleany tvoří společnou skupinu pro tažení mezi sourozenci.
Nabídnutá hranice se ověřuje proti vstupům Booleanů, uloženým závislostem
a referencím prvků mezi tělesy (včetně skic a cílových ploch). Během tahu
se nevolá OCCT a nemění se dokument. Puštění provede jednu transakci;
Esc ji zruší. Výpočet využívá předchozí cache nezměněných větví.

Prvky se přesouvají pouze uvnitř aktivního tělesa. Aktualizuje se jeho
vlastní pořadí i projekce v dokumentu; nelze přetáhnout prvek do jiné větve
ani jako první prvek přesunout odečet. Obě úrovně mají vlastní tažitelnou
značku Vložit zde. Změna kurzoru přebírá uloženou geometrii bez výpočtu.

### Referenční umístění těles

Těleso ukládá stejnou definici `Placement` jako ostatní kontejnery: reference,
odsazení, absolutní úhly, korekce orientace i vypočtenou polohu. Dialog používá
společnou `ContainerPlacementSection` a společný výběr ve stromu/pohledu,
včetně zadání celého počátku dokumentu. Reference lze vést na dokument
a původní objekty předchozích těles; vlastní a pozdější objekty se odmítají.
Závislost umístění se zohledňuje i při přesouvání těles v historii.

Řešení probíhá po tělesech. Čerstvě vyřešené počátky se předávají následujícím
větvím a vlastní počátek se dětským prvkům nabízí v lokálním rámci tělesa.
Náhled upravuje dočasný dokument bez OCCT; OK provede výpočet a transakci.
Testy ověřují převod dokumentového počátku do otočeného tělesa, změnu
zdrojového počátku, opakované řešení, odmítnutí vlastní/cyklické reference
a uložení definice. Grafický test ověřuje zadání počátku, odsazení a potvrzení.
Otevřené vlastnosti tělesa zobrazují nenulové kóty dostupných souřadnic,
odsazení referencí a absolutních úhlů či korekcí. Editace kóty mění stejná
pole dialogu; desetinná čárka i tečka jsou přijaty a Enter nepotvrzuje celé
vlastnosti. Zamčené souřadnice se nenabízejí. Zavření dialogu odstraní jeho
dočasné kóty. Vlastnictví parametrů se rozlišuje od zákazu reference:
vlastnosti tělesa nesmějí přepsat svůj parametr při editaci kóty cizího prvku.
Grafický test kontroluje přenos odsazení i úhlové korekce a odstranění kót.
Aktivovaný Part uvnitř Assembly zůstává součástí navazující integrace.

### Sketcher v rámci tělesa

Zobrazení skic převádí jejich geometrii z rámce vlastnícího tělesa do dokumentu.
Vstupní paprsky používají opačné pořadí: scéna → výskyt Partu → těleso → skica.
Vlastník se hledá podle ID skici nebo jejího kontejneru; dočasný profil Sweepu
může použít aktivní těleso. Samotná 2D data a vyřešený lokální rámec skici
se při zobrazování nemění. Náhledy kreslení používají tentýž převod bodů;
kolmý pohled převádí normálu a osu X tělesa před umístěním výskytu v sestavě.

Grafický test ověřuje posunuté těleso otočené o 90° kolem Y i Z, přesnou
polohu existující skici, kreslení bodu myší a jeho nabídnutí na stejném
místě obrazovky, následně uložení lokálních dat. Výběr a vykreslování
nevstupují do OCCT. Úplný audit všech příkazů v podsestavách zůstává navazující prací.

Náhled vlastností skici řeší umístění vůči referencím převedeným do rámce
jejího tělesa. Nová skica používá aktivní těleso, existující skica skutečného
vlastníka. Stejný rámec používají rovina, geometrie, kóty i úchop odsazení;
umístění výskytu v Assembly se přidává až za něj. Vstupní skica zůstává
platná pro sestavení referencí i sledování jejího řádku ve stromu.

Dočasný řádek nové skici patří pod těleso na jeho lokální hranici historie.
Vícetělesový strom již nevytváří pomocný starý kurzor, který by při obnově
editace mohl být odstraněn dvakrát. Grafický test pokrývá vytvoření i editaci,
navázání na počátek tělesa, odsazení v otočeném rámci a odstranění náhledu
po Cancel.

### Editace skici tělesa ve vnořené sestavě

Scéna aktivního Partu i náhled tažení bodu nebo kóty používají uloženou
hranici historie jeho tělesa. Zobrazení ji sestaví z vypočtených dat bez
nového volání OCCT. Do horní sestavy se vloží pouze náhrada přesné cesty
aktivního výskytu; ostatní výskyty včetně opakování stejného Partu zůstávají
pasivním kontextem. V aktivním výskytu jsou dostupné také počátky těles.

Grafický test otevírá zdrojové dokumenty, aktivuje Part přes podsestavu
s vlastní rotací a ověřuje výslednou polohu skici v otočeném tělese.
Kontroluje skrytí pozdějšího tělesa pouze v aktivním výskytu, zachování
okolní sestavy během tažení bodu a uložení změněných lokálních souřadnic
do zdrojového Partu. Tento test nepokrývá celý rozsah vlastností těles
ani všechny příkazy Sketcheru v sestavě.

### Externí reference mezi tělesy

Výběr zdroje i obnovení uložené projekce převádějí původní referenční geometrii
z dokumentu do rámce cílového tělesa; teprve Sketcher ji promítá do svého
lokálního 2D rámce. Převod zachovává vlastníka, sémantický klíč a cestu výskytu
a nemění sdílenou zdrojovou geometrii. Stejný převod používá obnova referencí
z jiného dílu v kontextu Assembly po převodu do cílového Partu.

Skica nabízí původní objekty předchozích těles a prvky před svou hranicí
vlastního tělesa. Pozdější objekty ani výsledná topologie Booleanu nejsou
zdrojem. Při řešení se ukládají také mezitělesové závislosti externích skic.
Změna rámce tělesa je součástí kontroly ustálení výpočtu; explicitní
Regenerovat uloží i změněné rámce, skici a závislosti.

Testy ověřují první grafický výběr bodu z předchozího tělesa do otočené skici,
odmítnutí pozdějšího zdroje, uložení a regeneraci. Výpočetní test navíc mění
umístění cílového tělesa i zdrojový bod a kontroluje novou projekci bez
změny identity reference nebo původní geometrie.

### Rozšíření ověřování: řetězce Booleanů, STEP a ořez skici

Výpočetní regrese ověřuje změnu umístění nástrojového tělesa před dvěma
navazujícími Booleany po obnovení uložených výsledků do nového kernelu.
Kontroluje objem mezivýsledku i konečného rozdílu, následnou změnu posledního
kroku na součet a průnik a zachování nezměněné nezávislé větve.

Scénář formy odečítá importovaný STEP nástroj z polotovaru. Po odstranění
zdrojového STEP a obnovení uložených dat mění rozměr polotovaru; kontroluje
nový objem dutiny a zachování přesných uložených dat nástroje. Změna
polotovaru nesmí vyvolat nový import nezměněného nástroje.

Ořez Sketcheru nyní převádí nabízené části křivek přes rámec tělesa stejně
jako ostatní geometrii skici, jak v Partu, tak v aktivním výskytu sestavy.
Dříve tyto části používaly jen lokální rámec skici a míjely její skutečnou
polohu v umístěném tělese. Grafická regrese zahrnuje rovinu kandidátů,
výběr společným pickerem, odstranění jednoho úseku v náhledu a Escape
bez uložení rozpracovaného ořezu do zdrojového Partu.

Ve Sketcheru otevřeném v sestavě již vypnutí běžné akce Výběr nevypíná
výběrový kontrakt aktivního příkazu. Příkaz si nadále nabízí vlastní typy
kandidátů, stejně jako ve Sketcheru samostatného Partu. Regrese ořezu
ověřuje tuto cestu skutečným kliknutím, nejen kontrolou vykreslených dat.

### Založení Partu a aktivace ve stromu

Nový Part vytvořený ze startovací šablony dostane vlastní nové „Těleso 1“
a rovnou je aktivuje. Šablona neukládá identitu tělesa; každý nový dokument
vytváří nové ID. Modelovací příkazy a lokální kurzor historie jsou dostupné
hned po potvrzení dialogu Nový dokument.

Kořen stromu s názvem souboru má kontextovou akci „Udělat aktivní“, která
aktivuje úroveň dokumentu. Stejná akce na tělese aktivuje jeho historii.
Aktivní úroveň je zelená. Po zavření dialogu Nový dokument se znovu obnoví
strom a dostupnost příkazů: stav z doby otevřeného dialogu již nezanechá
Vytvořit těleso zašedlé. Boolean nadále vyžaduje dva dostupné výsledky před
místem vložení; s jediným tělesem tedy zůstává oprávněně nedostupný.

Grafický test používá skutečné kontextové menu a tlačítka pravého panelu.
Ověřuje aktivaci dokumentu i obou těles, dostupnost tvorby tělesa po založení
Partu a po dokončení Booleanu a automaticky aktivované první těleso.

Při shodných počátcích se zvýrazněná osa vykresluje až po ostatních osách,
aby ji osa jiného vlastníka nepřekryla. Výběr nadále používá přesnou identitu
a cestu výskytu; pořadí kreslení nemění seznam kandidátů.

Dočasný náhled nového konstrukčního prvku je zařazen do historie aktivního
tělesa už v pracovní kopii dokumentu. Řešení náhledu proto zpracuje také
nový prvek a jeho živé kóty; potvrzená historie zůstává nezměněná do OK.

Grafická kontrola nového výchozího tělesa zahrnuje také konstrukční bod,
editaci jeho kóty a navazující modelování. Neplatné obrazové souřadnice kóty
(například při nepromítnutelné poloze během změny pohledu) se odmítají shodně
při vykreslování, výběru a určování polohy jejího editoru; nesmějí vyvolat
normalizaci nekonečného vektoru ani pád Qt.

Řešení historie těles může nahradit pracovní dokument. Náhled vlastního
profilu Extrusion/Revolution proto po řešení znovu vyhledá skicu podle ID;
nesmí používat původní iterátor. Při OK se příslušnost rozpracovaného profilu
ověří ještě před nahrazením dokumentu. Tím se odstraňují neplatné přístupy
k nahrazeným datům při náhledu a potvrzení vytažení.

Vlastnosti tělesa automaticky aktivují první volnou polohovou referenci.
Kliknutí na počátek dokumentu proto bez předchozího klikání do tabulky
vyplní všechny tři polohové roviny, stejně jako u kontejnerů. Dialog používá
`document_precision.decimal_places` pro všechny číselné prvky sdíleného
umístění včetně odsazení a úhlů, nikoli pevnou přesnost devíti míst.
Grafická regrese ověřuje nové i editované těleso a přesnost dvou míst také
na polích vytvořených po výběru referencí.

Příkaz Boolean je v pravém panelu zobrazen pouze na úrovni aktivního Partu
(aktivace názvu souboru ve stromu). Při aktivním tělese se skrývá a jeho
akce je vypnutá. Grafický test před tvorbou Booleanu skutečně aktivuje kořen
dokumentu a kontroluje nepřítomnost příkazu během práce v tělese.

Vlastnosti Booleanu nemají tlačítko Počátek: operace nemá vlastní umístění
a pracuje s již umístěnými vstupy. Tlačítko zůstává ve vlastnostech tělesa,
které skutečně podporují umístění a výběr referencí.

Pomocný kontext počátků již pro Extrusion nevytváří obecnou osu ve směru
normály skici. Vytažení obdélníku tak při zadávání dalšího kontejneru
nenabízí neexistující osu prvku. Geometrické osy publikované vypočtenými
profily zůstávají zachované; osy lokálního počátku jsou nadále součástí
samotného počátku. Grafická regrese kontroluje obdélníkové vytažení při
otevření vlastností následujícího kontejneru.

Návrat z vlastní skici rozpracovaného Extrusion/Revolution používá hranici
historie před tímto prvkem stejně jako běžná editace. Definice vytažení už
v pracovní historii může existovat bez nové vypočtené hranice; zobrazení
nesmí požadovat její dosud neexistující výsledek a tím skrýt předchozí solid.
Grafická regrese ověřuje zachování původního kvádru po návratu ze skicáře
a během změny délky před OK.

Náhled nového Sweep/Loftu a jeho bodů registruje rozpracovaný kontejner
v historii aktivního tělesa už v pracovní kopii. Stejné pravidlo platí pro
novou 3D křivku při otevření vlastností bodu. Pomocná dráha Sweep/Loftu
slouží pouze k zobrazení v samostatném lokálním dokumentu; nesmí zastínit
vlastnictví skutečné dráhy v historii tělesa. Reference bodu se řeší v rámci
tělesa a dráhy, výsledný náhled se umístí zpět do dokumentu.
Grafická regrese skutečným kliknutím vybere vrchol kvádru pro 3D křivku
i Sweep/Loft v posunutém tělese, ověří uloženou identitu reference,
souřadnice potvrzeného bodu a nezměněný dokument po zrušení kontejneru.

Příkazy Vytvořit těleso a Boolean jsou dostupné pouze při aktivním kořenu
Partu. Aktivní těleso nabízí vlastní modelovací příkazy. Strom zobrazuje
jediný kurzor Vložit zde: uvnitř aktivního tělesa, nebo mezi tělesy při
aktivním dokumentu. Otevřené vlastnosti nadále nahrazují kurzor rozpracovanou
položkou.

Polohové reference používají ortonormální řešení soustavy s projekcí původní
polohy; volné souřadnice zůstávají zachované. Tím se odstraňuje numerický
posun podél šikmé plochy při opakovaném řešení dřívějšími váženými normálními
rovnicemi. Již splněná poloha se zachová beze změny i pro kontrolu ustálení
regenerace. Regresní test opakuje řešení šikmé plochy s odsazením stokrát.
To řeší také selhání vytvoření dalšího tělesa, pokud regeneraci blokoval
stávající kontejner navázaný na šikmou koncovou plochu Sweep/Loftu.

### Viditelnost a reference počátků

Při aktivním kořeni Partu se zobrazuje počátek dokumentu, při aktivním tělese
jen počátek tohoto tělesa. Počátek dokumentu je vizuálně o 25 % větší než
počátek tělesa; rozměry lokálních počátků kontejnerů se nemění.
Vlastnosti tělesa mohou použít počátek dokumentu. Kontejnery uvnitř tělesa jej
ve View ani přes strom nepřijímají; používají počátek vlastního tělesa nebo
explicitní reference. Tlačítko Počátek umožňuje zobrazit také počátky jiných
těles a jejich kontejnerů. Změny se týkají prezentace a nabídky referencí,
nikoli výpočtu souřadných soustav nebo existujících uložených vazeb.

### Opravy zobrazení a umístění (2026-09-07)

- Náhledy vlastností používají referenční geometrii v souřadném systému
  vlastnícího tělesa také pro primitiva, konstrukce a všechny varianty Sweepu.
  Výběr celého počátku tělesa uzamkne vyřešené souřadnice a umožní odsazení rovin.
- Ve skicáři se automaticky zobrazuje počátek vlastnícího kontejneru místo
  počátku aktivního tělesa. Jde o zobrazení; uložené reference se nemění.
- Náhled počátku Vytažení a Rotace nepřidává samostatnou azurovou pomocnou osu.

### Budoucí přesun těles mezi Party

Diskuse 2026-09-07: těleso by bylo možné přesunout do jiného Partu včetně
historie a lokálního počátku, se zachováním polohy a převodem potřebných
referencí. Přesun sám neznamená sjednocení geometrie; Boolean je samostatná
volba. Případné odstranění prázdného zdrojového Partu vyžaduje vyřešení
odkazů z dalších dokumentů. Jde o návrh, nikoli aktuálně dostupný příkaz.

### Ověření aktuálního stavu

Sestavení Debug, všech 14 testů modulů, samostatné testy oken a celkový
GUI startup kontrakt prošly. Cílené GUI scénáře zahrnují odsazení vůči
otočenému tělesu, počátek kontejneru ve skicáři, reference mezi tělesy,
aktivaci ve vnořené Assembly a kopii modelu s výkresem. Nové regresní
scénáře ověřují také horní a dolní tečné spojení pevných kružnic s C + T.
Viz také [kopie dokumentu a výkresu](DOCUMENT_COPY.md),
[osy a koncové plochy Sweep/Loftu](3D_CURVE_AND_SWEEP.md)
a [chování skicáře](SKETCHER.md).
