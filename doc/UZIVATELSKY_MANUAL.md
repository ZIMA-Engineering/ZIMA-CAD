# ZIMA-CAD – uživatelský manuál

## Kompatibilita dokumentů

Každý nativní typ dokumentu má vlastní verzi: C++ Part `.prtz` nyní používá
`format_version` 19, Assembly `.asmz` verzi 5 a Drawing `.drwz` verzi 2.
ZIMA-CAD během vývoje nepoužívá tiché fallbacky pro starší experimentální
formáty: nepodporovanou verzi odmítne. Budoucí nekompatibilní změna formátu
musí zvýšit příslušnou verzi a případně nabídnout samostatnou řízenou migraci.

## Dialogy Otevřít a Uložit

Společné souborové dialogy ZIMA-CAD zobrazují soubory podle právě zvoleného
typu souboru. Například filtr dílů nabízí `.prtz`, filtr sestav `.asmz` a filtr
výkresů `.drwz`; soubory, které aktivnímu filtru neodpovídají, jsou skryté.
Adresáře zůstávají dostupné pro navigaci a jsou zobrazené před soubory. Obě
skupiny jsou ve výchozím stavu seřazené podle názvu vzestupně.

Adresář s rezervovaným názvem `0000-index` se v těchto dialozích nezobrazuje. Jde
o aplikačně spravovaný obsah používaný mimo běžné ruční otevírání a ukládání
dokumentů. Porovnání názvu nerozlišuje velikost písmen, takže stejné pravidlo
platí například také pro `0000-INDEX`. Skrytí v dialogu adresář ani jeho obsah
z disku nemaže.

## Výchozí šablona dílu

Nový dokument typu **Díl** se nezakládá jako pevně naprogramovaný prázdný
model. Načte se ze šablony `config/templates/start_part.prtz`, která je
zvolená v hlavní konfiguraci:

```ini
[Templates]
Part = start_part.prtz
```

Nový díl převezme geometrii, materiál, uživatelské parametry a relace ze
šablony, ale dostane nové ID dokumentu a název zadaný v dialogu nového
souboru. Projektový `config.ini` může sekci `[Templates]` přepsat a používat
vlastní startovací díl. Relativní název se hledá v adresáři určeném hodnotou
`Paths/Templates`.

## Parametry, relace a hmotnost

**Nástroje → Parametry** obsahují pouze uložené výsledné hodnoty. Vzorce se
v tabulce parametrů nezobrazují. **Nástroje → Relace** patří modelu dílu nebo
sestavy a přiřazuje bezpečný výraz jednomu cílovému parametru. Výchozí
startovací díl obsahuje:

```text
mass = model.mass
```

`model.mass` je hmotnost v kilogramech vypočítaná ze skutečného objemu
výsledného OCCT tělesa a z `MASS_DENSITY` přiřazeného materiálu. Podporované
jednotky hustoty jsou `kg/mm^3`, `kg/m^3`, `g/cm^3` a `lb/in^3`. Výsledek se
zapíše jako obyčejný text do parametru `mass`; razítko ani další uživatel
parametru nemusí znát jeho vzorec.

Výchozí klíče v prvním sloupci tabulky Parametry jsou stabilní anglické
identifikátory, například `name`, `standard`, `drawn_by`, `revision` a `mass`.
Viditelné názvy a hodnoty zůstávají jazykové. Český název `Název` a anglický
název `Name` proto odkazují na stejný klíč `name`, ale mohou mít různé hodnoty.
Nová sestava používá stejnou sadu parametrů. V aktuálním datovém modelu se
zakládá generátorem, nikoliv samostatným souborem `start_assembly.asmz`, a
generátor jí nastaví také relaci `mass = model.mass`.

Výrazy používají omezený pythonovský zápis, nikoliv spustitelný Python.
Podporují čísla, odkazy na dříve dostupné parametry, operátory
`+ - * / ** %`, porovnání, podmíněný výraz a funkce `abs`, `min`, `max`,
`round`, `sqrt`, `sin`, `cos` a `tan`. Systémové hodnoty první verze jsou
`model.volume`, `model.area`, `model.mass` a `material.density`. Importy,
přístup k souborům a volání jiných funkcí jsou zakázané. Více relací se
vyhodnocuje shora dolů a pozdější relace smí použít výsledek předchozí.

Relace se neukládají do `.drwz`. Ve výkresu otevře **Nástroje → Parametry**
parametry jeho zdrojového `.prtz` nebo `.asmz`, změny uloží do zdrojového
modelu a následně obnoví geometrii, varianty a razítko výkresu.

## Ovládání 3D pohledu

| Ovládání | Funkce |
| --- | --- |
| Prostřední tlačítko + pohyb myši | Otáčení pohledu |
| Prostřední + pravé tlačítko + pohyb myši | Posun pohledu |
| Kolečko dopředu | Oddálení |
| Kolečko dozadu | Přiblížení |
| Jeden klik prostředním tlačítkem | Použít změny v aktivním dialogu, pokud nabízí tlačítko Použít |
| Dvojklik prostředním tlačítkem | Potvrzení aktivního dialogu tlačítkem OK |
| F2 | Zavřít aktivní dokumentový tab |

Po posunu kombinací prostředního a pravého tlačítka se kontextové menu
neotevře.

Příkazy **Obnovit pohled** a základní pohledy (izometrický, přední, zadní,
levý, pravý, horní a dolní) používají plynulý animovaný přechod kamery.
Směr kolečka je shodný v Partu, sestavě i ve výkresu. Každý dokumentový tab si
uchovává vlastní natočení, posun a přiblížení; přepnutí tabu pohled automaticky
nepřizpůsobuje ani neresetuje.

## Pohled kolmo

1. V horní liště pohledu aktivujte tlačítko **Pohled kolmo**.
2. Ve 3D pohledu vyberte rovinnou plochu solidu nebo referenční rovinu.
3. Kamera se natočí kolmo k vybrané geometrii a příkaz se automaticky ukončí.

Příkaz lze před výběrem zrušit opětovným kliknutím na tlačítko nebo klávesou
`Esc`.

## Výběr referencí ve Vlastnostech

- Výběrová funkce prochází všechny podporované objekty pod kurzorem, včetně
  objektů uvnitř kontejnerů a zakrytých ploch. Pravým tlačítkem lze mezi
  kandidáty cyklovat; stavový řádek popisuje právě nabízený prvek a view jej
  současně zvýrazní oranžově.
- Filtr **Všechny objekty z kontejnerů** zahrne plochy, hrany, body i základní
  objekty kontejnerů. Při výběru geometrické reference se celý solid ani celý
  kontejner oranžově nezvýrazňuje — zvýrazní se pouze nabízená plocha, hrana
  nebo bod.
- V běžném režimu lze ve view vybrat kontejner solidu, bodu, osy nebo roviny.
  Systémový Počátek dokumentu se tímto způsobem nevybírá.
- Vybrané plochy, hrany, body, osy a roviny zůstávají ve view azurově
  zvýrazněné.
- Geometrie právě hledaná výběrovou funkcí má oranžový obrys stejně jako při
  běžném výběru modelu.
- Referenční plocha je označena pouze azurovými obvodovými hranami, nikoli
  barevnou výplní.
- Kliknutím na název reference v okně Vlastnosti lze její zvýraznění vypnout
  nebo znovu zapnout. Reference přitom zůstává součástí vazby.
- Skutečné odstranění reference provádí červené tlačítko **×** vlevo na jejím
  řádku.
