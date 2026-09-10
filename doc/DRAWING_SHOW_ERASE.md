# Show/Erase pro výkresy (2026-09-09)

Stav: implementováno v C++ výkresovém prostředí.
Ruční kótování je samostatný následný nástroj.

## Vstupy, prostředky a výstupy

Vstupem je vybraný výkresový pohled a původní kóty, osy a pomocná geometrie
jeho zdrojového dílu/sestavy. Prostředkem jsou uložené identity a referenční
geometrie ZIMA, projekce kamery a společný výběr ve View. Otevření nástroje,
hover ani změna filtru nesmí spouštět OCCT. Výstupem je výkresový seznam
zobrazených položek a jejich prezentační polohy, uložený v `.drwz`.

## První rozsah

- Jeden vnitřní dialog Show/Erase s OK/Zrušit.
- Přepnutí Show / Erase a filtry: původní kóty modelu, osy a pomocná geometrie.
  Další typy značek lze doplnit stejným datovým a výběrovým kontraktem.
- Režim výběru: vybrat položky k ponechání nebo vybrat položky k odebrání
  z nabízeného náhledu. Show pracuje se skrytými kandidáty; Erase s viditelnými.
- Náhled je dočasný. OK uloží změnu viditelnosti; Zrušit obnoví výchozí stav.
- První verze nezapisuje rozměry zpět do modelu. Přímá editace zdrojových
  rozměrů bude pozdější, odděleně ověřený krok s jasným vlastnictvím změny.

## Identity a uložení

Každá položka odkazuje na zdrojový dokument, vlastníka, stabilní ID kóty či
geometrie a v sestavě na přesnou cestu výskytu. Výkres ukládá viditelnost a
přesunutí zvlášť pro každý pohled. Kopie souřadnic bez zdrojové identity není
náhradou vazby na model. Regenerate aktualizuje odkazy z aktuálních otevřených
zdrojů; samotné přepnutí karty nespouští přepočet rodiče.

Prezentace parametrických kót se ukládá v jejich prostorové bázi v modelových
milimetrech; výkresové přestavení je nezávislé nastavení konkrétního pohledu.
Změna umístění nemění hodnotu kóty ani vazby zdrojové skici. Zmizelý zdrojový
objekt se označí jako nevyřešený; jiná geometrie ho nesmí tiše zastoupit.

## Konstrukční geometrie a osy

Konstrukční role je nezávislá na druhu křivky. Konečná úsečka zůstává
konečná, oblouk si zachová svůj rozsah a kružnice nebo uzavřená křivka svůj
tvar. Přepnutí na konstrukční geometrii zachovává identitu, body, kóty a vazby;
čerchované zobrazení a vyloučení z profilu tělesa nemění definici křivky.
Nekonečná osa/přímka zůstává samostatným druhem reference, ne vlastností
všech konstrukčních úseček. Show/Erase má nabídnout konečnou konstrukční
geometrii i osy; válcové osy zobrazí podle uloženého rozsahu válce.

## Manipulační body a vodítka

Hover zvýrazňuje entitu i manipulační bod, LMB potvrzuje společného kandidáta.
Po potvrzení lze přesouvat text a ovládat kótu za body ve styku šipek
s vynášecími čarami. Polohy jednotlivých bodů mají být zachovány po otevření,
změně měřítka a regeneraci. Samotný pohled si ponechá obdélníkovou oblast.

Ve vlastnostech pohledu jsou pracovní vodítka pro kóty: zapnutí/vypnutí,
odstup první čáry od obálky a rozteč dalších čar v mm. Vodítka jsou šedá a
čárkovaná jako nenápadné skryté hrany. Nejsou výkresovou geometrií a nepatří
do tisku/PDF. Dohodnutý výchozí odstup první čáry i rozteč dalších čar jsou 8 mm.

## Ověření před dokončením

Přepnutí pomocné kružnice musí zachovat její ID, střed, poloměr, kóty a vazby;
čerchované zobrazení nesmí zasahovat do solveru. Konstrukční role nadále
znamená vyloučení z profilu tělesa. Pro Show/Erase ověřit nezávislou viditelnost
ve dvou pohledech, opakované výskyty v sestavě, náhled/Cancel, ukládání poloh,
změny zdroje při explicitní regeneraci a absenci pracovních vodítek v PDF.


## Implementovaný základ (2026-09-09)

