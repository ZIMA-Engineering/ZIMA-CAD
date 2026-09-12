# Kóty ve výkresu a společné vlastnosti (2026-09-10)

## Vstupy → prostředky → výstupy

Vstupem je uložená geometrie konkrétního výkresového pohledu, jeho kamera,
typ kóty a reference jejích konců. Prostředkem je projekce referenčních
křivek a bodů, společná prezentace kót a interní okno vlastností.
Výstupem je asociativní měřicí kóta v rovině listu. Nemění parametry modelu.
Výběr, náhled, tažení ani otevření vlastností nevyvolávají výpočet OCCT.

## Jedny Vlastnosti kóty

Původní samostatné Zobrazení kóty je sloučeno s vlastnostmi.
Text před/za hodnotou, náhrada textu, tolerance a umístění mají společné
ovládací části pro skicář, Part, Assembly a Drawing.

Skicová kóta si zachovává jmenovitou hodnotu, řídicí/referenční stav
a zamknutí rozměru. Hodnota a umístění se potvrzují v jedné transakci.
U prezentace modelové kóty v Part/Assembly jsou texty a tolerance uloženy
u jejího prezentačního nastavení; změnu parametru dále zajišťuje jeho
hodnotový editor. Výkresová prezentace modelové kóty má vlastní přestavení,
které nezasahuje do zdrojového modelu.

Ruční výkresová kóta používá stejné textové a prezentační prvky,
navíc má záložku Typ a vazby. Její měřená hodnota se pouze zobrazuje.
Jednotka mm navazuje přímo na číslo. Všechny formuláře používají interní
PropertiesSubWindow, OK/Zrušit a potvrzení dvojklikem MMB nad View.
Krátké MMB pouze ukončí zadávání reference; tažení MMB nepotvrzuje dialog.

## Příkaz Kóta

Příkaz Kóta nahradil experimentální zadání dvou rovnoběžných hran.
Otevře stejné okno při vytvoření i pozdější úpravě.

1. Zvolte lineární, poloměrovou, průměrovou, řetězovou nebo úhlovou kótu.
2. Kliknutím do referenčního pole aktivujte příslušný konec.
3. Vyberte geometrii pohledu. U první vazby se určí pohled; další patří jemu.
4. Po doplnění vazeb určete LMB umístění a potvrďte OK nebo dvojklikem MMB.

Vše až do OK zůstává náhledem. Zrušit zahodí nové kóty i rozpracované
změny existující kóty. Kontextové menu potvrzené kóty nabízí její vlastnosti,
odstranění a u lineární kóty také pokračování řetězce z kteréhokoli konce.
Nad fialovým bodem se běžné kontextové menu neotevírá.

Klávesa **Delete** odstraní označenou ruční kótu. U kóty převzaté z modelu
ji skryje pouze v daném pohledu (Erase); zdrojový parametr se zachová.
Kliknutí na kótu předá výkresovému prostoru také klávesový fokus.

Jmenovité hodnoty kót ve Sketcheru, Partu, sestavě i Drawingu používají
desetinnou čárku. Přesnost určuje maximální počet desetinných míst; po
zaokrouhlení se koncové nuly nepíší. Při přesnosti 3 například `10mm`,
`10,5mm`, `10,526mm` pro původní hodnotu 10,52584. Malá záporná hodnota
zaokrouhlená na nulu se zobrazí jako `0`. Také číselné tolerance se vykreslí
s čárkou; jejich zadané nuly zůstanou zachované. Ručně přepsaný celý text
kóty se nemění.

## Napojení a výběr

Každý konec má samostatně nastavitelný způsob napojení:

- Automaticky: přednost mají blízké konce a středy křivek, dále geometrie.
- Bod: uložený bod nebo konec/střed navázané křivky.
- Bod na křivce: uložená křivka a její parametr ve zdrojové geometrii.
- Úsečka: při automatickém směru první úsečka určuje kolmici kótovací čáry.
- Střed (C): střed kružnice, kruhového oblouku nebo zobrazené osy.
- Tečna (T): dotyk ve směru měření; RMB dovolí zvolit druhou stranu.
- Průsečík (I): dvě samostatně uložené reference a zvolená větev průsečíku.