- Každá nová reference se nejprve ověří v dočasném řešení. Konfliktní,
  přeurčující nebo nadbytečná reference se do seznamu nepřidá a původní
  geometrie zůstane beze změny.
- Při dosažení 0 stupňů volnosti je objekt plně určený. Geometrii lze stále
  vybrat, ale jako další reference se přijme až po odebrání některé existující
  vazby.
- Klik na zobrazené Body v místě importovaného STEP lze mapovat zpět na jeho
  původní kontejner a označit jej ve stromu. Při zadávání polohy nebo orientace
  Protrusion, Revolve a dalších kontejnerů se plochy, hrany a vrcholy berou
  přímo z původního STEP solidu. Pickerem zjištěný index se předává přímo do
  reference, takže rychle načtený BREP nepotřebuje předem naplněnou globální
  cache zdrojové topologie.
- Vrchol aktuálního výsledného tělesa lze použít jako polohovou referenci bodu
  nebo kontejneru. Ukládá se stabilní `VertexRef` a odebere všechny tři
  posuvné stupně volnosti. Když vrchol zmizí, reference se označí jako
  chybějící, zachová poslední polohu a nepřeskočí na jiný vrchol podle pořadí.
- Každý kontejner vlastní úplný lokální souřadný systém a ve Vlastnostech má
  šest stupňů volnosti `X/Y/Z + RX/RY/RZ`, včetně kontejneru bodu, osy a roviny.
  Vrchol může určit tři posuvné DOF; orientaci kontejneru následně určí nejvýše
  dvě nezávislé orientační reference. Po dosažení 0 DOF se další nadbytečné,
  duplicitní nebo konfliktní reference nepřidají.
- Tabulka polohy přijme nejvýše tři konstrukční reference pro určení
  počátku. Orientační reference mají samostatné dva sloty a do této tabulky
  se nepřidávají.
- Kontejner **Osa** lze umístit výběrem kruhové hrany nebo válcové plochy
  původního solidu. První reference určí středovou přímku: použije uložený
  střed kružnice a její normálu, respektive uloženou osu válce. Protože tato
  reference ještě neurčuje polohu počátku podél osy, výběr se automaticky
  přepne na plochy. Následující rovinná plocha umístí počátek osy do svého
  průsečíku se středovou přímkou. Poté se obnoví běžný výběrový filtr.
- Kuželová plocha ani obecné zaoblení se pro vytvoření středové osy
  nepovažují za válcovou plochu. Zaoblení je nadále možné obejít výběrem
  odpovídající původní kruhové hrany, pokud taková stabilní hrana existuje.
- Pole `RX/RY/RZ` jsou úhlové korekce aplikované na základní rámec určený
  referencemi. Zůstávají proto editovatelná i po úplném určení jeho orientace;
  ukazatel DOF nadále popisuje určenost základního rámce.
- První orientační slot přijímá rovinnou plochu nebo rovinu a roli
  **FRONT/BACK**. Druhý slot přijímá
  nezávislou rovinu, plochu, hranu nebo osu a roli
  **TOP/BOTTOM/LEFT/RIGHT**. Rovnoběžná druhá reference se odmítne.
- Při zadání rovinné plochy jako reference Sketch, Protrusion nebo Revolve se
  první orientační slot automaticky nastaví na **BACK** podle vnější normály
  solidu. Čelní pohled proto míří na pracovní plochu z vnější strany. Pozdější
  ruční změna na **FRONT** zůstává zachována až do nového zadání nebo nahrazení
  plošné reference.
- Oba orientační sloty jsou nepovinné. Bez nich se použije vlastní lokální rámec
  kontejneru a jeho korekce `RX/RY/RZ`; jde o plnohodnotné a podporované
  umístění, nikoliv o chybějící definici.
- **Odsazení pracovní roviny** je samostatná hodnota a nepatří k referenci
  FRONT. U roviny, skici, Protrusion a Revolve posune pouze pracovní nebo
  profilovou rovinu v lokálním směru její normály. Neposouvá počátek
  kontejneru ani jeho polohové reference.
- Každá změna `X/Y/Z`, posunutí polohové reference, `RX/RY/RZ` nebo odsazení
  pracovní roviny okamžitě znovu sestaví azurový drát náhledu. Jde pouze o
  viewerový náhled z rozepsaných hodnot; definice kontejneru a historie se
  uloží a přepočítají až tlačítkem **OK**.
- Externí skica předává Protrusion nebo Revolve pouze svou parametrickou
  2D geometrii. Světovou polohu, orientaci i odsazení profilu určuje cílový
  kontejner.

## Běžný výběr ve 3D pohledu

Bez aktivního příkazu nabízí pohled pouze objekty, se kterými lze běžně
pracovat, nikoli topologii výsledného tělesa:

- v Partu je kandidátem celý kontejner historie;
- v sestavě je kandidátem nejnižší konkrétní výskyt Partu pod kurzorem, také
  uvnitř libovolně vnořených sestav;
- systémový Počátek, jeho osy a roviny nejsou samostatnou identitou komponenty.

Oranžový hover, potvrzení levým tlačítkem a cyklování pravým tlačítkem používají
jediný společný seřazený seznam kandidátů. Pravé tlačítko před potvrzením pouze
přepne na dalšího kandidáta. Levé tlačítko potvrdí přesně nabízený objekt
azurovým obrysem a současně označí tutéž položku ve stromu.

Po potvrzení již pravé tlačítko necykluje, ale otevře kontextové menu vybraného
objektu. U vnořené komponenty obsahuje příkaz **Vybrat rodiče**. Každé jeho
použití posune výběr právě o jednu úroveň výše; opakováním lze projít od Partu
přes všechny vnořené sestavy až k nejvyšší vložené komponentě. Strom a pohled
zůstávají synchronizované a každé opakované vložení stejného zdrojového souboru
se rozlišuje vlastní cestou instance.

Při aktivním výběrovém příkazu platí jeho užší filtr. Pravé tlačítko nadále
cykluje kandidáty tohoto příkazu a běžné kontextové menu se otevře až po jeho
dokončení nebo zrušení.

## Živé úpravy ve Vlastnostech

- Při vytváření nového kontejneru se ve 3D pohledu ihned zobrazí jeho
  lokální počátek, barevné osy X/Y/Z a roviny XY/YZ/XZ. Náhled se živě
  posouvá a otáčí podle zvolených referencí a hodnot `X/Y/Z + RX/RY/RZ`.
  Po prvním potvrzení jej nahradí skutečný počátek vytvořeného kontejneru.
- Změna číselné hodnoty, včetně použití šipek `+` a `−`, se okamžitě projeví
  ve 3D pohledu.
- `Enter` v číselném poli dokončí zadání hodnoty, ale okno nezavře.
- **Použít** potvrdí aktuální stav a ponechá okno otevřené.
- Jeden klik prostředním tlačítkem odpovídá tlačítku **Použít**. Pravidlo
  platí globálně také v dialozích nového souboru, materiálu a nastavení.
- **OK** potvrdí změny a zavře okno.
- **Zrušit** obnoví stav při otevření okna nebo stav naposledy potvrzený
  tlačítkem **Použít**.
- Dvojklik prostředním tlačítkem odpovídá tlačítku **OK** v aktivním dialogu.

