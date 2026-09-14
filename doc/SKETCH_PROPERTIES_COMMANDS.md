# Vlastnosti skici přes GUI a CLI

GUI i příkazy potvrzují celou skicu a její umístění společnou funkcí
`commit_sketch_properties` pro Part i Assembly. Samostatná skica sestavy
má vlastní trvalý kontejner v `.asmz`; její změna nepočítá komponentové vazby
ani odečty. Podrobnosti: [Vlastnosti skic sestavy](ASSEMBLY_SKETCH_PROPERTIES.md).

## Příkazy

`sketch.set` přijímá `sketch`, volitelně `name`, `plane` (XY/XZ/YZ),
`plane_offset_mm`, `placement`, `back`, `quarter_turns` (0–3) a `document`.
Odsazení je v mm a musí ležet mezi −1 000 000 a 1 000 000 mm.
`placement` je objekt číselných polí společného umístění, např.
`{"x":10,"y":20,"z":30}` nebo `{"reference_offset:0":5}`.
Zamčená nebo referencí určená pole číselný vstup nepřepíše.

`sketch.reference.set` přijímá `sketch`, `index` (0–4), původní `reference`
s `owner` a `key`, volitelně `offset_mm`, `flip` a `document`.
První rovinný zdroj určí pracovní rovinu. Další poziční řádky zachovají
její orientaci podle stejných pravidel jako Vlastnosti skici.
Původní identita se neodvozuje z pořadí hran nebo ploch OCCT.

U samostatného kontejneru fungují také obecné
`placement.reference.set` a `placement.reference.remove` s jeho `object` ID.
`sketch.get` vrací navíc `plane_offset_mm`; celé umístění lze číst přes
`placement.get`. Čtení nezapojuje OCCT.

## Transakce a vlastnictví

Název, rovina, odsazení a umístění se potvrdí v jediném kroku Undo/Redo.
Geometrie i její identity zůstávají zachované. GUI Cancel ponechá původní
dokument. Neplatné zdroje, zdroje za hranicí historie, neaktivní či
odvozené těleso a neřešitelné umístění se odmítnou před zveřejněním změny.

U skici patřící protažení nebo rotaci se aktualizuje současně její
odsazení i parametr odsazení vlastníka. OK a příkazová změna jsou
výslovným výpočtem Partu. Vytvoření samostatné Part skici používá tutéž
transakci. Prázdná změna nepřidává historii.

Pravidlo pracovní roviny bylo přesunuto beze změny z dialogu do
`document/sketch_placement.hpp`. Solver společného umístění se nemění.
Formát Partu ani start Part se v této etapě nemění.

## Ověření

Modelový test zahrnuje běžný a natočený Part, samostatnou a vlastněnou
skicu, původní roviny, neplatné/dopředné reference, zámky, Undo/Redo,
nativní uložení a znovuotevření. Objem válce z kružnice R3 a délky 5
kontroluje nezávisle proti 45π mm³ a kontroluje také jeho polohu.
GUI test porovnává celé uložené definice skici a kontejneru s výsledkem
CLI po Cancel/OK a po editaci odsazení reference.

Dialog nově potvrzuje a zobrazuje také samostatné řádky FRONT/TOP.
Dříve odebíral pouze poziční řádky a orientační pole při OK zahodil.
Test porovnává i samostatný FRONT bez poziční rovinné reference.
Před výpočtem protažení/rotace se vyřeší nový pracovní rám skici;
kontrola polohy zachytila chybějící přípravu rámu v nové transakci.

Sestavení obou aplikací a všech testů prošlo. Závěrečná související regrese
**12/12 za 244,41 s**: profilové rámy GUI, dialogové kontrakty, překlady,
samostatný proces CLI, skici, odebírání referencí, vlastněné profilové skici,
profilové operace/reference, nové Vlastnosti skici, katalog příkazů a konzole
GUI. Tato Part etapa měla 291 příkazů a 158 testů; aktuální navazující
ověření Assembly je v [samostatném přehledu](ASSEMBLY_SKETCH_PROPERTIES.md).
