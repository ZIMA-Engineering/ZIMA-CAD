# Popisky pohledů a konce řezných čar z CLI

`drawing.view.labels.get` čte uloženou polohu popisku pohledu, označení řezu
a obou konců řezných čar. `drawing.view.labels.set` je mění jedním Undo krokem.
Oba příkazy vyžadují `view`; volitelný `document` označuje otevřený Drawing.
Změna podléhá běžné ochraně aktivního dokumentu a otevřeného editoru.

```json
{"command":"drawing.view.labels.set","arguments":{"view":"VIEW_ID","values":{"caption_position_mm":[12,-7],"section_label_position_mm":[-3,14],"markers":[{"section":"SECTION_ID","offsets_mm":[3,-2]}]}}}
```

- Polohy popisků jsou `[x,y]` v **milimetrech papíru**, od počátku pohledu,
  doprava a nahoru. Nejsou násobeny měřítkem modelu. `null` vrací automatickou
  polohu nad obrysem. Změna polohy nemění viditelnost ani název popisku.
- `markers` je částečný seznam značek daného pohledu. Každá položka obsahuje
  právě `section` a `offsets_mm`. Pole `[první,druhý]` určuje podepsaný posun
  každého konce podél řezné čáry. Kladná hodnota prodlužuje, záporná zkracuje.
  Hodnota `null` odstraní oba vlastní posuny dané značky.
- Stejně jako myš se posun omezí minimem společného `section_trace_layout`.
  První konec se upraví před druhým; druhý respektuje novou délku společného
  úseku. Zbude nejméně délka dovolená stávajícím vykreslením. Nezadává se
  odhadnuté minimum od klienta.
- Dotaz vrací uložené `offsets_mm`, skutečné `effective_offsets_mm`,
  `minimum_offsets_mm` a `displayable`. Bez zobrazitelné řezné čáry jsou
  odvozené hodnoty `null`; lze obnovit automatickou polohu, ale zadání
  nenulového/vlastního pole konců vrátí `trace_unavailable`.
- Vynechané hodnoty zůstávají beze změny. Prázdný objekt a shodné hodnoty
  nevytvářejí historii. Implicitní nulové posuny se zbytečně nepřipínají.
  Neznámé položky, duplicity, nesprávné typy a nečíselné/nekonečné hodnoty
  se odmítají. Selhání i poslední značky ponechá celý dokument nezměněný.

GUI tažení i příkaz používají `set_drawing_label_position` a
`set_drawing_section_end` v `drawing_label_operations`. Posuny během tažení
zůstávají v náhledu a dosavadní uvolnění myši potvrzuje historii. Příkaz
pracuje nad soukromou kopií Drawingu a potvrdí až celou platnou změnu.

Neprobíhá načítání Partu/Assembly, OCCT, nová projekce ani změna kót nebo
geometrie pohledu. Podklady řezné čáry pocházejí ze stejného uloženého pohledu
a dalších pohledů **téhož listu**, které používá vykreslení. Výsledky zůstávají
ve stávajících polích `.drwz`; formát ani šablony se nemění.

Testy pokrývají skutečné vzdálenosti konců, měřítko 2:1, mezní zkrácení,
chybějící zdroj, atomické chyby, editovací ochranu, reset, Undo/Redo a nativní
uložení. Samostatný proces CLI provádí otevření, dotaz, úpravu, Undo/Redo a
uložení. GUI test přečte skutečné tažení příkazem a ověří polohy viditelných
úchopů po příkazu, Undo i Redo.