U Protrusion a Revolve má **Použít** zvláštní rychlý režim: vytvoří samostatný
azurový náhled prvku bez operace Fuse/Cut a ponechá Vlastnosti otevřené. Náhled
zůstává azurový také při změně směru, délky nebo úhlu, jednostranného,
oboustranného či symetrického rozsahu a operace. Teprve **OK** zavře dialog a
provede skutečný booleovský výpočet výsledného tělesa.

### Rollback při editaci kontejneru

Otevření **Vlastností** libovolného kontejneru v historii dočasně vrátí model
těsně před tento kontejner. Nejde jen o Zaoblení: stejné pravidlo platí pro
všechny editovatelné kontejnery. Ve 3D pohledu proto není výsledek upravovaného
kontejneru ani výsledky pozdějších operací. Zobrazuje a vybírá se skutečná
vstupní geometrie daného kroku, nikoliv finální těleso z cache.

Upravovaný kontejner ze stromu nezmizí. Zůstává na svém místě, je zeleně
označený jako právě editovaný a za ním je značka **Vložit zde**. Následující
kontejnery jsou po dobu úpravy viditelně potlačené:

```text
Těleso → předchozí kontejnery → zelený editovaný kontejner
        → Vložit zde → potlačené následující kontejnery
```

**Použít** přepočítá tentýž kontejner, zobrazí jeho dočasný náhled a ponechá
rollback i okno aktivní. Nevytváří kopii kontejneru a opakovaný náhled se vždy
počítá ze stejné vstupní geometrie, nikoliv z předchozího náhledu. Výběrové
zvýraznění zůstává při otáčení pohledu zachované.

**OK** zahrnuje použití změn, ukončí editaci, vrátí **Vložit zde** na konec
stromu a znovu vyhodnotí následující operace. **Zrušit** obnoví stav před
otevřením nebo stav posledního úspěšného použití. V obou případech view odstraní
dočasný náhled, editační kóty a pomocná zvýraznění, obnoví běžný režim výběru a
zobrazí celé výsledné těleso. Natočení a přiblížení kamery se přitom nemění.

### Operace solidu

V horní části Vlastností solidu je výrazný přepínač operace:

- zelené **+ Přičíst** přidává objem,
- červené **− Odečíst** odebírá objem.

Změna operace se okamžitě projeví ve view. Stejná operace je nadále dostupná
také v kontextovém menu objektu v tree. Obě místa pracují se společným
nastavením solidu.

### Rozsah vysunutí Až k ploše

Protrusion podporuje číselnou délku, **Až k ploše** a pro odečítání také
**Skrz vše**. **Až k ploše** přijímá rovinnou plochu, datumovou rovinu,
válcovou, kulovou, kuželovou i obecnou NURBS/B-spline plochu. Směr vysunutí
zůstává kolmý ke skicové rovině. U zakřiveného cíle musí každý vzorkovaný
paprsek profilu mít jednoznačný první kladný průsečík; smíšené, chybějící nebo
nejednoznačné zásahy se odmítnou.

Azurový drátový náhled se počítá pouze z persistovaných bodů a vzorkovaných
křivek skici a z uloženého analytického popisu nebo triangulace cílové plochy.
Otevření Vlastností, změna parametru ani výběr reference proto nespouští OCCT.
Rovina, koule, válec a kužel používají společný analytický řešič; obecná plocha
se v náhledu protíná s persistovanými trojúhelníky svého posledního výpočtu.

Při **OK** nebo explicitní regeneraci vytvoří OCCT vysunutí s
potřebným přesahem a ořízne je objemem odpovídajícím uložené analytické ploše.
U obecné plochy v tomto explicitním výpočtu vyřeší stabilní referenci a použije
přesnou OCCT plochu; triangulace slouží pouze náhledu. U roviny se používá čistá
nekonečná podpůrná rovina odvozená z vybrané plochy; hranice konečné vybrané
plochy se jako hranice ořezu nepoužívají.

Operace se neprovede, pokud je směr vysunutí rovnoběžný s cílem, některá část
profilu cílovou plochu mine, profil cílovou plochu kříží nebo cílovou referenci
nelze vyřešit. Kontejner v takovém případě zůstane označený jako chybný místo
vytvoření zdánlivě platného tělesa s jiným koncem.

Po dvojkliku na Protrusion nebo Revolve se jejich prostorové kóty zobrazují na
skutečné profilové rovině. Je-li zadané odsazení pracovní roviny, počátek
lineární kóty Protrusion i úhlová kóta Revolve toto odsazení respektují.

Uzavřený profil vytváří běžný solid. Otevřený profil automaticky nabídne režim
**Thin**, který vytvoří tenkostěnný solid s tloušťkou na první stranu, druhou
stranu nebo symetricky. U uzavřené smyčky lze mezi **Těleso** a **Thin** zvolit
ručně: například uzavřený obdélník vytvoří buď plný kvádr, nebo
dutou obdélníkovou stěnu mezi dvěma odsazenými smyčkami. Profil může obsahovat
úsečky, kruhové oblouky, elipsy, eliptické oblouky, spline a rádiusy vytvořené
ve společném bodu dvou úseček. Běžný Part nevytváří samostatná plošná tělesa;
ta budou patřit do budoucího plošného modeláře.

Azurový Thin náhled zobrazuje jednotlivé hrany obou odsazených obrysů,
podélné hrany v každém rohu a u otevřeného profilu také oba koncové uzávěry.
Po změně tloušťky nebo volby **První strana / Druhá strana / Symetricky** se
drát znovu sestaví a nahradí předchozí overlay; nesmí pouze probliknout, zmizet
ani se nahradit šedým mesh výsledného tělesa. Implementace podporuje jeden
souvislý nevětvený řetězec nebo jednu uzavřenou smyčku z podporovaných křivek.
Rádius nejprve zkrátí obě sousední úsečky a mezi jejich tečnými body vloží
skutečný oblouk. Větvení, několik oddělených řetězců, kolaps příliš velkého
odsazení a samoprotínající se paralelní obrys se odmítnou s chybou profilu Thin.

Protrusion zobrazuje po celou dobu aktivních Vlastností fialový manipulátor a
žlutou editovatelnou kótu. Tažení je plynulé, zobrazená hodnota se přichytává po
1 mm. Záporná hodnota zadaná do prostorové kóty délky nebo úhlu obrátí směr
prvku, ale uložená a zobrazená velikost zůstane kladná. U oboustranného rozsahu
**Otočit** vymění Start/End i jejich dvě hodnoty; symetrický rozsah hodnoty
sjednotí, ale identity obou koncových ploch zachová.

Skica, náhled, výsledné těleso, fialový manipulátor a žlutá kóta používají
stejný fyzický rámec i při záporném odsazení pracovní roviny a korekcích
`RX/RY/RZ`. Skica se proto při otevření kreslí přímo v odsazené a natočené
rovině, nikoliv dočasně v počátku kontejneru.

### Zaoblení hrany

Příkaz **Zaoblení** rovnou otevře jediné okno **Vlastnosti zaoblení**, které se
používá také při pozdější editaci. Ve 3D pohledu zvolte jednu nebo více
podporovaných hran. `Ctrl` přidává a odebírá hrany ze společného výběru; hranu
lze odebrat také ze seznamu v okně. Všechny uvedené hrany používají jeden
společný poloměr.

Souvislá tečná trasa může být po předchozích booleovských operacích tvořena
několika samostatnými OCCT hranami. V seznamu je zobrazena jako nadřazená trasa
s jednotlivými hranami pod ní. Označení dítěte a odebrání smaže pouze tuto
hranu; označení nadřazené trasy odstraní celou trasu. **Obnovit kontinuální
trasu** znovu dopočítá její aktuální členy.

