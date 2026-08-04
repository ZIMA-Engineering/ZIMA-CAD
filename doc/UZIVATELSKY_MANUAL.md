# ZIMA-CAD – uživatelský manuál

## Ovládání 3D pohledu

| Ovládání | Funkce |
| --- | --- |
| Prostřední tlačítko + pohyb myši | Otáčení pohledu |
| Prostřední + pravé tlačítko + pohyb myši | Posun pohledu |
| Kolečko myši | Přiblížení a oddálení |
| Jeden klik prostředním tlačítkem | Použít změny v aktivním dialogu, pokud nabízí tlačítko Použít |
| Dvojklik prostředním tlačítkem | Potvrzení aktivního dialogu tlačítkem OK |
| F2 | Zavřít aktivní dokumentový tab |

Po posunu kombinací prostředního a pravého tlačítka se kontextové menu
neotevře.

Příkazy **Obnovit pohled** a základní pohledy (izometrický, přední, zadní,
levý, pravý, horní a dolní) používají plynulý animovaný přechod kamery.

## Pohled kolmo

1. V horní liště pohledu aktivujte tlačítko **Pohled kolmo**.
2. Ve 3D pohledu vyberte rovinnou plochu solidu nebo referenční rovinu.
3. Kamera se natočí kolmo k vybrané geometrii a příkaz se automaticky ukončí.

Příkaz lze před výběrem zrušit opětovným kliknutím na tlačítko nebo klávesou
`Esc`.

## Výběr referencí ve Vlastnostech

- Vybrané plochy, hrany, body, osy a roviny zůstávají ve view azurově
  zvýrazněné.
- Geometrie právě hledaná výběrovou funkcí má oranžový obrys stejně jako při
  běžném výběru modelu.
- Referenční plocha je označena pouze azurovými obvodovými hranami, nikoli
  barevnou výplní.
- Kliknutím na název reference v okně Vlastnosti lze její zvýraznění vypnout
  nebo znovu zapnout. Reference přitom zůstává součástí vazby.
- Skutečné odstranění reference provádí červené tlačítko **×** vlevo na jejím
  řádku.
- Každá nová reference se nejprve ověří v dočasném řešení. Konfliktní,
  přeurčující nebo nadbytečná reference se do seznamu nepřidá a původní
  geometrie zůstane beze změny.
- Při dosažení 0 stupňů volnosti je objekt plně určený. Geometrii lze stále
  vybrat, ale jako další reference se přijme až po odebrání některé existující
  vazby.
- Vrchol aktuálního výsledného tělesa lze použít jako polohovou referenci bodu
  nebo kontejneru. Ukládá se stabilní `VertexRef` a odebere všechny tři
  posuvné stupně volnosti. Když vrchol zmizí, reference se označí jako
  chybějící, zachová poslední polohu a nepřeskočí na jiný vrchol podle pořadí.
- Každý kontejner vlastní úplný lokální souřadný systém a ve Vlastnostech má
  šest stupňů volnosti `X/Y/Z + RX/RY/RZ`, včetně kontejneru bodu, osy a roviny.
  Vrchol může určit tři posuvné DOF; orientaci kontejneru následně určí nejvýše
  dvě nezávislé orientační reference. Po dosažení 0 DOF se další nadbytečné,
  duplicitní nebo konfliktní reference nepřidají.
- Pole `RX/RY/RZ` sledují skutečně zbývající rotační DOF. Bez orientační
  reference jsou aktivní všechna, po první zůstane aktivní pouze rotace kolem
  určeného směru a po druhé nezávislé referenci všechna zašednou. Úhlový offset
  patří konkrétní vazbě a nesmí z uzamčené rotace znovu udělat volný DOF.

## Živé úpravy ve Vlastnostech

- Změna číselné hodnoty, včetně použití šipek `+` a `−`, se okamžitě projeví
  ve 3D pohledu.
- `Enter` v číselném poli dokončí zadání hodnoty, ale okno nezavře.
- **Použít** potvrdí aktuální stav a ponechá okno otevřené.
- Jeden klik prostředním tlačítkem odpovídá tlačítku **Použít**. Pravidlo
  platí globálně také v dialozích nového souboru, materiálu a nastavení.
