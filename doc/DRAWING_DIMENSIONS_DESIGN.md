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

1. Zvolte lineární, poloměrovou, průměrovou nebo řetězovou kótu.
2. Kliknutím do referenčního pole aktivujte příslušný konec.
3. Vyberte geometrii pohledu. U první vazby se určí pohled; další patří jemu.
4. Po doplnění vazeb určete LMB umístění a potvrďte OK nebo dvojklikem MMB.

Vše až do OK zůstává náhledem. Zrušit zahodí nové kóty i rozpracované
změny existující kóty. Kontextové menu potvrzené kóty nabízí její vlastnosti,
odstranění a u lineární kóty také pokračování řetězce z kteréhokoli konce.
Nad fialovým bodem se běžné kontextové menu neotevírá.

## Napojení a výběr

Každý konec má samostatně nastavitelný způsob napojení:

- Automaticky: přednost mají blízké konce a středy křivek, dále geometrie.
- Bod: uložený bod nebo konec/střed navázané křivky.
- Bod na křivce: uložená křivka a její parametr ve zdrojové geometrii.
- Úsečka: při automatickém směru první úsečka určuje kolmici kótovací čáry.
- Střed (C): střed kružnice nebo kruhového oblouku.
- Tečna (T): dotyk ve směru měření; RMB dovolí zvolit druhou stranu.
- Průsečík (I): dvě samostatně uložené reference a zvolená větev průsečíku.

Kótovací čára může mít směr podle vazeb, vodorovný, svislý nebo rovnoběžný
s další uloženou úsečkou. Po prvním vstupu typu Úsečka lze připojit bod
nebo rovnoběžnou úsečku; nerovnoběžná druhá úsečka není platný vstup.
Průsečíky přímek mohou ležet i za konci vybraných úseček.

Hover a potvrzení používají jeden uspořádaný seznam kandidátů.
RMB před potvrzením mění pouze aktivního kandidáta. Zelený rámeček
označuje jediné aktivní referenční pole; oči samostatně zapínají prohlížení
již uložených referencí a azurové zvýraznění jejich geometrie.
Změna kamery nepřesouvá uložený parametr bodu na jinou část zdrojové křivky.

## Řetězec a oprava vazby

Běžnou lineární kótu lze rozšířit na řetězec z prvního i druhého konce.
Původní úsek si zachovává identitu, vazby, směr, základnu a umístění.
Každý další úsek měří sousední reference. V záložce Umístění lze zvolit úsek,
jehož prezentační nastavení se upravuje.

Ztracená reference je ve vlastnostech označena jako chybějící.
Poslední platná prezentace udržuje kótu vybratelnou, ale hodnotu nahrazuje ?.
Kliknutím na konkrétní referenci lze zadat náhradu; ostatní vazby a styl
zůstávají zachované. Obnovení stejné geometrie znovu vyřeší původní vazbu.

## Projekce a měření

Lineární kóty měří průmět do aktuální roviny pohledu. Kontrolní příklad:
úsečka 40mm skloněná o 60° od roviny má ve směru svého průmětu hodnotu 20mm.
R/Ø se zobrazují pouze při kruhovém průmětu. Natočení kružnice do elipsy je
skryje, aniž by odstranilo vazby či umístění. Návrat do kolmého pohledu je
obnoví. Otočení v rovině listu není důvodem ke skrytí.

Měřicí geometrie se zachytí s projekcí pohledu z uložených zdrojových
referencí. Kruhovost se ověřuje proti všem uloženým vzorkům křivky;
samotná podobnost s kružnicí na obrazovce není podkladem pro R/Ø.
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
