# Řezy dílu, sestavy a výkresu

## Vytvoření a úprava

Příkaz **Řezy…** nad View otevře vlastnosti řezu pro Part nebo Assembly
na vlastní kartě. Používá stejný panel **Umístění kontejneru** jako ostatní
kontejnery: souřadnice, orientaci, reference a jejich odsazení. Společný
výpočet umístění se nemění. Referencemi jsou uložené původní objekty modelu.

1. Nastavte umístění kontejneru řezu.
2. Vyberte jeho vlastní skicovou rovinu **XY, XZ nebo YZ**.
3. Tlačítkem **Skica…** otevřete běžný Sketcher. Nakreslete jednu otevřenou
   úsečku nebo souvislou lomenou čáru. Lze používat vazby, kóty i tažení bodů.
4. **Dokončit skicu** vrátí rozpracovanou skicu do vlastností řezu.
   **Zrušit skicu řezu** zahodí pouze právě probíhající úpravu skici.
5. Nastavte stranu řezu, viditelnost řezné plochy a případně režimy komponent.
   **OK** uloží celý řez jako jednu vratnou transakci.

Čára se na obou koncích prodlužuje do nekonečna; řezná plocha prochází
kolmo ke skicové rovině celým modelem. U lomené čáry vzniká odpovídající
zalomená plocha. Výchozí ponechaná strana je vlevo při průchodu od prvního
nakresleného segmentu. **Obrátit stranu řezu** ponechá druhou část.
Uzavřené, rozvětvené nebo nesouvislé profily a křížení čáry či prodloužení
jejích konců nejsou přípustné. Řezná geometrie je tvořena přímými úsečkami.

Skupina **Řezy** je hned za počátkem dokumentu. První položka **Bez řezu**
obnovuje celý model. Skupinu ani tuto výchozí položku nelze odstranit.
Následují A–A, B–B…; jejich jména lze upravit. Kontextové menu nabízí
**Aktivní**, vlastnosti, úpravu skici a odstranění konkrétního řezu.
Řezy jsou mimo historii tvorby těles, přestože jsou ve stromu nahoře.

Vytvoření i pozdější editace používají jeden vnitřní dialog se společnými
**OK / Zrušit**. Náhled a změny ve Sketcheru zůstávají dočasné až do OK
vlastností řezu. Zrušit vlastnosti zahodí i již dokončené úpravy skici.
Undo/Redo během skicování pracuje pouze s rozpracovanou skicou.
Dvojklik prostředním tlačítkem nad View potvrzuje otevřené vlastnosti;
krátký klik ani navigační tažení nepotvrzují.

## Zobrazení a sestavy

Výchozí aktivní stav dokumentu je **Bez řezu**. Strom nemá zaškrtávací políčka;
**Zobrazit rovinu řezu** ve vlastnostech zviditelní pomocný obrys. **Aktivní** v menu
A–A nebo **Aktivní řez** ve vlastnostech zapne skutečně oříznuté zobrazení;
v jednom dokumentu je současně aktivní nejvýše jeden takový řez. Při
modelovacích vlastnostech a editaci běžné skici se používá úplná geometrie.

Reference umístění se obnovují při explicitní regeneraci zdrojového dokumentu.
Chybějící referenci lze opravit ve vlastnostech; neplatný řez se nevypočítá.
Samotné přepnutí karty nevyvolává nový výpočet nadřazené sestavy.

Řez je nastavení prezentace. Nemění výsledná tělesa, jejich objem, hmotnost,
zdrojové soubory komponent ani množství v kusovníku.

Seznam komponent rozlišuje **Řezat + šrafovat**, **Řezat bez šraf** a **Neřezat**.
Part používá identity těles, Assembly úplné cesty jednotlivých výskytů,
včetně opakovaných dílů ve vnořených sestavách. Výběr dílu ve View označí
odpovídající řádek. Hromadné označení řádků umožňuje společnou změnu režimu,
úhlu, rozteče, posunutí a typu šraf. Přímá úprava nastaví styl příslušného
tělesa; samostatné zaškrtávání vlastního stylu není potřeba. **Obrátit** otočí šrafy o 90°.
Při označení více řádků otočí každý z jeho aktuálního úhlu, takže zůstane
zachováno jejich střídání. Tabulka končí hned za posledním řádkem.

## Výkres a tisk

