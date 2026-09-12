# Vytažení a rotace přes konzoli a CLI

Příkazy `extrusion.create/get/set` a `revolution.create/get/set` používají
stejné potvrzení profilu jako OK ve Vlastnostech. Tvorba převádí samostatnou
skicu na profilový prvek: zachová ID kontejneru, jeho počátek, umístění,
vlastníka v tělese a pozici historie. Nová operace získá nové ID prvku;
její skica zůstane v témže kontejneru. Nejde o kopii skici.

## Zadání

Nejprve vytvořte skicu pomocí `sketch.create` a doplňte uzavřený profil
skicovými příkazy. Následující ID pocházejí z výsledků příkazů, nejsou to
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

## Transakce a současné hranice

Mutace vyžaduje aktivní Part a aktivní vlastnící těleso. Během GUI editace je
odmítnuta, stejně jako editace odvozeného tělesa. Nepodařený výpočet ani
neplatné argumenty nesmějí změnit skicu, historii, revizi či vypočtená tělesa.
Úspěšná změna je jedna Undo/Redo transakce. OK i `.set` výslovně počítají profil
rovněž při stejných číselných hodnotách, protože se mohla změnit jeho skica.
Závislé sestavy se automaticky neregenerují.

- Příkazové zadávání nových cílů `up_to` zatím chybí. Existující cíle se čtou
  a zachovávají; `up_to` bez platné reference je odmítnuto.
- Testy odhalily, že původní výpočet Extrusion/Revolution ignoruje Thin,
  přestože náhled jeho parametry zobrazuje. Společné potvrzení nyní vrací
  `unsupported_operation` místo uložení plného tělesa jako tenkostěnného.
  Oprava skutečného výpočtu Thin je následující otevřený krok. Argumenty
  `result_type`, `thin_thickness_mm`, `thin_mode` i uložené hodnoty jsou
  dostupné; Thin zatím nelze potvrdit. Totéž platí pro nové společné OK v GUI.
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
`build/profile-final-tests.log`. Zahrnuje i přeložené odmítnutí Thin v obou
skutečných dialozích a zachování dokumentu. Oba běžné programy jsou sestavené;
`build/profile-final-build.log`.
