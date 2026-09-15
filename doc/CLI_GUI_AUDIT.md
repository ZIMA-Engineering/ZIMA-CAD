# Závěrečný audit GUI a CLI (2026-09-15)

Rozsah je příkazové pokrytí současných modelových operací Partu,
Assembly, Drawing a editoru šablon. Přehled domén a průběžné výsledky
jsou v [CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).

## Přímé zápisy z GUI

Audit produkčních zápisů do dokumentových sessions odlišil skutečnou
modelovou operaci od publikování již připraveného výsledku myší.
Tři nalezené samostatné inline cesty jsou sjednocené:
katalog závitu, offset komponentové vazby a rádius 3D křivky / Sweepu.
Podrobnosti a regrese jsou v
[INLINE_DIMENSION_COMMANDS.md](INLINE_DIMENSION_COMMANDS.md).

Zbývající přímé zápisy mají tyto protějšky:

| GUI cesta | Modelový výsledek dostupný příkazem | Úloha adaptéru GUI |
| --- | --- | --- |
| `assembly_drag.cpp` | `component.set` s `placement` | Projekce ukazatele do roviny a dostupných stupňů volnosti, řešený náhled, potvrzení tahu nebo předání návrhu Vlastnostem |
| `sketch_drag.cpp`, bod | `sketch.point.move` | Souřadnice ze společného průsečíku paprsku a roviny, nativní `Sketch::move_point`, publikování výsledného návrhu |
| `sketch_drag.cpp`, zaoblení rohu | `sketch.corner_fillet.create` | Poloměr z ukazatele, nativní `Sketch::add_corner_fillet`, potvrzení výsledku |
| `sketch_drag.cpp`, umístění kóty | `sketch.dimension.set` s `position` | Zápis umístění popisku; nejde o nový hodnotový manipulátor |
| `sketch_drag.cpp`, existující tah reference | `component.set` s `placement_references` | Projekce lineárního/úhlového offsetu, meze, řešení nativních vazeb a publikování náhledu |
| `primitive_properties.cpp` / `sketch_document.cpp`, vlastněný profil | `extrusion/revolution.create/set/sketch.edit` | Přechod mezi Vlastnostmi a vlastněnou skicou, dočasný obal a návrat do editoru prvku; výsledný profil potvrzuje společná operace |
| Skici tažení a řezů | Příkazy tažení a `section.sketch.edit` | Samostatný návrh skici a předání do OK vlastnící operace |

Tyto vstupy nepotřebují příkazy simulující stisk tlačítka, pozici kurzoru
nebo jednotlivý snímek náhledu. CLI předává výsledné souřadnice,
reference a parametry. Hover a výběr poskytuje existující kontextový
adaptér konzole; dávkový proces pracuje s explicitními ID a cestami výskytů.

Kontrola nepřesouvá solver, společné umístění ani formáty. Nemění
pravidla regenerace ani vlastnictví zdrojových dokumentů.
Operace vytažení a rotace v Assembly zůstávají výhradně odečty.

## Rozsah dokončení

Příkazové pokrytí se vztahuje k funkcím, které program nyní podporuje.
Napojení AI a hlasu je další samostatná etapa. Řízení rozměrů relacemi
a generování rodinných variant zatím nejsou modelovou funkcí GUI;
existující příkazy spravují jejich nativní tabulky.

`export.view` snímá existující View prostřednictvím hostitelského adaptéru.
Samostatný dávkový proces bez View vrací dokumentovanou chybu
`view_unavailable`. Geometrické exporty a exporty listů mají samostatné
datové příkazy a nevyžadují ovládání widgetů.

Plošný audit Undo/Redo napříč všemi kombinacemi gest a návratů mezi editory
zůstává samostatným úkolem podle dohodnutého pořadí modelovacích funkcí.
Každá převedená operace již má své relevantní regrese historie a odmítnutí
neplatného vstupu. Dokončení příkazového pokrytí není tvrzení, že software
nemůže obsahovat další chybu.

## Kontrolní podklady

- Modelové a procesové testy ověřují příkazy, stabilní reference,
  vlastnictví, geometrické výsledky, serializaci a odmítnuté změny.
- `console_ui_verification.cpp` ověřuje skutečné ovládací prvky,
  společné operace, uložené soubory a opakované výskyty.
- `zima_cpp_workspace_startup_contract` zahrnuje gesta, otevřené
  Vlastnosti, jejich potvrzení/zrušení a průchody jednotlivými nástroji.
- Závěrečná Windows Release sada a přesné výsledky jsou zaznamenány
  v [CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).