Ve **Vlastnostech pohledu → Řez** vyberte uložený řez zdrojového dokumentu.
Orientaci vždy určuje výkresový pohled. Výběr, změna sklonu, obrácení ani
vypnutí řezu nemění kameru. Výkres automaticky volí ponechanou stranu podle
orientace pohledu při prvním výběru, otočení i regeneraci. U lomeného řezu
rozhoduje převládající promítnutá plocha řezu, nikoli délka čáry mimo těleso.
Automatické určení strany nemá samostatný přepínač. Změna kamery nemění
stranu zobrazení zdrojového řezu v Partu nebo Assembly.
Řezné plochy se zobrazí a vyšrafují tam, kde jsou v této orientaci viditelné. Plocha viděná
přesně z boku nemá promítnutou plochu pro šrafování. Základní i izometrické
pohledy používají stejné pravidlo. Lomený řez se promítá ve své skutečné
prostorové poloze; jednotlivé úseky se automaticky nerozvíjejí do jedné roviny.
Šrafování se ovládá pouze v tabulce těles/komponent: úhel, rozteč, posunutí,
typ (rovnoběžné, křížové, čárkované) a **Obrátit**. Tyto parametry patří
zdrojovému řezu v Partu či Assembly. Výkres je načítá z modelu a po **OK**
zapíše změny do jeho otevřeného dokumentu; samostatné přepsání stylu pohledem
neexistuje. **Zrušit** nemění model ani výkres. Zdrojový model poté uložte
běžným příkazem Uložit. Režim Neřezat také patří definici modelového řezu.
Změna tabulky nepočítá solid a neregeneruje nadřazené sestavy.

**Řezat bez šraf** ve výkresu vypne pouze šrafy konkrétního tělesa v daném
pohledu. Řezná plocha, hrany a nastavení 3D zobrazení zůstávají zachovány.
Part/Assembly používá stejný styl i pro zelené šrafy na skutečné 3D řezné
ploše; jeho tabulka nezávisle ovládá viditelnost šraf ve 3D. Rozteč a posunutí
ve výkresu jsou mm na papíře bez ohledu na měřítko; ve 3D představují mm modelu.
Sousední komponenty bez vlastního stylu střídají úhel o 90°.

Uložená projekce obsahuje poslední vypočtený stav zdrojového řezu.
Po pozdější změně modelu jej výkres převezme explicitní **Regenerací**;
staré lokální přepsání stylu jej již nemůže přebít.

Pokud list obsahuje běžný pohled stejného zdroje, řez se k prvnímu takovému
pohledu připojí čárou se směrovými šipkami a označením. Šrafy respektují dutiny
a zakrytí dalšími díly. Směrové šipky propojené trasy sledují skutečnou stranu
příslušného řezového pohledu včetně náhledu a PDF. V režimu stínování bez hran
lze šrafy ponechat zapnuté.

Pracovní šrafy jsou zelené. PDF je exportuje vektorově černým tenkým perem
(výchozí **0,25 mm**). Trasa řezu používá žluté tenké čerchované pero **0,25 mm**,
koncové a zlomové úseky, šipky a označení bílé pero **0,5 mm**. Náhled tlouštěk nemění fyzické tiskové tloušťky. Export zachovává
uloženou projekci a nevolá OCCT.

**Regenerovat** ve výkresu znovu načte definici řezu i vypočtenou geometrii
zdroje. Otevřené dokumenty poskytují aktuální neuložený stav. Smazaný zdrojový
řez vyvolá chybu a výkres se nepřepíše částečným výsledkem; ve vlastnostech
je nutné zvolit jiný řez nebo Bez řezu.

## Výpočet a ověření

Vstupy jsou uložená skica, umístění jejího kontejneru, vypočtená zobrazovací síť
modelu a režimy jednotlivých komponent. Výstupem je oříznutá síť, uzavřené
plochy řezu, projekce hran a šrafy v papírových jednotkách. Ořez a šrafování
používají deterministický výpočet nad uloženou triangulací; zakřivené plochy
proto mají přesnost zobrazovací tessellace, nikoli nového analytického B-Rep.
Řez nevolá OCCT a nevytváří nové trvalé reference ploch či hran modelu.

Kontrolní testy ověřují plochu řezu kvádru 100 mm², dutého profilu 84 mm²,
L řez kvádru s objemem 250/750 mm³ a odsazený řez dutého profilu s plochou
108 mm² a objemem 420 mm³,
obrácení ponechané strany, tečný řez bez falešné plochy, přesné vynechání
opakovaného výskytu a konstantní rozteč šraf při změně měřítka i šikmé projekci.
GUI test pokrývá umístění a vlastní skicovou rovinu, běžný Sketcher,
tažení bodů s lokálním Undo/Redo, oba stupně zrušení úprav, prostřední tlačítko, opětovnou
editaci, strom, uložení do Part/Assembly a výkres s PDF.

Název pohledu a označení řezu (například A–A) mají samostatné volby
**Zobrazit název pohledu** a **Zobrazit označení řezu**. Výchozí poloha je nad
obrysem pohledu, při současném zobrazení ve dvou řádcích. Každý popisek lze
samostatně přesunout tažením levým tlačítkem za jeho manipulační bod. Polohy se ukládají v mm na
papíře vůči pohledu, zachovají se po otevření i regeneraci a používají se v PDF.
Přesunutí pohledu přesune i jeho popisky; samotné tažení popisku nemění model.