- **OK** potvrdí změny a zavře okno.
- **Zrušit** obnoví stav při otevření okna nebo stav naposledy potvrzený
  tlačítkem **Použít**.
- Dvojklik prostředním tlačítkem odpovídá tlačítku **OK** v aktivním dialogu.

### Operace solidu

V horní části Vlastností solidu je výrazný přepínač operace:

- zelené **+ Přičíst** přidává objem,
- červené **− Odečíst** odebírá objem.

Změna operace se okamžitě projeví ve view. Stejná operace je nadále dostupná
také v kontextovém menu objektu v tree. Obě místa pracují se společným
nastavením solidu.

### Zaoblení hrany

Spusťte **Zaoblení** a potom ukažte podporovanou hranu výsledného tělesa ve
3D pohledu. Zadejte kladný poloměr. Nový prvek se vloží do historie a hranu
uchovává její stabilní sémantickou referencí, nikoliv pořadovým číslem
geometrického jádra. Poloměr lze později změnit dvojklikem nebo přes
**Vlastnosti** prvku. Neproveditelná hodnota zachová poslední platnou geometrii.

První podporovaný rozsah je jedna stabilně pojmenovaná hrana Boxu/Wedge nebo
hrana, jejíž identita byla zachována podporovanou historií. Výběr více hran a
Chamfer budou doplněny v dalším kroku.

## Režim skici

Ve Vlastnostech skici tlačítko **SKETCH** potvrdí její umístění a otevře
samostatný režim kreslení. Tlačítko je dostupné po výběru roviny nebo rovinné
plochy, která určuje orientaci skici.

Po vstupu se pohled nastaví kolmo ke skice. Lokální osy X/Y jsou zobrazené
hnědou tenkou čárkovanou čarou přes celé view. Profilová geometrie je modrá,
body jsou žluté a konstrukční čáry jsou žluté a čerchované jako osy.

Základní nástroje jsou **Konstrukční čára**, **Bod**, **Úsečka**,
**Obdélník**, **Kružnice**, **Oblouk** a **Spline**. Kružnice se zadává
prvním kliknutím do středu a druhým kliknutím na obvod. Druhý klik pouze určí
poloměr; trvalým řídicím bodem kružnice je její střed. Pravé tlačítko zruší
rozpracovaný prvek; u spline ji po zadání alespoň dvou bodů dokončí.

Jeden klik prostředním tlačítkem potvrdí právě zadávanou entitu. Tažení
prostředním tlačítkem nadále pouze otáčí pohled a zadání nepotvrdí.

Každý bod má interní souřadnice X/Y, ty ale bez explicitní uživatelské kóty
nejsou podmínkou a nezobrazují se. Solver je používá jako aktuální polohu a smí
je měnit při řešení vazeb. Konstrukční čára i další geometrie odkazují na své
řídicí body; kliknutí do volného místa bod vytvoří a kliknutí poblíž
existujícího bodu jej znovu použije.

Vazba **Tečná** se vytvoří postupným výběrem úsečky (nebo konstrukční čáry)
a kružnice v libovolném pořadí. Bod dotyku musí ležet v rozsahu vybrané
úsečky. V místě dotyku vznikne odvozený bod označený značkou **T**. Solver
zachová stranu kružnice vůči čáře z okamžiku vytvoření vazby.

Vratný rádius společného rohu dvou úseček se vytváří v režimu výběru.
Kliknutím se vybere první úsečka a `Ctrl`+kliknutím se k ní přidá druhá.
Tažením jejich společného bodu podél některého ramene vzniká živý tečný
oblouk. Původní ostrý roh zůstává virtuálně zachovaný. Tažením některého
tečného konce zpět do tohoto rohu se poloměr zmenší na nulu a rádius odstraní.

Řídicí souřadnicová kóta se vytvoří příkazem **Kóty → Vodorovná vzdálenost**
nebo **Kóty → Svislá vzdálenost** a následným výběrem bodu. Teprve takto
vytvořená kóta se zobrazí a její hodnota vstupuje do solveru.