**Použít** spočítá dočasný náhled a nechá okno otevřené. Náhled nevytváří další
kontejner a každé další použití se znovu počítá z původního ostrého tělesa.
**OK** provede stejnou kontrolu, vloží právě jeden prvek Zaoblení do historie a
okno zavře. Krátký klik prostředním tlačítkem odpovídá **Použít**, dvojklik
prostředním tlačítkem odpovídá **OK**; tažení prostředním tlačítkem pouze otáčí
pohled.

Při editaci se historie dočasně vrátí těsně před upravované Zaoblení, takže je
ve view dostupná původní ostrá hrana. Upravovaný kontejner zůstává ve stromu
viditelný a je označen zeleně, zatímco následující prvky jsou po dobu úpravy
potlačené. **Použít** upraví existující prvek bez založení kopie a **OK** změnu
potvrdí a vrátí vyhodnocení na konec historie.

Vybrané zaoblení se ve view zvýrazňuje pouze svými hraničními hranami, nikoliv
celou plochou nebo celým dílem. Dvojklik levým tlačítkem zobrazí editovatelnou
kótu poloměru. Okno vlastností se otevírá přes **Vlastnosti** v kontextovém
menu vybraného prvku. Hrany se ukládají stabilními sémantickými referencemi,
nikoliv pořadovými čísly geometrického jádra. Neproveditelný poloměr nebo
nekompatibilní kombinace hran zachová poslední platné těleso a okno zůstane
otevřené.

Pouhé najetí nad plochu vytvořenou Zaoblením zobrazí oranžově jen její
persistované hraniční hrany. Drát celého výsledného tělesa ani původní ostrá
hrana se v tomto režimu nepřekreslují. Přechod na jiný kandidát předchozí
oranžové zvýraznění vždy odstraní.

U kruhového zaoblení se kóta poloměru zobrazí ve stabilním normálovém řezu mezi
oběma krajními kružnicemi zaoblené plochy. Hrot leží na oblouku mezi nimi a
směřuje ke středu poloměru. Poloha řezu se při změně hodnoty ani překreslení
náhledu nepřehodí na jinou část kružnice.

### Sražení hrany

**Sražení** používá stejné uspořádání a ovládání jako Zaoblení, ale jde o
samostatný příkaz a samostatný typ kontejneru. **Vlastnosti zaoblení** obsahují
jen poloměr a **Vlastnosti sražení** jen vzdálenost sražení; žádné z těchto oken
nenabízí přepnutí na druhou operaci. První verze Sražení je symetrická: stejná
vzdálenost se měří na obou sousedních plochách.

Výběr více hran pomocí `Ctrl`, seznam hran, **Použít**, **OK**, prostřední
tlačítko, rollback stromu a chování náhledu jsou shodné se Zaoblením. Změna
hodnoty se vztahuje na všechny vybrané hrany. Existující Zaoblení ani Sražení
se ve Vlastnostech na druhý typ nepřevádí.

Kliknutí ve view zvýrazní pouze hraniční hrany sražené plochy. Dvojklik zobrazí
editovatelnou lineární kótu vzdálenosti a pravé tlačítko → **Vlastnosti** otevře
společné okno. Neproveditelná vzdálenost nebo kombinace hran zachová poslední
platné těleso.

Stejně jako u Zaoblení zvýrazňuje najetí pouze persistované hraniční hrany
sražené plochy. Dvojklik zobrazuje hodnotu sražení v kompaktním tvaru
`5x45°`. U kruhového sražení leží kóta ve stabilním normálovém řezu a obě její
vynášecí čáry začínají přímo na krajních kružnicích sražené plochy.

## Režim skici

Ve Vlastnostech skici tlačítko **SKETCH** potvrdí její umístění a otevře
samostatný režim kreslení. Tlačítko je dostupné po výběru roviny nebo rovinné
plochy, která určuje orientaci skici.

Po vstupu se pohled nastaví kolmo ke skice. Lokální osy X/Y jsou zobrazené
hnědou tenkou čárkovanou čarou přes celé view. Profilová geometrie je modrá,
body jsou žluté a konstrukční čáry jsou žluté a čerchované jako osy.
Také u skici na šikmé rovině kamera respektuje celý lokální rámec: normálu
roviny i natočení její osy X. Stejný převod kamery používá příkaz
**Nastavit orientaci**.
Po dokončení nebo opuštění skici se kamera plynule vrátí do polohy, kterou
měla před vstupem do skicáře.

První přiblížení běžné modelové skici vychází z logického DPI monitoru a míří
přibližně na poměr 1 mm modelu ku 1 mm na obrazovce. Nejde o metrologicky
přesné měřidlo, ale nově kreslená úsečka proto vizuálně odpovídá své skutečné
délce podstatně lépe. Editor rámečku nebo razítka je výjimka: při otevření
zobrazí celý list, aby zůstala dostupná celá šablona.

Základní nástroje jsou **Konstrukční čára**, **Bod**, **Úsečka**,
**Obdélník**, **Kružnice**, **Oblouk** a **Spline**. Kružnice se zadává
prvním kliknutím do středu a druhým kliknutím na obvod. Druhý klik pouze určí
poloměr; trvalým řídicím bodem kružnice je její střed. Pravé tlačítko zruší
rozpracovaný prvek; u spline ji po zadání alespoň dvou bodů dokončí.

V nativní pravé liště jsou existující příkazy skici seskupené pod tlačítky
**Vazby** a **Kóty**. Položky uvnitř nabídek se povolují podle právě vybrané
geometrie; seskupení nemění jejich výběr, výpočet ani způsob potvrzení.

Samostatný příkaz **Text** otevře stejné interní okno Vlastností jako ostatní
parametry skici. U nového textu nejprve klikněte na kotevní bod ve výkresovém
prostoru, nastavte obsah, výšku, vodorovné a svislé zarovnání, barvu, natočení
a případné vodorovné převrácení a potvrďte **OK**. Změny se před potvrzením
zobrazují jen jako dočasný náhled; **Cancel** model nezmění. Dvojklik nebo
**Vlastnosti** v kontextové nabídce otevře pro existující text stejné okno a
**Odstranit** či `Delete` smaže celý text jako jednu entitu. Obrys ISO písma se
vypočítá při explicitním OK a uloží se společně se sémantickými parametry;
otevření, výběr a vykreslení textu proto nevolá OCCT ani znovu nenačítá písmo.
V aktuálním C++ řezu je text samostatná skicová grafika a zatím nevstupuje do
profilu Vytažení nebo Rotace.

Příkaz **Externí reference** vedle nástroje Výběr přepne nativní C++ Sketcher
do výběru původní persistované topologie. V tomto prvním řezu lze převzít hranu
nebo vrchol lokálního Partu z kontejneru, který leží před prvním Vytažením či
Rotací používající aktivní skicu. Tím se zabrání kruhové závislosti skici na
jejím vlastním výsledku. Hover zvýrazní přesně nabízenou hranu nebo vrchol,
pravé tlačítko cykluje společný seznam kandidátů a levé tlačítko uloží projekci
jako jednu Part revizi. Režim zůstane aktivní pro další výběr a ukončí se
opětovným stisknutím tlačítka.

