# Helical Sweep

Helical Sweep je jeden kontejner historie Partu s operací Přičíst nebo
Odečíst. Vlastní tři skici; jejich vytvoření a změny zůstávají rozpracované
ve společném interním okně až do OK. Dokončit skicu se vrací do tohoto okna.
Cancel zahodí celý rozpracovaný kontejner. Editace používá uložený vstup
před kontejnerem a po ukončení obnoví normální historii.

Přičíst / Odečíst jsou dole ve vlastnostech jako společná dvojice tlačítek
s Protrusion. Volba je do potvrzení OK pouze rozpracovaná.

## Vstupy

1. Základní skica leží v rovině kolmé k ose vinutí. První verze přijímá
   kružnici a konkrétní počáteční bod na ní. Jediná kružnice a jediný
   samostatný bod se předvyberou; uložená identita bodu se nemění přidáním
   dalších bodů. Střed kružnice určuje osu vinutí.
2. Radiální skica leží v rovině osy a počátečního bodu. Její počátek je
   ukotvený; lokální X znamená změnu poloměru a Y osovou výšku. Obsahuje
   jedinou otevřenou, souvislou, nevětvenou dráhu s tečnými spoji.
   Podporované segmenty jsou úsečka, oblouk, eliptický oblouk a otevřená
   spline. Křivka postupuje ve výšce jedním směrem, nedosáhne osy a nemá
   čistě radiální tečnu.
3. Stoupání je kladný osový posun za jednu otáčku. Volba Pravý / Levý
   mění smysl obíhání. Výchozí je pravé vinutí.
4. Skica průřezu má počátek na začátku prostorové dráhy a rovinu kolmou
   k její tečně. Přijímá jednu uzavřenou oblast včetně vnitřních otvorů.
   Průřez může být vůči počátku posunutý.

Konec radiální křivky ukončuje vinutí i uprostřed otáčky. Počet otáček
plyne z absolutní osové výšky dělené stoupáním; délka radiální křivky
měřená po oblouku není osovou výškou.

## Výpočet a reference

Vstupy → prostředky → výstupy: tři skici a stoupání → analytická definice
prostorové dráhy, její kontrolovaná aproximace a explicitní OCCT sweep →
solid přičtený nebo odečtený v jedné hranici historie.

Pro radiální dráhu `(x(u), y(u))` je poloměr `R + x(u)`, výška `y(u)`
a úhel `±2π y(u) / stoupání`. První kružnice určuje střed a radiální
počáteční směr. Tečna zahrnuje obíhání, výškový posun i změnu poloměru.
Konec se nezaokrouhluje na celé otáčky.

Náhled používá pouze data ZIMA: dráhu, koncové obrysy průřezu a podélné
spojnice. Rám průřezu se přenáší podél dráhy bez zadaného dodatečného
kroucení. OCCT se používá při OK a explicitní regeneraci; při výpočtu
kontroluje také platnost tělesa a samoprotínání.

Startovní a koncová plocha mají samostatné identity `start:from:…` a
`end:from:…`, jejichž rodičem je oblast profilové skici. Změna stoupání,
výšky ani pravého/levého vinutí tyto role neprohazuje. Analytické roviny
se ukládají do původní referenční geometrie pro navázání dalších prvků.
Boční plochy odkazují na zdrojové křivky průřezu; hrany na křivky nebo
body, ze kterých vznikly. Vzorkovací indexy aproximace nejsou trvalé
identity topologie.

Umístění a orientace používají běžnou sekci kontejneru: tři poziční
reference, FRONT/TOP, X/Y/Z, absolutní natočení a korekce, obrácení orientace
a tlačítko POČÁTEK. Výběr ve View i v Tree používá společný picker.
Při vytváření je aktivní první poziční reference. Krátký prostřední klik
ukončí zadávání referencí; dvojklik potvrzuje celé okno.

Základní skica leží v lokální rovině kontejneru (výchozí XZ / FRONT,
uložené skici zachovávají svou rovinu). Vlastnosti nemají samostatný řádek
„Rovina skici v kontejneru“. Všechny tři skici přebírají stejné umístění; změna
posunu nebo orientace přenese celé vinutí. Odvozené roviny zůstávají
součástí funkce. Samostatná světová reference základní roviny se nepoužívá.
Sdílený kontrakt umístění kontejnerů se nemění.
Náhled umístění zobrazuje počátek bez přidané pomocné konstrukční osy.
Vstup do vlastněné skici natočí kameru na její skutečnou rovinu stejným
postupem jako u běžné skici v dokumentu.
Po celou dobu otevřených Vlastností se vedle náhledu vinutí zobrazují dráty
všech tří zdrojových skic. Zůstávají viditelné také při neúplné nebo neplatné
dráze. Odvozené roviny se aktualizují podle dostupných vstupů; pokud vstup
chybí, skica zůstane v posledním vyřešeném rámci. Zobrazení používá data
skic bez výpočtu tělesa přes OCCT.

## Ověření

`zima_cpp_helical_sweep_contract_tests` kontroluje směr vinutí, částečné
otáčky, objem válcového vinutí, proměnný poloměr, radiální oblouk a spline,
dutý průřez, neplatné dráhy, samoprotínání a trvalé start/end reference
po změně parametrů a uložení/načtení.

Integrační běh aplikace:

```sh
ZIMA_VERIFY_HELICAL_SWEEP_ONLY=1 ./build/cpp-debug/zima-cad-cpp --verify-startup
```

Prochází vytvoření, vstup do všech tří skic, návrat do Vlastností, OK,
uložení, znovuotevření, strom vlastněných skic a Cancel bez změny modelu.
Kontroluje také společná tlačítka Přičíst / Odečíst.

Výpočet dráhy používá kubické úseky s kontrolou vzorkovaných odchylek podle
lineární tolerance dokumentu (polovina pro dráhu, polovina pro OCCT sweep);
parametrizace vzorkování neovlivňuje identity výsledných ploch. Aktuální
výpočet odmítá více než 1000 otáček v jednom kontejneru.

Přesnost se přebírá z Nastavení souboru, viz [Numerická přesnost](NUMERICAL_PRECISION.md).
Odchylka triangulace řídí také jemnost vykreslených hran vinutí.
