# Původní reference umístění tažení přes CLI

Příkazy `sweep2d.reference.set`, `sweep3d.reference.set` a `helical.reference.set`
přiřazují původní reference existujícímu 2D, 3D nebo šroubovicovému tažení
v aktivním Partu. Stejnou operaci zpřístupňuje `placement.reference.set`
s argumentem `object` místo `container`.

```json
{"command":"sweep2d.reference.set","arguments":{"container":"ID_TAZENI","index":0,"reference":{"owner":"ID_PARTU:origin","key":"origin:plane:yz"},"offset_mm":3}}
{"command":"sweep3d.reference.set","arguments":{"container":"ID_TAZENI","index":0,"reference":{"owner":"ID_PARTU:origin","key":"origin:plane:yz"},"offset_mm":3}}
{"command":"helical.reference.set","arguments":{"container":"ID_TAZENI","index":0,"reference":{"owner":"ID_PARTU:origin","key":"origin:plane:yz"},"offset_mm":3}}
{"command":"placement.get","arguments":{"object":"ID_TAZENI"}}
```

## Kontrakt

Argumenty odpovídají [společnému přiřazení](PLACEMENT_REFERENCE_COMMANDS.md):
`index` 0–2 označuje poziční pole, 3 FRONT a 4 TOP; `reference` obsahuje
`owner`, `key` a volitelnou prázdnou `instance_path`. `offset_mm`, `flip`,
`derive_orientation` a ochranné `document` jsou volitelné. Názvy příkazů,
argumentů a chyb zůstávají anglické; překládají se uživatelské zprávy.

Tažení přijímá místní původní reference Partu: Part Origin, dostupné Body
Origins a předcházející geometrii. Vlastní nebo pozdější zdroj, neplatná cesta,
duplicitní reference a nepovolený index jsou odmítnuty. Neaktivní a odvozené
Body chrání stejná pravidla jako ostatní prvky. FRONT/TOP zůstávají oddělené
od pozičních polí. Zamčená vzdálenost se při přiřazení zachovává.

Adaptér používá existující `prepare_part_feature_reference` a `commit_sweep`
s režimem Replace, který potvrzuje i GUI OK. Nevytváří další řešič umístění,
nemění pravidla drah ani nepřebírá novou skicu. Identita kontejneru, dráhy,
vlastních skic a profilových stanic zůstává zachovaná. Reference roviny 2D dráhy,
odsazení základní skici šroubovice a reference jednotlivých bodů 3D dráhy
zůstávají samostatnými vlastnostmi.

Výsledek konkrétního příkazu odpovídá jeho `.get`, navíc obsahuje `changed`.
Obecný vstup vrací data `placement.get`, včetně celého uloženého umístění.
Stejné přiřazení vrátí `changed:false` bez výpočtu a Undo. Skutečná změna
použije jeden výpočet a jednu transakci. Aktivní GUI editor blokuje mutaci;
Cancel ponechává dokument beze změny a OK potvrzuje celý návrh.

Ukládání nadále používá `.prtz`. Formát ani startovací šablony se nemění.

## Ověření

Modelové testy měří přesun obou krajních X souřadnic výsledného tělesa o 3 mm
při umístění Body v počátku. Přímá tažení kruhu R = 2 mm po dráze 20 mm
zachovávají objem 80π mm³. Šroubovice R = 10 mm, stoupání 5 mm, výška 10 mm
a profil R = 0,5 mm se porovnává s objemem průřezu krát délka šroubovice.
Zahrnuty jsou všechny tři konkrétní příkazy i obecný vstup, původní identity,
no-op, zámek, chybné vstupy, aktivní editor, neaktivní Body, Undo/Redo a nativní
uložení s vypočteným tělesem.

Procesní test spouští skutečné CLI nad nativními dokumenty, přiřadí referenci,
provede Undo/Redo, uloží Part a ověří polohu i objem. GUI test otevře Vlastnosti
každého typu tažení, změní offset 3 → 4 mm a kontroluje Cancel/OK, čtení dosud
nepotvrzeného modelu, blokování příkazu během editace, Undo/Redo a uložené těleso.


GUI regrese odhalila a opravila ztrátu pozičních referencí při editaci 3D
tažení: adaptér zobrazoval reference kontejneru, ale přebíral režim Absolute
z lokální vlastněné dráhy. Editor nyní předává všechny reference umístění
kontejneru; při návratu zachová původní definici dráhy. Samotný společný
řešič ani ContainerPlacementSection se nemění. Samostatný test dialogu
ověřuje 0, 1 a 3 poziční reference, změnu offsetu, potvrzení/zrušení i původní
identity a body lokální dráhy.


Závěrečné sestavení obou aplikací a všech testovacích cílů uspělo.
Cílená Windows Release regrese prošla **9/9 za 228,05 s**:
tři geometrické testy tažení, UI kontrakty, překlady, skutečný proces CLI,
příkazy tažení, katalog a GUI konzole (build/sweep-reference-final-tests.log).
Test dialogu navíc ověřuje zachování 0, 1 a 3 pozičních referencí a lokální
dráhy. Před opravou adaptéru test zachytil ztrátu reference při GUI OK;
po opravě změna 3 → 4 mm projde včetně uložení a Undo/Redo.
Úplná sada nebyla v této etapě opakována; poslední úplný výsledek je
150/150 v build/assembly-profile-reference-full-tests.log.