Uložená externí hrana se kreslí hnědou čárkovanou čarou a vrchol hnědým
křížkem. Jsou read-only; lze je vybrat a odstranit přes pravé tlačítko nebo
`Delete`. Kliknutí, hover, otevření ani mazání nevolá OCCT: používá se pouze
stabilní identita a geometrie z persistovaného viewer packetu. Plochy, osy,
zdroje z jiné komponenty sestavy a asociativní obnovení projekce při explicitní
regeneraci budou doplněny v navazujících řezech.

V novém C++ pracovním prostoru se **B-spline skici** zadává posloupností
řídicích bodů; kubická křivka potřebuje nejméně čtyři a `Enter` ji dokončí.
Dvojklikem na hotovou křivku se otevřou interní **Vlastnosti B-spline**, kde
lze změnit stupeň, souřadnice řídicích bodů a přepnout uzavřenou periodickou
křivku. `Delete`
odstraní vybranou spline. Uzavřená spline může tvořit přesný profil Vytažení
nebo Rotace; výpočet používá přesnou OCCT B-spline, nikoli čárový náhled.

Nativní příkaz **Eliptický oblouk** používá postupně střed, konec hlavní
poloosy, délku a stranu kolmé vedlejší poloosy, počáteční bod a koncový bod.
Kurzor u obou konců pouze volí parametr; uložené body leží přesně na elipse.
Do posledního platného bodu je celý tvar jen náhled. `Escape` jej zruší bez
změny dokumentu; dokončený oblouk vznikne jako jedna vratná Part revize.

Při kreslení úsečky směrem k lokální ose X nebo Y se přichycení k ose kombinuje
s nabízenou vodorovnou nebo svislou vazbou. Koncový bod proto skončí přesně na
ose a úsečka se současně srovná v nabízeném směru; nevznikne pouze přibližná
vizuální shoda.

Název **Konstrukční čára** označuje konkrétní čárový prvek určený dvěma
řídicími body. Může nést vazby a kóty, ale nevstupuje do profilu Protrusion.
První konstrukční čára skici se u Revolve používá jako osa rotace; její směr
je pro rotační výpočet matematicky prodloužený. Obecné přepnutí kružnice,
oblouku nebo jiné geometrie mimo výsledný profil se terminologicky označuje
jako **konstrukční geometrie**. Tvar takového prvku se přepnutím nemění.

Jeden klik prostředním tlačítkem potvrdí právě zadávanou entitu. Tažení
prostředním tlačítkem nadále pouze otáčí pohled a zadání nepotvrdí.

Každý bod má interní souřadnice X/Y, ty ale bez explicitní uživatelské kóty
nejsou podmínkou a nezobrazují se. Solver je používá jako aktuální polohu a smí
je měnit při řešení vazeb. Konstrukční čára i další geometrie odkazují na své
řídicí body; kliknutí do volného místa bod vytvoří a kliknutí poblíž
existujícího bodu jej znovu použije.

Vazba **Tečná** se vytvoří postupným výběrem úsečky (nebo konstrukční čáry)
a kružnice v libovolném pořadí. Bod dotyku musí ležet v rozsahu vybrané
úsečky. V místě dotyku vznikne odvozený bod označený značkou **T**. Solver
zachová stranu kružnice vůči čáře z okamžiku vytvoření vazby.

Vratný rádius společného rohu dvou úseček se vytváří v režimu výběru.
Kliknutím se vybere první úsečka a `Ctrl`+kliknutím se k ní přidá druhá.
Tažením jejich společného bodu podél některého ramene vzniká živý tečný
oblouk. Původní ostrý roh zůstává virtuálně zachovaný. Tažením některého
tečného konce zpět do tohoto rohu se poloměr zmenší na nulu a rádius odstraní.

Řídicí souřadnicová kóta se vytvoří příkazem **Kóty → Vodorovná vzdálenost**
nebo **Kóty → Svislá vzdálenost** a následným výběrem bodu. Teprve takto
vytvořená kóta se zobrazí a její hodnota vstupuje do solveru.

Příkaz **Kóty → Vzdálenost** vytvoří po výběru dvou bodů šikmou řídicí kótu.
Její hodnota je skutečná eukleidovská vzdálenost bodů. Při změně hodnoty se
zachová dosavadní směr spojnice, pokud jej jiné vazby neurčují jinak.
Explicitní kóty jsou ve skicáři zobrazené vždy; samostatný přepínač jejich
viditelnosti se nepoužívá.

V nabídce **Vazby → Kolmá** vyberte postupně dvě úsečky nebo konstrukční
čáry. První určuje referenční směr a druhá je řízená.
U druhé zůstane zachovaný první bod a délka a její druhý bod se dopočítá tak,
aby byly čáry kolmé. Pravé tlačítko zruší první výběr. Vazbu lze odstranit
z kontextové nabídky ve stromu skici.

U vazby **Rovnoběžná** je pořadí stejné: první vybraná čára je reference,
druhá se srovná a nese symbol rovnoběžnosti. První čára se nepřepočítává.
Pokud je druhá čára již směrově zavazbená, ohlásí se konflikt.

Nativní příkaz **Stejné** přijímá buď dvě úsečky, nebo dvě kruhové křivky:
kružnice a kruhové oblouky lze vzájemně kombinovat. První výběr je reference,
druhý řízený prvek. U úseček se zachová směr druhé a převezme se referenční
délka. U kruhových křivek se převezme poloměr; střed řízené křivky zůstane na
místě, konce oblouku zachovají své úhly a body navázané na konstrukční kružnici
se přesunou radiálně spolu s ní. Pevný závislý bod, sdílená reference nebo
rozporná řídicí kóta celou operaci odmítne bez částečné změny. Elipsy ani
eliptické oblouky se pro shodný poloměr nenabízejí. První klik nic neukládá,
druhý platný klik vytvoří jednu Part revizi a `Escape` příkaz zruší.

Nativní vazba **Bod ve středu** se zadává výběrem samostatného bodu a potom
úsečky nebo konstrukční čáry. Bod se asociativně sváže s přesným průměrem obou
konců; jeden z koncových bodů téže čáry proto nelze zvolit jako cílový bod.
Po úspěchu vznikne jedna vratná Part revize a příkaz čeká na bod další vazby.
`Escape` zruší pouze rozpracovaný výběr. Pokud jsou všechny tři body pevné a
jejich poloha si odporuje, vazba se odmítne bez částečné změny skici.

Nativní vazba **Symetrická** používá tři výběry: referenční bod, řízený bod a
konstrukční čáru jako osu. Oba body musí být různé a běžná profilová úsečka se
jako osa nepřijme. Řízený bod se umístí do přesného zrcadlového obrazu
referenčního bodu; pozdější přesun reference nebo osy jej znovu dopočítá jen z
persistovaných dat Skicáře. První dva výběry jsou pouze rozpracovaný stav.
Platná osa vytvoří jednu vratnou Part revizi, zatímco `Escape` neuloží nic.

Nativní vazba **Soustředná** přijímá kružnici, kruhový oblouk, elipsu nebo
eliptický oblouk. První vybraná křivka je reference a druhá je řízená; B-spline
se v tomto příkazu nenabízí. Po potvrzení se střed druhé křivky přesune přesně
na referenční střed a společně s ním se přeloží všechny její řídicí body.
Platí to také pro vrcholy pravidelného mnohoúhelníku vázané na jeho konstrukční
kružnici. Tvar, poloměry a úhlové rozsahy se tím nemění. Pevný závislý bod nebo
jiný neřešitelný konflikt celou operaci odmítne bez částečné změny. První výběr
je pouze rozpracovaný stav, platná druhá křivka vytvoří jednu Part revizi a
`Escape` příkaz zruší.

