# Řezy dílu, sestavy a výkresu

## Vytvoření a úprava

Příkaz **Řezy…** na liště nad View otevře společné vlastnosti řezu pro Part
nebo Assembly na vlastní kartě. Při aktivním vnořeném dílu se definice
nadřazené sestavy neupravují; nejprve aktivujte sestavu nebo otevřete
samostatnou kartu zdrojového dílu. Dvěma kliknutími určete začátek a konec čáry v aktuálním
pohledu. Směr kamery a pracovní rovina se zachytí při vytvoření; pozdější
otáčení pohledu řez neposouvá. Rovinný řez prochází čárou a směrem původního
pohledu. **Obrátit směr řezu** prohodí ponechanou polovinu.

Řezy se ukládají do samostatné skupiny **Řezy** na konci stromu dokumentu,
mimo historii těles. Název A–A, B–B… lze změnit ve vlastnostech. Kontextové
menu umožňuje vytvoření, úpravu čáry, přejmenování a odstranění. Vlastnosti
editují oba koncové body nebo délku a úhel. Identita řezu a bodů zůstává
zachovaná. **Upravit čáru řezu…** znovu zadá oba body ve stejné pracovní rovině.

V této etapě je podporována jedna přímá čára. Lomený/stupňovitý řez a úplná
editace vazeb ve Sketcheru jsou další rozšíření; dialog zatím nabízí přímé
zadání souřadnic, délky a úhlu.

Dialog používá vnitřní okno se společným OK/Zrušit. Náhled je dočasný;
OK uloží jednu vratnou transakci, Zrušit obnoví původní zobrazení. Dvojklik
prostředním tlačítkem nad View potvrzuje OK; krátký klik ani navigační tažení
nepotvrzují. Při kreslení je nutné nejprve dokončit oba body.

## Zobrazení a sestavy

Nový řez je běžně skrytý. Zaškrtnutí řádku ve stromu nebo **Zobrazit rovinu**
zviditelní jeho rovinu. **Zobrazit model v řezu** zapne oříznuté zobrazení;
v jednom dokumentu je současně aktivní nejvýše jeden takový řez. Při
modelovacích vlastnostech a editaci běžné skici se používá úplná geometrie.

Řez je nastavení prezentace. Nemění výsledná tělesa, jejich objem, hmotnost,
zdrojové soubory komponent ani množství v kusovníku.

Seznam komponent rozlišuje **Řezat + šrafovat**, **Řezat bez šraf** a **Neřezat**.
Part používá identity těles, Assembly úplné cesty jednotlivých výskytů,
včetně opakovaných dílů ve vnořených sestavách. Výběr dílu ve View označí
odpovídající řádek. Hromadné označení řádků umožňuje společnou změnu režimu,
úhlu, rozteče a typu šraf. Vlastní šrafování komponenty přebíjí obecný styl.

## Výkres a tisk

Ve **Vlastnostech pohledu → Řez** vyberte uložený řez zdrojového dokumentu.
Volba **Pohled kolmo k řezu** nastaví čelní pohled na plochu řezu. Po jejím
vypnutí lze použít například izometrický pohled. **Šrafování** lze vypnout;
úhel, rozteč, posunutí a typ (rovnoběžné, křížové, čárkované) se ukládají
s pohledem. Rozteč a posunutí jsou milimetry na papíře, nezávislé na měřítku.
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

Vstupy jsou uložená čára, její pracovní rovina, vypočtená zobrazovací síť
modelu a režimy jednotlivých komponent. Výstupem je oříznutá síť, uzavřené
plochy řezu, projekce hran a šrafy v papírových jednotkách. Ořez a šrafování
používají deterministický výpočet nad uloženou triangulací; zakřivené plochy
proto mají přesnost zobrazovací tessellace, nikoli nového analytického B-Rep.
Řez nevolá OCCT a nevytváří nové trvalé reference ploch či hran modelu.

Kontrolní testy ověřují plochu řezu kvádru 100 mm², dutého profilu 84 mm²,
obrácení ponechané strany, tečný řez bez falešné plochy, přesné vynechání
opakovaného výskytu a konstantní rozteč šraf při změně měřítka i šikmé projekci.
GUI test pokrývá kreslení, OK/Zrušit, prostřední tlačítko, opětovnou editaci,
Undo, strom, uložení do Part/Assembly a výkres s PDF.
