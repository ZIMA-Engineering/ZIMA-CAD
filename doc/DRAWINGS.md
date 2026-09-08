# Výkresy

Tento dokument popisuje aktuální datový a uživatelský model výkresů ZIMA-CAD.
Základní postup je také v
[uživatelském manuálu](UZIVATELSKY_MANUAL.md#základní-práce-s-výkresem).

## Dokument, listy a zdrojový model

Výkres má příponu `.drwz` a odkazuje na jeden zdrojový díl `.prtz` nebo
sestavu `.asmz`. Jeden dokument může obsahovat více listů. Každý list má
samostatně uložený formát, rámeček, razítko, vložené pohledy, kóty a hodnoty
polí náležející pouze listu.

Tlačítko **VÝKRES** v záhlaví stromu zdrojového Dílu nebo Sestavy otevře jeho
již existující výkres. Pokud ještě neexistuje, vytvoří nový soubor výkresu a
otevře jej v novém tabu. Vazba na zdroj je uložená přímo ve výkresovém
dokumentu už před vložením prvního pohledu. Opačné tlačítko **DÍL** nebo
**SESTAVA** proto vždy přejde zpět na přesný zdrojový dokument. Přejmenování
zdroje aktualizuje jeho cestu a název také v navázaném výkresu.

Rámeček i razítko se při vložení zkopírují přímo do dat listu. Otevřený výkres
proto není závislý na pozdější existenci souboru `.frmz` nebo `.tblz` v
`config/formats`. Smazání, přejmenování nebo úprava knihovní šablony již
vložené listy nezmění. List převezme jinou definici teprve při výslovné výměně
rámečku nebo razítka.

## Rámečky a razítka

Soubory `.frmz` a `.tblz` se upravují ve skicáři. Editor šablony vždy zobrazí
celý list; nepoužívá fyzické výchozí měřítko běžné modelové skici. Uložený
počátek šablony se bez skryté kompenzace mapuje na počátek výkresu v pravém
dolním rohu.

Dodávaná razítka jsou:

- `ZE-RAZITKO.tblz` pro české popisky a české lokalizované názvy parametrů;
- `ZE-TITLE-BLOCK.tblz` pro anglické popisky a anglické lokalizované názvy.

Text začínající `&` je programovatelné pole. Interní klíče parametrů jsou
anglické, například `name`, ale lokalizovaný název současně vybírá jazyk
hodnoty. `&Název` tedy čte českou hodnotu klíče `name`, zatímco `&Name` čte
jeho anglickou hodnotu. Při zapnuté sdílené hodnotě oba tokeny vrátí stejný
text. Prázdná hodnota se ve výkresu zobrazuje jako `-`, nikoliv jako historická
ukázková hodnota ze šablony.

Upravitelné pole se při přejetí zvýrazní oranžově a po výběru azurově.
Dvojklik otevře jeho vlastnosti. Změna pole navázaného na parametr modelu
změní tento parametr ve zdrojovém dílu nebo sestavě. Hodnota patřící pouze
listu zdrojový model nemění.

## BOM Repeat Region v razítku

Oblast kusovníku se v editoru razítka označuje objektem **BOM Repeat Region**.
Je zobrazena jako fialový drátový obdélník, nikoliv jako vyplněná plocha.
Oblast má při výběru nejvyšší prioritu: přejetí nad kteroukoliv její hranou
zvýrazní celý region včetně popisků. Geometrie pod regionem zůstává dostupná
překliknutím kandidátů pravým tlačítkem.

První dvojřádek obsahuje dvě zvláštní BOM funkce:

- vlevo nahoře **Item Number** — pořadové číslo položky od 1;
- vpravo nahoře **Quantity** — počet výskytů dílu v sestavě.

Ostatní pole řádku jsou běžné parametry zdrojového dílu. Pro samostatný díl se
zobrazí jeden řádek. Pro sestavu se označený region opakuje ve směru uloženém
v regionu a vytvoří řádky kusovníku.

## Vložené pohledy a aktualizace

**Vložit pohled → kliknout na list → Vlastnosti pohledu** je společný postup
pro všechny zdroje. První pohled je izometrický. Zdrojový otevřený model nebo
soubor se vybírá přímo ve vlastnostech, společně s názvem, popiskem,
orientací, stylem, měřítkem a polohou. Vytváření a editace používají stejný
interní dialog. Změny jsou do OK jen náhled; Zrušit nevytvoří nový pohled ani
nezmění existující. Prostřední dvojklik nad listem potvrdí OK.

Výběr pohledu používá celou obdélníkovou oblast jeho projekce včetně prázdného
vnitřku. Hover, kliknutí a kontextová nabídka používají stejnou oblast a
pořadí překrývajících se pohledů. Rámeček je vidět jen při hoveru a výběru.
Strom a View označují tentýž pohled; kliknutí mimo pohledy výběr zruší.

Kontextový příkaz **Projekční pohled** připojí náhled ke kurzoru a přichytává
směr po 45 stupních podle rodiče a metody promítání listu. Potomek ukládá ID
rodiče, směr a kameru. Přesun rodiče přenese potomky, přesun potomka se drží
projekčního paprsku. Název/popisek a volba vlastního či listového měřítka se
ukládají do `.drwz`.

Pohled uchovává odkaz na zdrojový model a poslední vypočtenou projekci.
Otevření či přepnutí tabu nevyvolává výpočet OCCT ani automatickou obnovu
závislostí. **Regenerovat** výslovně načte poslední vypočtený stav otevřeného
zdroje, případně jeho uložený soubor, a z něj obnoví projekci a kóty.

Roletka **Varianta** dole obsahuje zatím pouze jméno zdrojového souboru.
Přepínání a výpočet variant Family Table jsou budoucí funkce. **Parametry**
nad View nebo v kontextové nabídce názvu výkresu otevřou parametry zdrojového
dílu/sestavy při zachování zobrazeného výkresu.

Výběr formátu a razítka začíná v adresáři **Formats** z globální konfigurace,
a to i po změně konfigurace za běhu aplikace.

## Lineární kóta

Aktuální výkresová kóta podporuje dvě rovnoběžné přímé hrany vloženého
pohledu. Plochy a obecné křivky zatím nejsou platnou referencí.

Postup:

1. Zapněte **Kóta**. Kurzor zůstane běžnou šipkou.
2. Přejeďte nad hranou vloženého pohledu. Použitelná hrana se zvýrazní
   oranžově; pravé tlačítko přepíná překrývající se kandidáty a stavový řádek
   oznamuje právě nabízený objekt.
3. Levým tlačítkem potvrďte první a potom druhou hranu. Potvrzené reference
   zůstanou azurové.
4. Žlutý náhled ukazuje skutečnou modelovou vzdálenost. Pohybem kurzoru určete
   polohu a krátkým kliknutím prostředního tlačítka ji potvrďte.
5. Nástroj zůstane aktivní pro další kótu. Rychlý dvojklik prostředním
   tlačítkem jej ukončí.

Kóta ukládá stabilní topologické reference základních objektů, nikoliv pouze
obrazové souřadnice nebo pořadí hran v aktuálním meshi. Po regeneraci modelu
se reference znovu vyřeší a hodnota i poloha kóty se přepočítají. Pokud
reference chybí nebo je nejednoznačná, nesmí se kóta tiše připojit k jiné
hraně.

Šipky používají společný ostrý tvar s polovičním úhlem 10°. Stejná geometrie
se používá také u kót ve view, os počátku a směru BOM regionu.

## Současná omezení

Podporována je první asociativní lineární kóta mezi dvěma rovnoběžnými
přímými hranami. Další typy ISO kót, řezy, detaily, tolerance, pozice,
technické symboly a export PDF/DXF jsou další vývojové kroky. BOM v razítku
už není v této skupině: Repeat Region, Item Number a Quantity jsou funkční.
