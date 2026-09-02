# Skicář ZIMA-CAD

Tento dokument popisuje závazné chování interaktivního zadávání, zachytávání,
vazeb a řešení stupňů volnosti ve skicáři. Datový formát a rovnice jsou popsány
v `SKETCH_MODEL.md`, názvosloví v `SKETCHER-TERMINOLOGY.md` a běžné ovládání
také v `UZIVATELSKY_MANUAL.md`.

## Potvrzování nástrojů

Zadávání geometrie používá jednotné ovládání. Levým tlačítkem se potvrzují
všechny definiční body včetně bodu, který dokončuje běžný objekt. Krátký klik
prostředním tlačítkem geometrii nikdy nevytváří ani nepotvrzuje; je vyhrazený
navigaci. Textová kotva se zadává levým tlačítkem ve View. Rychlý dvojklik
prostředním tlačítkem ukončí aktivní nástroj a vrátí skicář na `Výběr`.
U vícebodové B-spline před ukončením uloží pouze již potvrzené LMB body;
poloha kurzoru při dvojkliku se nikdy nepřidá jako další bod.

`Enter` uvnitř číselného nebo textového editoru potvrzuje pouze jeho hodnotu;
nesmí zároveň odeslat geometrii, zavřít Vlastnosti ani vyvolat **OK**.

Po dokončení geometrie, vazby, kóty nebo jiné opakovatelné operace zůstává
zvolený nástroj aktivní a je připravený k dalšímu zadání. Samovolně se na
`Výběr` nepřepíná. Nástroj se ukončí rychlým dvojklikem prostředním tlačítkem,
výslovným zvolením `Výběr` nebo zrušením příslušného dialogu.

## Jednotné zrušení a Escape

Skicář používá jednu centrální stavovou akci **Zrušit**, kterou vyvolá klávesa
`Esc` i budoucí tlačítko v liště. Jednotlivé nástroje nesmějí implementovat
vlastní neslučitelné varianty Escape. Akce postupuje od nejmenšího
rozpracovaného stavu k celému nástroji:

1. první `Esc` zruší právě rozpracovaný bod, dočasnou geometrii nebo aktuálně
   zvoleného kandidáta, ale ponechá opakovatelný nástroj aktivní;
2. další `Esc`, pokud už není nic rozpracováno, ukončí aktivní nástroj a
   přepne skicář na **Výběr**;
3. v režimu **Výběr** `Esc` zruší hover a potvrzené označení;
4. otevřené Vlastnosti se klávesou `Esc` nezavírají a jejich změny se tím
   nezahazují; k tomu slouží explicitní **Zrušit** v dialogu.

Tlačítko **Zrušit** musí volat tutéž stavovou akci jako klávesa `Esc`. Obdobně
krátký prostřední klik a `Enter` volají jednu centrální akci **Potvrdit**.
Stavový automat, myš, klávesnice a tlačítka tedy nesmějí mít oddělené
implementace téhož příkazu.

## Výchozí měřítko a text

Běžná modelová skica se při prvním otevření přiblíží podle logického DPI
aktuálního monitoru. Cílem je přibližně 1 modelový mm na 1 fyzický mm
obrazovky. Přiblížení zůstává uživatelské a nejde o kalibrované měření.
Rámečky `.frmz` a razítka `.tblz` místo toho při otevření zobrazí celý list.

Po potvrzení kotevního bodu textu se otevřou společné interní
**Vlastnosti skici**. Dokud uživatel nepotvrdí **OK** nebo **Cancel**, view
nesmí přijmout další bod textu. Editor používá víceřádkové
pole přibližně pro pět řádků a ukládá zalomení řádků. Stejný dialog a stejné
ovládání se používají při vytvoření i pozdější editaci textu.
Výchozí natočení je `0°` a obrys je čitelný zleva doprava bez implicitního
zrcadlení. Zrcadlení provádí pouze výslovná volba **Převrátit vodorovně**.

## Základní princip

Zadávání geometrie je postupné odebírání možností, tedy stupňů volnosti.
Každý potvrzený údaj omezuje následující údaj, ale bez výslovného konfliktu
nesmí rušit dříve potvrzené podmínky.

Při kreslení vícebodové geometrie platí:

1. První potvrzený bod je kotva a určuje výchozí podmínky.
2. Právě zadávaný bod je řízený kurzorem a přizpůsobuje se již potvrzeným
   bodům, aktivním inferencím a zachytávání.