Nativní vazba **Tečná** pracuje buď s jednou konečnou úsečkou (profilovou i
konstrukční) a kružnicí, kruhovým obloukem, elipsou či eliptickým obloukem,
nebo se dvěma kružnicemi či kruhovými oblouky. První výběr je reference a druhá
geometrie je řízená: solver ji pouze přeloží, zachová délku úsečky, poloměry,
natočení i skutečný rozsah oblouku. U dvojice kruhových křivek při vytvoření
zvolí nejbližší geometricky platnou vnější nebo vnitřní tečnost a tento režim
asociativně uloží. Pro elipsu a úsečku počítá přesný opěrný bod v natočených
osách, nikoli kruhovou nebo osově zarovnanou aproximaci. Bod dotyku musí ležet
uvnitř konečné úsečky a u každého oblouku také uvnitř jeho parametrického
rozsahu; jinak se vazba odmítne bez změny skici. Pevný nebo s referencí sdílený
bod, který brání tuhému překladu, je rovněž transakční konflikt. Tečnost dvou
křivek, z nichž je některá eliptická, zatím příkaz nepodporuje. První klik nic
neukládá, druhý platný klik vytvoří jednu Part revizi a `Escape` příkaz zruší.

Příkazy **Vazby → Vodorovná** a **Vazby → Svislá** se aplikují výběrem jedné
úsečky nebo konstrukční čáry. První bod zůstane pevný a odpovídající souřadnice
druhého bodu se dopočítá. Vodorovné a svislé vazby jsou uprostřed geometrie
označené zeleným písmenem **H** nebo **V**.

Stejnými příkazy lze spojit také dva body. Nejprve se vybere referenční bod a
potom řízený bod. Vodorovná vazba přenese na řízený bod souřadnici Y,
svislá souřadnici X. Značka **H** nebo **V** se zobrazí u řízeného bodu.

Podrobný popis pořadí výběru, priorit přichytávání, stupňů volnosti a typů
vazeb je v dokumentu [Skicář](SKETCHER.md).

Zobrazené explicitní kóty lze pravým tlačítkem **Zamknout** nebo
**Odemknout**. Zamknutá kóta je řídicí: solver zachová její hodnotu.
Do editačního pole kóty ve skicáři i ve 3D view lze místo hotové hodnoty
zadat výpočet s operátory `+`, `-`, `*`, `/` a závorkami. Například
`5+4*4` se po potvrzení vyhodnotí jako `21`; podporovaná je také desetinná
čárka. Dělení nulou a jiné než číselné výrazy se odmítnou.
Odemknutím přestane být podmínkou a z pohledu zmizí. Stav se ukládá společně
se skicou.

Tlačítko **Pohled kolmo** s ikonou normálového pohledu kdykoliv znovu narovná
a vystředí kameru podle aktivní skici.

Nástroj **Výběr** s ikonou šipky ukončí rozpracovaný kreslicí příkaz. Kliknutím
lze označit bod nebo geometrii oranžově a odstranit ji tlačítkem
**Vymazat vybrané** nebo klávesou `Delete`. Odstranění řídicího bodu odstraní
také geometrii, která je na něj navázaná.

- **Dokončit skicu** uloží změny a obnoví předchozí 3D pohled.
- **Zrušit úpravy** zahodí změny od vstupu do režimu a vrátí se do Vlastností
  skici.

## Pomocná geometrie kontejneru

Viditelnost pomocných bodů, os a rovin se ovládá v tree pravým tlačítkem nad
položkou **Počátek** příslušného kontejneru:

- **Skrýt** pomocnou geometrii,
- **Odkrýt** pomocnou geometrii.

Nastavení je součástí dokumentu.

## Základní práce se sestavou

Nový dokument typu **Sestava** používá příponu `.asmz` a automaticky otevře
aplikaci **Sestava**. Tlačítko **Vložit** v pravém panelu vloží existující díl
`.prtz` nebo vnořenou sestavu `.asmz`. První komponenta se vloží do počátku a
další se podle skutečných rozměrů automaticky rozloží vedle dosavadní sestavy.

Každý vložený soubor je ve stromu samostatná instance pojmenovaná podle
zdrojového souboru. Po rozbalení ukazuje strom obsah zdrojového Partu nebo
Assembly a lokální počátek instance. Poloha každé komponenty patří výhradně její
bezprostředně nadřazené Assembly; nadřazená sestava polohuje vnořenou Assembly
jako jeden celek a nepřebírá vlastnictví vazeb jejích vnitřních komponent.

Kontextové menu komponenty rozlišuje **Skrýt/Odkrýt** a
**Potlačit/Obnovit**. Skrytí je pouze stav zobrazení: komponent zůstává aktivní,
jeho vazby a reference zůstávají platné a závislé komponenty se nemění.
Potlačení komponent vyřadí z aktivního modelu sestavy. Komponenty závislé přes
sestavové vazby nebo externí reference skic se potom automaticky potlačí také,
a to rekurzivně přes celý závislý řetězec.

Automatické potlačení následníků není ukládáno jako jejich ruční potlačení.
Příkaz **Obnovit** na zdrojovém komponentu proto znovu vyhodnotí pouze jeho
závislý řetězec; neobnovuje všechny komponenty sestavy a ručně potlačené
komponenty ponechá beze změny. Následník se obnoví, pouze pokud už nemá jiný
potlačený zdroj. Chybějící nebo neplatná aktivní reference se nemaže: komponent
se obnoví v červeném chybovém stavu, aby bylo možné vazbu později opravit.

Změny polohy ve Vlastnostech se zobrazují živě přímo ve view. Strom sestavy
zůstává sestavový i při práci s obsahem vložených dílů. Dvojklik na instanci
přímo ve 3D view neotevírá Vlastnosti: zobrazí nebo skryje její klikací kóty
vazeb. Vlastnosti se nadále otevírají dvojklikem ve stromu nebo z kontextového
menu.

Pravým tlačítkem nad instancí zvolte **Vlastnosti**. Okno vychází z vlastností
kontejneru v Partu a obsahuje tři řádky ustavení:

`reference dílu ↔ reference sestavy | Typ vazby | Hodnota | Flip`

Klikněte na první zeleně označené pole a vyberte plochu, rovinu nebo osu
umisťovaného dílu přímo ve 3D pohledu. Potom vyberte odpovídající referenci
druhého dílu nebo sestavy. Aktivní výběr automaticky pokračuje další dvojicí.
Dialog podle geometrie a zbývajících stupňů volnosti nabízí pouze použitelné
typy: rovinnou vazbu s posunutím, **Souosost** nebo **Úhel**. Hodnota používá
`mm` nebo stupně podle typu; **Flip** obrací orientaci. Uživatelské i generované
osy zdrojového dílu lze vybírat ve view i ve stromu. Zakřivené plochy jako
rovinné reference podporované nejsou.

