# Výkresy

Tento dokument popisuje aktuální datový a uživatelský model výkresů ZIMA-CAD.
Základní postup je také v
[uživatelském manuálu](UZIVATELSKY_MANUAL.md#základní-práce-s-výkresem).

## Dokument, listy a zdrojový model

Výkres má příponu `.drwz` a odkazuje na jeden zdrojový díl `.prtz` nebo
sestavu `.asmz`. Jeden dokument může obsahovat více listů. Každý list má
samostatně uložený formát, rámeček, razítko, vložené pohledy, kóty a hodnoty
polí náležející pouze listu.

Tlačítko **VÝKRES** v záhlaví stromu zdrojového Dílu nebo Sestavy otevře jeho
již existující výkres. Pokud ještě neexistuje, vytvoří nový soubor výkresu a
otevře jej v novém tabu. Vazba na zdroj je uložená přímo ve výkresovém
dokumentu už před vložením prvního pohledu. Opačné tlačítko **DÍL** nebo
**SESTAVA** proto vždy přejde zpět na přesný zdrojový dokument. Přejmenování
zdroje aktualizuje jeho cestu a název také v navázaném výkresu.

Rámeček i razítko se při vložení zkopírují přímo do dat listu. Otevřený výkres
proto není závislý na pozdější existenci souboru `.frmz` nebo `.tblz` v
`config/formats`. Smazání, přejmenování nebo úprava knihovní šablony již
vložené listy nezmění. List převezme jinou definici teprve při výslovné výměně
rámečku nebo razítka.

## Rámečky a razítka

Soubory `.frmz` a `.tblz` se upravují ve skicáři. Editor šablony vždy zobrazí
celý list; nepoužívá fyzické výchozí měřítko běžné modelové skici. Uložený
počátek šablony se bez skryté kompenzace mapuje na počátek výkresu v pravém
dolním rohu.

Dodávaná razítka jsou:

- `ZE-RAZITKO.tblz` pro české popisky a české lokalizované názvy parametrů;
- `ZE-TITLE-BLOCK.tblz` pro anglické popisky a anglické lokalizované názvy.

Text začínající `&` odkazuje na parametr. Interní klíče, například `name`,
i lokalizované popisky (`&Název`, `&Name`, `&Наименование`) označují stejný
parametr. **Vlastnosti listu → Jazyk razítka** určují jazyk čtené a zapisované
hodnoty. Při vložení se převezme `Locale` šablony; sdílená hodnota má vždy
přednost před jazykovou. Nevyplněná položka používá výchozí zástupný text pole.

Příkaz **Hodnoty razítka…** zobrazí aktuální hodnoty. Upravitelné pole se ve
výkresu zvýrazní oranžově při hoveru a azurově po výběru; dvojklik otevře
stejný dialog a zaměří jeho editor. Načítání funguje i před vložením prvního
pohledu, pokud má výkres přiřazený zdrojový model. Po vložení pohledů určuje
zdroj razítka první pohled aktivního listu.

**OK** zapisuje změněné hodnoty jednoduchých parametrických polí s `WriteBack`
do Parameters zdrojového dílu nebo sestavy v jediné revizi zdroje. **Cancel**
neprovede zápis. Otevřený model má přednost před souborem na disku. Dosud
neotevřený zdroj se při potvrzení načte do workspace; změna zůstane neuložená
stejně jako při běžné editaci Parameters a lze ji vrátit pomocí Undo zdroje.
Výraz `&parametr` zůstává v razítku zachovaný, nevzniká lokální přepis jeho hodnoty.

Samostatné texty `&parametr` mimo oblast kusovníku, které neměly vlastní
sekci `Field`, se při vložení také zpřístupní jako parametrická pole.
Položky `&drawing.*` a `&local.*` se ukládají pouze do listu.
Systémové hodnoty, složené výrazy a parametry řízené vztahy jsou jen ke čtení.
Řádky opakovaného kusovníku nejsou přes tento dialog editory parametrů komponent.

Hmotnost řízená `model.mass` a její jednotka se řídí nastavením zdroje;
[pravidla výpočtu a jeho hranice](PHYSICAL_PROPERTIES.md) popisují hustotu,
jednotky, snímky komponent a explicitní Regenerate sestavy.

## BOM Repeat Region v razítku

Sestavový pohled při vložení nebo explicitní regeneraci načte komponenty
zdrojové sestavy. Stejný zdroj se seskupí do jedné položky s odpovídajícím
množstvím, odlišné zdroje vytvoří další řádky. Každý řádek načítá vlastní
Parameters příslušného dílu či podsestavy. Potlačené komponenty se vynechávají;
pouze skryté komponenty se stále započítávají. Počet řádků určuje počet
položek BOM, fialový rámeček definuje opakovanou geometrii a rozteč.

Oblast kusovníku se v editoru razítka označuje objektem **BOM Repeat Region**.
Je zobrazena jako fialový drátový obdélník, nikoliv jako vyplněná plocha.
Oblast má při výběru nejvyšší prioritu: přejetí nad kteroukoliv její hranou
zvýrazní celý region včetně popisků. Geometrie pod regionem zůstává dostupná
překliknutím kandidátů pravým tlačítkem.

První dvojřádek obsahuje dvě zvláštní BOM funkce:

- vlevo nahoře **Item Number** — pořadové číslo položky od 1;
- vpravo nahoře **Quantity** — počet výskytů dílu v sestavě.

Ostatní pole řádku jsou běžné parametry zdrojového dílu. Pro samostatný díl se
zobrazí jeden řádek. Pro sestavu se označený region opakuje ve směru uloženém
v regionu a vytvoří řádky kusovníku.

## Vložené pohledy a aktualizace

**Vložit pohled → kliknout na list → Vlastnosti pohledu** je společný postup
pro všechny zdroje. První pohled je izometrický. Zdrojový otevřený model nebo
soubor se vybírá přímo ve vlastnostech, společně s názvem, popiskem,
orientací, stylem, měřítkem a polohou. Vytváření a editace používají stejný
interní dialog. Změny jsou do OK jen náhled; Zrušit nevytvoří nový pohled ani
nezmění existující. Prostřední dvojklik nad listem potvrdí OK.

Výběr pohledu používá celou obdélníkovou oblast jeho projekce včetně prázdného
vnitřku. Hover, kliknutí a kontextová nabídka používají stejnou oblast a
pořadí překrývajících se pohledů. Rámeček je vidět jen při hoveru a výběru.
Strom a View označují tentýž pohled; kliknutí mimo pohledy výběr zruší.

Kontextový příkaz **Projekční pohled** připojí náhled ke kurzoru a přichytává
směr po 45 stupních podle rodiče a metody promítání listu. Potomek ukládá ID
rodiče, směr a kameru. Přesun rodiče přenese potomky, přesun potomka se drží
projekčního paprsku. Název/popisek a volba vlastního či listového měřítka se
ukládají do `.drwz`.

Pohled uchovává odkaz na zdrojový model a poslední vypočtenou projekci.
Otevření či přepnutí tabu nevyvolává výpočet OCCT ani automatickou obnovu
závislostí. **Regenerovat** výslovně načte poslední vypočtený stav otevřeného
zdroje, případně jeho uložený soubor, a z něj obnoví projekci a kóty.

Roletka **Varianta** dole obsahuje zatím pouze jméno zdrojového souboru.
Přepínání a výpočet variant Family Table jsou budoucí funkce. **Parametry**
nad View nebo v kontextové nabídce názvu výkresu otevřou parametry zdrojového
dílu/sestavy při zachování zobrazeného výkresu.

Výběr formátu a razítka začíná v adresáři **Formats** z globální konfigurace,
a to i po změně konfigurace za běhu aplikace.

## Lineární kóta

Aktuální výkresová kóta podporuje dvě rovnoběžné přímé hrany vloženého
pohledu. Plochy a obecné křivky zatím nejsou platnou referencí.

Postup:

1. Zapněte **Kóta**. Kurzor zůstane běžnou šipkou.
2. Přejeďte nad hranou vloženého pohledu. Použitelná hrana se zvýrazní
   oranžově; pravé tlačítko přepíná překrývající se kandidáty a stavový řádek
   oznamuje právě nabízený objekt.
3. Levým tlačítkem potvrďte první a potom druhou hranu. Potvrzené reference
   zůstanou azurové.
4. Žlutý náhled ukazuje skutečnou modelovou vzdálenost. Pohybem kurzoru určete
   polohu a krátkým kliknutím prostředního tlačítka ji potvrďte.
5. Nástroj zůstane aktivní pro další kótu. Rychlý dvojklik prostředním
   tlačítkem jej ukončí.

Kóta ukládá stabilní topologické reference základních objektů, nikoliv pouze
obrazové souřadnice nebo pořadí hran v aktuálním meshi. Po regeneraci modelu
se reference znovu vyřeší a hodnota i poloha kóty se přepočítají. Pokud
reference chybí nebo je nejednoznačná, nesmí se kóta tiše připojit k jiné
hraně.

Šipky používají společný ostrý tvar s polovičním úhlem 10°. Stejná geometrie
se používá také u kót ve view, os počátku a směru BOM regionu.

## Současná omezení

Podporována je první asociativní lineární kóta mezi dvěma rovnoběžnými
přímými hranami. Další typy ISO kót, řezy, detaily, tolerance, pozice,
technické symboly a export DXF jsou další vývojové kroky. BOM v razítku
už není v této skupině: Repeat Region, Item Number a Quantity jsou funkční.


## Editace rámečků a razítek v C++ aplikaci

Příkaz **Otevřít** přijímá `.frmz` a `.tblz` a otevře je přímo ve
stávajícím skicáři v kartě dokumentu. **Nový** umí oba typy vytvořit.
**Uložit** zapisuje původní typ souboru; **Uložit jako** vytváří samostatnou
kopii. Před přepsáním se uchová číslovaná předchozí verze souboru.
Geometrie se při otevření automaticky nerozmisťuje ani nevyrovnává.

Původní knihovní šablony se převádějí včetně geometrie, kotev textu, vazeb,
kót, barev per a vlastností parametrických polí. Záporné délkové kóty mezi
body se převedou na kladnou velikost a opačné pořadí bodů, se stejnou rovnicí
pro jejich polohu. Souřadnicové umístění vůči osám a počátku si znaménko
ponechává. Uložená šablona používá SchemaVersion 4 a nativní C++ data skici.

Ve skicáři razítka je příkaz **Oblast kusovníku**. Dvěma kliknutími se zadají
rohy fialového obdélníku, poté se v jeho vlastnostech nastaví poloha, rozměry,
směr a rozteč opakování. OK oblast uloží, Storno návrh zahodí. Stejné vlastnosti
se otevírají dvojklikem na existující oblast nebo ze stromu; nabídka umožňuje
oblast odstranit. Platí i potvrzení dvojklikem prostředního tlačítka nad View.

Fialový obrys nemá výplň, je kreslený nad ostatní geometrií a při hoveru má
nejvyšší prioritu. LMB vybírá celou oblast a synchronizuje strom. Před potvrzením
lze RMB přepnout na další geometrii pod obrysem. Pomocný obdélník se do výkresu
netiskne. Ve výkresu se opakují skutečné čáry, kružnice a texty uvnitř oblasti
podle jejího směru a rozteče. Výrazy `&bom.item_number` a `&bom.quantity` dávají
číslo položky a počet; ostatní modelové parametry patří zdrojovému dílu daného
řádku. Pro samostatný díl vzniká jeden řádek. Výkres ukládá vlastní vloženou
kopii šablony včetně oblasti BOM, takže pozdější editace knihovny jeho vzhled
sama nezmění.

### Obrázky v razítku (C++)

Editor `.tblz` nabízí příkaz **Obrázek** s vlastní ikonou. Po výběru SVG, PNG,
JPEG, BMP nebo WebP se otevřou společné vnitřní **Vlastnosti obrázku**.
Umístění určuje bod kliknutý ve skice nebo souřadnice X/Y. Vodorovné
zarovnání Vlevo / Na střed / Vpravo a svislé Dole / Uprostřed / Nahoře
vztahují obdélník k tomuto bodu. Souřadnice mohou být záporné, rozměry jsou kladné.

Šířka a výška jsou v milimetrech. Volba **Zachovat poměr stran** je při
vložení zapnutá; změna kteréhokoli rozměru dopočítá druhý podle původního
obrázku. Po vypnutí této volby lze rozměry nastavit samostatně. **Vybrat soubor…**
umožní v témže dialogu vyměnit obsah. Náhled je dočasný: pouze OK změnu
uloží, Zrušit obnoví původní stav. OK funguje také dvojklikem prostředního
tlačítka nad View.

Obrázek lze vybrat přes celou jeho obdélníkovou plochu nebo ve stromu.
Dvojklik otevře stejné vlastnosti; kontextové menu nabízí Vlastnosti a
Odstranit. Fialová oblast kusovníku se kreslí poslední a má vyšší prioritu
výběru než obrázek. Obrázek uvnitř oblasti se opakuje spolu s jejím obsahem.

Rastr se normalizuje na PNG, zachová průhlednost a uloží přímo do `.tblz`
i do výkresu `.drwz`. Původní externí soubor není po vložení potřeba.
Příkaz je dostupný pouze v razítku. Dekódované obrázky používají omezenou
sdílenou paměťovou cache, takže pohyb kurzoru znovu nenačítá zdrojové soubory.

SVG se ukládá jako původní vektorová data a v editoru i výkresu se vykresluje
vektorově prostřednictvím Qt SVG. Zvětšení ani změna rozměrů jej nepřevádí
na bitmapu. Poměr stran vychází z přesného `viewBox`; rastry používají
původní rozlišení. Podpora obsahu SVG odpovídá rendereru Qt SVG; pro přenosné
firemní logo je vhodné mít text převedený na křivky a případné další obrázky
vložené přímo do SVG. Animace se v technickém výkresu nepřehrávají.

### Vazby dodávaných razítek

České a anglické razítko ponechává 14 unikátních řídicích rozměrů místo
52 opakovaných kót. První 10mm odsazení od počátku řídí ostatní desetimilimetrové
úseky vazbou stejná délka. Stejně jsou sjednoceny další opakované hodnoty.
H/V vazby udržují zarovnání, počátek je navázaný na počátek skici.
U vodorovných či svislých odsazení mezi diagonálně položenými body jsou
použité pomocné projekční úsečky; porovnání délek tak neměří chybnou diagonálu.
Původní souřadnice, texty a tisknutelná geometrie zůstávají zachované.
Regresní test řešiče kontroluje reziduum i maximální pohyb všech bodů do 1e-6 mm.

Pravoúhlé řetězce H/V a stejných délek v šabloně mají přesný lineární
výpočet počátečního řešení; standardní řešič následně ověří všechny vazby.
Zkouška změny hlavního odsazení z 10 na 12 mm ověřuje i změnu navázaných řádků.

Zámek ve skicáři chrání hodnotu rozměru při tažení geometrie. Ve View jej
lze přepnout přes **Zamknout rozměr / Odemknout rozměr**, také je dostupný
ve vlastnostech rozměru. Zamčený řídicí rozměr se kreslí černě, nezamčený
žlutě a měřený hnědě. Výběr a hover dál používají azurovou a oranžovou.
Zámek nefixuje polohu popisku; číselnou hodnotu lze záměrně změnit ve vlastnostech.


Číselná pole a rozměry ve View používají [společné zámky hodnot](NUMERIC_VALUE_LOCKS.md),
včetně jednorázového převzetí současné hodnoty při zadávání reference.

Vlastnosti obrázku, oblasti kusovníku, jejich příkazy, zarovnání a směry
opakování mají české, anglické, německé, francouzské a ruské texty podle
[jazyka aplikace](LOCALIZATION.md). Přepnutí jazyka nemění rozměry,
zarovnání, tokeny kusovníku ani vlastní texty uložené v razítku.

## Základní pohledy, skryté hrany a PDF

Vlastnosti pohledu nabízejí přední, zadní, levý, pravý, horní, dolní a
izometrický pohled. Odvozené projekční pohledy respektují první či třetí
kvadrant listu. Zobrazení má čtyři režimy: pouze viditelné hrany, viditelné
a skryté hrany, stínované s viditelnými hranami a stínované bez hran.
Skryté hrany lze zobrazit čárkovaně nebo šedou plnou čarou.

Projekce používá uložené křivky a trojúhelníky vypočteného tělesa. Rozděluje
hrany v místech zakrytí a doplňuje obrysové křivky zakřivených ploch,
například obě boční tvořící přímky válce. Parametrické švy periodických
ploch se nezobrazují. Obrys odvozený z trojúhelníků není novou modelovou
hranou a nelze na něj připojit kótu jako na stabilní topologickou referenci.
Přesnost hladkých obrysů odpovídá uložené tessellaci zdroje.

**Vlastnosti listu** nastavují tloušťky per v milimetrech. Dohodnuté výchozí
mapování je **bílá 0,50 mm, červená 0,70 mm, žlutá a zelená 0,25 mm**.
Viditelné hrany používají bílou tloušťku; skryté hrany (čárkované i šedé)
a kóty používají slabou tloušťku 0,25 mm. Přepínač
**Náhled tlouštěk** zapíná tyto tloušťky na obrazovce; výchozí režim **Tenké
čáry** umožňuje kreslit bez zobrazení fyzických tlouštěk. Export používá
skutečné tloušťky vždy, nezávisle na pracovním náhledu.
Na černém pracovním pozadí se všechny skryté i viditelné tečné hrany kreslí
tlumenou tmavě šedou (#666666), včetně čárkovaného režimu skrytých hran.
PDF zachovává černé čárkované skryté hrany nebo zvolenou šedou souvislou
variantu; tečné hrany se tisknou podle nastaveného pera a tloušťky.
Čárkovaná skrytá hrana má na papíře čárku 3 mm a mezeru 1,5 mm nezávisle
na měřítku modelu.

**PDF…** na spodní liště nebo **Soubor → Export** v hlavním okně uloží
všechny listy Drawing do jednoho PDF. Samostatné okno Drawing má také
příkaz **Výkres → Uložit jako PDF…**. Každá stránka má skutečný formát svého
listu včetně případné kombinace A4 a A3. Čáry a text se exportují vektorově. Stínovaná výplň používá hloubkově
vyhodnocenou bitmapu při rozlišení exportu 720 dpi, nejvýše 16 milionů
pixelů a 8192 pixelů na stranu pohledu; vložené obrázky zůstávají bitmapami. Výběrové
rámečky, zvýraznění a rozpracované náhledy se netisknou. Běžná pera jsou na
bílém papíře černá, volitelně šedé skryté hrany zůstávají šedé.

Při tisku PDF zvolte **skutečnou velikost / 100 %**. Volba „přizpůsobit
stránce“ v tiskovém programu mění měřítko i fyzické tloušťky. Export nevolá
OCCT ani neregeneruje zdrojové modely; tiskne uložený stav pohledů. Změny
zdrojové geometrie nejprve převezměte explicitním Regenerate pohledu.

### Tečné hrany zaoblení

Vlastnosti pohledu obsahují volbu **Tečné hrany: Silné čáry / Tenké čáry / Skrýt**.
Ta zvýrazňuje hladké přechody mezi plochami, zejména v prostorových pohledech.
Silné používají bílé pero (výchozí 0,5 mm), tenké používají tloušťku 0,25 mm.
Volba se ukládá pro každý pohled a platí i pro PDF. Zakryté tečné hrany se nikdy
nekreslí, ani při zapnutém zobrazení běžných skrytých hran. Vnější obrys zakřivené
plochy zůstává zachován. Pracovní režim tenkých čar nemění tiskové tloušťky.

Rozlišení tečných hran vychází z uložených směrů sousedních ploch; otevření
vlastností nevolá OCCT. Chybějící údaje se nepovažují za důkaz tečnosti.
U již uložených pohledů použijte Regenerovat k obnovení projekce a klasifikace hran.

### Regenerace propojeného výkresu

Příkaz **Regenerovat** obnovuje všechny pohledy na všech listech, i bez výběru
pohledu. Otevřený díl je zdrojem v aktuálním neuloženém stavu; uzavřený díl se
načte z uvedeného souboru. Smazání posledního tělesa vyprázdní také projekce.
Aktualizují se i vazby kót a kusovník. Změna se použije až po úspěšném načtení
všech zdrojů. Projekce v souboru výkresu jsou uložený stav pohledů, nikoli
samostatný model; explicitní regenerace je nahradí podle propojeného zdroje.