`ModelAnnotation` ukládá zdrojový dokument, vlastníka, sémantické ID a přesný
výskyt. Každý `DrawingView` má samostatný seznam v `.drwz`. Projektovaná geometrie
je v jednotkách modelu. Parametrická kóta navíc ukládá původní prostorovou
geometrii, rám, prezentaci modelu a volitelné místní přestavení pohledu.

`refresh_model_annotations` přijímá výslovně dodané ZIMA pakety kót, os a pomocných
křivek. Zachová viditelnost a polohy podle identity, chybějící položky označí jako
nevyřešené a při chybě nemění výchozí stav. Nepřepočítává model ani nevolá OCCT.
Opakované výskyty se nerozlišují názvem či pořadím. Konečné pomocné křivky se
projektují se zachováním rozsahu; zdrojové body a konstrukční role se nemění.

`ShowEraseSession` poskytuje kandidáty a kopii náhledu pro Show/Erase, filtry typů
a výběr k ponechání/odebrání. Dokud volající náhled nepotvrdí, pohled zůstává
nezměněný. Cizí či nenabízenou identitu odmítne.

`zima_cpp_show_erase_contract_tests` ověřuje dva pohledy, opakované výskyty,
pomocnou kružnici, náhled bez zápisu, zmizení a návrat zdroje, zachování papírových
poloh při změně měřítka a regeneraci, atomické odmítnutí duplicit a `.drwz` roundtrip.
Nástroj je dostupný v liště Výkres jako **Show / Erase…**. Dialog používá společné
vnitřní okno vlastností, umístěné vpravo. Výběr ve View používá seznam anotací
společný s hoverem a RMB cyklováním. Prázdné kliknutí ruší potvrzený výběr.

## Použití

1. Vyberte výkresový pohled. Pro načtení nových položek ze zdroje zvolte
   **Regenerovat**; nově vložený pohled je načte při OK ve vlastnostech.
2. Otevřete **Show / Erase…**. Show nabízí skryté položky, Erase viditelné.
   Filtry rozlišují původní kóty skic, osy a konečnou pomocnou geometrii.
   Sběr zahrnuje také kóty již uložené ve zdrojovém viewer paketu a uložené
   osy těles (například osu válce); nezavádí nové ručně měřené kóty.
3. Zvolte výběr k ponechání nebo odebrání. Kliknutí ve View i zaškrtnutí
   v seznamu přepíná stejnou položku. Skryté nabízené položky jsou šedé,
   zachované položky mají běžné zobrazení. Pravé tlačítko cykluje překryté nabídky.
4. OK zapíše pouze viditelnost tohoto pohledu, Zrušit obnoví výchozí stav.
   Prostřední dvojklik potvrzuje OK také nad plátnem; krátké MMB nepotvrzuje.
5. Po uzavření nástroje lze kótu vybrat a táhnout její text nebo úchyt u šipky.
   Lineární kóta při změně odstupu zachová směr; úhlová mění poloměr oblouku.
   Hodnota a vazby zdrojové skici se nemění.

Ve vlastnostech pohledu zapněte **Pracovní vodítka kót**. Promítají se z
orientovaného kvádru vlastníka kóty. Geometrická obálka a pracovní odsazení
jsou oddělené; posunutí kóty tedy nezvětšuje model. První odstup i rozteč jsou
**8 mm na papíře**. Kóta rovnoběžná s papírovými vodítky se při tažení odstupu
přichytává k blízké úrovni. U šikmé projekce se používá prostorová poloha kóty.
Pracovní rámy nevstupují do tisku, PDF ani DXF.

Osa při pohledu ve svém směru vytvoří křížek o rozpětí 6 mm s bodem ve středu.
Každá díra má vlastní značku. Z boku se zachová uložená délka válce s přesahem
2 mm na obou koncích a bodem uprostřed. Pomocná úsečka ani oblouk se
neprodlužují na nekonečnou osu. Zobrazení pracuje s uloženou
projekcí; samotné otevření nástroje, filtry ani přepnutí karty nepřepočítávají model.
Explicitní regenerace používá otevřené zdroje, nebo uložené soubory, a rozlišuje
každou úroveň vnořené sestavy i opakované výskyty stejného Partu. Zrcadlené
a opakované komponenty používají uložené definice Mirror/Pattern a jejich přesné
cesty výskytů; anotace se neponechávají na místě původní komponenty.

