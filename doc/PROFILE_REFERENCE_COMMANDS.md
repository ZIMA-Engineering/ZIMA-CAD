# Reference umístění profilů Partu

`extrusion.reference.set` a `revolution.reference.set` přiřadí původní referenci
existujícímu vysunutí nebo rotaci v aktivním Partu. Oba příkazy používají
stejné `commit_profile` jako potvrzení Vlastností a zachovávají vlastní skicu.

```json
{"command":"extrusion.reference.set","arguments":{"container":"ID_VYSUNUTI","index":0,"reference":{"owner":"ID_PARTU:origin","key":"origin:plane:xy"},"offset_mm":7}}
{"command":"revolution.reference.set","arguments":{"container":"ID_ROTACE","index":0,"reference":{"owner":"ID_PARTU:origin","key":"origin:plane:xy"},"offset_mm":7}}
```

Argumenty odpovídají [společnému vstupu](PLACEMENT_REFERENCE_COMMANDS.md):
`index` 0–2 pro polohu, 3 FRONT, 4 TOP; `reference` obsahuje `owner`, `key`
a volitelnou prázdnou `instance_path`. `offset_mm`, `flip`, `derive_orientation`
a `document` jsou volitelné. Výsledek odpovídá příslušnému `.get`, navíc
obsahuje `changed`. Stejnou operaci lze vyvolat přes `placement.reference.set`
s argumentem `object` místo `container`; ten vrací obecný popis umístění.

Přijímají se původní uložené reference předcházející prvku v historii,
Part Origin a dostupné Body Origins. Vlastní profilová skica, samotný
prvek a pozdější objekty nejsou platným zdrojem. Neaktivní a odvozená tělesa
chrání stávající pravidla. Náhrada zamčené reference zachová změřenou
vzdálenost podle společného přiřazení.

Nový adaptér příkazového vstupu sdílí přípravu s primitivy; nemění společný
řešič ani GUI. Profily používají stávající normalizaci FRONT a vlastního
souřadného systému skici. Potvrzení vypočítá těleso a uloží profil i skicu
jako jednu transakci. Stejné přiřazení nic nepřepočítává ani nepřidává Undo.
Chybný požadavek nezanechá částečnou změnu.

Tato etapa podporuje **Part**. Reference umístění profilových odečtů Assembly
ještě nejsou těmito příkazy zpřístupněné. Formáty a startovací šablony se nemění.

## Ověření

Modelový test kontroluje vysunutí obdélníku 2 × 3 mm o 5 mm (30 mm³)
a úplnou rotaci obdélníku mezi poloměry 2 a 4 mm o výšce 3 mm (36π mm³).
Změna offsetu posune skutečnou výslednou geometrii o zadané 2 mm a zachová
objem, identitu i křivky vlastní skici. Zahrnuty jsou bezezměnové přiřazení,
zamítnuté reference, GUI editační zámek, Undo/Redo a nativní uložení.

Samostatný CLI proces vytváří oba profily, přiřadí referenci a znovu otevře
uložený výsledek. GUI test otevře skutečné Vlastnosti každého profilu a ověří
Cancel i OK po úpravě offsetu a následné Undo/Redo.

Obě aplikace a všechny testovací cíle jsou sestavené. Související Windows
Release regrese prošla **10/10 za 132,22 s**:
`build/profile-reference-integration-tests.log`. Katalog má **265 příkazů**,
sada **145 testů**. Poslední úplný běh před touto etapou byl 144/144;
tento údaj není úplným během nové sady.
