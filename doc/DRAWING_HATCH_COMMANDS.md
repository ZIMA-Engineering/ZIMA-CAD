# Šrafování výkresových řezů v GUI a CLI

## Příkazy

`drawing.view.hatch.get` vyžaduje `view`, volitelně `document` a `limit`
(1–10000, výchozí 2000). Vrací aktuální nastavení řezu v jeho zdrojovém
Partu nebo Assembly a nezávislou viditelnost šraf v požadovaném pohledu.
Zdroj čte z otevřeného dokumentu, případně z nativního souboru relativně
k cestě výkresu. Nepromítá geometrii, neotevírá dokument ani nemění historii.
Pro čistě uložený stav pohledu bez dostupného zdroje slouží
`drawing.view.get`.

Výsledek obsahuje `source_document`, `section`, `items`, `total`, jednotky
mm a stupně. Každá položka uvádí přesný klíč `component`, název,
`available`, režim pohledu `mode`, nezávislý `source_mode`,
`hidden_in_view`, `custom_hatch` a účinné parametry `hatch`.
V Partu je klíčem ID tělesa, v Assembly úplná cesta konkrétního výskytu.
Dva výskyty stejného šroubu jsou dvě samostatné položky. Dříve uložené
nedostupné komponenty jsou rozpoznatelné pomocí `available: false`.

`drawing.view.hatch.set` vyžaduje `view` a neprázdné pole `components`,
volitelně `document`. Upravuje jen uvedené položky. Jedna komponenta může
být v dávce právě jednou; celá dávka má nejvýše 10000 položek.

```json
{
  "command": "drawing.view.hatch.set",
  "arguments": {
    "view": "<ID pohledu>",
    "components": [{
      "component": "<ID tělesa nebo přesná cesta výskytu>",
      "mode": "cut_only",
      "hatch": {
        "pattern": "cross",
        "angle_degrees": 30,
        "spacing_mm": 3.125,
        "offset_mm": 0.375
      }
    }]
  }
}
```

## Vlastnictví parametrů

Režim tabulky vlastností a příkazu má stejný význam:

| `mode` | Výkresový pohled | Zdrojový řez |
| --- | --- | --- |
| `cut_hatch` | Řez se šrafováním | Zachová nezávislou 3D viditelnost; přepne případné `uncut` na řez |
| `cut_only` | Řez bez šraf pouze v tomto pohledu | Zachová nezávislou 3D viditelnost; přepne případné `uncut` na řez |
| `uncut` | Komponenta bez řezu | Změní zdrojový režim komponenty na `uncut` |

Styl patří komponentě zdrojového řezu. Jeho změna se promítne do všech
pohledů téhož řezu v upravovaném výkresu; každému zůstane vlastní skrytí
šraf. Ostatní otevřené výkresy obnoví vypočtené projekce při explicitní
regeneraci. Samotné přepnutí tabu neprovádí projekci ani výpočet tělesa.

`hatch` obsahuje libovolnou neprázdnou podmnožinu parametrů. Vzor je
`parallel`, `cross` nebo `dashed`, rozteč 0,1–100 mm na papíře. Úhel
ve stupních a posun v mm musí být konečná čísla. Číselné hodnoty JSON
se ukládají bez zaokrouhlení na počet desetinných míst ovládacího prvku.
Uvedení `hatch` zapne vlastní styl. `custom_hatch: false` obnoví děděný
styl včetně střídání směru sousedních komponent; nemůže být spojeno
s novými hodnotami `hatch` v téže položce.

## Potvrzení, historie a soubory

GUI a CLI používají `workspace::set_drawing_section_components` a
`prepare_section_component_commit`. Parser komponent je společný
s příkazy `section.create/set`.

Nejprve se ověří celá dávka a soukromý návrh výkresu včetně všech dotčených
projekcí. Teprve po jejich úspěchu se zapíše zdroj a výkres. Neplatná
komponenta, chybná rozteč nebo chyba navazujícího pohledu tak nezanechá
částečně uloženou úpravu. Shodné nastavení je no-op bez nové historie.
Změnu hlásí `changed`; `source_changed` odlišuje změnu zdroje od pouhého
místního skrytí. `body_calculated` je vždy `false`.

Zavřený zdroj se otevře až při potvrzení skutečné změny zdrojových
parametrů. Zachová se aktivní výkres. Zdroj je označený jako změněný,
nikdy se automaticky neukládá na disk. Při místním skrytí šraf se
zdrojové okno neotevře. ID zdroje musí souhlasit i tehdy, když na stejné
cestě leží jiný soubor nebo je otevřen jiný dokument.

Výkres a zdroj mají nadále **samostatné historie Undo/Redo**. Undo výkresu
obnoví jeho uložené projekce a místní viditelnost; změnu zdrojového stylu
vrací Undo ve zdroji. Při změně stylu je třeba uložit oba dokumenty.
Není zavedená nová společná historie více dokumentů.

Připravený zápis odmítne mezitím změněný zdroj, včetně Undo na stejnou
revizi a zavření/znovuotevření téhož dokumentu. Nekopíruje starý model
zpět přes nové změny. Tělesa, původní reference, skica řezu a sdílené
zdrojové geometrie zůstávají zachované. Nepoužívá OCCT.

Veškeré nastavení zůstává v existujících `.prtz`, `.asmz` a `.drwz`.
Formát ani šablony se tímto krokem nemění.

## Ověření

`drawing_hatch_command_tests` pokrývá čtení, místní skrytí, přesné styly,
dědění, režim `uncut`, neplatné dávky, pozdní chybu projekce, Undo/Redo,
neaktuální editaci, nativní soubory, otevření zavřeného Partu/Assembly,
identitu zdroje a vnořené opakované výskyty. Kontroluje také zachování
vypočteného tělesa a sdílení zdrojové geometrie. `cli_process_tests`
spouští skutečnou příkazovku bez inicializace GUI. `section_ui_verification`
ověřuje obousměrně GUI → CLI → stejné GUI vlastnosti a zachování Cancel.

První testovací model neměl vytvořený BodyHistory vlastník a jeho čtení
způsobilo pád testu; po opravě sestavení testovacího modelu prošla nová
modelová sada 1/1 za 0,32 s. První integrace 7/8 za 37,07 s odhalila chybný předpoklad GUI testu
o dvoumístném zobrazení. Po oddělení kontroly zobrazení od uložené
přesnosti a obnovení výběru po konzolové mutaci prošly GUI a překlady
2/2 za 16,82 s. Následná **úplná sada prošla 130/130 za 542,95 s**.
Obě aplikace a všechny testy se sestavily. Logy:
`build/drawing-hatch-full-build.log`, `build/drawing-hatch-final-build.log`,
`build/drawing-hatch-model-tests.log`, `build/drawing-hatch-integration-tests.log`,
`build/drawing-hatch-gui-tests.log`, `build/drawing-hatch-full-tests.log`.
Katalog má 234 příkazů.
