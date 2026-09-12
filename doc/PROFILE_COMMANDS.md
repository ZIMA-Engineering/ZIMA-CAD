# Vytažení a rotace přes konzoli a CLI

Příkazy `extrusion.create/get/set` a `revolution.create/get/set` používají
stejné potvrzení profilu jako OK ve Vlastnostech. Tvorba převádí samostatnou
skicu na profilový prvek: zachová ID kontejneru, jeho počátek, umístění,
vlastníka v tělese a pozici historie. Nová operace získá nové ID prvku;
její skica zůstane v témže kontejneru. Nejde o kopii skici.

## Zadání

Nejprve vytvořte skicu pomocí `sketch.create` a doplňte uzavřený profil
skicovými příkazy; pro Thin může být profil i otevřený. Následující ID pocházejí z výsledků příkazů, nejsou to
názvy ve stromu ani pořadová čísla hran.

```json
{"command":"extrusion.create","arguments":{"sketch":"<sketch-ID>","length_forward_mm":20,"name":"Vytažení"}}
{"command":"extrusion.get","arguments":{"container":"<container-ID>"}}
{"command":"extrusion.set","arguments":{"container":"<container-ID>","extent":"two_sides","length_forward_mm":30,"length_reverse_mm":5}}
{"command":"revolution.create","arguments":{"sketch":"<sketch-ID>","axis":"<centerline-ID>","angle_degrees":90}}
{"command":"revolution.get","arguments":{"container":"<container-ID>"}}
{"command":"revolution.set","arguments":{"container":"<container-ID>","angle_degrees":180}}
```

Rotace používá zelenou konstrukční osu ve vlastní skice. Úsečku na ni změní
`sketch.segment.centerline`. Bez argumentu `axis` se použije jediná dostupná
osa; při více osách je nutné zadat její přesné ID. Chybné výslovně zadané ID
se nenahrazuje jinou osou.

Číselné argumenty jsou skutečná čísla JSON: délky v mm, úhly ve stupních,
nezávisle na zobrazovaných jednotkách dokumentu. `name`, `combine` (`add`,
`subtract`), `extent` (`one_side`, `two_sides`, `symmetric`), `direction`
(`forward`, `reverse`) a `profile_offset_mm` jsou společné pro oba prvky.
`placement` přijímá objekt číselných polí ze společného `placement.set`,
včetně existujících omezení a zámků.

Vytažení používá `length_forward_mm` a `length_reverse_mm`, rotace
`angle_degrees` a `angle_reverse_degrees`. Rozsah délky je 0,001 až
1 000 000 mm; úhel každé strany 0,001 až 360°. Skutečný součet rotace musí
splnit omezení modelového jádra. Symetrická varianta nastaví obě strany
na hodnotu vpřed; souběžné zadání rozdílné zadní hodnoty je chyba.

`end_forward` a `end_reverse` mají hodnoty `length`, `through_all` a `up_to`.
Průchozí zakončení je určeno pouze pro odečtení. Jednostranný průchozí řez
zasahuje od roviny profilu ve zvoleném směru; pro řez oběma směry nastavte
`two_sides` a obě zakončení na `through_all`.

`get` vrací identitu dokumentu, kontejneru, prvku, skici a tělesa, parametry,
zámky, platnost umístění a uložené cíle zakončení. Může číst i jiný již otevřený
Part přes `document`, rovněž během otevřených Vlastností. Neprovádí výpočet,
řešení referencí, změnu historie ani načítání závislostí.

## Tenkostěnný výsledek

`result_type` volí `solid` nebo `thin`. Thin používá `thin_thickness_mm`
a `thin_mode` (`one_side`, `other_side`, `symmetric`). Změna typu či strany
počítá skutečné těleso a vstupuje do výpočetního otisku; dřívější chyba,
kdy se přes Thin náhled ukládal plný objem, je odstraněna z modelového výpočtu.

```json
{"command":"extrusion.set","arguments":{"container":"<container-ID>","result_type":"thin","thin_thickness_mm":1,"thin_mode":"symmetric"}}
```

Uzavřená kontura tvoří stěnu mezi dvěma odsazenými obrysy. Otevřená kontura
se uzavře na svých dvou původních koncových bodech. Zakončení mají vlastní
identitu odvozenou od těchto bodů, boky od původních křivek. Vnější a vnitřní
strana uzavřeného profilu si zachovávají identitu při změně tloušťky i strany.
Náhled používá stejné pořadí profilu a počáteční bod jako výpočet, bez OCCT.

Kružnice zůstává analytická; u samostatné spline se její matematický offset
převede na jednu B-spline s kontrolovanou odchylkou nejvýše 1e-7 mm.
Nepřijatelný výsledek aproximace je
odmítnut. Hrana si uchová původní identitu a ve vypočteném dokumentu se uloží
její křivková data, nikoli jen zobrazovací lomená čára.

Thin používá kontury bez vnitřních otvorů; příliš silná stěna, zborcený obrys,
nesouvislá dráha nebo odsazení měnící podporovanou topologii vrací chybu.
Další omezení odsazování navazujících křivek jsou stejná jako u společného
Thin výpočtu pro tažení. Příkaz chybnou geometrii nevynechá ani nenahradí
plným výsledkem.

## Transakce a současné hranice

Mutace vyžaduje aktivní Part a aktivní vlastnící těleso. Během GUI editace je
odmítnuta, stejně jako editace odvozeného tělesa. Nepodařený výpočet ani
neplatné argumenty nesmějí změnit skicu, historii, revizi či vypočtená tělesa.
Úspěšná změna je jedna Undo/Redo transakce. OK i `.set` výslovně počítají profil
rovněž při stejných číselných hodnotách, protože se mohla změnit jeho skica.
Závislé sestavy se automaticky neregenerují.

