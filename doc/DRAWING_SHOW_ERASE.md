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

Polohy textů a přestavení kót se ukládají v milimetrech na papíře vůči pohledu.
Změna umístění nemění hodnotu kóty ani vazby zdrojové skici. Zmizelý zdrojový
objekt se označí jako nevyřešený; jiná geometrie ho nesmí tiše zastoupit.

## Konstrukční geometrie a osy

Konstrukční role je nezávislá na druhu křivky. Konečná úsečka zůstává
konečná, oblouk si zachová svůj rozsah a kružnice nebo uzavřená křivka svůj
tvar. Přepnutí na konstrukční geometrii zachovává identitu, body, kóty a vazby;
čerchované zobrazení a vyloučení z profilu tělesa nemění definici křivky.
Nekonečná osa/přímka zůstává samostatným druhem reference, ne vlastností
všech konstrukčních úseček. Show/Erase má nabídnout konečnou konstrukční
geometrii i osy; nekonečnou referenci při zobrazení ořízne na oblast pohledu.

## Manipulační body a vodítka

Hover zvýrazňuje entitu i manipulační bod, LMB potvrzuje společného kandidáta.
Po potvrzení lze přesouvat text a ovládat kótu za body ve styku šipek
s vynášecími čarami. Polohy jednotlivých bodů mají být zachovány po otevření,
změně měřítka a regeneraci. Samotný pohled si ponechá obdélníkovou oblast.

Ve vlastnostech pohledu budou pracovní vodítka pro kóty: zapnutí/vypnutí,
odstup první čáry od obrysu a rozteč dalších čar v mm. Vodítka budou šedá a
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
je v jednotkách modelu; ruční polohy textu a šipek jsou oddělené v papírových mm.

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

Ve vlastnostech pohledu zapněte **Pracovní vodítka kót**. Pomocné čáry využívají
obdélníkovou oblast View a zobrazí čtyři odstupy. Výchozí první odstup i rozteč
jsou **8 mm**, tedy čáry 8, 16, 24 a 32 mm od obrysu. Text a úchyty se při tažení
přichytávají k blízkým vodítkům. Pomocné čáry se nekreslí do tisku ani PDF.

Osy jsou při zobrazení oříznuté na oblast pohledu s přesahem 5 mm. Pomocná
úsečka ani oblouk se neprodlužují na nekonečnou osu. Zobrazení pracuje s uloženou
projekcí; samotné otevření nástroje, filtry ani přepnutí karty nepřepočítávají model.
Explicitní regenerace používá otevřené zdroje, nebo uložené soubory, a rozlišuje
každou úroveň vnořené sestavy i opakované výskyty stejného Partu. Zrcadlené
a opakované komponenty používají uložené definice Mirror/Pattern a jejich přesné
cesty výskytů; anotace se neponechávají na místě původní komponenty.

`zima_cpp_show_erase_ui_contract` ověřuje skutečnou skicu, vnořenou otočenou
sestavu, kliknutí ve View, OK/Zrušit, MMB, přesun textu, .drwz a shodu tiskového
vykreslení při přepnutí vodítek. Sdílenou kreslicí cestu používá také PDF export.

## Orientace kót a izolace pohledu

Show/Erase nabízí a vykresluje pouze kóty, jejichž rovina je kolmá ke směru
pohledu (rovnoběžná s papírem). Platí to z obou stran roviny; šikmé kóty ani
kóty viděné z boku se nenabízejí a netisknou. Rozhoduje uložená normála kóty
po transformaci přes přesný výskyt sestavy, včetně Mirror/Pattern. Tolerance
1e-6 pro sinus odchylky zachycuje pouze numerické chyby transformací.
Normála se ukládá do `.drwz`; filtr pracuje bez OCCT. Změna orientace nemaže
uloženou viditelnost ani polohy textu a úchytů.

Během příkazu se kandidáti omezují na upravovaný výkresový pohled. Kliknutí
na tutéž zdrojovou kótu v jiném pohledu ji do výběru nepřidá. Regresní testy
ověřují tuto izolaci, čelní/opačný/šikmý/boční pohled, otočenou podsestavu,
uložení normály a shodné filtrování ve View i tiskové cestě PDF.

Sběr anotací při převodu přes sestavu odděluje skutečné zdrojové položky od
pomocných referencí převodní scény. Nevytváří duplicitní osy a zachovává také
transformovanou polohu textu kóty. Test zahrnuje úplnou projekci těchto paketů,
nikoli pouze souřadnice jednotlivých zdrojových položek.
