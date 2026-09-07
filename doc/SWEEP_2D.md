# 2D tažení (2D Sweep)

2D tažení je kontejner historie dílu s rovinnou dráhou a profilovými skicami
v jejích stanicích. Podporuje Přičíst / Odečíst, přechod mezi různými profily
(Loft) a výsledek Těleso / Thin. Vytvoření i editace používají jedno interní
okno Vlastností. Rozpracované skici a parametry se uloží až při OK; Cancel
obnoví původní historii. Editace zobrazuje skutečný vstup před kontejnerem.

## Umístění a skica dráhy

Kontejner používá společné umístění: poziční reference, FRONT/TOP, X/Y/Z,
natočení, korekce a volbu počátku. První rovinná reference předvyplní
samostatné zelené pole před tlačítkem **Skica dráhy**. Tuto rovinu lze změnit
výběrem roviny nebo rovinné plochy původního objektu ve View či Tree, aniž
se změní umístění kontejneru. Nezadávají se dvě povinné kolmé roviny pro
průřez a dráhu; profilové roviny se odvozují od tečny dráhy.

Dráha je jedna otevřená souvislá rovinná křivka začínající v počátku skici.
Počáteční směr může být libovolný. Používá úsečky, oblouky, eliptické oblouky
a otevřené spline včetně vyhodnocených skicových zaoblení. Může měnit směr
v rovině; nemusí postupovat podél jedné osy. Tažení končí ve skutečném
koncovém bodě dráhy.

## Profilové skici a Loft

Konce křivek a skutečné body Sketcheru ležící na dráze nabízejí profilové
skici. Středy oblouků a řídicí body neinterpolačních spline nejsou stanice.
Profily leží kolmo k místní tečně; v ostrém rohu jsou příchozí a odchozí
stanice samostatné. Profilové skici mají trvalé identity a jsou dostupné
ve stromu. Vstup do skici natočí kameru na její vyřešenou rovinu.

První stanice musí mít vlastní profil. Další prázdná stanice přebírá
poslední vyplněný profil ve směru dráhy. Vyplnění další skici umožňuje
přechod mezi profily. **Pořadí bodů**, značky ve View a párovací body
s vazbou **C / K** používají stejné ovládání jako
[3D tažení](3D_CURVE_AND_SWEEP.md).

Režim **Těleso** přijímá uzavřenou oblast včetně vnitřních otvorů. Například
dvě soustředné kružnice v průřezu vytvoří trubku. Navazující profily musí
mít odpovídající obvody, párovací body a stejný počet otvorů.

## Thin — tloušťka

**Thin** přijímá otevřenou nebo uzavřenou konturu, kladnou tloušťku a směr
**Dovnitř / Ven / Symetricky**. Symetricky rozděluje celkovou tloušťku
napůl na obě strany zdrojového profilu. U otevřené kontury stranu určuje
její orientace. Uzavřený profil vytvoří dutý průřez, otevřený profil pás
uzavřený na koncích kontury. Jeden Loft nekombinuje otevřené a uzavřené
profily. Tloušťka se měří v profilových rovinách; u proměnného Loftu nemusí
být konstantní kolmo k výsledné šikmé stěně.

Příliš velké odsazení, změna topologie nebo neplatný průřez se odmítne
s vysvětlením. Rozpracovaný dialog zůstane dostupný k opravě a zdrojové
skici se odsazením nepřepisují.

## Náhled, výpočet a reference

Skici a náhled se zobrazují z uložených dat ZIMA i při neúplné dráze;
nevypočítávají těleso přes OCCT. Oko referenčního pole zapíná inspekci,
kliknutí na text aktivuje zelený vstup. Krátký prostřední klik ukončí
zadávání reference, prostřední dvojklik potvrzuje OK i nad View.

Až OK nebo explicitní **Regenerovat** vypočítá těleso. Rovina dráhy,
zdrojové body, skici a párování se ukládají v aktuálním formátu dílu.
Staré uspořádání dvou skic se nepřevádí. Úsečky a oblouky se počítají
přesně, obecné rovinné křivky se adaptivně převedou podle lineární tolerance
dokumentu. Odchylka triangulace řídí zobrazení; samostatný parametr přesnosti
prvku není potřeba. Viz [Numerická přesnost](NUMERICAL_PRECISION.md).

Identity odvozených ploch, hran a bodů vycházejí ze zdrojových skic,
profilových oblastí a významu výsledku. Pořadí průchodu OCCT ani vzorkování
dráhy neurčuje trvalé reference.

## Ověření

`zima_cpp_sweep2d_contract_tests` a společné testy 3D tažení ověřují
geometrii, více profilů, Thin, otvory, umístění a uložené reference.
GUI scénář `ZIMA_VERIFY_SWEEP2D_ONLY=1` s `zima-cad-cpp --verify-startup`
kontroluje vlastněné skici, natočení kamery, změnu roviny dráhy, ovládání
Vlastností, OK/Cancel, uložení a opětovné načtení.

### Osová dráha hotového solidu

Solid publikuje čerchovanou osovou dráhu podle zdrojových křivek
(`centerline:from:<source_id>`). Úsečky, zaoblení, spline i helix zachovávají
tvar; aproximační části jedné zdrojové křivky mají společnou referenci.
Pouze přímé části nabízejí také osovou referenci pro další prvky.
Zobrazení respektuje přepínač Os, včetně stínovaného režimu. Geometrie se
ukládá při výpočtu solidu; vykreslení a výběr nevolají OCCT. Dříve vypočtený
model doplní osovou dráhu explicitním příkazem Regenerovat.