3. Potvrzením se vybraná inference uloží jako skutečná vazba.
4. Pokud žádná inference neplatí, bod se uloží volně.

V jedné poloze skici existuje pouze jeden interní bod. Potvrzení nabídky `C`
na existujícím bodě znovu použije jeho stabilní ID; nesmí vytvořit druhý bod
ve stejné poloze a teprve jej spojit duplicitní vazbou. Navazující úsečky proto
sdílejí jeden skutečný koncový bod. Nabídka `C` je v tomto okamžiku informace
náhledu; po sloučení dvou nativních bodů nezůstává samostatná vazba ani značka
`C`. Trvalé `C` patří pouze bodu ležícímu na jiné geometrii.

Vratný radius společného rohu vzniká výběrem dvou napojených úseček a tažením
jejich společného bodu. Toto přesné gesto má přednost před běžným tažením
bodu. Skupinový přesun více vybraných geometrií není podporován; jinak se
přesouvá pouze přímo uchopený bod v mezích solveru. Radiusová kóta je při
editaci skici řídicí a měnitelná dvojklikem. V běžném výsledném view je stejně
jako ostatní skicové kóty skrytá, samotný oblouk zůstává součástí profilu.

U kreslené úsečky nebo konstrukční čáry se automatická směrová inference
ukládá jako bodová vazba `H/V` na druhém bodu, nikoli jako další vazba celé
čáry. Pokud jsou oba konce už totožně připojené k počátku nebo ke stejné ose,
jejich vazby `C C` směr určují a redundantní `H/V` se nevytvoří. Aktivní druhý
bod je při zadávání vždy zobrazen oranžově.

Obdélník používá stejnou reprezentaci: jeho hrany nenesou geometrické `H/V`.
Směr je uložen bodovými vazbami mezi navazujícími rohy; první roh je kotva a
další rohy jsou postupně řízené. Poslední roh uzavírají bodové vazby vůči
předchozímu rohu a prvnímu rohu.

Při zadávání jsou oranžovým kolečkem zobrazené oba vznikající konce ramen,
která vycházejí z prvního rohu: konec vodorovné a konec svislé hrany. Běžný
obdélník lze po určení prvního rohu a velikosti potvrdit prostředním tlačítkem.

Po zadání prvního rohu přepne pravý klik v prázdném prostoru do režimu výběru
osy. Pravý klik nad geometrií tento režim nezapne a zachová běžné přepínání
překrývajících se kandidátů. Konstrukční čára se při najetí zvýrazní oranžově
a levý klik ji zvolí jako osu; vybraná osa se zobrazí modře. Pohyb kurzoru pak
určuje délku natočeného obdélníku podél osy a prostřední tlačítko jej potvrdí.
První roh zůstává referenční; jeho protějšek a oba vzdálené rohy jsou řízené.
Uloží se dvě vazby symetrie a jedna nezbytná rovnoběžnost podélné strany s
konstrukční osou. Druhá podélná rovnoběžnost a kolmost příčných stran už ze
symetrie vyplývají, proto se neukládají duplicitně.

Stejně se `H/V` nevytváří, pokud oba body nové úsečky nebo konstrukční čáry
leží vazbou `C` na téže vodorovné či svislé úsečce. Společná nosná geometrie
už jejich směr určuje; zůstanou pouze vazby `C`.

Při dodatečném vytváření vazby výběrem existujících prvků platí:

1. První vybraný prvek je reference.
2. Druhý vybraný prvek je řízený a při vytvoření vazby se přizpůsobí.
3. Výjimka musí být uvedena u konkrétního nástroje a nesmí vzniknout jen jako
   vedlejší efekt řešiče.

Po vytvoření je vazba matematický vztah. Pokud pozdější kóta nebo vazba pohne
referencí, řízený prvek ji následuje. U nedostatečně zavazbené skici řešič
volí řešení nejbližší poslednímu platnému stavu.

## Priority interaktivního zadávání

Kandidáti se vyhodnocují pouze uvnitř obrazové tolerance. Vyšší priorita má
přednost před nižší; vzdálenost od kurzoru rozhoduje mezi kandidáty stejné
priority.

1. **Totožnost** — existující bod nebo lokální počátek skici.
2. **Bod na geometrii** — osa, úsečka, oblouk, kružnice nebo externí reference.
3. **Zarovnání bodů** — stejné Y (`H`) nebo stejné X (`V`) vůči existujícímu
   bodu.
