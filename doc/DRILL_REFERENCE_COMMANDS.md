# Přiřazení referencí Hole a Opening

`hole.reference.set` a `opening.reference.set` přiřazují původní referenci
umístění existujícího otvoru. Stejné typy nově přijímá také
`placement.reference.set` s argumentem `object` namísto `container`.

```json
{"command":"hole.reference.set","arguments":{"container":"HOLE_ID","index":0,"reference":{"owner":"PART_ID:origin","key":"origin:plane:xy"},"offset_mm":-20}}
```

Argumenty odpovídají ostatním příkazům přiřazení:

- `container`: existující Hole, respektive Opening v aktivním Partu;
- `index`: poziční řádky 0–2, FRONT/TOP 3–4;
- `reference`: původní `owner`, `key`, volitelně prázdný `instance_path`;
- `offset_mm`: podepsaná vzdálenost od rovinné reference, výchozí 0;
- `flip`: otočení reference, výchozí `false`;
- `derive_orientation`: běžné automatické doplnění orientace, výchozí `true`;
- `document`: volitelná kontrola aktivního dokumentu.

Zdroj musí předcházet funkci v historii nebo být dostupným počátkem.
Vlastní profil otvoru, jeho vlastní výsledek, pozdější objekt, neexistující
geometrie a cizí výskyt se odmítají. Aktivní Body musí být zapisovatelný.
Běžná ochrana otevřených editorů platí i pro tyto příkazy.

Příkaz používá stávající `prepare_part_feature_reference` a schválené
`assign_placement_reference`; společné řešení umístění se nemění. Potvrzení
provádí původní `commit_hole` / `commit_opening`, shodně s OK ve Vlastnostech.
Proběhne explicitní výpočet tělesa a jedno potvrzení historie. Neúspěch
zachová dokument i vypočtená data. Stejný požadavek nevytváří historii
ani nové těleso. Vlastněné kružnice, skici, sražení a vrtací hrot si zachovají
původní identitu; u Opening se zachová i závitová plocha.

První rovinná reference je FRONT. Směr vrtání se řídí konkrétní existující
funkcí: referencovaný Hole používá lokální +Y, Opening lokální −Y.
Test s FRONT=XY proto vrtá blok od z=−20 u Hole a od z=+20 u Opening.
Při výběru reference je třeba zvolit vstupní stranu a případné otočení podle
náhledu stejně jako v GUI. Příkaz tyto orientační konvence nepřepisuje.

Odpověď obsahuje dokument, kontejner, souřadnicový systém, aktuální uložené
umístění s referencemi, revizi, `changed` a `body_calculated`.
Další rozměry otvoru vracejí existující `hole.get` a `opening.get`.
Data se ukládají do stávajícího `.prtz`; formát ani šablony se nemění.

Regresní test počítá očekávaný odebraný objem válce a ověřuje skutečné
souřadnice stěn při posunu otvoru o 4 mm. Zahrnuje Hole, hladký Opening,
metrický, Whitworthův a trubkový závit, odmítnuté reference, bezezměnové
požadavky, Undo/Redo a nativní uložení. Procesní test používá skutečný CLI
program. GUI test otevírá stejné Vlastnosti a kontroluje referenční řádek,
Cancel, OK, Undo/Redo a uložený objem.
