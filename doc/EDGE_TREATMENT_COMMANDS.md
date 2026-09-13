# Příkazy zaoblení a sražení hran

První etapa zpřístupňuje skutečný vypočtený vstup operace a její tečné trasy.
Tvorba a editace parametrů Fillet/Chamfer bude následovat; tyto dva dotazy
model pouze čtou.

```json
{"command":"edge_treatment.edges","arguments":{}}
{"command":"edge_treatment.edges","arguments":{"container":"ID-ZAOBLENI","owner":"ID-ZDROJE"}}
{"command":"edge_treatment.route","arguments":{"seed":{"owner":"ID-ZDROJE","key":"KLIC-HRANY"},"container":"ID-ZAOBLENI"}}
```

Bez `container` je vstupem aktivní Těleso u svého kurzoru historie. S
`container` je vstupem těleso před daným Fillet/Chamfer kontejnerem, takže
editace najde i hranu, kterou původní operace odstranila. `document` je
volitelné ID otevřeného Partu; čtení ho nemusí aktivovat. Vrácené souřadnice
jsou v mm v rámci uvedeném jako `coordinate_system` (`body` nebo `document`),
a pole `body` identifikuje konkrétní tělesový rámec. Nejde automaticky o rámec
celé zobrazené sestavy.

## Hrany

`edge_treatment.edges` podporuje `owner`, `offset` (0–100000000) a `limit`
(1–5000, výchozí 500). Vrací `items`, `total`, `offset`, `limit`, `has_more`,
kontext dokumentu, kontejneru, Tělesa a revizi. Každá položka obsahuje původní
`owner`, `key`, prázdnou `instance_path`, příznak `ambiguous` a `segments`.
Segment uvádí počet vykreslovacích bodů, uloženou délku `length_mm` (nebo
`null`), příznak parametrického švu a koncové body s vlastními identitami a
`position_mm`. Vrací uložená data bez geometrického výpočtu.

Více geometrických segmentů se stejnou identitou se nepojmenuje podle
pořadí; položka je označena jako nejednoznačná. Seznam je přehled vstupní
geometrie; geometrickou použitelnost hrany pro konkrétní úpravu ověřuje
výpočet operace.

## Tečná trasa

`edge_treatment.route` vyžaduje `seed` s textovými `owner`, `key` a volitelně
prázdnou `instance_path`. Nepřijímá číslo hrany v OCCT ani `geometry_index`.
Volitelné `tolerance_degrees` je JSON číslo od 0 do 90, výchozí 35 stupňů.
Výsledek obsahuje `edges`, volné `endpoints`, `endpoints_complete`, `closed`,
toleranci a stejný kontext jako seznam. Každý volný konec má stabilní identitu
a polohu. Pokud uložený paket nemá úplné reference konců (například samostatná
kružnice), trasa zůstane platnou hranou, ale `endpoints_complete` je `false`,
`endpoints` je prázdné a `closed` je `null`. Dotaz se o uzavřenosti nedohaduje
podle blízkosti bodů. Jinak je `closed` boolean odvozený z uložených spojení.

GUI i dotaz používají `kernel::tangent_edge_route` nad uloženými daty vieweru.
Hrany propojí pouze shodné původní body. Prostorová blízkost sama nestačí.
Trasa smí pokračovat přes různé zdrojové kontejnery, ale pouze uvnitř jednoho
výskytu; větvení se dvěma možnými pokračováními ji zastaví. Směr, který se
vrací zpět na stejnou stranu bodu, se nepovažuje za tečné pokračování.

GUI předává měřítko obrazu pro přeskočení téměř nulových vzorků polyčáry;
CLI bez kamery používá 0,000001 mm. Tato tolerance nespojuje různé identity
bodů. Nejednoznačný nebo chybějící zdroj se odmítá; neúplné reference konců se
označí ve výsledku.
Obnovení trasy v GUI nyní výslovně nepředává žádný platný index vybrané hrany,
takže ani první hrana v poli nemůže obejít kontrolu jednoznačnosti.

## Ověření

