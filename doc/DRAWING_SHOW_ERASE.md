# Návrh Show/Erase pro výkresy (2026-09-09)

Stav: dohodnutý návrh dalšího kroku, zatím není implementovaný nástroj.
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
do tisku/PDF. Výchozí vzdálenosti ještě nejsou uživatelem určené.

## Ověření před dokončením

Přepnutí pomocné kružnice musí zachovat její ID, střed, poloměr, kóty a vazby;
čerchované zobrazení nesmí zasahovat do solveru. Konstrukční role nadále
znamená vyloučení z profilu tělesa. Pro Show/Erase ověřit nezávislou viditelnost
ve dvou pohledech, opakované výskyty v sestavě, náhled/Cancel, ukládání poloh,
změny zdroje při explicitní regeneraci a absenci pracovních vodítek v PDF.
