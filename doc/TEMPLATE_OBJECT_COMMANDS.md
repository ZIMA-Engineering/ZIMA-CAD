# Obrázky a oblasti kusovníku přes CLI

`template.image.list/get/create/set/remove` a `template.region.list/get/create/set/remove`
obsluhují objekty existujícího editoru šablon. Tvorba patří do razítka `.tblz`,
stejně jako v GUI. Dotazy a úprava již uložených objektů používají aktuální
šablonu; volitelný `document` chrání mutaci proti změně aktivní karty.

```json
{"command":"template.new","arguments":{"kind":"title_block","name":"Razítko"}}
{"command":"template.image.create","arguments":{"path":"logo.svg","x_mm":10,"y_mm":20,"width_mm":30}}
{"command":"template.image.list"}
{"command":"template.image.set","arguments":{"image":"ID_Z_VÝSLEDKU","height_mm":12,"horizontal":"center","vertical":"middle"}}
{"command":"template.region.create","arguments":{"x_mm":0,"y_mm":0,"width_mm":180,"height_mm":8,"step_mm":10,"direction":"up"}}
{"command":"template.region.list"}
{"command":"template.save"}
```

Souřadnice a rozměry jsou v milimetrech v systému šablony: X doleva, Y nahoru.
Obrázek vyžaduje cestu a bod umístění; výchozí šířka je 30 mm a výška odpovídá
zdrojovému poměru stran. PNG/JPEG/BMP/WebP se převádějí společným importérem do
vloženého PNG, SVG zůstává vložené SVG. GUI i CLI mají stejné limity a validaci.
`path` v `set` vymění obsah a název, zachová identitu, umístění, zarovnání a zámky.
Dotaz vrací formát, vnitřní velikost, délku vloženého base64 obsahu a skutečné
rohy `corners_mm`; neposílá celé zakódované médium.

`lock_aspect` je výchozí true. Zadání jedné strany dopočítá druhou z vnitřních
rozměrů obrázku. Současné zadání obou stran musí odpovídat poměru, nebo musí
požadavek obsahovat `lock_aspect: false`. Zapnutí poměru či výměna obsahu
zachová šířku; zamčená výška je přednostní. Při zamčení obou stran se rozměry
nemění. Konfliktní číselný požadavek se odmítne bez transakce.
Zarovnání je `left/center/right` a `bottom/middle/top`.

Oblast vyžaduje `x_mm`, `y_mm`, `width_mm`, `height_mm`. Vynechaný `step_mm`
použije při tvorbě výšku; pozdější změna výšky rozteč automaticky nemění.
`direction` je `up/down/left/right`. Oblast zachovává stávající pravidla výkresu
pro opakování řádků kusovníku.

`value_locks` nahrazuje množinu číselných zámků: `x`, `y`, `width`, `height`,
u oblasti navíc `step`. Prázdné pole zámky uvolní. Zamčenou hodnotu lze změnit
jen po odemčení; stejný příkaz může zámek odebrat a hodnotu změnit. Novou
hodnotu původně nezamčeného pole lze zároveň zamknout. Nepodporované názvy,
neplatné rozměry, cizí identita a neplatný soubor se odmítnou před potvrzením.

GUI i CLI používají společné potvrzení a odstranění. Úprava zachovává ID,
jedna změna odpovídá jednomu Undo. Bezezměnové `set` a odstranění již chybějícího
objektu vrátí `changed: false`, nepřidají historii ani přepočet. `body_calculated`
je vždy false. Dialogové náhledy zůstávají přechodné; Cancel nic nezapíše.

Obsah a zámky se ukládají do stávající skici šablony. Původní obrázek není při
znovuotevření potřeba. Nativní výkres přebírá vložená data dosavadním způsobem;
nevzniká nový formát ani povinný vedlejší soubor.

Ověření: modelový test všech deseti příkazů **1/1 za 0,15 s**, integrační
sada **8/8 za 121,90 s** a test exportu obrázků výkresu **1/1 za 0,29 s**.
Testy kontrolují skutečné soubory, odstranění zdrojového média před načtením,
Undo/Redo, chybové/no-op transakce a dialogy GUI včetně zámků a Cancel/OK.
Logy: `build/template-objects-model-tests.log`,
`build/template-objects-integration-tests.log`, `build/template-objects-image-tests.log`.