4. **Směr od předchozího bodu** — vodorovná nebo svislá nová geometrie.
5. **Geometrická inference nástroje** — tečnost, kolmost, rovnoběžnost,
   průsečík, střed a charakteristický bod křivky.
6. **Volné umístění**.

Povinná návaznost konkrétního nástroje může pořadí lokálně změnit. Typickým
příkladem je tečné pokračování úsečky z kružnice nebo oblouku: potvrzený bod
dotyku je kotva a tečný směr omezuje pohyb druhého bodu dříve než běžná
vodorovná či svislá inference. Aktivní kandidát musí být vždy viditelný v
náhledu; skryté přepsání uživatelova záměru není dovoleno.

Pokud je pod kurzorem více platných objektů nebo zachycení, pravé tlačítko je
při zadávání postupně překlikává. Zvláštní pravá akce konkrétního nástroje se
provede až tehdy, když pod kurzorem není další kandidát k výběru.

Stejný cyklus se má rozšířit také na platné varianty inference/vazby pro právě
zadávanou geometrii. Příklad úsečky může podle situace nabídnout volný bod,
totožnost, bod na geometrii, vodorovnost, svislost, tečnost, kolmost nebo
rovnoběžnost. Do cyklu vstupují pouze matematicky platné a nekonfliktní
varianty. Pravý klik mění oranžově zobrazenou variantu, levý nebo prostřední
klik potvrdí přesně tuto variantu a potvrzená inference se uloží jako explicitní
vazba. Automatika smí nabízet a řadit kandidáty, ale nesmí po potvrzení vytvořit
jinou skrytou vazbu, než jakou ukazoval náhled.

## Stupně volnosti bodu

Volný bod má dvě možnosti pohybu: X a Y.

- vodorovná vazba bodů určí Y řízeného bodu podle reference a odebere jeden
  stupeň volnosti;
- svislá vazba bodů určí X řízeného bodu podle reference a odebere jeden
  stupeň volnosti;
- totožnost určí X i Y a odebere oba stupně volnosti;
- bod na přímce nebo křivce ponechá obvykle jeden parametr pohybu;
- souřadnicová kóta určí příslušnou souřadnici;
- další nezávislé pravidlo může určit poslední zbývající možnost.

Redundantní vazba se nemá přidat podruhé. Vazba odporující již platným
podmínkám se odmítne a uživatel dostane jednoznačné hlášení. Skicář nesmí kvůli
novému konfliktu tiše odstranit starší vazbu.

## Vodorovná a svislá vazba

Stejný příkaz podporuje geometrii i body.

### Úsečka nebo konstrukční čára

Vybere se jeden prvek. Jeho první definiční bod zůstane při vytvoření na místě
a druhý bod se srovná:

- `H`: oba body úsečky mají stejné Y;
- `V`: oba body úsečky mají stejné X.

Značka se zobrazuje uprostřed geometrie.

### Dva body

Nejprve se vybere referenční bod a potom řízený bod:

- `H`: řízený bod převezme Y referenčního bodu;
- `V`: řízený bod převezme X referenčního bodu.

Značka `H` nebo `V` se zobrazuje u řízeného, tedy druhého bodu. Výběr značky
zvýrazní oba body. Odstranění vazby neodstraní žádný z bodů.

Výběr geometrické značky `H/V` zvýrazní její dva řídicí koncové body, nikoli
celou úsečku. Trvalé `C` zvýrazní bod a jeho nosnou geometrii; dva sloučené
nativní body už žádnou vazbu `C` nemají. `T` zvýrazní obě tečné geometrie.
Fixovaný bod používá značku `F`; `K` je vyhrazeno přesnému generovanému
charakteristickému bodu křivky.
Výběr bodové značky `H/V` zvýrazní pouze referenční a řízený bod. Při kreslení
má kombinace totožnosti `C` a bodové `H/V` přednost před automatickou
kolmostí i rovnoběžností. Značka `H/V` se v náhledu zobrazuje přímo u právě
zadávaného druhého bodu. Při tečném vytažení úsečky ke kružnici, oblouku,
elipse nebo eliptickému oblouku se u běžného kontaktního bodu zobrazují
současně `C` a `T`. Pouze přesný dotyk v potvrzeném čtvrtinovém bodě používá
`K + T`.

Výběr vazby `C` bodu na ose zvýrazní běžnou výběrovou azurovou barvou bod i
osu. Totéž platí pro jinou referenci, ke které vazba `C` náleží.

