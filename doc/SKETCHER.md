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

Ve vlastnostech textu je **Režim textu**:

- **Běžný text** zobrazuje vyplněné znaky a zůstává editovatelnou textovou
  entitou. Protrusion, Revolve i tažení jej ignorují při sestavování profilu.
- **Geometrie pro modelování** nabízí obrysy znaků jako profil. Uzavřené obrysy
  vytvářejí tělesa, vnitřní obrysy písmen zůstávají otvory.

Přepnutí režimu nepřevádí text na jednotlivé čáry ani neztrácí jeho obsah,
polohu a zarovnání. Oba režimy používají jeden dialog pro tvorbu i editaci,
jedno OK/Cancel a společné potvrzení dvojklikem prostředního tlačítka myši.
Nové texty razítek a rámečků používají běžný režim; běžná modelová skica
nabízí ve výchozím stavu geometrii. Texty starého razítka s ID jako
`field1:text` lze vybírat a editovat stejně jako nově vytvořené texty.

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

Při přepnutí z oblouku zpět na úsečku v **Lomené čáře** zůstává konec
oblouku společným bodem. Při aktivní automatické tečnosti pokračování
nabízí přichycení `C T`, zvýrazní podpůrný oblouk a po potvrzení uloží
tečnou vazbu. Náhled i potvrzení používají stejnou inferenci. Střed oblouku
je samostatný bod; na styku úsečky a oblouku se druhý bod nevytváří.

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

U bodů na společné svislici automatická volba nabídne svislý rozměr,
u bodů na společné vodorovné přímce vodorovný rozměr, i když kurzor leží
za koncem úsečky. Nesmí omylem zvolit nulový kolmý průmět. Odmítnutí
nadbytečné nebo konfliktní kóty zobrazí zprávu a ponechá rozpracovanou kótu;
výjimka solveru nesmí ukončit aplikaci.

Stejný postup platí i ve vlastněných skicách sweepů. GUI regrese 2D Sweepu
kreslí řetěz úsečka–oblouk–úsečka od počátku skici dráhy, přímo přepne na
kótování první úsečky a potvrdí ji kliknutím do prostoru. Po opětovném
vstupu ověřuje také kótování úsečky a dvojice jejích bodů a kontroluje
sdílené konce bez přebytečných bodů.

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

Univerzální kóta přijímá symetrickou délkovou kótu posloupností **bod – osa –
bod** (stejně také **osa – bod – bod**). Už po výběru bodu a osy zobrazuje
editovatelný náhled kolmé vzdálenosti. Třetí výběr stejného bodu vytvoří
průměrovou symetrickou kótu přes osu; výběr jiného bodu řídí oba body na
opačných stranách osy polovinou celkové hodnoty. Osa může být hlavní osa skici
nebo konstrukční čára. Symetrické bodové, lineární a úhlové kóty jsou řídicí
rovnice solveru, nikoli pouze grafické anotace, a proto se podílejí na stupních
volnosti, redundanci, editaci hodnoty i tažení geometrie.

Pokud je kótovaný bod současně průsečíkem s nosnou úsečkou, solver přenese
změnu do této úsečky a teprve potom dopočítá ostatní vazby. Jestliže koncový bod
šikmé stěny řídí symetrická délková kóta a stěnu současně řídí symetrický úhel,
zůstává tento bod kotvou a natáčí se volný konec stěny. Tím se obě řídicí kóty
navzájem neruší ani u profilu určeného pro rotaci.

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

## Uzavření spline, výběr účastníků a posuv po ose (2026-09-06)

Při zadávání spline lze poslední bod přichytit k prvnímu. Uzavření sdílí jednu
identitu koncového bodu. Příkaz **Tečná** podporuje dvě spline se společným
koncovým bodem vytvořeným vazbou C i výběr téže spline dvakrát pro tečnost
v jejím společném začátku a konci. Nejde o periodickou 3D spline; tato změna
patří výhradně skicáři. Samouzavření s tečností potřebuje dostatek řídicích
bodů pro nezávislá koncová ramena. Náhled nepřidává druhý shodný bod, pokud
kurzor zůstává na právě potvrzeném bodě, například na ose.

