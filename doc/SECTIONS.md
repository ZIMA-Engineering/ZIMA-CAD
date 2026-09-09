# Řezy dílu, sestavy a výkresu

## Vytvoření a úprava

Příkaz **Řezy…** nad View otevře vlastnosti řezu pro Part nebo Assembly
na vlastní kartě. Používá stejný panel **Umístění kontejneru** jako ostatní
kontejnery: souřadnice, orientaci, reference a jejich odsazení. Společný
výpočet umístění se nemění. Referencemi jsou uložené původní objekty modelu.

1. Nastavte umístění kontejneru řezu.
2. Vyberte jeho vlastní skicovou rovinu **XY, XZ nebo YZ**.
3. Tlačítkem **Sketch…** otevřete běžný Sketcher. Nakreslete jednu otevřenou
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

Výchozí aktivní stav dokumentu je **Bez řezu**. Zaškrtnutí řádku ve stromu
nebo **Zobrazit řeznou plochu** zviditelní pomocný obrys. **Aktivní** v menu
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
úhlu, rozteče a typu šraf. Vlastní šrafování komponenty přebíjí obecný styl.

## Výkres a tisk

Ve **Vlastnostech pohledu → Řez** vyberte uložený řez zdrojového dokumentu.
Volba **Pohled kolmo k řezu** nastaví pohled kolmo k prvnímu úseku řezné
plochy. Lomený řez se promítá ve své skutečné prostorové poloze; jednotlivé
úseky se automaticky nerozvíjejí do jedné roviny. Po jejím
vypnutí lze použít například izometrický pohled. **Šrafování** lze vypnout;
úhel, rozteč, posunutí a typ (rovnoběžné, křížové, čárkované) se ukládají
s pohledem. Nastavení zdrojového řezu a přepsání stylu pouze ve výkresu
jsou oddělená. Rozteč a posunutí jsou milimetry na papíře, nezávislé na měřítku.
Sousední komponenty bez vlastního stylu střídají úhel o 90°.

Pokud list obsahuje běžný pohled stejného zdroje, řez se k prvnímu takovému
pohledu připojí čárou se směrovými šipkami a označením. Šrafy respektují dutiny
a zakrytí dalšími díly. V režimu stínování bez hran lze šrafy ponechat zapnuté.

Pracovní šrafy jsou zelené. PDF je exportuje vektorově černým tenkým perem
(výchozí **0,25 mm**). Čára označující řez používá červené pero (výchozí
**0,7 mm**). Náhled tlouštěk nemění fyzické tiskové tloušťky. Export zachovává
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