**Orientace** vybírá pevný základní pohled. Vedle ní jsou čtvrtotáčky
**Doleva / Doprava / Nahoru / Dolů 90°**, které otáčejí aktuální kameru,
i izometrickou. Odvozené projekční pohledy automaticky přebírají skutečnou
kameru rodiče a způsob promítání listu. Vlastní kamera se ukládá s výkresem.
Orientaci odvozeného pohledu řídí kamera rodiče. Řez nemá vliv na kameru
ani jednoho pohledu. Explicitní regenerace vyhodnocuje vazby od rodiče
k potomkům i tehdy, když jsou pohledy v souboru uložené v opačném pořadí.
Přepnutí dokumentu samo výkresové projekce nepřepočítává.

Ve vlastnostech každého pohledu je tabulka **Zobrazit trasy řezů**. Zaškrtněte
řezy, jejichž průběh má být na tomto pohledu vidět. Výběr je nezávislý na
zobrazení samotného řezu i jeho označení A–A. Nový řez nabídne svou trasu
na prvním zdrojovém pohledu automaticky; další skrývání řídí tato tabulka.
Stopa rovinného řezu se zobrazí i v bočním projekčním pohledu, kde se samotná
čára jeho skici promítá do bodu. V takovém směru se promítá celá řezná rovina
podél jejího nekonečného vysunutí, se stejnými šipkami, manipulačními body,
odstupy od obrysu a tiskovými tloušťkami jako v dolním či horním pohledu.

Trasa používá tenkou čerchovanou čáru přes rozsah pohledu. Konce a významné
zlomy mají krátké silné úseky, konce plné šipky a jednotlivá písmena (A, A).
Délky značek a velikost písma jsou na papíře nezávislé na měřítku modelu.
Písmena A i označení A–A jsou ve View bílá, vysoká **5 mm**; běžný název
pohledu je zelený. Na bílém papíře/PDF se text tiskne černě. Silné a tenké
čáry používají nastavení listu, výchozí **0,5 / 0,25 mm**. Explicitní regenerace
obnoví polohu i směr šipek z aktuálního zdrojového řezu.

Písmeno u šipky zůstává na vnější straně příslušného konce. S otočením trasy
mění polohu, ale text je vždy vodorovný a vzpřímený. Umístění zohledňuje
odstup od obrysu modelu, tras, šipek a již umístěných písmen. Při kolizi
se písmeno posune dál na stejnou stranu. Silné úseky ve zlomech mají délku
nejvýše 3 mm na každé větvi a končí nejpozději u sousedního zlomu.

## Manipulační body ve výkresech

Název pohledu, označení řezu, kóta a konce trasy řezu mají manipulační bod.
Hover zvýrazní entitu i bod oranžově; kliknutí vybarví entitu azurově a
manipulační bod fialově. Bod je jednoduchá plná tečka stejné velikosti jako
běžné body ve View (poloměr 4,5 logického pixelu). Text
nebo kótu přesouvejte tažením za bod, nikoli za libovolné místo jejich textu.
Pohled se nadále ovládá přes obdélníkovou oblast. Před potvrzením RMB cykluje
společným seznamem nabízených anotací; po potvrzení otevře kontextové menu.

Bod konce řezu je ve styku šipky s čarou. Posouvá koncovou značku podél
trasy; průběh řezu se upravuje ve zdrojové skice. Polohy značek se ukládají
v mm na papíře a omezují se tak, aby nepřekročily sousední zlom. Písmeno
se přemístí spolu se značkou a zachová vodorovné, čitelné zobrazení.
Čerchovaná trasa je ve View žlutá (0,25 mm); silné části a označení bílé
(0,5 mm). Manipulační body jsou pouze pracovní pomůcka a netisknou se.

Úsečky, kružnice, oblouky, elipsy, eliptické oblouky a B-spline ve Sketcheru
lze přes kontextové menu převést na pomocnou geometrii. Všechny tyto křivky
se pak zobrazují čerchovaně a nevstupují do profilu tělesa; zachovají si
původní rozsah, body, kóty i vazby.

## Ovládání okna a stromu řezů

Příkaz **Nový řez…** nabízí pouze kontextové menu hlavní složky **Řezy**.
**Bez řezu** nabízí aktivaci; jednotlivé řezy nabízejí aktivaci, vlastnosti,
úpravu skici a odstranění. Větší okno vlastností využívá až 1040 pixelů
výšky, s omezením na prostor hlavního okna. Tlačítko **Skica…** používá
společný zelený styl ostatních vstupů do skicáře.

Šipky řezů používají stejný štíhlý tvar jako kótovací šipky ve Sketcheru.
Poměr poloviční šířky základny k délce je společný (přibližně 0,1763);
velikost šipky na papíře se s měřítkem modelu nemění.