Při vytahování nové úsečky kolmo z existující geometrie patří náhledová značka
`⊥` vždy k prvnímu, již potvrzenému kontaktnímu bodu. Zachycení druhého bodu
nesmí značku kolmosti přesunout na konec nové úsečky.

## Ostatní vazby

- **Shodnost**: dva nativní body se sloučí do jednoho stabilního topologického
  bodu bez uložené značky `C`. Po výběru bodu lze jako druhý prvek zvolit osu,
  úsečku či křivku; tehdy vznikne trvalá vazba bodu na geometrii `C`. Hlavní
  osu X/Y lze zvolit také jako první referenci a potom vybrat řízený bod.
- **Rovnoběžná**: první čára je reference, druhá se natočí rovnoběžně.
- **Kolmá**: první čára je reference, druhá se natočí kolmo.
- **Stejná**: první délka nebo poloměr je reference, druhý ji převezme.
  Značka `=` se kreslí pouze u druhého, řízeného potomka; reference se
  zvýrazní až při výběru vztahu.
- **Střed**: nejprve se vybere řízený bod, poté referenční úsečka; jde o
  výslovnou výjimku, protože typ druhého prvku určuje význam operace.
- **Symetrická**: vyberou se dva body a následně osa symetrie; oba body tvoří
  společně řízenou dvojici.
- **Tečná**: pořadí a pevný kontaktní bod závisí na podporované dvojici křivek
  a je zobrazen v náhledu značkou `T`.
- **Soustředná**: první kružnice nebo oblouk je reference, druhý převezme
  střed.

## Společná tečná úsečka

**Společná tečna** je nástroj tvorby geometrie, nikoliv pouze dodatečná vazba.
Přijímá kružnici, kruhový oblouk, elipsu, eliptický oblouk nebo B-spline.
Úsečky, body, osy, externí reference a křivky jiné skici se během příkazu
nenabízejí.

Postup je následující:

1. uživatel klikne na první křivku poblíž požadovaného dotyku;
2. klikne na druhou křivku poblíž druhého požadovaného dotyku;
3. obě polohy kliknutí určují počáteční větev řešení — například horní,
   dolní, levou, pravou, vnější nebo vnitřní tečnu;
4. solver vytvoří běžnou profilovou úsečku a uloží oba její konce jako body
   na příslušných křivkách společně s tečností na obou stranách.

Výsledkem není neasociativní vypočtená čára. Úsečka, oba dotykové body a čtyři
vztahy zůstávají v persistovaném ZIMA Sketch modelu a po změně zdrojových
křivek se znovu řeší. Výpočet používá pouze analytickou nebo persistovanou
skicovou geometrii; OCCT se při hoveru, výběru ani vytvoření nevolá.

Oblouky a otevřené B-spline navíc omezují dotyk na vlastní parametrický rozsah.
Pokud v okolí zvolených míst společná tečna neexistuje, je degenerovaná nebo
je v konfliktu s existujícími vazbami, odmítne se celá operace bez bodu,
úsečky či vazby navíc. První klik je pouze transientní stav. `Escape` jej
zruší; druhý platný klik uloží vše jako jednu vratnou revizi.

## Výběr, tažení a transientní zobrazení

Výběr obdélníkem ukládá jednu množinu bodů, čar, křivek a textů. Tree tuto
množinu pouze zrcadlí a nesmí ji při označování jednotlivých řádků postupně
zmenšovat. `Delete` odstraní celý výběr v jedné revizi a zároveň bezpečně
odstraní osiřelé body a související vazby.

Tažení bodu nebo kóty pracuje nad transientní kopií dokumentu. Náhled smí
zobrazit pouze aktivní skicu a stejný pasivní modelový kontext jako běžný
Skicář; ostatní skici Partu se během stisku myši nesmějí dočasně objevit.

Tažení jednoho bodu respektuje jeho geometrický význam. Střed kružnice nebo
oblouku překládá příslušnou křivku bez změny poloměru. Koncový bod kruhového
oblouku je radiální rukojeť: jeho vzdálenost od středu mění poloměr a jeho směr
mění rozsah oblouku. Zamknutá poloměrová kóta ponechá poloměr pevný a dovolí
pouze úhlový pohyb konce. Nezamknutá řídicí poloměrová kóta převezme hodnotu
dosaženou přímým tažením.

Stejný rigidní překlad středu platí při vytvoření vazby a při topologickém
sloučení, nejen při přímém tažení. Na osu nebo konec úsečky se proto nepřesune
samotná souřadnice středu odděleně od konců oblouku. Přenesou se všechny
závislé řídicí a kontaktní body; pevná či externě ukotvená závislost operaci
transakčně odmítne.

