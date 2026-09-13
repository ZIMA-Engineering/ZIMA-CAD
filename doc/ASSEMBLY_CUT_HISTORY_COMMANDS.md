# Historie profilových odečtů sestavy

Navazuje na [PROFILE_COMMANDS.md](PROFILE_COMMANDS.md). Operace spravují pouze
odečty přesné otevřené vlastnící Assembly. Zdrojové Party ani interní
komponenty vložené podsestavy se těmito příkazy neupravují.

## Příkazy

```json
{"command":"assembly.cut.suppress","arguments":{"container":"ID-ODECTU","suppressed":true}}
{"command":"assembly.cut.can_move","arguments":{"container":"DRUHY-ODECET","before":"PRVNI-ODECET"}}
{"command":"assembly.cut.move","arguments":{"container":"DRUHY-ODECET","before":"PRVNI-ODECET"}}
{"command":"assembly.cut.remove","arguments":{"container":"ID-ODECTU"}}
```

Každý příkaz přijímá volitelné `document` podle společného cílení dokumentů.
`container` je stabilní identita odečtu, nikoli název nebo pozice ve stromu.
`before` lze vynechat pro konec seznamu. Neznámý zdroj ani cíl přesunu se
nepovažuje za úspěšný prázdný krok.

- `suppress` nastaví potlačení nebo obnovení. Shodný stav vrací `changed=false`
  bez výpočtu a bez nového Undo.
- `move` přepočítá nové pořadí přes společnou transakci. Shodné pořadí je
  beze změny. Přesun před zdroj dříve platné reference se odmítne; po výpočtu
  se ověří i zachování původní referenční geometrie a uložených odkazů.
- `can_move` pouze kontroluje identitu a závislosti pořadí. Vrací `allowed`
  a `would_change` bez výpočtu těles. Nezaručuje úspěch budoucího výpočtu
  změněného zdroje; ten se ověřuje až při skutečném přesunu.
- `remove` odstraní odečet a skicu, kterou vlastní. Jiná skica může ponechat
  svou poslední promítnutou křivku; místní reference na odstraněný zdroj je
  označena jako porušená. Undo obnoví odečet, vlastněnou skicu i platný odkaz.

Úspěšné změny vracejí `changed`, `document`, `container`, `revision` a `order`.
Aktivní dialog brání konfliktní mutaci. Výpočet pracuje na připraveném dokumentu
s aktuálními otevřenými zdroji; publikuje se až hotový výsledek jedním commitem
historie. Chyba nezanechá částečné obnovení komponent. Nativní formát se v této
etapě nemění.

## Sdílení s GUI

Kontextová nabídka odečtu, volby výše/níže, tažení ve stromu a CLI používají
`set_assembly_cut_suppressed`, `move_assembly_cut` a `remove_assembly_cut`.
Původní oddělené výpočty v GUI byly odstraněny. Při odstranění už nevzniká
osiřelá skica s neexistujícím vlastníkem. Přesun z nabídky nyní používá stejné
kontroly závislostí jako přesun tažením.

## Ověření

Modelové testy **3/3 za 1,58 s** ověřily dvě nezávislé operace v jednom
výskytu, objemy 970/994/1000 mm³, vstupní tělesa po změně pořadí, potlačení,
prázdné kroky, závislosti, chybějící identity, zámek během editace, Undo/Redo
a nativní uložení. Samostatný scénář ověřil zachování promítnuté křivky při
odstranění zdroje včetně porušeného odkazu a jeho obnovení. Log:
`build/assembly-cut-history-model-tests.log`.

Po sestavení obou aplikací a všech testů prošla rozšířená integrace
**9/9 za 115,96 s**. Ověřila skutečné GUI nabídky výše/níže, potlačení,
obnovení a mazání včetně potvrzení, objemu a Undo, samostatný CLI proces,
Part historii, profilové reference a překlady. Logy:
`build/assembly-cut-history-integration-build.log`,
`build/assembly-cut-history-integration-tests.log`. Katalog má 240 příkazů,
celková sada 137 testů. Poslední úplná regrese 136/136 patří předchozí
etapě profilových odečtů; tato etapa má uvedenou cílenou regresi.