Příkaz **Kóty → Vzdálenost** vytvoří po výběru dvou bodů šikmou řídicí kótu.
Její hodnota je skutečná eukleidovská vzdálenost bodů. Při změně hodnoty se
zachová dosavadní směr spojnice, pokud jej jiné vazby neurčují jinak.
Explicitní kóty jsou ve skicáři zobrazené vždy; samostatný přepínač jejich
viditelnosti se nepoužívá.

V nabídce **Vazby → Kolmá** vyberte postupně dvě úsečky nebo konstrukční
čáry. První je ta, která se má srovnat; druhá určuje referenční směr.
U první zůstane zachovaný první bod a délka a její druhý bod se dopočítá tak,
aby byly čáry kolmé. Pravé tlačítko zruší první výběr. Jako druhou referenci
lze zvolit také čárkovanou lokální osu X nebo Y skici nebo externí čárovou
referenci. Vazbu lze odstranit z kontextové nabídky ve stromu skici.

U vazby **Rovnoběžná** je pořadí stejné: první vybraná čára se srovná a nese
symbol rovnoběžnosti, druhá čára určuje referenční směr. Druhá čára se nikdy
nepřepočítává. Pokud je první čára již směrově zavazbená, ohlásí se konflikt.

Příkazy **Vazby → Vodorovná** a **Vazby → Svislá** se aplikují výběrem jedné
úsečky nebo konstrukční čáry. První bod zůstane pevný a odpovídající souřadnice
druhého bodu se dopočítá. Vodorovné a svislé vazby jsou uprostřed geometrie
označené zeleným písmenem **H** nebo **V**.

V nabídce **Vazby → Rovnoběžná** vyberte referenční a potom řízenou úsečku.
Solver zachová délku řízené úsečky a natočí ji rovnoběžně; vazbu označuje
zelený symbol **∥**.

Zobrazené explicitní kóty lze pravým tlačítkem **Zamknout** nebo
**Odemknout**. Zamknutá kóta je řídicí: solver zachová její hodnotu.
Odemknutím přestane být podmínkou a z pohledu zmizí. Stav se ukládá společně
se skicou.

Tlačítko **Pohled kolmo** s ikonou normálového pohledu kdykoliv znovu narovná
a vystředí kameru podle aktivní skici.

Nástroj **Výběr** s ikonou šipky ukončí rozpracovaný kreslicí příkaz. Kliknutím
lze označit bod nebo geometrii oranžově a odstranit ji tlačítkem
**Vymazat vybrané** nebo klávesou `Delete`. Odstranění řídicího bodu odstraní
také geometrii, která je na něj navázaná.

- **Dokončit skicu** uloží změny a obnoví předchozí 3D pohled.
- **Zrušit úpravy** zahodí změny od vstupu do režimu a vrátí se do Vlastností
  skici.

## Pomocná geometrie kontejneru

Viditelnost pomocných bodů, os a rovin se ovládá v tree pravým tlačítkem nad
položkou **Počátek** příslušného kontejneru:

- **Skrýt** pomocnou geometrii,
- **Odkrýt** pomocnou geometrii.

Nastavení je součástí dokumentu.

## Základní práce se sestavou

Nový dokument typu **Sestava** používá příponu `.asmz` a automaticky otevře
aplikaci **Sestava**. Tlačítko **Vložit** v pravém panelu vloží existující díl
`.prtz`. První díl se vloží do počátku a další díly se podle skutečných rozměrů
automaticky rozloží vedle dosavadní sestavy.

Každý vložený soubor je ve stromu samostatná instance pojmenovaná podle
zdrojového souboru. Po rozbalení ukazuje strom zdrojového dílu a
lokální počátek instance. Poloha instance patří pouze sestavě; změna polohy
nemění zdrojový `.prtz`.

Změny polohy ve Vlastnostech se zobrazují živě přímo ve view. Strom sestavy
zůstává sestavový i při práci s obsahem vložených dílů. Dvojklik na instanci
přímo ve 3D view neotevírá Vlastnosti: zobrazí nebo skryje její klikací kóty
vazeb. Vlastnosti se nadále otevírají dvojklikem ve stromu nebo z kontextového
menu.

Pravým tlačítkem nad instancí zvolte **Vlastnosti**. Okno vychází z vlastností
kontejneru v Partu a obsahuje tři řádky ustavení:

