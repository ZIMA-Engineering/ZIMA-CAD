# Původní reference umístění řezu

`section.reference.set` přiřadí existujícímu řezu Partu nebo Assembly původní
referenci. Používá schválené společné přiřazení referenčních polí a stejnou
transakci jako OK ve Vlastnostech řezu. Algoritmus řešení umístění nemění.

```json
{
  "command": "section.reference.set",
  "arguments": {
    "object": "<ID řezu>",
    "index": 0,
    "reference": {
      "owner": "<ID původního objektu>",
      "key": "<sémantický klíč původní geometrie>"
    },
    "offset_mm": 2,
    "flip": false,
    "derive_orientation": true
  }
}
```

Položku `reference` lze získat z `reference.list/get`. V sestavě obsahuje také
`instance_path` přesného výskytu, včetně celé cesty přes podsestavy. Reference
ve vlastním Partu je místní a cestu výskytu neobsahuje. Názvy příkazů, argumentů
a chybových kódů zůstávají anglické ve všech jazycích.

## Chování

- `index` 0–2 označuje poziční pole, 3–4 samostatná pole FRONT/TOP.
- Nenulové `offset_mm` je dovoleno pouze pro poziční referenci plochy.
  Výchozí hodnota je nula; `flip` má výchozí hodnotu `false`.
- `derive_orientation` má výchozí hodnotu `true`. Stejně jako společné GUI
  přiřazení může doplnit samostatnou orientační referenci. Hodnota `false`
  potlačí toto doplnění; nepřepisuje již uložené orientační reference.
- Nahrazení zamčené poziční reference zachová zámek a použije vzdálenost
  naměřenou ze stávající polohy podle společného kontraktu umístění.
- Vlastní geometrie řezu, chybějící zdroj, duplicitní přiřazení, nepovolené
  pole či neřešitelná kombinace se odmítnou bez změny dokumentu.
- Zachovávají se identity řezu, jeho skici, počátku a cesta řezu.
- Operace podporuje dokument, který je současně aktivní i zobrazený,
  stejně jako současné Vlastnosti řezu. Aktivovaný zdrojový Part uvnitř
  zobrazené sestavy není touto cestou upravitelný.
- Otevřený editor blokuje mutující příkaz. Volitelné `document` slouží jako
  pojistka proti zápisu do jiného aktivního dokumentu.

Výsledek obsahuje podrobnosti řezu, `document`, `revision`, `changed`
a `body_calculated: false`. Požadavek beze změny nevytvoří další krok historie.
Skutečná změna tvoří jeden krok Undo/Redo.

Potvrzení odvodí řez z již vypočtených dat ZIMA a ověří jeho platnost.
Nevyvolává OCCT ani přepočet zdrojových těles či vazeb sestavy. Již vypočtená
geometrie Partu i sdílení zdrojů Assembly zůstávají zachovány. Všechny údaje
se ukládají do existujících `.prtz` a `.asmz`; formát ani šablony se nemění.

## Ověření

Modelový test `section_reference_command_tests.cpp` měří řez kvádrem
10 × 20 × 30 mm: při posunu na X = 2 mm zůstává průřez 600 mm² a objem tělesa
6000 mm³. Zahrnuje zamčenou vzdálenost, odmítnuté vstupy, původní identity,
nativní uložení a Undo/Redo. Sestava obsahuje dva výskyty stejné podsestavy;
změna přesné referenční cesty přesune řez z X = 3 na X = 28 mm při zachování
plochy řezu a sdílené zdrojové geometrie.

Procesní test spouští samostatné CLI a následně čte uložený Part. GUI test
otevírá Vlastnosti z položky stromu, ověřuje hodnotu zadanou příkazem,
Cancel, OK, Undo/Redo a původní objem. Závěrečný výsledek běhu je uveden
v [CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).