Kontextové menu má v sestavě dvě odlišné vlastnické úrovně. Nad komponentou ve
view nebo ve stromu jsou dostupné pouze příkazy sestavy: **Upravit** a
**Vlastnosti** mění vazby, polohu a vlastnosti komponenty v bezprostředně
vlastnící Assembly. Nad solidem nebo interní geometrií neaktivního zdrojového
Partu se Partové příkazy nenabízejí. Akce jako **Skica**, datumová geometrie,
úprava historie nebo mazání jsou dostupné až po příkazu **Aktivovat díl**; ten
přepne editovatelný dokument na konkrétní zdrojový Part. U vnořené Assembly se
stejné pravidlo aplikuje rekurzivně a každá komponenta je upravována pouze ve
své bezprostředně vlastnící sestavě.

Skutečná plocha pod kurzorem se zvýrazní oranžově. Po potvrzení zůstane plocha
ve view azurová a azurové zůstane také příslušné pole reference ve
Vlastnostech. Toto zobrazení používá dočasný index aktuálního sestavového
meshe, ale do `.asmz` se ukládá pouze stabilní reference původního solidu.
Základní roviny počátku zůstávají standardně hnědé; oranžové jsou pouze při
hoveru. Otevření Vlastností komponenty samo nezapíná zobrazení počátku.

První pole vždy vybírá původní těleso právě umisťovaného dílu. Druhé pole
vybírá pouze původní geometrii komponent, které jsou ve stromu před ním.
Výsledné plochy po sestavových odečtech ani celé kontejnery nejsou platnou
náhradou. Pokud starší cache dílu původní plochy neobsahuje, otevřete zdrojový
Part, použijte **Regenerovat**, uložte jej a vraťte se do sestavy. Úplný
datový kontrakt popisují [Reference a vazby sestavy](ASSEMBLY_REFERENCES.md).

Po souososti zůstává volný posun a rotace podél společné osy. Dosednutí čelních
ploch může uzamknout zbývající posun a následná dvojice bočních rovin pak
automaticky nabízí úhlovou vazbu. Solver zachovává nejbližší platnou polohu a
opačnou větev volí pouze pomocí **Flip**.

Kóty zobrazené dvojklikem na díl zahrnují také nulové rovinné a souosé vazby.
Úhel a rovinné posunutí lze přepsat přímo v kótě a potvrdit Enterem; souosost je
zobrazená jako zamčená informační vazba.

Změna polohy nebo natočení rodičovské komponenty se okamžitě přenese na
komponenty, jejichž cílové reference míří na rodiče, a rekurzivně na další
potomky. Není nutné otevírat Vlastnosti každého potomka. **Zrušit** vrátí celý
živě přesunutý řetězec do posledního potvrzeného stavu.

Sestavové vazby jsou v současnosti prototyp. Plochy jednoduchých těles Box a
Wedge a podporované plochy, hrany a vrcholy Extrusion a Revolve mají stabilní
sémantickou identitu. Sestavová reference ukládá identitu konkrétní instance a
zdrojovou referenci Partu, nikoliv pořadové číslo plochy výsledného compoundu.
Podporované booleovské plochy a kruhové hrany se po změně zdrojového Partu
obnoví; chybějící nebo nejednoznačná reference zůstane výslovně nevyřešená a
nesmí se tiše nahradit jinou geometrií se stejným indexem.

### Aktivace dílu v sestavě

Příkaz **Aktivovat díl** zpřístupní modelovací nástroje dílu přímo v tabu
sestavy. Strom zůstane sestavový a ostatní díly lze nadále použít jako zdroje
externích referencí. Aktivní díl se ve view zobrazuje ve své původní podobě
před sestavovými řezy.

Skica aktivního dílu používá jeho lokální počátek a transformaci instance,
nikoliv počátek sestavy. Změny provedené v aktivním dílu se ukládají do jeho
zdrojového `.prtz`. Tlačítko **Zpět do sestavy** ukončí kontextovou editaci.

Je-li stejný díl otevřený také v samostatném tabu, aplikace používá společný
aktuální dokumentový stav. Externí reference vytvořená v sestavě uchovává
identitu sestavy a konkrétní instance; chybějící nebo nejednoznačný zdroj se
označí jako ztracená reference místo použití nesprávné geometrie.

Po změně otevřeného Partu nebo vnořené Assembly zůstávají nadřazené sestavy v
posledním vypočteném stavu. Přepnutí tabu, uložení, obnova stromu ani běžné
překreslení view závislosti automaticky nepřepočítávají. Nový stav převezmete
výslovným příkazem **Regenerovat** v cílové Assembly. Regenerace použije právě
otevřené dokumenty jako autoritativní zdroj, takže zahrne i jejich dosud
neuložené změny, obnoví celý vnořený řetězec a přepočítá požadovanou sestavu.

Externí reference skici na podporované plochy, hrany a vrcholy Extrusion se
ukládají podle původu ve zdrojové skici, nikoliv podle aktuálního pořadí
topologie. Po změně rozměru rodiče se potomek automaticky regeneruje. Bod ležící
na hlavní ose skici zůstává samostatně vybratelný a lze jej odstranit stejně
jako ostatní externí reference. Bodové externí reference se při výběru
nezobrazují všechny současně: nejbližší bod se zvýrazní až po přiblížení
kurzoru a kliknutím se potvrdí. Již vybraný bod zůstává viditelný. Příkaz
**Dokončit skicu** se při vstupu z vlastností kontejneru vrátí zpět do těchto
vlastností.

### Sestavové řezy

V sestavě jsou dostupné operace **Protrusion** a **Revolve** pouze jako
odečítání materiálu. Řez lze omezit na vybrané díly; bez výběru působí na
všechny vložené instance. Výsledek se ukládá výhradně v `.asmz` a nemění
původní soubory dílů.

Při hoveru se pro každou instanci střídá její sestavově upravený a původní
stav, potom se pokračuje další instancí. Po aktivaci dílu se hover řídí
pravidly Partu.

Komponenty se ve výsledku sestavy uchovávají jako samostatné tvary v OCCT
compoundu; běžné zobrazení je neslučuje operací Fuse. Zdrojové `.prtz`
dokumenty se načítají jednou a jejich již vytvořená geometrie se znovu používá.
Při uložení Partu s importovaným STEP se vedle parametrického zdroje ukládá
podpisem ověřená komprimovaná BREP cache. Starší importovaný Part je vhodné
jednou otevřít a uložit; následující načtení a vložení pak použije rychlou
BREP cache místo opakovaného převodu STEP.

Při interaktivním otevření `.prtz` přes **Soubor → Otevřít** probíhá načtení
BREP a příprava zobrazovacího meshe velkého vloženého STEP mimo hlavní GUI
vlákno. Hlavní okno proto během této práce zůstává překreslované a stavový
řádek zobrazuje načítaný soubor.

### Export STEP

Příkaz **Soubor → Exportovat model do STEP…** je dostupný pro díl i sestavu.
U dílu zapíše právě aktivní výsledné těleso. U sestavy zapíše všechny načtené
komponenty v jejich výsledných sestavových polohách jako samostatná tělesa.
Podporované přípony jsou `.step` a `.stp`; pokud přípona chybí, doplní se
`.step`.

STEP obsahuje pouze výslednou geometrii. Strom historie, skici, vazby,
materiály ani sestavové vazby se do něj nepřenášejí. Prázdný model nebo model
bez solidu nelze exportovat. Výkres `.drwz` se exportuje prostřednictvím svého
zdrojového dílu nebo sestavy, nikoliv přímo.

### Barvy a přejmenování

Barva nastavená pro vložený díl patří konkrétní instanci a nemění barvy
ostatních dílů. Příkaz **Soubor → Přejmenovat soubor…** zachová správnou
příponu a aktualizuje interní odkazy v dílech, sestavách a výkresech. Pokud má
přejmenovaný model stejně pojmenovaný `.drwz`, přejmenuje se s ním.

