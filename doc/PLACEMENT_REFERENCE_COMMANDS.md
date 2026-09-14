# Přiřazení původní reference umístění přes CLI

`placement.reference.set` používá společný vstup pro Body, samostatné
konstrukční kontejnery a šest primitiv: Box, Cylinder, Sphere, Cone,
Pyramid a Wedge, a profily Extrusion/Revolution v Partu. Body a konstrukce deleguje na jejich již existující
operace; primitivum potvrzuje přes `commit_primitive`, stejně jako OK
jeho Vlastností. Profily používají `commit_profile` a společně potvrzují
vlastní skicu; podrobnosti jsou v [PROFILE_REFERENCE_COMMANDS.md](PROFILE_REFERENCE_COMMANDS.md).
Nový příkaz nemění společný řešič ani dialogy.

```json
{"command":"placement.reference.set","arguments":{"object":"ID_KONTEJNERU","index":0,"reference":{"owner":"ID_DOKUMENTU:origin","key":"origin:plane:xy"},"offset_mm":7}}
{"command":"placement.get","arguments":{"object":"ID_KONTEJNERU"}}
{"command":"placement.set","arguments":{"object":"ID_KONTEJNERU","values":{"reference_offset:0":13}}}
```

`object` je skutečné ID tělesa nebo kontejneru. `index` 0–2 označuje
poziční pole, 3 FRONT a 4 TOP. Zdroj reference má `owner`, `key` a volitelný
`instance_path`; u Partu musí být cesta prázdná. Konstrukce v sestavě
zachovávají přesnou cestu výskytu. Identifikátory pocházejí z původní
uložené geometrie, nikoli z výsledné topologie nebo pořadí triangulace.

`offset_mm` je podepsané odsazení rovinné poziční reference; ostatní
reference přijímají jen nulu. Zamčená vzdálenost při náhradě zdroje
zachovává skutečně změřenou vzdálenost. `flip` přenáší příznak orientace,
`derive_orientation` (výchozí true) dovolí automatické doplnění FRONT/TOP
podle společného přiřazení. Přechodná měřená hodnota se neukládá.

Nový vstup primitiva při opakování zdroje zachová již uloženou orientační
roli této reference. Opakované přiřazení stejné roviny proto nevytvoří
paralelní FRONT/TOP ani novou transakci. Stávající samostatná orientační
pole zůstávají samostatná podle sdíleného kontraktu.

Primitivum smí odkazovat na Part Origin, vlastní nebo dřívější Body Origin
a původní geometrii, která mu předchází v historii. Vlastní a pozdější
objekty se odmítají; jiný Body smí být zdrojem jen tehdy, pokud předchází
vlastnímu Body. Reference se před řešením vyjádří v místní soustavě
vlastního Body. Neaktivní a odvozené Body chrání stávající pravidla.

Návrh se ověří před potvrzením. Změna primitiva je jeden výpočet a jedna
transakce Undo/Redo, bezezměnové přiřazení nepřepočítává. Chybný vstup
nezanechá částečně upravený dokument. `document` volitelně chrání aktivní
kartu a rozpracovaný GUI příkaz blokuje mutace.

Výsledek má stejný tvar jako `placement.get`, doplněný o `changed`.
Formáty ani startovací Part/Assembly šablony se nemění. Stav reference
i vypočtená data zůstávají v nativním souboru.

Ostatní modelové prvky, profilové odečty Assembly a řezy dosud tento nový vstup nepoužívají. Reference
bodů křivky jsou připravené na samostatné místní větvi
`codex/curve-reference-pending` s reprodukcí chyby staré polohy. Její
oprava společného řešení referencí a přesun společného odebrání reference
čekají na samostatné výslovné souhlasy uživatele.

## Ověření

Integrace 11/11 za 109,47 s zahrnuje všech šest primitiv, zámky,
bezezměnové opakování, zamítnuté vstupy, původní plochu ve vedlejším
Body s rozdílnými souřadnými soustavami a změnu zdrojového Body.
Samostatné CLI a GUI ověřují Cancel/OK, Undo/Redo, skutečný soubor
a zachovaný objem. Katalog obsahuje 263 příkazů, sada 144 testů.

Úplná Windows Release regrese prošla **144/144 za 574,23 s**, včetně
všech GUI, modelových, procesních, výkresových a skicových testů.
Záznam: `build/primitive-reference-full-tests.log`.