Výběr kóty nebo značky vazby ve View či Tree zvýrazňuje její související
geometrii. Účastníci pocházejí z uložených referencí skici. Délková kóta
vytvořená kliknutím na běžnou úsečku používá její koncové body A a B.
Kóta viditelně zkrácené úsečky s rádiusem zachovává také referenci úsečky,
aby měřila skutečné tečné konce.

Zamčená délka spojnice mezi středem úsečky a bodem na ose nefixuje celý
bod na ose. Při tažení volného konce první úsečky se střed přepočítává a
konec spojnice může klouzat po ose. Solver pro délkovou podmínku hledá
průsečík podpůrné přímky s kružnicí danou délkou a zvolí bližší řešení.
Nedosažitelná poloha se odmítne bez částečného zápisu. Zamčená délka se
nemění; odemčené kóty při tažení sledují dosaženou geometrii.

Regresní test `zima_cpp_sketcher_contract_tests` obsahuje konstrukci této
soustavy i uloženou skicu z hlášeného `02.prtz` ve fixture
`cpp/tests/fixtures/midpoint_axis_locked_rod.json`. Ověřuje několik dosažitelných
tahů, zachování vazeb a zamčené délky i odmítnutí nedosažitelné polohy.

### Stav ověření

Po opravě prošly testy skicáře, obecné dokumentové testy, testy 3D křivky/Sweepu
a kontrola aplikace `--verify-startup`. Samostatný test oken naposledy skončil
na kontrole odloženého otevření katalogu závitů (`Deferred thread catalog did
not open after the pointer gesture`); tento problém není touto opravou vyřešen.
Dříve hlášené dočasné zablokování výběru kóty, které uvolnil nový příkaz Kóta,
nemá zatím potvrzenou příčinu a nelze je považovat za opravené.

### Kruhové externí reference

Vazby Stejné (poloměr) a Soustřednost přijímají také externí kružnici nebo
kruhový oblouk. Zdrojem může být hrana nebo jediný kruhový obrys/řez externí
plochy. Externí geometrie zůstává referencí; mění se vlastní kružnice či
oblouk skici. Stejné sjednotí poloměr, Soustřednost sjednotí střed.

Rozpoznání středu a poloměru používá uložené body reference bez OCCT a
ověřuje všechny body; není závislé na rovnoměrném vzorkování. Elipsy,
neplatné reference a více obrysů jedné plochy se nepovažují za jedinou
kružnici. Plocha ležící v rovině skici poskytuje svůj konečný obrys,
nekoplanární plocha zachovává dosavadní průsečnici/řez. Uložení a opětovné
otevření zachová vazby i identitu zdroje; změna reference aktualizuje vazbu.

Skici 2D Sweepu a Helixu lze otevřít ze stromu i při nedořešené předchozí
geometrii. Pokud nelze aktuálně odvodit jejich rámec, editor použije uloženou
rovinu a uvede chybějící závislost ve stavovém řádku. Neprovádí při otevření
OCCT výpočet ani nepotvrzuje neplatný solid; Cancel zachová původní historii.
Regrese kontroluje všechny dvě, respektive tři skici také bez vypočteného
předchozího tělesa.

### Automatická tečnost a společná tečna kružnic (2026-09-07)

Při kreslení společné tečny úsečkou mezi dvěma kružnicemi se na obou
koncích ukládá C + T (bod na kružnici a tečnost). Významné body slouží
k výběru větve; kontakt se nezamyká do kvadrantu další vazbou K.
Náhled ukazuje C + T u obou kontaktů. Pokud jiná automatická tečná vazba
už vyplývá z existujících vazeb, její nadbytečnost nezruší vytvoření úsečky.
Konfliktní nebo neplatná vazba se tímto pravidlem neignoruje. Ručně zadávaná
vazba nadále hlásí nadbytečnost.