U lineární kóty může mít kótovací čára směr podle vazeb, vodorovný, svislý nebo rovnoběžný
s další uloženou úsečkou. Po prvním vstupu typu Úsečka lze připojit bod
nebo rovnoběžnou úsečku; nerovnoběžná druhá úsečka není platný vstup.
Průsečíky přímek mohou ležet i za konci vybraných úseček.

Hover a potvrzení používají jeden uspořádaný seznam kandidátů.
RMB před potvrzením mění pouze aktivního kandidáta. Zelený rámeček
označuje jediné aktivní referenční pole; oči samostatně zapínají prohlížení
již uložených referencí a azurové zvýraznění jejich geometrie.
Změna kamery nepřesouvá uložený parametr bodu na jinou část zdrojové křivky.

Zobrazená osa patří do stejného seznamu referencí. Při pohledu do osy lze
vybrat libovolné rameno křížku a navázat jeho skutečný střed; při bočním
pohledu také přímku osy. Kreslení, nabídka a zvýraznění používají stejné
čtyři větve nebo úsečku, včetně přesahu 2mm na papíře. Opakované výskyty
dílu si zachovávají vlastní reference. Skrytá osa se nově nenabízí, ale její
dříve uložená vazba zůstává platná. Chybějící osa vyvolá opravu reference.
Pole Bod zachová skutečný typ vybrané vazby (vrchol, bod křivky či střed).

## Řetězec a oprava vazby

Běžnou lineární kótu lze rozšířit na řetězec z prvního i druhého konce.
Původní úsek si zachovává identitu, vazby, směr, základnu a umístění.
Každý další úsek měří sousední reference. V záložce Umístění lze zvolit úsek,
jehož prezentační nastavení se upravuje.

Ztracená reference je ve vlastnostech označena jako chybějící.
Poslední platná prezentace udržuje kótu vybratelnou včetně poslední hodnoty.
Podle dohody z 2026-09-12 se nepřidává otazník: neplatná kóta je červená,
po opravě je znovu žlutá. Neplatnost zůstává červená také v exportu výkresu;
platné exportované kóty používají obvyklou barvu kresby.
Kliknutím na konkrétní referenci lze zadat náhradu; ostatní vazby a styl
zůstávají zachované. Obnovení stejné geometrie znovu vyřeší původní vazbu.

## Projekce a měření

Lineární kóty měří průmět do aktuální roviny pohledu. Kontrolní příklad:
úsečka 40mm skloněná o 60° od roviny má ve směru svého průmětu hodnotu 20mm.
R/⌀ se zobrazují pouze při kruhovém průmětu. Natočení kružnice do elipsy je
skryje, aniž by odstranilo vazby či umístění. Návrat do kolmého pohledu je
obnoví. Otočení v rovině listu není důvodem ke skrytí.

Měřicí geometrie se zachytí s projekcí pohledu z uložených zdrojových
referencí. Kruhovost se ověřuje proti všem uloženým vzorkům křivky;
samotná podobnost s kružnicí na obrazovce není podkladem pro R/⌀.
Geometrie, reference včetně cesty výskytu, styl a polohy úseků se ukládají
do aktuálního formátu Drawing. Staré experimentální ruční kóty se nepřevádějí.

## Rádius podle náčrtu koty.bmp

Společné vykreslování ve skicáři, Part, Assembly a Drawing používá tři režimy.
Při držení fialového bodu LMB cykluje RMB postupně:

1. Čára od středu k oblouku a šipka zvenku.
2. Zachovaná čára střed–oblouk a obrácená šipka.
3. Zkrácená kóta bez povinné čáry do středu. Orientaci šipky určuje strana,
   na kterou od jejího hrotu pokračuje pomocná čára.

Další RMB se vrací do prvního režimu. První dva režimy dovolují text za
obloukem nebo za středem; třetí navíc mezi středem a obloukem.
Pomocná čára navazuje přímo na vedení od šipky, text leží nad ní.
V šikmém prostorovém průmětu zůstává textová police vodorovná a vedení
od šipky zachovává promítnutý směr rádiusu.