- Příkazové zadávání nových cílů `up_to` zatím chybí. Existující cíle se čtou
  a zachovávají; `up_to` bez platné reference je odmítnuto.
- Řezy vlastněné Assembly zatím používají svou dosavadní samostatnou cestu.
  Vnořená aktivace a příkazové sestavové řezy jsou další část celkové CLI.

Nativní formát se touto etapou nemění, start šablony zůstávají platné.
Všechny potřebné údaje nadále žijí v `.prtz`, `.asmz` a `.drwz`.

## Ověření

Modelové testy porovnávají objemy s nezávislými výpočty: obdélníkové vytažení,
mezikruží vytvořené rotací, jednostranné a oboustranné průchozí odečtení.
Ověřují také skicu s nezměněnými parametry prvku, identitu kontejneru,
zámky, chyby, Undo/Redo a uložení vypočteného tělesa. Samostatné CLI procesy
pracují bez dostupného Qt platformního pluginu. GUI konzole vytváří oba prvky,
edituje je jejich skutečnými Vlastnostmi a porovnává uložený objem.

Úplná Windows Release sada prošla **90/90** (420,90 s),
`build/profile-full-tests.log`. Následná oprava bezpečného uchování první
reference před mazáním staré orientační položky a ochrana před převzetím
skici jiného prvku mají samostatné regresní testy. Závěrečný běh jejich
modelových a GUI cest prošel **9/9** (112,75 s),
`build/profile-final-tests.log`. Tato předchozí etapa ještě Thin odmítala;
navazující etapa jej ověřuje jako skutečné stěny. Oba běžné programy jsou sestavené;
`build/profile-final-build.log`.


Navazující test Thin porovnává objemy válcové stěny, obdélníkové stěny,
otevřené úsečky, oblouku, spline a rotace s nezávislým výpočtem. U spline
používá numerický integrál délky podkladové kubiky. Ověřuje změny tloušťky,
strany a směru, shodu náhledu s tělesem, nativní uložení, otisk cache a původ
plošných referencí. Společné geometrické a 3D Sweep regrese prošly **3/3**
(17,70 s), `build/thin-profile-spline-tests.log`.

Úplná Windows Release sada po zpřístupnění Thin v GUI i CLI ověřila **90/91**
testů (427,39 s), `build/thin-profile-full-tests.log`. Oba programy a všechny
testy byly sestaveny (`build/thin-profile-full-build.log`). Jediné selhání byl
10sekundový timeout automatického výběru DXF při editaci profilu. Samostatný
běh stejného sestavení prošel **1/1** (9,07 s),
`build/thin-profile-dxf-recheck.log`. Po doplnění diagnostiky timeoutu prošly
**tři opakované běhy** (27,75 s), `build/thin-profile-dxf-repeat-tests.log`.
Příčina ojedinělého timeoutu nebyla reprodukována; nepovažujeme ji za opravenou
chybu importního algoritmu. Další úplná sada ji musí nadále pokrývat.

Zpřísněná kontrola spline a obráceného směru prošla **1/1** (0,52 s),
`build/thin-profile-precision-tests.log`: odchylka objemu oproti nezávislému
integrálu byla **1,990028e-9 mm³**, povolená mez je 1e-4 mm³. Odsazená hrana
obsahuje uloženou přesnou B-spline; změna směru zachová rodiče jejích ploch.
Závěrečné GUI/testovací sestavení: `build/thin-profile-final-build.log`.


## Výpočet dvou cílových mezí (navazující etapa)

Kernel má nyní nezávislou dopřednou a zpětnou mez vysunutí. Obě mohou omezovat
profil šikmou rovinou; zpětná mez podporuje také původní přesnou plochu.
Průchozí řez může být kombinován s cílovou mezí na opačné straně. Změna mezí
zachovává identitu obou čel a boků odvozenou od původního profilu.

Kontrola šikmé roviny používá přesné meze celého profilu v souřadnicích roviny.
Odmítne tedy také rovinu protínající kružnici mimo její švový bod. Rovinná
plocha původního tělesa při explicitním výpočtu poskytuje svou aktuální rovinu;
starý číselný snímek cíle ji nemůže přepsat. Kernel ověřuje existenci přesného
původního vlastníka i u sdružených profilových prvků.

Tato část je zatím kernelovým základem: nativní adaptéry, druhý konec náhledu,
obnova cílových referencí a jejich CLI zadání se dokončují v další etapě.
Nepřidává příkaz ani nové pole nativního dokumentu.

První kontrola základních kontraktů a Thin prošla **3/3** (7,31 s),
`build/extrusion-limits-kernel-tests.log`. Rozšířená sada ověřila všechny
stávající geometrické kontrakty, Sweep 2D/3D, ShaftThread, Hole a Thin;
nová řezová zkouška nejprve chybně nezařadila zásobu do tělesa (**6/7**, 22,48 s,
`build/extrusion-limits-related-tests.log`). Po opravě přípravy testu prošla
**1/1** (0,23 s), `build/extrusion-limits-fixture-tests.log`. Objemové testy
zahrnují dvě šikmé roviny, obě kombinace průchozího řezu, Thin, původní rovinu
se záměrně zastaralým snímkem, přesnou povrchovou mez, chybnou stranu,
chybějící zdroj, identitu ploch a změnu otisku cache.