`zima_cpp_show_erase_ui_contract` ověřuje skutečnou skicu, vnořenou otočenou
sestavu, kliknutí ve View, OK/Zrušit, MMB, přesun textu, .drwz a shodu tiskového
vykreslení při přepnutí vodítek. Sdílenou kreslicí cestu používá také PDF export.

## Orientace kót a izolace pohledu

Parametrické kóty zůstávají dostupné také v šikmém nebo bočním pohledu.
Promítají se jejich prostorové měřicí body, ramena a uložená poloha textu;
číselná hodnota se nepřepočítává z délky na papíře. Při změně kamery se
projekce obnoví. Původní papírové úchyty z jiné orientace se nepoužijí.

Po výběru kóty otevřete pravým tlačítkem **Zobrazení kóty…**. Lze změnit
rovinu kolem směru měření, stranu obálky, odsazení a posunutí textu.
U úhlové kóty určují rovinu obě měřená ramena; volba roviny je proto vypnutá.
Úhlová kóta zůstává u své měřicí osy/čepu a mění se poloměr oblouku.

Výchozí prezentace pochází z Partu nebo Assembly. Výkres si změnu ukládá
jako vlastní nastavení tohoto pohledu. Nemění zdrojový dokument ani jiný
pohled. Regenerace převezme novou hodnotu a geometrii a ponechá místní
nastavení prezentace. Vlastnosti používají společné vnitřní okno OK/Zrušit.

Během příkazu se kandidáti omezují na upravovaný výkresový pohled. Kliknutí
na tutéž zdrojovou kótu v jiném pohledu ji do výběru nepřidá. Regresní testy
ověřují tuto izolaci, čelní/opačný/šikmý/boční pohled, otočenou podsestavu,
uložení normály a shodné filtrování ve View i tiskové cestě PDF.

Sběr anotací při převodu přes sestavu odděluje skutečné zdrojové položky od
pomocných referencí převodní scény. Nevytváří duplicitní osy a zachovává také
transformovanou polohu textu kóty. Test zahrnuje úplnou projekci těchto paketů,
nikoli pouze souřadnice jednotlivých zdrojových položek.


## Doplnění ovládání a exportů (2026-09-10)

Show/Erase má vlastní ikonu oka s kótou. Funguje v obou pořadích: vybrat pohled
→ Show/Erase nebo Show/Erase → kliknout na pohled. Okno se otevře ihned.
Horní referenční pole zobrazuje cílový pohled; kliknutím se aktivuje zelený
rámeček a následným výběrem jiného pohledu se cíl nahradí. Krátký prostřední
klik ukončí zadávání reference, aniž smaže hodnotu nebo potvrdí dialog.
Tlačítka SHOW a ERASE označují aktivní režim zeleně. OK zapisuje pouze aktuální
pohled; rozpracovaný výběr předchozího pohledu se při změně cíle zahodí.
Bez vybraného pohledu je OK neaktivní. Bez pohledů je nástroj nedostupný.

Výkres Partu nabízí jeho původní kóty. Výkres Assembly přebírá osy z vložených
Partů a kóty vlastněné sestavami, například úhel uložení. Skicové kóty vložených
Partů se do sestavového výkresu nepřenášejí. Transformace respektuje přesný
výskyt, vnoření, zrcadlení i pole; sběr neprovádí nový výpočet geometrie.

Fialové úchyty po potvrzeném výběru označují přesouvatelné popisky pohledu/řezu
a kóty. Pevné položky razítka, osy a pomocná geometrie si ponechávají běžné
zvýraznění bez fialových úchytů. Bod ve středu osy je tisková značka, nikoli úchyt.

**Obnovit pohled** vystředí papír a přizpůsobí jeho výšku View s okrajem 24 px.
Při úzkém okně může šířka papíru přesahovat View.

**Soubor → Uložit jako → DXF – aktuální list** uloží právě aktivní list
v milimetrech, včetně formátu, razítka, textů, geometrie a viditelných anotací.
Používá společnou tiskovou kreslicí cestu bez výběru, náhledů a pracovních vodítek.
Text zůstává textem; křivky se převádějí na úsečky, výplně na HATCH/SOLID.
Vložené obrázky a stínování jsou samostatné barevné výplně, takže DXF nepotřebuje
vedlejší obrazové soubory. Export nemění dokument ani jeho cestu.