Používají se dosavadní fialové body. Bod pod textem posouvá textovou polici
podél rádiusu a nemění polohu šipky. Bod u šipky posouvá kótu po kružnici;
uložený úhel otáčí její prezentaci v rovině rádiusu. Žádný z těchto bodů
nemění měřený rozměr ani modelové vazby. Rovina se zachová také v šikmém
nebo téměř hranovém pohledu. Textová vrstva nadále maskuje geometrii
pod celým textem s okrajem 0,5mm ve výkresu.

## Ověření

Výpočtový kontrakt kontroluje C/T, obě větve průsečíků, tečný dotyk,
měření průmětu, oba konce řetězce, chybějící vazby, uložení/otevření
a invariantní rádius při tažení. UI kontrakt používá skutečné události
myši pro reference, náhled, Cancel, potvrzení MMB, textové tolerance
a ovládání všech tří režimů rádiusu. Sdílený prezentační kontrakt vykresluje
sedm stavů náčrtu do build/radius-seven-states-proof.png.

## Úhlová kóta dvou přímých hran (2026-09-12)

Typ **Úhlová** je součástí stejného příkazu Kóta a stejných vlastností.
Přijímá dvě různé původní přímé reference konkrétního pohledu, včetně přesné
cesty výskytu. Používá společný seznam kandidátů pro hover, LMB a RMB;
nefiltruje druhou hranu na rovnoběžnost. Pro tento typ jsou volby jiného
napojení, směru lineární čáry a rozšíření řetězce vypnuté.

Měří se úhel průmětů v rovině pohledu, nikoli skrytý prostorový úhel mezi
hranami. Hodnota i tolerance jsou ve stupních. Umístění LMB určí sektor mezi
přímkami (menší nebo doplňkový úhel) a vzdálenost oblouku od průsečíku.
Volby ramen jsou uložené; změna geometrie sama nezvolí jiný sektor.
Body u šipek mění poloměr oblouku a bod pod textem umístění textu, bez změny
měřené hodnoty. Zachovávají se běžné OK/Zrušit, dvojklik MMB, editace přes
vlastnosti, tolerance a obrácení šipek.

Po změně geometrie se při výslovné regeneraci pohledu použijí původní
reference. Oříznutí nebo změna délky přímé hrany při zachování její identity
neodpojí měření směru. Ztracená reference, záměna přímky za obecnou křivku
nebo nulový průmět způsobí neplatný stav: zůstane poslední kresba a hodnota,
červeně a vybratelná pro opravu. Shodná či blízká jiná hrana není náhradou
bez výslovného vstupu uživatele. Po obnovení vazby se kóta přepočítá a zežloutne.

Při rovnoběžnosti zůstává typ úhlový s hodnotou 0° nebo 180°. Neprovádí se
samovolný převod stupňů na milimetry. Místo neurčeného či příliš vzdáleného
vrcholu se použijí místní odkazové čáry ke skutečným bodům obou referencí.
Tento způsob se použije také u téměř rovnoběžných přímek s nepřiměřeně
vzdáleným průsečíkem, takže kóta neopustí okolí modelu. Poslední způsob kresby
je uložen spolu s poslední prezentací v `.drwz`; při následné ztrátě reference
zůstane vybratelný i tento mezní stav. Otevření vlastností a výběr nepoužívají OCCT.

Ověřeno sestavením GUI i CLI, pěti cílenými testy a celou regresní sadou
80/80 (2026-09-12). UI test používá skutečné události myši pro vytvoření,
Cancel, dvojklik MMB, ztrátu reference a její opravu. Kontroluje žluté/červené
pixely, poslední hodnotu bez otazníku, výběr neplatné kóty a uložení/otevření
`.drwz`. Samostatně je ověřeno 60°/120°, oříznutí, 0°/180° i téměř
rovnoběžné přímky; snímky platného, neplatného a opraveného stavu prošly
vizuální kontrolou.