Pokud View obsahuje více vybraných bodů nebo geometrií a tah začne na jednom
z vybraných bodů, celý výběr se z původního stavu přeloží jedním společným
`ΔX, ΔY`. Vnitřní délky, úhly, poloměry a vazby se zachovají. Vybraný bod
slouží pouze jako rukojeť. Fixovaný nebo externě řízený bod a vazba vedoucí do
nevybrané ukotvené části pohyb omezují; nesmějí se tiše odpojit.

## Vícekrokové křivky

U oblouku, elipsy, eliptického oblouku a obou typů spline zůstávají všechny již
potvrzené zadávací body během dalšího kroku viditelné. Zachycení na běžnou
geometrii se nabízí jako `C`, na charakteristický čtvrtinový bod kružnice,
oblouku nebo elipsy jako `K`. Potvrzená nabídka se uloží jako skutečná vazba;
náhled nesmí ukázat vazbu, která po dokončení zmizí.

Skicář nabízí dvě samostatné varianty nad stejným stabilním bodovým modelem.
**B-spline – řídicí body** používá potvrzené body jako řídicí vrcholy;
**Interpolační spline** všemi potvrzenými body skutečně prochází. Po prvním
potvrzení se zobrazuje bod, po druhém lomený náhled a od třetího skutečný
náhled zvoleného typu spline. Obě varianty vyžadují nejméně tři body a rychlý
dvojklik prostředním tlačítkem je dokončí na posledním potvrzeném bodě;
jednoduchý prostřední klik zůstává vyhrazen navigaci. Tříbodová spline zůstává
spline a nenabízí společný kruhový radius.

Dodatečná vazba `T` na konci otevřené B-spline zachová společný kontaktní bod
i připojenou úsečku a upraví sousední řídicí bod spline. Koncová tečna se
vyhodnocuje z přesné derivace koncového ramene, nikoli z obrazově vzorkované
polyčáry. `C` a `T` zůstávají samostatně zobrazitelné a odstranitelné vazby.

Tečný oblouk v **Lomené čáře** zobrazuje svůj odvozený střed a společný
počáteční bod. Přiblížení středu k hlavní ose X/Y nabídne `M`; potvrzení uloží
střed oblouku vazbou na danou osu. Koncový bod oblouku současně používá běžné
významné body `K` a bodové zarovnání `H/V` vůči existujícím bodům. Přesný `K`
má přednost před odvozeným přichycením středu k ose a potvrzené `H/V` se uloží
jako skutečná bodová vazba.

Text je po dobu umístění kreslicí nástroj, takže obdélníkový výběr nesmí
spotřebovat kliknutí do prázdného View. Levý klik určí kotvu a okamžitě zobrazí
transientní obrys podle hodnot v interních Vlastnostech. `OK` jej uloží,
`Cancel` nezmění skicu.

## Automatická kóta

Skicář vystavuje jeden příkaz **Automatická kóta**. První dva potvrzené body
určují délkovou referenci; úsečka nebo osa předá své dva definiční body. Pohyb
kurzoru zobrazuje délkovou, vodorovnou nebo svislou variantu podle polohy.
Kliknutí do prázdného View potvrdí její umístění. Kliknutí na další nabízenou
úsečku, osu nebo body pokračuje ve stejném příkazu směrem k úhlové kótě.

Úhlová kóta se vytvoří až ze dvou úplných směrů, tedy ze čtyř bodů. Dvě
úsečky, dvě osy nebo jejich kombinace jsou pouze zkratkou pro stejné čtyři
body. Druhá reference musí zůstat před potvrzením oranžově zvýrazněná. Po
získání obou směrů sleduje oblouk i text přesnou polohu kurzoru; poslední klik
určí výseč, znaménko, poloměr a skutečné uložené umístění kóty. Tažení
uloženého bodu kóty nesmí samo přepnout na sousední výseč.

Při editaci hodnoty je změna transakční. Solver zachová zvolenou úhlovou
větev a využije zbývající stupně volnosti navazující geometrie. Typický případ
je řetěz dvou úseček: první má délku od pevného počátku, mezi úsečkami je úhel
a vzdálený konec druhé leží na ose. Změna délky pohne společným bodem a solver
dopočítá nový průsečík druhé úsečky s osou bez změny úhlu. Duplicitní nebo již
jinou vazbou určená kóta se odmítne a skica zůstane beze změny.