**Soubor → Uložit jako → JPEG – aktuální pohled** uloží aktuální obsah View
v Partu, Assembly i výkresu. Zachová výřez, natočení, zoom a aktuální zobrazení,
včetně viditelného výběru. Rozlišení odpovídá framebufferu/plátnu; okolní panely
aplikace se neukládají. Výkresový JPG zachovává pracovní vzhled obrazovky,
zatímco DXF/PDF používá tiskové vykreslení. JPEG kvalita je 95.

## Sdílený prostorový rám (2026-09-10)

`ModelEnvelope` nese lokální počátek, tři osy a geometrické meze v této bázi.
Orientace pochází z vyřešeného uložení objektu; nezávisí na kameře.
Rámy se získávají z uloženého viewer/reference paketu, bez průchodu OCCT.
Platí pro historii prvků, konstrukční objekty, skici a tělesa. Skica může mít
nulovou tloušťku, bod nulový rozměr a osa pouze délku; rám nevyžaduje objem.
Bez vypočtené geometrie existuje lokální báze, nikoli vymyšlené rozměry.

Vypočtené snapshoty komponent uchovávají rámy a sestava transformuje jejich
počátky a směry po přesných cestách výskytů. Rámy kopií se transformují spolu
s Mirror/Pattern. Toto je datový základ pro hierarchii velkých sestav;
nový prostorový index ani optimalizace vykreslování zde zavedeny nejsou.

V Partu a Assembly zapíná **Zobrazení → Prostorový rám kót** pracovní kvádr
vybraného objektu. Parametrické kóty se standardně odsazují o 8 modelových mm.
Jejich kontextová nabídka obsahuje **Zobrazení kóty…**. Fialový úchop textu
mění jeho polohu, úchopy lineární kóty její odstup a úhlové kóty poloměr.
Tažení zapisuje prezentaci při puštění tlačítka; Esc jej zruší. Měřicí reference,
hodnota, vazby a geometrie modelu se nemění.

Fialové úchopy ve výkresech patří přesouvatelným anotacím, včetně konců
šipek řezu. Geometrie pevně svázaná s pohledem vlastní manipulační bod nedostává.

Cílený test `zima_cpp_dimension_layout_contract_tests` kontroluje lokální
rozměry otočeného kvádru, opakované výskyty, zrcadlení, oddělení měření od
prezentace, uložení Part/Assembly a nezávislé nastavení výkresových pohledů.
Dále ověřuje skutečné události tažení ve 3D, zrušení a společné vlastnosti.


## Opravy rovin, úchopů a velikosti os (2026-09-10)

Poloměr a průměr zachovávají skutečnou rovinu kružnice, včetně skic v otočeném
Body a náhledu zaoblení. Přepínač kolmých projekčních rovin je určen lineárním
kótám; radiální kótu nelze vyklopit z roviny její kružnice.

Skicář používá stejné fialové úchopy jako Part/Assembly: na šipkách a uprostřed
pomocné čáry pod textem. Úchopy se kreslí nad body geometrie, takže nezmizí při
jejich souběhu. Vzhled se ukládá přímo se skicou. U vloženého návrhu profilu
patří do stejného návrhu; v samostatně editované skice se uloží se změnami skici.
Pozdější zrušení nově otevřených vlastností již uložené změny skici nevrací.
Skica uchovává prezentaci také při serializaci a předává ji výkresu.

Zobrazovací délka osy nezvětšuje existující geometrický kvádr jejího vlastníka.
Pouze samostatný objekt osy bez geometrické obálky získá meze ze své délky.
Výkresové osy používají skutečné promítnuté minimum a maximum vlastního
pomocného kvádru, s přesahem 2 mm na každém konci na papíře. Rozsah se
nezrcadlí kolem počátku: například kvádr od 0 do 40 mm při měřítku 1:1
dává rozsah −2 až 42 mm, nikoli −42 až 42 mm. To platí i pro hlavní osy.
Ve směru osy vzniká kříž přes tyto meze, mimo tento směr úsečka.


### Volná poloha textu zkráceného rádiusu (2026-09-10)

V režimu rádiusu bez čáry do středu začíná spojnice přímo na šipce měřeného
oblouku a končí na pomocné čáře pod textem. Fialový úchop ve středu pomocné
čáry lze plynule přetáhnout mezi oblouk a střed, přes střed za osu i ven za
oblouk. Poloha textu nemění měřený bod šipky ani hodnotu rádiusu.
V isometrii zůstávají text a jeho pomocná čára vodorovné.
Toto vykreslení používají společně Sketcher, Part, Assembly a Drawing.


