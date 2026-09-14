# Vlastnosti samostatné skici v Assembly

## Datový model a společné příkazy

Každou samostatnou skicu sestavy vlastní HistoryContainer druhu Sketch.
Assembly jej ukládá v `sketch_containers`; skica používá `owner_container_id`.
Kontejner vlastní Placement, Origin, stabilní identitu a zámky. Skica uvnitř
odečtu patří přímo tomuto odečtu. Nativní validace odmítá osiřelé nebo dvojité
vlastnictví. Nezavádí se dodatečné vytváření kontejnerů při načítání legacy dat.

`sketch.create`, `sketch.set` a `sketch.reference.set` používají stejné
potvrzení jako Vlastnosti skici v GUI. Název, rovina XY/XZ/YZ, odsazení,
umístění, FRONT/BACK, otočení a reference se ukládají v jedné transakci.
`placement.get/set/reference.set/reference.remove` a `value_lock.list/set`
podporují vlastnící kontejner. Identifikátor skici i kontejneru vrací
`sketch.get`; hierarchii včetně Originu vrací `tree`.

Referenční objekt se zadává jako `{"owner":"…","key":"…","instance_path":"…"}`.
Cesta rozlišuje přesný výskyt opakovaného dílu nebo vnořené sestavy.
První rovinná reference určuje pracovní FRONT skici. Výměna této reference
přesune i její automatický orientační řádek na FRONT; nesmí ponechat novou
plochu současně jako FRONT a TOP. Bez poziční roviny zůstávají samostatné
orientační reference nezávislé.

Samostatná změna skici používá původní uloženou geometrii ZIMA a místní
řešení rámu. Nepočítá tělesa OCCT, nemění komponenty a nepřepočítává jejich
vazby ani existující odečty. Výsledek `sketch.set` má `body_calculated=false`.
Editace skici již vlastněné odečtem potvrzuje celý profilový odečet, proto
může tělesa počítat. Přepnutí záložky tento výpočet nespouští.

## Převod na odečet

Podle výslovného pravidla uživatele jsou Extrusion i Revolution v Assembly
vždy pouze odečty. Platí to pro vytvoření, editaci a převod samostatné skici,
v GUI i CLI. Společná transakce odmítne CombineMode::Add před změnou sestavy.
Cílem může být pouze bezprostřední editovatelný výskyt Partu.

Převod zachová ID kontejneru, Origin, umístění, reference a identitu skici.
Změní druh prvku a atomicky nahradí záznam v `sketch_containers` záznamem
v `cuts`. Undo obnoví původní samostatnou skicu a původní tělesa. Cancel
dialogu ponechá původní kontejner beze změny.

Zámek odsazení pracovní roviny se při převodu přenese z parametrů samostatné
skici do parametrů profilového prvku. Má jediného uloženého vlastníka.
Vlastnosti vnitřní skici tento zámek čtou a potvrzují přes společný adaptér;
změna hodnoty nejde obejít volbou jiného příkazu.

## Smazání a návaznosti

`sketch.delete` přijímá ID samostatné skici a odstraní zároveň její
kontejner. GUI používá tutéž operaci. Skicu uvnitř odečtu příkaz odmítne;
odstraňuje se její vlastnící prvek. Operaci lze vrátit přes Undo/Redo.

Reference umístění skici patří do kontroly závislostí komponent a
konstrukčních objektů. Cyklické navázání se odmítá před potvrzením.
Smazání samotné skici ponechává závislým konstrukcím jejich poslední
použitelný rám a stav chybějící reference.

Ve stromu je samostatný kontejner se svým Originem a skicou. V běžném
pohledu se nabízí jeho profil; pomůcky skicáře patří až aktivní editaci.
Náhled v aktivní podsestavě používá přesnou cestu výskytu a zachovává
pasivní kontext celé nadřazené sestavy.

## Nativní soubory

`.asmz` používá verzi INI 18 a vnitřní JSON verzi 27. Povinné pole
`sketch_containers` obsahuje ID, rodičovskou identitu, název, potlačení,
Placement a zámky. `config/templates/start_assembly.asmz` je aktualizovaný
se zachováním ostatních metadat šablony. Přípony se nemění. Formát Partu
ani struktura start Partu se kvůli této změně nemění.

Všechna potřebná data zůstávají v nativních `.prtz/.asmz/.drwz`.
Nevznikají povinné pomocné soubory, revize ani cache adresáře.

## Ověření

`assembly_sketch_properties_tests` kontroluje polohu v posunutém a
natočeném rámu, dva výskyty stejného Partu, výměnu první rovinné reference,
zámky, neplatné a cyklické zdroje, závislosti, nativní vlastnictví,
Undo/Redo a smazání. Kontroluje také nezměněné umístění a sdílenou
vypočtenou geometrii komponent.

Nezávislá kontrola objemu používá kvádr 10 × 10 × 10 mm:
kruhový profil R1 vytažený o 2 mm zanechá 1000 − 2π mm³;
rotace obdélníku mezi poloměry 1 a 2 mm s výškou 2 mm zanechá
1000 − 6π mm³. Druhý výskyt zůstává 1000 mm³. Oba profily musejí zachovat
původní kontejner a při odmítnutém přičtení se sestava nesmí změnit.

GUI konzole provádí stejný scénář Vlastností pro Part i Assembly:
Cancel/OK, název, rovina, odsazení, FRONT/BACK, otočení, reference
a samostatný FRONT. Porovnává celé znovuotevřené definice, nikoli jen
vybrané číselné hodnoty. Stávající GUI scénář převádí skicu sestavy na odečet.

Kontrola závislostí při mazání komponenty zahrnuje i externí reference
skici přímo v Assembly, které nemají kontext závislého Partu. Reference
jiné sestavy blokování nepřenáší na nesouvisející výskyty.

GUI regrese aktivuje také natočenou podsestavu, otevře její skicu,
vybírá konkrétní bod, táhne jej, ověří místní rovinu a uložené souřadnice.
Během tažení zůstává viditelná pasivní geometrie nadřazené sestavy.
Modelová regrese zobrazení ověřuje dva výskyty stejné podsestavy a
nezměněná sdílená data neaktivního výskytu.

Katalog obsahuje 292 příkazů a CTest registruje 159 testů.
Obě aplikace a všechny testovací programy jsou sestavené. Úplná regrese
prošla **159/159 za 647,73 s**. Zahrnuje samostatný proces CLI, celé GUI,
nové skici Assembly, vlastnosti, referenční závislosti, nativní uložení,
výkresy, modelové operace i testy přesných spline.

Logy: `build/assembly-sketch-release-verified-build.log` a
`build/assembly-sketch-release-verified-tests.log`.
Nová chybová hlášení mají překlady ve všech pěti jazykových souborech
a dialog Vlastností skici používá překlad i při odmítnutí změny.

Po posledním doplnění lokalizace bylo dokončené nové sestavení a znovu
prošly překlady a celý GUI/CLI scénář konzole: **2/2 za 122,21 s**.
Logy: `build/assembly-sketch-localized-build.log` a
`build/assembly-sketch-localized-tests.log`.