Společná tečna dvou kružnic používá přesné geometrické kandidáty. Volbu větve
určují polohy obou kliknutí. Platí to také pro stejně velké kružnice s vodorovně
zarovnanými středy a pro kružnice s pevně danou polohou a poloměrem.

### Kotevní bod textu

Text razítka zvýrazňuje při hoveru oranžově a při potvrzení azurově také svůj
kotevní bod. Bod zůstává samostatným bodem skici; zvýraznění nepřidává druhý
objekt do výběru. Svislé zarovnání Dole / Uprostřed / Nahoře se vztahuje ke
skutečnému rozsahu znaků, včetně diakritiky a více řádků. Při otevření šablony
se obrysy textů znovu odvodí z jejich hodnot, kotev a nastavení zarovnání.

### Tažení při zamčených kótách

Při tažení se kurzor promítne do povoleného směru, pokud polohu vůči počátku,
pevné referenci nebo ose určuje zamčená vodorovná/svislá vzdálenost. Zamčená
šířka obdélníku tedy nebrání změně výšky a zamčená výška nebrání změně šířky.
Totéž platí pro souřadnicové kóty X/Y, záporné souřadnice a přenos přes vazby
H/V a koincidenci. Odemčené řídicí kóty se přizpůsobují výsledku tažení;
zamčené hodnoty zůstávají pevné. Úplně zamčený bod se neposune.

## Konstrukční geometrie

Úsečky, kružnice, kruhové i eliptické oblouky, elipsy a B-spline lze přes
kontextové menu ve View i stromu přepnout příkazem **Převést na pomocnou
geometrii** a vrátit příkazem **Převést na obrys profilu**. Všechny pomocné
křivky se zobrazují čerchovaně, také při hoveru a potvrzeném výběru.

Přepnutí zachovává identitu, řídicí body, rozměry, rozsah i uložené vazby
nebo kóty; mění roli geometrie. Pomocná geometrie se nepoužívá jako profil
pro vytvoření tělesa. Úsečka zůstane konečná a oblouk si ponechá své konce.
Nekonečná osa je samostatná možnost: čerchování z úsečky osu neudělá.

### Textové šablony a kóty ve vlastnostech (2026-09-09)

Přepínač **Převrátit vodorovně** popisuje viditelné zrcadlení v aktuálních
souřadnicích skici. Normální text v razítku či rámečku jej má vypnutý stejně
jako text běžné skici. Uložená orientace a kontury existujících šablon se
nemění; odlišný směr os šablony se převádí pouze při čtení a potvrzení dialogu.

Přímá změna kóty dvojklikem ve Sketcheru upravuje aktuální pracovní skicu,
včetně profilu a dráhy 2D tažení, šroubovice, Sweep/Loftu a skici řezu.
Zámek kóty nadále chrání tažení geometrie; úmyslná změna číselné hodnoty
zůstává možná. Výpočet solidu patří až potvrzení celého kontejneru.
Vlastnosti kontejneru zobrazují i kóty jeho vlastních skic. Dvojklik na kótu
umožňuje změnit její hodnotu přímo při otevřených vlastnostech Skici,
Vytažení, Rotace, 2D tažení, šroubovice a Sweep/Loftu. Náhled používá
rozpracovanou skicu, včetně opakovaných změn a návratu přes tlačítko Skica.
**OK** potvrdí celý kontejner a provede jeho výpočet; **Zrušit** rozpracované
změny zahodí. Kóty uložených profilů a drah jsou dostupné také při zobrazení
parametrů kontejneru ve View.

Při změně poloměru oblouku solver respektuje i tečnou úsečku, jejíž druhý
konec patří dalšímu oblouku. Tečný bod se může posunout po své kružnici;
při jeho fixaci se hledá průsečík tečny s kružnicí protějšího oblouku.
Konce zůstávají na svých obloucích, uložené vazby se nemění a neřešitelná
změna se odmítne bez zásahu do původní skici. Zamčená kóta nadále dovoluje
úmyslnou číselnou editaci.