## Základní práce s výkresem

Výkres používá příponu `.drwz` a je navázaný na zdrojový díl `.prtz` nebo
sestavu `.asmz`. Lze jej založit přes **Soubor → Nový → Výkres**, nebo
tlačítkem **Výkres** v záhlaví stromu otevřeného dílu či sestavy. Tlačítko
nejprve otevře již existující stejně pojmenovaný výkres a teprve pokud
neexistuje, vytvoří nový.

V záhlaví stromu výkresu je opačné tlačítko **Díl** nebo **Sestava**, které
otevře zdrojový dokument.

### Listy a formáty

Jeden výkres může obsahovat více listů. Záložky listů jsou dole pod výkresovou
plochou:

- **+** přidá nový list,
- **−** odebere aktivní list; poslední list nelze odebrat,
- roletka **Formát** mění pouze aktivní list.

Podporované formáty jsou A4, A3, A2, A1 a A0. A4 je vždy na výšku; A3 až A0
jsou vždy na šířku. Rozměry odpovídají skutečnému papíru v milimetrech.

Soubory formátů `.frmz` a razítek `.tblz` lze otevřít příkazem
**Soubor → Otevřít**. ZIMA-CAD je otevře přímo ve Sketchi se stejnými
nástroji, vazbami, kótami, posunem a přiblížením jako běžnou skicu. Pohled
zůstává kolmý k šabloně, kladné X směřuje stejně jako na výkresu zprava
doleva a pohled nelze prostorově otáčet. Kontextová nabídka nad
geometrií umožňuje nastavit její barvu na bílou, zelenou nebo žlutou; stejná
volba je dostupná také pro text.

Automatická pole razítka jsou ve Sketchi zobrazena se znakem `&` na začátku.
Jedno textové pole smí kombinovat běžný text s libovolným počtem tokenů,
například `Číslo: &document.file_stem.&model.revision / &drawing.edice`.
`&model.revision` odkazuje přímo na anglický klíč zdrojového modelu. Lokalizovaný
token `&Verze` vybere českou hodnotu stejného parametru a `&Version` hodnotu
podle jazyka razítka. Token `&drawing.edice` patří pouze konkrétnímu listu výkresu
a nemění zdrojový model ani šablonu razítka. Tokeny `&document.file_stem`,
`&sheet.format`, `&sheet.scale` a `&sheet.position` jsou automatické systémové
hodnoty. Kód lze upravit jako běžný text; po uložení se zapíše zpět jako
programovatelné pole razítka, nikoli jako statický nápis.

Formáty a razítka jsou uloženy společně v adresáři `config/formats`. Při
uložení se předchozí obsah automaticky archivuje jako `soubor.frmz.1`,
`soubor.frmz.2` nebo obdobně `soubor.tblz.1`, `soubor.tblz.2`.

Při vložení se definice rámečku i razítka uloží přímo do aktivního listu.
Pozdější smazání, přejmenování nebo změna knihovního souboru již existující
výkres nezmění. Každý list vícelistového výkresu má vlastní nezávislou kopii.

Počátek každého listu leží v pravém dolním rohu. Kladná osa X směřuje zprava
doleva a kladná osa Y zdola nahoru. Při změně formátu se proto list mění
směrem doleva a nahoru a budoucí razítko může zůstat na místě.

Razítko se do listu nepřesouvá ani automaticky nenormalizuje. Uložený počátek
`(0, 0)` Sketcheru se vloží přesně do počátku `(0, 0)` výkresového prostoru a
každá úsečka, kružnice i textový kotevní bod používá přímo svou uloženou
souřadnici. Polohu vůči rámečku je proto nutné navrhnout přímo ve Sketchi;
renderer nepřidává skrytých 10 mm ani jiný korekční posun.

Výkres čte kanonická data `[Sketch]`, nikoliv zjednodušenou kopii geometrie.
Zachovává tak vodorovné i svislé zarovnání textu, otočení, převrácení, font,
barvu a výšku. Zadaná výška ISO textu v milimetrech znamená výšku velkého
písmene. Nezávisí na tom, zda konkrétní nápis obsahuje diakritiku, malá písmena
nebo spodní dotažnice; stejná hodnota má ve Sketcheru i ve výkresu stejnou
velikost.

Výkresová plocha má černé pozadí. List nemá barevnou výplň; jeho formát
znázorňuje pouze bílý obdélník. Výkresová geometrie je rovněž bílá.

### Ovládání výkresové plochy

| Ovládání | Funkce |
| --- | --- |
| Kolečko myši | Přiblížení a oddálení kolem kurzoru |
| Prostřední tlačítko + pohyb | Posun výkresové plochy |
| Obnovit pohled | Animovaně zobrazit celý aktivní list |
| Levé tlačítko + pohyb na pohledu | Přesunutí vloženého pohledu |
| Levé tlačítko + pohyb na popisku | Přesunutí názvu a měřítka pohledu |
| Delete | Odstranění vybraného pohledu |
| Esc | Zrušit právě umisťovaný pohled |

Výkresová plocha je čistě 2D a nepodporuje rotaci.

### Vložení pohledu

V pravém panelu aplikace Výkres je příkaz **Vložit pohled**. První pohled je
standardně izometrický; náhled se připojí ke kurzoru a levým kliknutím se
umístí na aktivní list. Příkaz **Vytvoř projekci** v kontextové nabídce pohledu
vytváří navázaný pohled podle evropské nebo americké projekční metody. Směr se
přichytává v osmi polohách po 45 stupních. Přesun rodiče přenese jeho odvozené
pohledy, zatímco samostatný přesun potomka zůstává na jeho projekčním paprsku.

Pohled uchovává odkaz na zdrojový model. Při aktivaci tabu se jeho geometrie
obnoví ze skutečné topologie nativního rendereru; stará uložená 2D cache
promítnutých čar se nepoužívá.

Vlastnosti pohledu nabízejí drátové zobrazení, skryté hrany, zobrazení bez
skrytých hran, stínované zobrazení s hranami a čisté stínované zobrazení.
Globální tlačítka režimu modelu jsou ve výkresu vypnutá, protože styl patří
každému pohledu samostatně. Stínování zachovává barvy modelu a komponent.
Zaškrtávací skupina **Zobrazit popisek pohledu** obsahuje název, zdroj měřítka
a měřítko. Nad obrysem pohledu zobrazí bílý popisek o velikosti 5 mm ve tvaru
`Název` a `M1:1`; popisek lze samostatně vybrat a přesunout.

První lineární výkresová kóta se vytváří výběrem dvou rovnoběžných přímých
hran modelu a umístěním žluté kóty v listu. Hover je oranžový, potvrzené hrany
azurové, prostřední klik kótu umístí a prostřední dvojklik nástroj ukončí.
Kóta drží stabilní reference a po změně rozměru zdrojového modelu se
přepočítá. Plochy a obecné křivky zatím nelze kotovat.

Rámečky, upravitelná parametrická razítka, česká i anglická mutace a BOM Repeat
Region s funkcemi **Item Number** a **Quantity** jsou funkční. Zbývá dokončit
řezy, detaily, úplnou sadu ISO kót, tolerance, pozice, popisky, technické
symboly a export PDF/DXF. Podrobné ovládání a omezení jsou v dokumentu
[Výkresy](DRAWINGS.md).
