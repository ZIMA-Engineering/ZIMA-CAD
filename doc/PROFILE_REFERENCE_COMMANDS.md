# Reference umístění profilů Partu a Assembly

`extrusion.reference.set` a `revolution.reference.set` přiřazují původní referenci
existujícímu Vytažení nebo Rotaci v aktivním Partu i profilovému odečtu Assembly.
Používají stejné `commit_profile` / `commit_assembly_profile` jako potvrzení
Vlastností a zachovávají vlastní skicu i vybrané cílové komponenty.

```json
{"command":"extrusion.reference.set","arguments":{"container":"ID_VYSUNUTI","index":0,"reference":{"owner":"ID_PARTU:origin","key":"origin:plane:xy"},"offset_mm":7}}
{"command":"revolution.reference.set","arguments":{"container":"ID_ROTACE","index":0,"reference":{"owner":"ID_SESTAVY:origin","key":"origin:plane:xy"},"offset_mm":1}}
{"command":"extrusion.reference.set","arguments":{"container":"ID_ODECTU","index":0,"reference":{"owner":"ID_PUVODNIHO_PRVKU","key":"PUVODNI_KLIC_PLOCHY","instance_path":"PRESNA_CESTA_VYSKYTU"},"offset_mm":-4,"derive_orientation":false}}
```

Argumenty odpovídají [společnému vstupu](PLACEMENT_REFERENCE_COMMANDS.md):
`index` 0–2 pro polohu, 3 FRONT, 4 TOP; `reference` obsahuje `owner`, `key`
a volitelnou `instance_path`. Part používá místní reference s prázdnou cestou.
V Assembly je cesta prázdná pro vlastní Origin/konstrukci a úplná pro geometrii
konkrétního vloženého výskytu, včetně všech úrovní podsestav. Cestu a klíč
přebírá klient z původních referencí; nejde o jméno dílu ani číslo plochy OCCT.

`offset_mm`, `flip`, `derive_orientation` a `document` jsou volitelné. Výsledek
odpovídá příslušnému `.get`, navíc obsahuje `changed`. `.get` vrací parametry
profilu, identitu vlastní skici a `reference_valid`; samostatné pole `placement`
v tomto výsledku není. Obecné `placement.reference.set` s argumentem `object`
namísto `container` obsluhuje profily Partu. Profilové odečty Assembly používají
výše uvedené příkazy `extrusion.reference.set` / `revolution.reference.set`.

## Pravidla

V Partu se přijímají původní uložené reference předcházející prvku v historii,
Part Origin a dostupné Body Origins. Vlastní profilová skica, samotný prvek
a pozdější objekty nejsou platným zdrojem. Neaktivní a odvozená tělesa chrání
stávající pravidla. V Assembly se používá původní uložená geometrie v souřadné
soustavě vlastní sestavy; vlastní profil a vlastní kontejner jsou vyloučené.
Stejný zdrojový díl vložený opakovaně má samostatné reference podle úplné cesty.

Poziční pole a FRONT/TOP jsou nezávislá. Výchozí `derive_orientation:true`
přidává orientaci podle stávajících pravidel do volného orientačního pole;
náhrada poziční reference nemaže jiné orientační reference. Dvě rovnoběžné
roviny zadané jako FRONT a TOP představují konflikt a jsou odmítnuty.
`derive_orientation:false` ponechává oddělené zadání polohy a orientace.
Náhrada zamčené reference zachová změřenou vzdálenost podle společného přiřazení.

Part a Assembly sdílejí přípravu reference: dohledání původní identity,
rozdělení polí, určení směru, zámek a stávající přiřazení i řešení umístění.
Tento přesun byl výslovně schválen uživatelem. Algoritmus řešiče a GUI kontrakt
se nemění; profily používají stávající normalizaci FRONT a souřadného systému skici.

Potvrzení vypočítá těleso a uloží profil i skicu jako jednu transakci.
Assembly počítá vlastní odečet; zdrojový Part neupravuje ani nepřepočítává.
Stejné přiřazení nic nepřepočítává ani nepřidává Undo. Chybný požadavek nezanechá
částečnou změnu. Aktivní GUI editor blokuje příkazovou mutaci až do OK/Cancel.
Formáty `.prtz`/`.asmz` a startovací šablony se nemění.

## Ověření

Testy Partu měří vysunutí obdélníku 2 × 3 mm o 5 mm (30 mm³) a úplnou rotaci
obdélníku mezi poloměry 2 a 4 mm o výšce 3 mm (36π mm³). Změna offsetu
posune skutečnou geometrii a zachová objem, identitu i křivky vlastní skici.

Assembly testy měří odečet z kvádru 10 × 10 × 10 mm. Vytažení profilu
2 × 3 mm o 4 mm odebere 24 mm³; po přesunu na okraj kvádru zbývá průnik
12 mm³. Rotace mezi poloměry 1 a 2 mm o výšce 2 mm odebere 6π mm³.
Ověřuje se i stejný díl ve dvou výskytech jedné podsestavy: změna celé cesty
přesune odečet z Z = 1 na Z = 3 mm a netargetovaný díl zůstává nezměněný.
Zdrojový Part zachová revizi i již vypočtená tělesa.

Zahrnuty jsou zámky, no-op, konfliktní či vlastní reference, neúplná cesta,
duplicitní pole, chybné typy, velké indexy, aktivní editor, Undo/Redo a nativní
uložení. Samostatný proces CLI otevře sestavu, přiřadí referenci, provede
Undo/Redo a uloží ji; test znovu načte skutečný `.asmz` a změří výsledný objem.
GUI test otevírá skutečné Vlastnosti obou profilů a ověřuje offset, Cancel,
OK, Undo/Redo a opětovný vstup do vlastní skici.

Obě aplikace a všechny testovací cíle jsou sestavené. Úplná Windows Release
regrese prošla **150/150 za 682,52 s**, včetně samostatného procesu CLI,
překladů, GUI konzole a souhrnného GUI běhu:
`build/assembly-profile-reference-full-tests.log`. Samostatná GUI kontrola
Part/Assembly profilů prošla rovněž (`build/assembly-profile-reference-gui-tests.log`).
Nový GUI test ukončuje iteraci stromu před otevřením Vlastností, protože jejich
rollback strom přestaví; živý iterátor zde způsoboval chybu samotného testu.