### První rovina a pracovní profil (2026-09-10)

První rovinná polohová reference Vytažení a Rotace určuje skicovou rovinu.
Další reference doplňují umístění a orientaci; nepřebírají roli první roviny.
Front/Back, otočení a odsazení profilu se musí ihned shodovat v náhledu,
kótách profilu, editoru skici a výsledném tělese.

Nový prvek se registruje i v dočasné historii náhledu aktivního tělesa,
aby jeho vlastní skica prošla běžným řešením referencí. Pracovní kopie
skici přebírá vyřešenou rovinu, počátek a osy téhož náhledu; její lokální
2D geometrie se při změně orientace nepřepisuje.

Vstup tlačítkem SKETCH a návrat do vlastností zachovávají celý rozpracovaný
prvek, včetně referencí, orientace, odsazení a délky či úhlu. To platí i při
editaci již vypočteného prvku. **OK** potvrdí aktuální návrh a vypočítá
těleso; **Zrušit** u editovaného prvku zachová původní skicu a parametry.

Regresní test 'zima_cpp_profile_frame_ui_contract' kontroluje první roviny
XY/XZ/YZ, osm kombinací stran a otočení, odsazení, návraty ze skicáře,
storno i shodu uložené skici a mezí vypočteného tělesa po OK.
Audit dále ověřuje první rovinu samostatné Skici, základní kružnice
šroubovice a dráhy 2D tažení. U 2D tažení první reference předvyplní rovinu
dráhy, kterou lze následně samostatně změnit. Průřezy tažení a Sweep/Loftu
zůstávají odvozené z tečny dráhy ve zvoleném bodě.

Oprava používá existující řešení umístění kontejneru beze změny jeho pravidel.

### Okamžitý náhled ořezu a vazby (2026-09-09)

Každé kliknutí nebo dokončené tažení nástroje Ořez se projeví ihned ve View
u samostatné skici, rozpracovaného profilu, šablony i vnořeného dílu.
Vykreslení a výběr používají tutéž rozpracovanou geometrii. Escape vrátí stav
před příkazem; dokončení skici zahrnuje i právě rozpracovaný ořez.

Ořez přenáší tečnost na část křivky, která obsahuje původní bod kontaktu,
i když se v jednom tahu rozdělí oba její vlastníci. Zachované koncové body
a středy si ponechávají svá ID a bodové vazby. Pokud se kontakt ořízne pryč,
příslušná tečnost zanikne. Spojení, které nově představuje jeden společný
koncový bod, zůstává v topologii bez duplicitní rovnice incidence.

Ořez zpracovává také napojení úsečky přes kvadrantový bod **K** kružnice
nebo elipsy. Na zachovaném oblouku převede toto napojení na **C**, případně
na jeden společný koncový bod; neukládá odkaz na již odstraněnou kružnici.
Posun konce oblouku nepřepisuje samostatně bod dalšího oblouku připojeného
úsečkou. Jeho pohyb dopočítají vazby. U stejných poloměrů řídí při tažení
změnu uchopený oblouk bez ohledu na pořadí původního výběru pro rovnost.
Tečnost v existujícím spoji C/K lze přidat i po změně směru úsečky;
kontakt určuje uložený spoj, nikoli tečný bod k dosavadnímu směru úsečky.

### Směrové kóty bodů na osách (2026-09-09)

Bod vázaný na přímku zůstává pohyblivý podél jejího směru. Při změně X/Y kóty
se tato vazba nepovažuje za fixaci obou souřadnic. To dovoluje změnit rozteč
středů profilu s oblouky, tečnými rameny a soustřednými otvory i tehdy,
když jeden střed leží v počátku a druhý na ose X. Poloměry, rovnosti a
tečné vazby se zachovají; test pokrývá původní rozteč 19,448732 mm a její
opakované změny na 12, 20 a 35 mm. Tečné kontakty si ponechávají vlastní
pravidla ukotvení při editaci délky úsečky.
