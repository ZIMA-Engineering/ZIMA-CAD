# Skicář ZIMA-CAD

Tento dokument popisuje závazné chování interaktivního zadávání, zachytávání,
vazeb a řešení stupňů volnosti ve skicáři. Datový formát a rovnice jsou popsány
v `SKETCH_MODEL.md`, názvosloví v `SKETCHER-TERMINOLOGY.md` a běžné ovládání
také v `UZIVATELSKY_MANUAL.md`.

## Potvrzování nástrojů

Zadávání geometrie používá jednotné ovládání. Levým tlačítkem se zadávají
počáteční a pomocné body. Poslední bod, který dokončuje právě kreslený objekt,
se zadá jedním krátkým kliknutím prostředního tlačítka. Samostatný bod je
výjimka a kvůli intuitivnímu ovládání se vytvoří jedním levým kliknutím.
Textová kotva se potvrzuje prostředním tlačítkem. Rychlý
dvojklik prostředním tlačítkem prvním klikem okamžitě potvrdí případný poslední
bod a druhým klikem ukončí aktivní nástroj a vrátí skicář na `Výběr`.

Po dokončení geometrie, vazby, kóty nebo jiné opakovatelné operace zůstává
zvolený nástroj aktivní a je připravený k dalšímu zadání. Samovolně se na
`Výběr` nepřepíná. Nástroj se ukončí rychlým dvojklikem prostředním tlačítkem,
výslovným zvolením `Výběr` nebo zrušením příslušného dialogu.

## Výchozí měřítko a text

Běžná modelová skica se při prvním otevření přiblíží podle logického DPI
aktuálního monitoru. Cílem je přibližně 1 modelový mm na 1 fyzický mm
obrazovky. Přiblížení zůstává uživatelské a nejde o kalibrované měření.
Rámečky `.frmz` a razítka `.tblz` místo toho při otevření zobrazí celý list.

Po potvrzení kotevního bodu textu se otevřou společné interní
**Vlastnosti skici**. Dokud uživatel nepotvrdí **OK**, **Použít** nebo
**Zrušit**, view nesmí přijmout další bod textu. Editor používá víceřádkové
pole přibližně pro pět řádků a ukládá zalomení řádků. Stejný dialog a stejné
ovládání se používají při vytvoření i pozdější editaci textu.

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

Výběr geometrické značky `H/V` zvýrazní úsečku i oba její koncové body.
Výběr bodové značky `H/V` zvýrazní pouze referenční a řízený bod. Při kreslení
má kombinace totožnosti `C` a bodové `H/V` přednost před automatickou
kolmostí i rovnoběžností. Značka `H/V` se v náhledu zobrazuje přímo u právě
zadávaného druhého bodu. Při tečném vytažení úsečky ke kružnici, oblouku,
elipse nebo eliptickému oblouku se u kontaktního bodu zobrazují současně `C`
a `T`.

Výběr vazby `C` bodu na ose zvýrazní běžnou výběrovou azurovou barvou bod i
osu. Totéž platí pro jinou referenci, ke které vazba `C` náleží.

Při vytahování nové úsečky kolmo z existující geometrie patří náhledová značka
`⊥` vždy k prvnímu, již potvrzenému kontaktnímu bodu. Zachycení druhého bodu
nesmí značku kolmosti přesunout na konec nové úsečky.

## Ostatní vazby

- **Totožná**: první bod je reference, druhý bod se s ním ztotožní.
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
odstraní pouze vazbu. Základní značky jsou `H`, `V`, `C`, `M`, `T`, `=`, `S`,
`∥` a `⊥`.

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
