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
→ Show/Erase nebo Show/Erase → kliknout na pohled. Při čekání na pohled se nabízí
pouze oblast pohledu; Escape příkaz ukončí. Bez pohledů je nástroj nedostupný.

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