Samostatný test datového průchodu ověřuje větvení, otočení zpět, kruhovou
trasu, různé zdrojové vlastníky, opakované výskyty, nejednoznačný a prázdný
zdroj. Modelový test dotazů kontroluje skutečné hrany a konce kvádru,
uzavřené kružnice válce, rollback před zaoblením, více Těles, kurzor historie,
stránkování a nezměněnou revizi i cache. Přidány jsou skutečné CLI a GUI
konzolové scénáře. Výsledky běhů jsou uvedeny níže.

Nativní datový formát ani start šablony se nemění. Dotazy nevolají OCCT,
nespouštějí regeneraci a nezavádějí vedlejší soubory.


První cílený běh měl **1/2** úspěšných testů: datový průchod prošel, modelový
test nesprávně předpokládal dva uložené konce samostatné kružnice. Po ověření
současného datového kontraktu vrací dotaz tento případ bez vymyšlených konců
s `endpoints_complete: false` a `closed: null`. Cílená sada následně prošla
**2/2** (0,16 s), `build/edge-query-circular-build.log` a
`build/edge-query-circular-tests.log`. Přidána je i regrese skutečného vieweru,
která rozlišuje konkrétní vybranou hranu od nejednoznačného programového
obnovení. Geometrie ani nativní ukládání se tím nemění.

Oba programy a testovací cíle jsou sestavené. Související sada prošla
**10/11** (74,43 s); nový GUI test poslal místo prázdného objektu `null`.
Po opravě testovacího požadavku prošel celý skutečný GUI scénář **1/1**
(45,78 s). Produkční dotazy, proces CLI, viewer, modelové regrese, historie
a překlady v prvním běhu prošly. Logy: `build/edge-query-related-tests.log`,
`build/edge-query-final-build.log`, `build/edge-query-gui-tests.log`.


## Tvorba a editace Fillet/Chamfer

Příkazy `fillet.create/get/set` a `chamfer.create/get/set` jsou zapojené na
`workspace::commit_edge_treatment`, kterou používá i tvorba a Vlastnosti v
GUI. Katalog obsahuje 195 příkazů; předchozí výsledky výše patří pouze
čtecím dotazům.

```json
{"command":"fillet.create","arguments":{"radius_mm":2,"routes":[{"edges":[{"owner":"ID-ZDROJE","key":"KLIC-HRANY"}]}]}}
{"command":"fillet.set","arguments":{"container":"ID-ZAOBLENI","mode":"linear","radius_mm":2,"radius_end_mm":5,"routes":[{"edges":[{"owner":"ID-ZDROJE","key":"KLIC-HRANY"}],"start":{"owner":"VLASTNIK-BODU","key":"KLIC-BODU-R1"}}]}}
{"command":"chamfer.create","arguments":{"mode":"two_distances","distance_a_mm":2,"distance_b_mm":5,"flip":false,"routes":[{"edges":[{"owner":"ID-ZDROJE","key":"KLIC-HRANY"}]}]}}
```

- `fillet`: `mode` je `constant` nebo `linear`; `radius_mm` je R nebo R1,
  `radius_end_mm` je R2. `reverse` obrací přiřazení R1/R2 mezi konci.
- `chamfer`: `mode` je `equal_distance`, `two_distances` nebo
  `distance_angle`; `distance_a_mm` a `distance_b_mm` jsou A/B,
  `angle_degrees` je úhel. `flip` volí druhou podpěrnou plochu podle
  existujícího stabilního pořadí původních identit ploch.
- Rozměry mají stejný rozsah jako Vlastnosti: 0,001–1000000 mm, úhel
  0,1–89,9 stupně. Argumenty rozměrů jsou JSON čísla v mm nezávisle na
  zobrazovaných jednotkách. Výchozí hodnoty jsou 1 mm a 45 stupňů.
- `routes` je úplná náhrada seznamu tras, 1–10000 neprázdných tras,
  dohromady nejvýše 10000 hran. Každá trasa má pole `edges` a volitelný
  `start` (nebo `null`). Reference obsahuje pouze textové `owner`, `key`
  a volitelnou prázdnou `instance_path`. Shodná hrana smí být vybraná jednou.
