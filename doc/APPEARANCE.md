# Barvy a vzhled

Nástroj **Barvy a vzhled…** je dostupný ikonou barevné koule nad View
nebo v nabídce barev. Používá společné vnitřní okno vlastností s OK/Zrušit.

## Paleta a povrchy

Roleta rozděluje paletu na Základní barvy, Plasty, Laky, Kovy a vlastní třídy.
Zůstávají původní barevné odstíny. Kovy obsahují ocel, matnou a leštěnou nerez,
hliník, matný a leštěný bronz, mosaz a měď. Náhled koule používá stejný renderer
jako model. Barvu lze zadat jako `#RRGGBB`; posuvníky mění lesk a kovový charakter.

Pro vlastní vzhled vyplňte název, upravte povrch, vyberte nebo napište třídu
a stiskněte **Přidat do palety**. Vlastní položky se ukládají do
`appearances.json` vedle aplikačního nastavení až po OK. Zrušit je neuloží.

## Těleso a skupiny ploch

Tlačítko **Těleso — základní vzhled** vybírá základní povrch aktivního tělesa
(nebo celého Partu bez aktivního tělesa). **+ Skupina** vytvoří pojmenovanou
skupinu ploch. Její barevné políčko vybírá vzhled; pole počtu ploch zeleně
aktivuje vstup. Klikáním ve View přiřazujete viditelné **výsledné plochy**.
Každá plocha patří nejvýše jedné skupině; nové přiřazení ji přesune.
Oko nezávisle zvýrazní přesné obrysy přiřazených ploch azurově.
Krátké prostřední tlačítko ukončí vstup a inspekci bez smazání hodnot.

**Vyčistit plochy** odstraní přiřazení a ponechá skupiny. **Výchozí nastavení**
obnoví základní vzhled a odstraní skupiny upravovaného tělesa. Vlastní paleta
zůstane zachována. OK potvrdí celek; Zrušit obnoví původní vzhled. Dvojklik
prostředním tlačítkem potvrzuje OK i nad View.

V sestavě vzhled vybraného bezprostředně vlastněného Partu patří jeho
výskytu. Nepřepisuje zdrojový Part a explicitní Regenerate jej zachová.
Pro úpravu samotného Partu jej nejprve aktivujte.

## Zobrazení a data

Povrch používá barvu, drsnost a kovový charakter, přímý spekulární odlesk
a procedurální studiové osvětlení se dvěma měkkými odrazy. Nejde o ray tracing
ani odrazy okolních součástí. Vzhled nenastavuje fyzikální materiál či hustotu.

Skupiny ukládají ZIMA identity výsledných ploch do Partu, výskytové nastavení
do Assembly. Jsou to prezentační vazby; výsledná plocha se tím nestává
vlastníkem konstrukční reference. Otevření okna, výběr, náhled koule ani změna
vzhledu nevolají OCCT a nespouštějí regeneraci rodičů. Po změně geometrie se
vzhled aplikuje jen na zachované identity; zmizelé plochy se nepřiřazují podle
pořadí nebo blízkosti jiné ploše.

## Ověření

`zima_cpp_appearance_contract_tests` ověřuje serializaci Part/Assembly a palety,
výběr výsledných ploch v sestavě, výlučné přiřazení skupin, Clear/Default,
transakci OK/Zrušit, prostřední dvojklik a rozdíl vykreslení matného a kovového
povrchu pomocí skutečného OpenGL framebufferu.