`reference dílu ↔ reference sestavy | Typ vazby | Hodnota | Flip`

Klikněte na první zeleně označené pole a vyberte plochu, rovinu nebo osu
umisťovaného dílu přímo ve 3D pohledu. Potom vyberte odpovídající referenci
druhého dílu nebo sestavy. Aktivní výběr automaticky pokračuje další dvojicí.
Dialog podle geometrie a zbývajících stupňů volnosti nabízí pouze použitelné
typy: rovinnou vazbu s posunutím, **Souosost** nebo **Úhel**. Hodnota používá
`mm` nebo stupně podle typu; **Flip** obrací orientaci. Uživatelské i generované
osy zdrojového dílu lze vybírat ve view i ve stromu. Zakřivené plochy jako
rovinné reference podporované nejsou.

Po souososti zůstává volný posun a rotace podél společné osy. Dosednutí čelních
ploch může uzamknout zbývající posun a následná dvojice bočních rovin pak
automaticky nabízí úhlovou vazbu. Solver zachovává nejbližší platnou polohu a
opačnou větev volí pouze pomocí **Flip**.

Kóty zobrazené dvojklikem na díl zahrnují také nulové rovinné a souosé vazby.
Úhel a rovinné posunutí lze přepsat přímo v kótě a potvrdit Enterem; souosost je
zobrazená jako zamčená informační vazba.

Sestavové vazby jsou v současnosti prototyp. Plochy jednoduchých těles Box a
Wedge a plochy Extrusion mají stabilní sémantickou identitu; nepodporovaná
plocha se nesmí tiše nahradit plochou se stejným pořadovým číslem. Stabilní
pojmenování zatím není dokončené pro Revolve ani pro plochy změněné
booleovskými operacemi a sestavovými řezy.

### Aktivace dílu v sestavě

Příkaz **Aktivovat díl** zpřístupní modelovací nástroje dílu přímo v tabu
sestavy. Strom zůstane sestavový a ostatní díly lze nadále použít jako zdroje
externích referencí. Aktivní díl se ve view zobrazuje ve své původní podobě
před sestavovými řezy.

Skica aktivního dílu používá jeho lokální počátek a transformaci instance,
nikoliv počátek sestavy. Změny provedené v aktivním dílu se ukládají do jeho
zdrojového `.prtz`. Tlačítko **Zpět do sestavy** ukončí kontextovou editaci.

Je-li stejný díl otevřený také v samostatném tabu, aplikace používá společný
aktuální dokumentový stav. Externí reference vytvořená v sestavě uchovává
identitu sestavy a konkrétní instance; chybějící nebo nejednoznačný zdroj se
označí jako ztracená reference místo použití nesprávné geometrie.

Externí reference skici na podporované plochy, hrany a vrcholy Extrusion se
ukládají podle původu ve zdrojové skici, nikoliv podle aktuálního pořadí
topologie. Po změně rozměru rodiče se potomek automaticky regeneruje. Bod ležící
na hlavní ose skici zůstává samostatně vybratelný a lze jej odstranit stejně
jako ostatní externí reference. Bodové externí reference se při výběru
nezobrazují všechny současně: nejbližší bod se zvýrazní až po přiblížení
kurzoru a kliknutím se potvrdí. Již vybraný bod zůstává viditelný. Příkaz
**Dokončit skicu** se při vstupu z vlastností kontejneru vrátí zpět do těchto
vlastností.

### Sestavové řezy

V sestavě jsou dostupné operace **Protrusion** a **Revolve** pouze jako
odečítání materiálu. Řez lze omezit na vybrané díly; bez výběru působí na
všechny vložené instance. Výsledek se ukládá výhradně v `.asmz` a nemění
původní soubory dílů.

Při hoveru se pro každou instanci střídá její sestavově upravený a původní
stav, potom se pokračuje další instancí. Po aktivaci dílu se hover řídí
pravidly Partu.

### Barvy a přejmenování

