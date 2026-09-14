# Doplnění Vlastností skici do společné příkazové vrstvy

## Zjištění auditu

GUI `SketchPropertiesDialog` mění název, výchozí rovinu XY/XZ/YZ,
odsazení pracovní roviny, umístění a původní reference. Části jsou
v `cpp/app/sketch_properties_dialog.cpp` a potvrzení v
`cpp/app/workspace/sketch_properties.cpp`.

CLI má tvorbu skici, úpravy geometrie a vlastnosti profilových prvků.
Chybí odpovídající úplná operace Vlastností samostatné skici.
Návrh doplní `sketch.set` a přiřazení/odebrání referencí; používá
již existující transakce a sdílená pravidla, bez volání widgetů z CLI.

## Part: existující datový model

Samostatnou skici vlastní HistoryContainer druhu Sketch.
Umístění již má trvalého vlastníka. Nové a editované hodnoty se potvrdí
v jednom kroku se zachováním skici, geometrických identit a zámků.
GUI a CLI budou používat společnou přípravu a potvrzení. Výsledek
se ověří i následným vytažením, Undo/Redo a nativním souborem.
Formát Partu se kvůli této části nemění.

## Assembly: schválený návrh

Uživatel výslovně odpověděl **„Ano, schvaluji tuto změnu“** na otázku
o trvalém kontejneru umístění samostatných skic v Assembly přímo do `.asmz`,
zachování solveru, přesných cest výskytů, explicitní regenerace a
aktualizaci startovní šablony. Toto schválení platí pro níže uvedenou změnu.


Současný callback GUI uloží `committed_placement` pro Part a vlastní
Assembly odečet, ale pro samostatnou Assembly skici jej zahodí.
`AssemblyDocument` uchovává seznam skic, nikoli jejich kontejnery umístění.
Jeho `resolve_constructions` řeší pouze konstrukční objekty.

Navrhované doplnění:

- Samostatná Assembly skica dostane vlastní trvalý kontejner umístění.
  Použije existující Placement, stabilní ID a pravidla původních referencí.
- Definice kontejneru a jeho reference budou součástí `.asmz`. Nebudou
  vznikat žádné povinné externí či vedlejší soubory.
- GUI i CLI použijí stejné potvrzení. Zrušení návrh zahodí; jedna
  potvrzená změna bude jeden krok Undo/Redo.
- Reference na komponentu zachová přesnou cestu výskytu. Skica nebude
  měnit umístění komponenty ani řídit vnitřek podsestavy.
- Místní pracovní rám se vyřeší existujícím řešením ZIMA, bez OCCT.
  Samostatná editace skici nebude přepočítávat vazby komponent ani
  Assembly odečty. Ty zůstanou na explicitní regeneraci.
- Aktualizuje se odpovídající startovní šablona Assembly v configu
  a ověří se také start Part. Přípony zůstanou stejné; legacy migrace se
  nezavede.

Požadovaný výslovný souhlas podle Container placement protection v
AGENTS.md je získaný. Nové potvrzení pro tento návrh není potřeba.

## Ověření před uzavřením

Shoda celých nativních definic z GUI a CLI; změny při Cancel/OK; zámky
nulových i nenulových hodnot; rovinné reference a jejich odebrání;
správná rovina a odsazení v posunutém/natočeném tělese; chybné a
dopředné zdroje beze změny; geometrie po explicitní regeneraci;
Assembly výskyty a vlastnictví; uložení a znovuotevření.

## Upřesnění uživatele: operace sestavy

Protažení a rotace v Assembly jsou vždy pouze odečty z vybraných
bezprostředních dílů. Platí to pro GUI, CLI i převod samostatné skici.
Převod zachová kontejner umístění a jeho reference, ale výsledný prvek
musí mít CombineMode::Subtract. Samostatná skica sama materiál nevytváří.

## Vazby implementace Assembly

Samostatné kontejnery budou v seznamu `sketch_containers`; každá samostatná
skica na svůj kontejner odkazuje pomocí stávajícího `owner_container_id`.
Kontejner bude vlastníkem umístění, Originu a zámků. Skica vlastněná odečtem
zůstane přímo jeho dítětem. Nativní validace odmítne chybějící nebo dvojité
vlastnictví; nevytváří kontejner dodatečně při načítání starého souboru.

Změna se promítne do stromu GUI i `tree`, výběru a Vlastností, nativního
uložení, přehledu rozměrů, referencí a zámků. Převod samostatné skici na
odečet zachová identitu kontejneru a Originu. Samotné sestavové komponenty
ani jejich vypočtená tělesa se při změně vlastností skici nepřepočítají.

Audit navíc našel, že smazání samostatné Assembly skici je dosud přímý
callback GUI. Při změně vlastnictví se přesune do společné operace,
aby se při smazání nezanechal osiřelý kontejner a aby šel tentýž výsledek
provést příkazem. Smazání skici patřící odečtu zůstane v operaci jejího
vlastníka.


## Realizace schváleného návrhu

Datový kontejner Assembly skici, společné Vlastnosti, reference, zámky,
smazání a převod na odečet jsou implementované. Přípony zůstaly stejné;
formát Assembly je INI 18 / vnitřní JSON 27 a startovní Assembly je aktualizovaná.
Podrobný výsledný kontrakt a ověření jsou v
[ASSEMBLY_SKETCH_PROPERTIES.md](ASSEMBLY_SKETCH_PROPERTIES.md).
