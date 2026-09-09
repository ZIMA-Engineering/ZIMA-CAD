# Zvýraznění a editace v modelování

Potvrzený výběr ve View a Tree používá azurový drát. Je viditelný i přes
materiál ve všech pěti režimech zobrazení, takže lze prohlédnout celý otvor
i jeho vnitřní podprvky. Zvýrazňuje se pouze vybraný kontejner, podprvek nebo
přesný výskyt komponenty, nikoli jiné výskyty stejného zdrojového dílu.
Hover používá oranžový drát.

Po dvojkliku na kontejner zůstává azurový drát zobrazený společně s kótami.
Toto vizuální označení je oddělené od výběru pro zadávání: kóty lze dále
vybírat a opakovaně upravovat. Kliknutí do prázdného View vyčistí potvrzený
výběr, inspekční drát i související označení Tree. Změny parametrů otvoru
a závitu zachovávají nastavení kamery.

Drát používá uloženou geometrii kontejneru. Sražení a Zaoblení používají
hrany skutečného vstupu před operací; jejich výběr není obecnou referencí
umístění. Zobrazení, hover ani výběr nevyvolávají OCCT výpočet.

Tree zobrazuje prvky se ztracenými referencemi červeným pozadím řádku
v Partu i Assembly, včetně řezů. Chyba zmizí až po skutečné opravě reference;
samotné OK ji nepotlačí. Ztracená reference má ve vlastnostech prázdné červené
pole a lze ji nahradit. Původní identita zůstává zachovaná až do nahrazení
nebo odstranění. Platné reference používají čitelné názvy, například
**Plocha 6**, místo interních identifikátorů.
Samostatný Závit navíc uchovává poslední vypočtenou geometrii ztracených
referencí pro běžné přepočty; otevření jeho Vlastností vyžaduje nové zadání
chybějících povinných referencí. Podrobnosti ukládání a chování Cancel jsou
v dokumentaci [vnějšího závitu](SHAFT_THREAD.md).

V pravém panelu příkazů Partu odděluje zelená čára Shell od skupiny začínající
příkazem Otvor. Příkaz Otvor a jeho základní podprvek Tree mají shodné jméno
i ikonu. Vnější Závit má samostatnou ikonu.

Ověření společného vykreslení zajišťují `zima_cpp_ui_contract_tests`, včetně
zakrytých hran, všech režimů zobrazení a přesné cesty výskytu. Integrační
ověření závitu kontroluje také zachování kamery a drátu po editaci kóty.

## Výchozí měřítko View a první skici

Prázdný dokument používá přibližné měřítko 1:1 podle výšky View a DPI
monitoru. Přesnost závisí na údajích, které o monitoru poskytne operační
systém. Počátky a osy jsou pomocné reference; nezpůsobí přiblížení prázdné
skici na zlomek milimetru ani při opakovaném zastoupení stejného počátku.
Existující geometrie se při Obnovit pohled nadále přizpůsobí velikosti View.
Velikost symbolů počátků na obrazovce je na tomto měřítku nezávislá.