- Proměnné zaoblení vyžaduje explicitní původní koncový bod R1 každé jedné
  souvislé otevřené trasy. CLI směr neodhaduje. Konce získá z čtecího dotazu;
  uzavřená kružnice bez dvou konců umožňuje konstantní zaoblení.
- `name` je volitelný název, `document` volitelné ID aktivního dokumentu.
  Čtení `.get` může cílit i jiný otevřený Part. Změna vyžaduje aktivní
  vlastnící Těleso a nesmí upravovat odvozenou kopii.

`.get` vrací identity kontejneru, prvku a Tělesa, parametry, původní trasy,
uložené počátky, `value_locks` a revizi. `.create/.set` přidávají `changed`.
Shodný patch nic nepřepočítá. Zámky mají stejné klíče jako GUI: `primary`,
`secondary`, `treatment_angle`. Chybějící či nejednoznačné hrany, špatné R1,
hrany jiného Tělesa a neplatné rozměry nezanechají částečnou změnu. Kernel
ověřuje geometrickou proveditelnost při explicitním potvrzení.

GUI náhled zachovává uložený R1 i při jiném pořadí hran. Rozdělení trasy po
odebrání člena používá původní společné pravidlo přesunuté do
`document/edge_treatment_selection.hpp`: konce a směr určuje z původních
bodů. Tato změna nezasahuje do obecného umístění kontejnerů.

První modelový test odhalil nesprávný předpoklad testu o objemu proměnného
zaoblení: integrál rovinných čtvrtkružnic v souřadnici Z není jeho obecný
objemový vztah. Kontrola nyní ověřuje skutečné koncové poloměry, objem mezi
analytickými mezemi konstantních R1/R2 a objemovou symetrii po obrácení
na symetrickém kvádru. Další běh došel k chybějícím rozměrům pomocného
kvádru v testu; požadavek byl doplněn. Produkční geometrie kvůli těmto
předpokladům testu upravována nebyla.

Cílený modelový běh následně prošel **1/1** (0,66 s), včetně skutečného
objemu kruhového zaoblení válce. Oba programy jsou sestavené a související
sada prošla **12/12** (82,26 s), `build/edge-treatment-related-tests.log`.
GUI ověřuje vytvoření obou typů, OK/Cancel, nativní výsledek a uložený R1
na opačném konci proti výchozímu pořadí vieweru.

Závěrečná revize zachovává i dosavadní obnovu závislostí stromové úpravy:
společné potvrzení volá existující `calculate_part_with_resolved_references`,
aby uložilo stejný stav geometrie a návazných skic/referencí. Obecný solver
umístění se nemění. Doplněná GUI regrese odstraňuje první ze dvou tras,
ověřuje počátek zbývající trasy a odmítnutí prázdného zaoblení. Následoval úplný
běh 101 testů.

Úplný běh skončil **100/101** (471,33 s). Nová dvoutrasová GUI regrese
odhalila chybu výpočtu R1: rozbalení jedné původní hrany na více runtime
použití posunulo index do původního seznamu konců a mohlo číst mimo jeho
rozsah. Samostatný modelový test čtyř tras reprodukoval stejné odmítnutí.

Oprava přenáší uložený R1 společně s každým runtime výskytem jeho původní
hrany. Identitu stále určuje původní ZIMA reference. Verze výpočetního otisku
Filletu zneplatňuje staré odvozené výsledky této operace; struktura dokumentu
a prázdné start šablony se nemění. Nová regrese měří všech osm koncových
poloměrů, obrácení směru, přeuspořádání tras a nový výpočet z nativního souboru.
Cílený modelový test opravy prošel **1/1** (1,66 s). Následné sestavení
obou programů i všech testovacích cílů dokončeno; úplná regresní sada
prošla **101/101** (470,10 s). Zahrnuje také skutečné CLI procesy, GUI
OK/Cancel, více tras s opačným R1, stromové odebrání hrany/trasy a překlady.
Logy: `build/fillet-multiple-routes-fixed-tests.log`,
`build/fillet-multiple-routes-all-build.log`,
`build/fillet-multiple-routes-full-tests.log`.
