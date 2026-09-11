# Přesné projekce spline hran do skici

## Datový tok

Vstupem je zdrojová spline hrana a rovina skici. Výstupem je vlastní
racionální B-spline skici, která zachovává tvar ortogonální projekce.
Při explicitním výpočtu tělesa OCCT omezí zdrojovou spline na skutečný
rozsah hrany. Uložíme stupeň, řídicí body, uzly včetně násobností a váhy.
Ve viewer paketu původních referencí jsou tato data spolu se stabilním
vlastníkem hrany. Pozice ve výčtu OCCT se nestává identitou.

Reference → obrys promítne řídicí body do roviny skici; uzly a váhy
zachová. Skicář vyhodnocuje racionální de Boorův algoritmus z našich dat.
Výběr, projekce, otevření vlastností a obnova z uloženého souboru nevolají
OCCT. Přesnost promítnuté spline není dána hustotou zobrazovací sítě.
Data se transformují společně s geometrií Partu, výskytu sestavy,
zrcadlením a polem. Výpočet navazujícího tělesa používá stejné uzly a váhy;
jsou zahrnuty do otisku vstupů pro cache.

## Vlastnictví a aktualizace

Vlastní křivka má samostatné ID a vlastní řídicí body. Externí reference
je samostatná vazba. Delete na externí referenci smaže vazbu a její
seskupení; vlastní křivka, body a jejich identity zůstanou zachovány.
Existující rozměry a vazby k vlastní geometrii tím nezaniknou.

Při explicitní obnově zdroje se aktualizují řídicí body, uzly a váhy.
Při stejném počtu řídicích bodů zůstávají jejich identity zachované.
Pokud zdroj zmizí, je nejednoznačný nebo změní počet řídicích bodů,
zůstane poslední platný tvar a reference se označí jako porušená.
Nedochází k automatickému přiřazení jiné podobné hrany.

Vlastnosti přesné spline zachovávají stupeň a neperiodickou parametrizaci;
řídicí body jsou během externího navázání pouze pro čtení. Pouhé OK
nezkracuje přesnost původních souřadnic podle počtu zobrazovaných desetinných
míst. Po odpojení lze polohy řídicích bodů editovat.

## Rozsah této změny

Přesný převod se týká B-spline hran, včetně racionálních a oříznutých
zdrojových splinů. Původní rozpoznávání ostatních druhů hran se tímto
krokem nemění. Přesné spline zatím neprocházejí starým ořezem, který
rekonstruuje tvar ze vzorků. Asociativní ořez je další samostatný krok,
po něm následuje offset našich křivek. Spojnice budou ruční.

Ukládání zůstává v `.prtz`, `.asmz`, `.drwz`; nevzniká povinný doprovodný
soubor. Interní verze jsou Part 18 / JSON 42, Assembly 15 / JSON 24,
Drawing 14 / JSON 6 a Sketch 32. Startovací šablony a testovací dokumenty
se aktualizují zároveň. Staré formáty se nepřevádějí při běžném načítání.

## Ověření

Regresní test `zima_cpp_exact_spline_contract_tests` ověřuje analytickou
racionální čtvrtkružnici, nerovnoměrné uzly, obrácení směru, zrcadlení,
uložení, aktualizaci zdroje, porušenou vazbu, odpojení a objem extruze.
Zahrnuje také plnou periodickou hranu, omezený parametrický rozsah,
obrácenou orientaci a prostorový posun. Dialogový test ověřuje zachování
přesnosti souřadnic při pouhém OK.

Na testu `63113_0H030_mg___773WF0593_01.stp` bylo prověřeno 2 077 spline
hran v rovinách XY, XZ a YZ. Z 6 231 kombinací bylo 30 projekcí
zdegenerovaných do bodu a 6 201 křivek prošlo vytvořením a uložením/načtením
skici. Porovnání 1 025 parametrických pozic každé křivky proti původní
OCCT hraně naměřilo maximum 9,87818e-10 mm. Jde o bodové měření, nikoli
formální důkaz spojité horní meze chyby. Samotný audit trval 3,40 s;
nejde o měření celého importu do Partu.

Předchozí převod používal až 16 bodů zobrazovacího lomeného obrysu jako
řídicí body nové spliny. Na stejném souboru dříve naměřil odchylky až
0,281186 mm při zobrazovací odchylce 0,1 mm a 0,0650588 mm při 0,01 mm.

Finální Windows Release sestavení prošlo všemi 46 CTest testy (385,29 s).