## Regresní scénáře solveru

Tyto případy tvoří průběžnou ověřovací matici; základní varianty jsou již
pokryté a při rozšíření solveru se nesmějí ztratit:

1. **Úhlové kóty** — dále rozšiřovat regresní kombinace pro řídicí, zamknutou
   a referenční variantu, záporné hodnoty a odstranění ve složitějších
   zavazbených řetězcích.
2. **Navazující křivky** — učit mobilitu řetězců úsečka–oblouk,
   oblouk–úsečka, eliptický oblouk–úsečka a úsečka–B-spline se samostatnými
   kombinacemi `C`, `T`, `H/V`, pevného bodu a řídicí kóty. Každý tah musí mít
   vratný test `A -> B -> A`; u volného konce úsečky tečné ke konci kruhového
   oblouku je dopředný tah ověřený, ale návrat po stejné větvi zatím může
   klást odpor a zůstává otevřenou chybou. Přímé tažení samotného konce oblouku
   funguje správně a není součástí této chyby.
3. **Středy na osách** — bodová vazba středu kružnice/oblouku na hlavní osu,
   obě pořadí výběru osy a bodu, rigidní přenos konců oblouku a sloučení středu
   s koncem úsečky. Solver musí pohyb propagovat do volné větve a nesmí
   odtáhnout aktivní bod.
4. **Křivkové parametry** — současně měnit poloměr/natočení kruhových a
   eliptických objektů, velikost a natočení mnohoúhelníku a koncové rameno
   B-spline bez porušení kontaktního bodu.
5. Každý případ ověřit prakticky v C++ aplikaci a převést jeho posloupnost
   kliknutí, zobrazené inference a persistované vazby na regresní test.

Při zadávání druhého bodu úsečky nebo konstrukční čáry se v omezené obrazové
toleranci nabízejí také délky existujících úseček. Kandidát přichytí nový
konec na stejnou délku, zobrazí `=`, oranžově zvýrazní referenční úsečku a po
potvrzení uloží skutečnou vazbu stejné délky. Tato nabídka nepřebíjí `C`,
bodové `H/V` ani povinnou tečnou návaznost.

Stejná délka se nabízí také při vodorovném nebo svislém zadávání. Náhled v
takovém případě ukáže současně `=` a `H/V` a po potvrzení uloží obě nezávislé
podmínky.

Druhý bod nové úsečky lze magneticky nabídnout jako zrcadlo prvního bodu vůči
konstrukční čáře. Náhled zvýrazní osu a ukáže `S` společně s `⊥`; je-li
spojnice dvojice vodorovná nebo svislá, ukáže místo toho `S + H/V`. Potvrzením
se uloží skutečná symetrická vazba obou bodů ke konstrukční ose. Samostatná
redundantní kolmost ani `H/V` se neukládá, protože je již důsledkem symetrie.

## Značky a výběr

Značka vazby patří řízenému prvku. Přejetí zvýrazní vztah oranžově, výběr
modře. Výběr značky musí umožnit dohledat všechny účastníky. Odstranění značky
odstraní pouze vazbu. Základní značky jsou `H`, `V`, `C`, `K`, `M`, `T`, `F`,
`=`, `S`, `∥` a `⊥`. `C` značí libovolnou polohu bodu na geometrii, `K` pouze
přesný generovaný charakteristický bod. Samotný typ „křivka“ nikdy není
důvodem změnit `C` na `K`.

Počet současně zobrazených náhledových značek není pevně omezen. Bodové a
vztahové značky u stejného bodu používají společné pořadí a vodorovné sloty,
aby se kombinace jako `C + H`, `C + T`, `= + H` nebo `S + V` nekreslily přes
sebe.

Náhledové značky u stejného bodu sdílejí také jednu svislou základní linku.
Značka stejné délky `=` patří geometrii, proto se při zadávání vždy kreslí
uprostřed nové úsečky; případná současná značka `H/V` zůstává u druhého bodu.

## Konflikty a výjimky

Výjimka z pořadí reference–řízený prvek je přípustná pouze tehdy, pokud ji
vyžaduje gesto nástroje nebo již plně zavazbený účastník. Nástroj musí výjimku
oznámit nebo jasně ukázat v náhledu; nesmí pořadí obrátit potichu. Dlouhodobým
cílem je ukládat u vztahů explicitní role reference a řízeného prvku a použít
je při řešení i při zobrazení závislostí.
