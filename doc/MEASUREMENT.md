# Měření ve View

Nástroj **Měření** je na liště nad 3D pohledem Partu a sestavy a lze jej
použít také v otevřeném Sketcheru. Měří aktuální vypočtenou geometrii;
nevyvolává regeneraci modelu ani jeho závislostí.

## Výběr a výsledky

Okno obsahuje dvě reference nad sebou. První výběr rovnou vypíše vlastnosti
entity a aktivuje druhé pole. Druhá reference je volitelná.

| Entita | Informace |
| --- | --- |
| Bod | Souřadnice X, Y, Z v zobrazeném prostoru |
| Hrana nebo křivka | Délka |
| Ohraničená plocha | Obsah |
| Těleso nebo komponenta | Obsah povrchu, objem; hmotnost při známé hustotě |
| Osa | Bod a směr nekonečné osy |
| Konstrukční rovina | Bod a nekonečná rovina |

Se dvěma referencemi se navíc zobrazí nejkratší vzdálenost a spojnice jejích
koncových bodů. Plocha se měří pouze v rámci svého obrysu. Osy a konstrukční
roviny se pro vzdálenost chápou jako nekonečné. Překrývající se tělesa a bod
uvnitř uzavřeného tělesa mají vzdálenost nula.

Při výběru platí společné pořadí kandidátů ve View; pravé tlačítko přepíná
překryté kandidáty. Kliknutí do textu reference aktivuje její zadání či
nahrazení (zelený rámeček). Oko nezávisle zapíná azurovou inspekci uložené
reference. Křížek referenci odstraní.

- Krátké prostřední tlačítko ukončí zadávání a dočasné inspekční zvýraznění.
  Reference a výsledek zůstanou v okně.
- Prostřední tažení ovládá pohled.
- Dvojklik prostředním, také nad View, provede OK a zavře okno.
- **OK** ani **Zrušit** neukládají měření do historie.
- **Uložit** uloží pojmenované měření do stromu zobrazeného dokumentu a zavře okno.

## Přesnost a jednotky

Délky hran a obsahy ploch, které jsou k dispozici z explicitního výpočtu
tělesa, se uloží spolu s jeho geometrií. Inspector je čte bez dalšího volání
OCCT. Objem a hmotnost celé komponenty využívají poslední vypočtené údaje
jejího zdroje. Hmotnost se neodhaduje bez materiálové hustoty.

Vzdálenosti ke zakřivené geometrii používají její polygonální zobrazení.
Takové výsledky, stejně jako délky či obsahy bez dostupné přesné uložené
hodnoty, jsou označeny **≈**. Přesnost těchto hodnot závisí na rozlišení
zobrazené geometrie. Vzdálenosti mezi body, přímými hranami a rovnými
polygonálními plochami se počítají přímo.

Výstup používá délkové a hmotnostní jednotky dokumentu; obsah a objem
používají druhou a třetí mocninu délkové jednotky. Například `12,000mm`,
`240,000mm²` nebo `0,047kg`.

## Uložené měření

Měření je informační položka, ne operace tělesa. V Partu se vloží k aktuálnímu
místu historie tělesa, v sestavě do stromu sestavy. Nese název, stabilní
reference včetně cesty instance a poslední uložené výsledky. V sestavovém
kontextu se ukládá do zobrazené sestavy.

**Vlastnosti** znovu vyhodnotí reference nad aktuální geometrií. Chybějící
reference se označí červeně a lze ji nahradit výběrem do téhož pole. Uložení
je do opravy blokované. Zrušení opravy původní záznam zachová.
Kontextové menu obsahuje také **Odstranit**. Uložení, změna i odstranění
podporují dokumentové Undo/Redo.

Uložené hodnoty jsou snímkem posledního uložení měření; otevření vlastností
je aktualizuje pro právě zobrazený vypočtený model. Změny zdrojového Partu
se do nadřazené sestavy nadále přebírají pouze příkazem **Regenerovat**.

## Související ovládání kót

Jeden klik na komponentu sestavy ji pouze označí azurově. Dvojklik zobrazí
kóty jejího uložení. Kóta se vybírá samostatně nad hodnotou; dvojklik otevře
úpravu její hodnoty, pravé tlačítko nabídne vlastnosti kóty. Zamknutí a meze
hodnoty zůstávají účinné. Kliknutí do prázdna nebo ukončení zobrazení kót je skryje.

Generované průměrové kóty ve Sketcheru, Partu, sestavě a Drawingu používají
**⌀ (U+2300)** ze společného seznamu symbolů. Tato značka se přenáší také
do výstupů používajících společné formátování kót.

## Ověření

- `zima_cpp_measurement_contract_tests`: analytické vzdálenosti, ohraničené
  plochy, průniky, uzavřená tělesa, posunutý model, uložené přesné veličiny
  válce, persistence Part/Assembly, Undo a značka průměru.
- `zima_cpp_measurement_inspector_ui_contract`: skutečný společný picker,
  informace o první a druhé entitě, prostřední tlačítka nad View, Uložit,
  strom, opětovné otevření a oprava chybějící reference.
- `zima_cpp_assembly_refresh_ui_contract`: výběr komponenty, dvojklik,
  dostupnost kóty a její vlastní editor.