Barva nastavená pro vložený díl patří konkrétní instanci a nemění barvy
ostatních dílů. Příkaz **Soubor → Přejmenovat soubor…** zachová správnou
příponu a aktualizuje interní odkazy v dílech, sestavách a výkresech. Pokud má
přejmenovaný model stejně pojmenovaný `.drwz`, přejmenuje se s ním.

## Základní práce s výkresem

Výkres používá příponu `.drwz` a je navázaný na zdrojový díl `.prtz` nebo
sestavu `.asmz`. Lze jej založit přes **Soubor → Nový → Výkres**, nebo
tlačítkem **Výkres** v záhlaví stromu otevřeného dílu či sestavy. Tlačítko
nejprve otevře již existující stejně pojmenovaný výkres a teprve pokud
neexistuje, vytvoří nový.

V záhlaví stromu výkresu je opačné tlačítko **Díl** nebo **Sestava**, které
otevře zdrojový dokument.

### Listy a formáty

Jeden výkres může obsahovat více listů. Záložky listů jsou dole pod výkresovou
plochou:

- **+** přidá nový list,
- **−** odebere aktivní list; poslední list nelze odebrat,
- roletka **Formát** mění pouze aktivní list.

Podporované formáty jsou A4, A3, A2, A1 a A0. A4 je vždy na výšku; A3 až A0
jsou vždy na šířku. Rozměry odpovídají skutečnému papíru v milimetrech.

Počátek každého listu leží v pravém dolním rohu. Kladná osa X směřuje zprava
doleva a kladná osa Y zdola nahoru. Při změně formátu se proto list mění
směrem doleva a nahoru a budoucí razítko může zůstat na místě.

Výkresová plocha má černé pozadí. List nemá barevnou výplň; jeho formát
znázorňuje pouze bílý obdélník. Výkresová geometrie je rovněž bílá.

### Ovládání výkresové plochy

| Ovládání | Funkce |
| --- | --- |
| Kolečko myši | Přiblížení a oddálení kolem kurzoru |
| Prostřední tlačítko + pohyb | Posun výkresové plochy |
| Obnovit pohled | Animovaně zobrazit celý aktivní list |
| Levé tlačítko + pohyb na pohledu | Přesunutí vloženého pohledu |
| Levé tlačítko + pohyb na popisku | Přesunutí názvu a měřítka pohledu |
| Delete | Odstranění vybraného pohledu |
| Esc | Zrušit právě umisťovaný pohled |

Výkresová plocha je čistě 2D a nepodporuje rotaci.

### Vložení pohledu

V pravém panelu aplikace Výkres je příkaz **Vložit pohled**. První pohled je
standardně izometrický; náhled se připojí ke kurzoru a levým kliknutím se
umístí na aktivní list. Příkaz **Vytvoř projekci** v kontextové nabídce pohledu
vytváří navázaný pohled podle evropské nebo americké projekční metody. Směr se
přichytává v osmi polohách po 45 stupních. Přesun rodiče přenese jeho odvozené
pohledy, zatímco samostatný přesun potomka zůstává na jeho projekčním paprsku.

Pohled uchovává odkaz na zdrojový model. Při aktivaci tabu se jeho geometrie
obnoví ze skutečné topologie nativního rendereru; stará uložená 2D cache
promítnutých čar se nepoužívá.

Vlastnosti pohledu nabízejí drátové zobrazení, skryté hrany, zobrazení bez
skrytých hran, stínované zobrazení s hranami a čisté stínované zobrazení.
Globální tlačítka režimu modelu jsou ve výkresu vypnutá, protože styl patří
každému pohledu samostatně. Stínování zachovává barvy modelu a komponent.
Zaškrtávací skupina **Zobrazit popisek pohledu** obsahuje název, zdroj měřítka
a měřítko. Nad obrysem pohledu zobrazí bílý popisek o velikosti 5 mm ve tvaru
`Název` a `M1:1`; popisek lze samostatně vybrat a přesunout.

První lineární výkresová kóta se vytváří výběrem dvou skutečných hran modelu
a umístěním žluté kóty v listu.

Rámečky a zóny, razítka, ISO font, řezy, detaily, úplná sada ISO kót, tolerance,
pozice, popisky, technické symboly, kusovníky a export PDF/DXF jsou další etapy
vývoje a v této verzi zatím nejsou dokončené.