Přepnutí zkráceného rádiusu zachovává rovinu kružnice a radiální směr šipky
i spojnice. Případná složka uložené polohy textu kolmá na rovinu se odstraní.
Tažení určuje podepsanou polohu podél promítnutého poloměru bez omezení
na vnitřek nebo vnějšek; vodorovná pomocná čára v isometrii na něj navazuje.


### Vrchní textová vrstva kót (2026-09-10)

Hodnoty kót se vykreslují v samostatné vrstvě nad geometrií, osami,
vynášecími čarami a šrafováním. Celou hodnotu včetně značky R/Ø a jednotky
obklopuje krycí obdélník bez obrysu, s přesahem 0,5 mm na všech stranách
ve výkresu. Na obrazovce přebírá barvu pozadí, při tisku/PDF barvu papíru.
Ve 3D View je přesah přepočtený podle DPI obrazovky.

Texty používají stabilní pořadí uložených kót; pozdější text a jeho maska
překryjí dřívější. Výběrové fialové úchopy zůstávají nad textovou vrstvou.


### Více pohledů, kříže otvorů a ovládání kót (2026-09-10)

Krátký stisk prostředního tlačítka v Show / Erase ukončí výběr položek pro
aktuální pohled a aktivuje políčko pro výběr dalšího pohledu. Rozpracovaná
viditelnost zůstává v náhledu i po přechodu jinam. Kliknutím do políčka lze
pohled změnit také přímo. Pokud je políčko právě aktivní, krátký stisk
prostředního ukončí zadávání reference a vrátí výběr položek dosavadního pohledu.

OK nebo dvojklik prostředním potvrdí všechny rozpracované pohledy najednou
a zavře dialog. Zrušit zahodí změny všech pohledů. Přechod mezi pohledy,
krátký stisk ani prostřední tažení neprovádějí mezilehlé uložení. Tlačítko
Apply zde není. Stejný dvojklik nad výkresovým prostorem potvrzuje také
vlastnosti pohledu a listu prostřednictvím společného `PropertiesSubWindow`.

Kříž osy při pohledu do válce tvoří čtyři ramena po 90° a bod ve středu.
Všechna ramena patří jedné referenci: výběr, SHOW i ERASE ovládají celý kříž.
Dva otvory mají dva samostatné kříže. Osy kruhových profilů vytažení používají
poloměr konkrétního profilu, nikoli příčný rozměr celého vytažení. Například
otvor Ø10mm při měřítku 1:1 má od středu ke konci každého ramene 7mm:
5mm poloměr a 2mm přesah na papíře. Obálka i směry sledují přesný výskyt
v sestavě včetně zrcadlení a pole.

Skicová osa přebírá obálku skutečných skicových křivek a bodů ještě před
výběrem výkresových anotací; pracovní délka os skici 100mm ji nezvětšuje.
Seznam rozlišuje osu skici, osu počátku a osu válce. Změněná zdrojová data os
a rovin se do již uloženého výkresu načtou příkazem **Regenerovat**.

Texty rozměrů používají zápis `10mm`, `R10mm`, `Ø10mm`. Explicitně zadaný
vlastní text kóty se zachovává. Stejný formát používá View, výkres i export.

Tažení používá společný převod obrazovky do roviny kóty. Pokud se rovina
promítá hranově, zachová se možnost posuvu v jejím viditelném směru.
Kóta se skryje až při zhroucení samotné měřicí čáry do bodu. Režim zkráceného
rádiusu lze znovu uchopit a táhnout, i když byl předtím přepnut pravým tlačítkem.
Výkresové úchopy mění pouze prezentaci daného pohledu; modelový rozměr zůstává.

Regresní testy ověřují jednotlivá ramena obou otvorů, normály skici XZ,
opakované tažení textu a šipkových úchopů, RMB cyklus, hromadné OK/Cancel
přes dva pohledy a prostřední potvrzení vlastností listu i pohledu.
Volitelný `ZIMA_TEST_ANNOTATION_PART` umožňuje při testu Show / Erase ověřit
referenční díl se dvěma otvory Ø10mm v rovině XZ a jeho sousední `.drwz`;
soubory se pouze čtou a interakce probíhá nad pracovní kopií dokumentu.
