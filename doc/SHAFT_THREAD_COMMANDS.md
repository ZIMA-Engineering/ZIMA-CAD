# Příkazy vnějšího závitu

`shaft_thread.create/get/set` ovládají současný nativní **ShaftThread**.
Vnější závit přidává technologickou patní plochu a případný výběh. Nemění
objem hřídele a nevytváří šroubovicově vyřezaný profil závitu.

GUI Vlastnosti a CLI používají společné `commit_shaft_thread`, stejné původní
reference před hranicí editace a stejný výběr katalogové velikosti. Výpočet
probíhá při potvrzení změny. `get` pouze čte uložená data; shodné `set`
nemění revizi ani nepřepočítává těleso. Úspěšná změna je jeden krok Undo.

## Příklad

Vytvořte Part a válec příkazy `new part hridel` a `cylinder.create 5 30`.
Do následujícího požadavku doplňte vrácené ID kontejneru válce. Klíče
`side` a `z_min` jsou původní sémantické plochy tohoto typu válce;
u jiné geometrie použijte její skutečné původní reference.

```json
{"command":"shaft_thread.create","arguments":{"cylinder":{"owner":"ID-VALCE","key":"side"},"start":{"owner":"ID-VALCE","key":"z_min"},"designation":"M10","length_mm":15}}
{"command":"shaft_thread.get","arguments":{"container":"ID-ZAVITU"}}
{"command":"shaft_thread.set","arguments":{"container":"ID-ZAVITU","length_mm":20,"root_diameter_mm":8.05}}
{"command":"shaft_thread.set","arguments":{"container":"ID-ZAVITU","end_condition":"up_to","end":{"owner":"ID-VALCE","key":"z_max"}}}
```

## Parametry

Všechny rozměry jsou JSON čísla v milimetrech, nezávisle na zobrazovaných
jednotkách dokumentu. `create` vyžaduje `cylinder` a `start`; `set` vyžaduje
`container` a alespoň jeden měněný parametr. Volitelné `document` určuje Part.

| Parametr | Význam |
|---|---|
| `name` | Neprázdný název; výchozí název se překládá |
| `standard` | `metric`, `whitworth`, `pipe` |
| `designation` | Přesné označení ze společného `thread.catalog` |
| `root_diameter_mm` | Vlastní patní průměr |
| `length_mm` | Délka od počáteční plochy, nikoli od konce vstupního sražení |
| `end_condition` | `length`, `up_to`, `through_all` |
| `runout_enabled` | Výběh; povolený pouze při zakončení délkou |
| `runout_pitch_factor` | Nezáporný násobek stoupání pro výběh |
| `cylinder` | Původní vnější válcová plocha hřídele |
| `start` | Původní počáteční rovinná plocha |
| `chamfer` | Volitelná původní kuželová plocha vstupního sražení |
| `end` | Původní rovina, válec nebo kužel pro zakončení `up_to` |

Reference má textové `owner` a `key`, volitelně prázdné `instance_path`.
V této etapě jde o lokální původní plochy Partu. Reference na výsledek,
neexistující plochy, pozdější prvky nebo jiné výskyty nejsou přijaty.
`{}` odstraní volitelné `chamfer` nebo `end`; povinnou plochu nelze takto
vyprázdnit a potvrdit. `get` vrací nepřítomné volitelné reference jako `null`.
Do vstupu se nepředává analytická náhradní geometrie.

Změna normy/velikosti převezme jmenovitý průměr, stoupání a **vnější** patní
průměr katalogu. Výslovně zadaný `root_diameter_mm` má přednost. Při změně
normy bez velikosti se zachová dostupné označení, jinak se vybere první
položka nové normy stejně jako ve Vlastnostech. Zámek rozměru brání i jeho
nepřímé změně katalogovým výběrem v CLI i při potvrzení Vlastností.
Vlastnosti dovolují zámek výslovně odemknout a pak změnu potvrdit.

`up_to` a `through_all` vypnou výběh; explicitní `runout_enabled: true`
s těmito režimy je chyba. Výběh u délkového zakončení respektuje zbývající
délku hřídele. Průsečík patní plochy se vstupním a koncovým sražením počítá
stávající geometrické pravidlo. Zachovává se i dosavadní možnost nezávisle
zvolené patní plochy mimo materiál hřídele.

`get` navíc vrací ID dokumentu, kontejneru, prvku a tělesa, jmenovitý průměr,
stoupání, zámky a revizi. Editace zachovává identity. Neplatný požadavek
nezmění dokument, vypočtenou geometrii ani Undo. Odvozené těleso se přímo
neupravuje; při editaci musí být aktivní vlastní těleso závitu.

## Rozsah a ověření

Nativní formát, start šablony a obecné řešení umístění se nemění. Reference
pro regeneraci při ztrátě zdroje nadále používají dosavadní pravidla;
tato etapa zpřístupňuje existující příkaz a jeho Vlastnosti v CLI.
Samostatný Hole, DrillPoint a vnořené části otvoru zůstávají v plánu CLI.

Modelové testy kontrolují nezměněný objem hřídele, konkrétní poloměry a
začátky/konce patní plochy a výběhu, obrácený směr, sražení, zakončení,
katalogový výběr, zámky, chyby referencí, Undo/Redo a nativní uložení se
studeným výpočtem. Procesní a GUI testy ověřují tvorbu a editaci skutečnou
konzolí, OK/Cancel, Undo a shodu katalogového výběru.

Sestavení obou programů a všech testů prošlo. Související regrese **9/9**
(70,03 s), `build/shaft-thread-command-full-build.log` a
`build/shaft-thread-command-related-tests.log`. Závěrečná kontrola přesunula
ověření zámku také do společného potvrzení, aby jej neobešel katalog v GUI.
Po této úpravě prošlo znovu sestavení a **3/3** (57,52 s): model, skutečný
CLI proces a GUI včetně odmítnutého potvrzení uzamčeného průměru. Logy:
`build/shaft-thread-command-final-build.log`,
`build/shaft-thread-command-final-tests.log`.

První modelový pokus opravil chybnou očekávanou hodnotu testu M12:
10,106 mm je vnitřní patní průměr, vnější je 9,853 mm. Při doplnění
sražené hřídele byla opravena také const kvalifikace cesty v testovacím
přípravku. Výrobní katalog ani geometrická pravidla se kvůli těmto chybám
neměnila. Testy navíc ověřily Whitworth a G, začátek/konec na sražení
0,08/29,92 mm a odmítnutí reference na pozdější prvek.


Následný úklid odstranil dvě kopie celého referenčního paketu při otevření
Vlastností, které se ihned přepisovaly výsledkem společného filtru. Výběr,
rollback ani řešení referencí se nemění. Sestavení prošlo; modelový test
závitu **1/1** (0,29 s) a GUI konzole **1/1** (43,86 s).
Logy: `build/shaft-thread-reference-copy-build.log`,
`build/shaft-thread-reference-copy-tests.log` a
`build/shaft-thread-reference-copy-gui-tests.log`.
