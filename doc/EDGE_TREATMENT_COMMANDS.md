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
