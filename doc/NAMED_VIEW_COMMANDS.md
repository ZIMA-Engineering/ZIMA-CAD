# Pojmenované pohledy – GUI a CLI

Stav implementace: 2026-09-15. Pojmenovaný pohled v Partu nebo Assembly
uchovává úplný stav kamery. Původní dialog zapisoval pouze posun a měřítka,
takže po novém otevření ztratil natočení. Assembly navíc položku
`named_views` četla, ale vůbec ji nezapisovala do souboru.

## Příkazy

| Příkaz | Povinné argumenty | Výsledek |
| --- | --- | --- |
| `view.named.list` | žádné | Uspořádané `views` |
| `view.named.get` | `name` | Jeden úplný `view` |
| `view.named.set` | `name`, `camera` | Uložení nebo nahrazení, `changed` |
| `view.named.delete` | `name` | Odstranění, `changed` |

Všechny příkazy přijímají volitelné `document`. Dotazy mohou číst libovolný
otevřený Part/Assembly; zápis patří aktivnímu dokumentu. Při aktivaci
podsestavy se zapíše její zdrojový dokument, zobrazená nejvyšší sestava
se nemění. Neexistující název vrací `named_view_not_found`.

`camera` je úplný objekt:

```json
{
  "rotation": [0.70710678, 0, 0, 0.70710678],
  "zoom": 3.25,
  "pan_x": -17.5,
  "pan_y": 41.25,
  "reference_scale": 7.5
}
```

`rotation` je kvaternion v pořadí `w, x, y, z`; uvedený příklad otočí osu
X na kladnou osu Y. `pan_x/pan_y` jsou posuny v logických pixelech
prohlížeče. Obě měřítka mají stejný význam jako hodnoty `MeshView`;
nejde o přesnost geometrie ani importní toleranci. Pohled se vztahuje
k zobrazené scéně, není referencí na topologii ani umístěním komponenty.

`view.named.set` uloží dodaná data; `view.named.get/list` vracejí data
bez změny živé kamery. Obnovení pohledu v dialogu používá uložených
osm hodnot. Sedm standardních směrů zůstává součástí prohlížeče,
neukládají se jako uživatelské položky.

## Společná transakce

Qt-free `document/named_views.hpp` validuje a serializuje záznamy.
`workspace/named_view_operations.hpp` sdílí GUI a command host.
Přepsání stejného názvu zachová jeho místo v seznamu; opakované
uložení totožného stavu nevytvoří transakci ani krok Undo.

Název má 1 až 1024 bajtů UTF-8, bez řídicích znaků a krajních mezer.
Všech osm čísel musí být konečných, obě měřítka kladná a kvaternion
nenulový. Nenormalizovaný kvaternion se normalizuje. Záznam musí
obsahovat všechna pole, neznámá pole a duplicitní názvy se odmítají.
Validace předchází zveřejnění změny.

Úpravy jsou metadata: `body_calculated=false`. Nevyvolávají OCCT,
mate solving ani regeneraci závislostí. Vypočtené geometrie se sdílejí
beze změny. Undo/Redo obnoví seznam a celý uložený stav kamery.

Dialog nejprve potvrdí společnou operaci a teprve potom aktualizuje
seznam. Při chybě zobrazí zprávu uvnitř okna a ponechá rozepsaný název
nebo mazanou položku. Zachovává dosavadní chování: Uložit/Odstranit
jsou výslovné akce nad záložkami; Cancel vrátí kameru před otevřením
dialogu a OK ponechá vybraný pohled.

## Nativní soubory a ověření

Záznam se ukládá výhradně v `.prtz/.asmz` jako `named_views`:
název, čtyři složky `rotation`, `zoom`, `pan_x`, `pan_y` a
`reference_scale`. Part používá INI **20** / interní JSON **44**,
Assembly INI **19** / JSON **28**. Startovní šablony a sdílené
testovací dokumenty jsou aktualizované. Přípony zůstávají stejné;
nevznikají povinné další soubory. Starší schémata se nemigrují.
`.drwz` se v této etapě nemění.

Nový modelový test `zima_cpp_named_view_command_tests` ověřuje:

- samostatný geometrický výpočet účinku čtvrtotáčkového kvaternionu;
- Part i Assembly, přesnou kameru po uložení a novém načtení;
- neplatná data, transakční atomitu, dotazy, no-op, Undo/Redo;
- zachování existujících vypočtených těles a Assembly vazeb;
- aktivovanou podsestavu a odmítnutí zápisu do neaktivního dokumentu.

GUI regrese skutečně otevře dialog, zachytí natočenou a posunutou
kameru, uloží a znovu otevře dokument, vybere pohled a ověří všech
osm hodnot. Porovná celé nativní soubory z GUI a CLI a kontroluje
chování při chybě i odstranění položky. Ověřuje také zánik pracovního
okna s dosud otevřeným dialogem Pohledy: dialog se musí zničit ještě
za života členů okna, které používá jeho callback `destroyed`.

Obě aplikace i všechny testovací programy jsou sestavené.
Cílený modelový a GUI test prošly **2/2 za 129,39 s**.
Úplná regrese prošla **161/161 za 659,35 s**, včetně CLI procesu,
konzole, spuštění pracovního okna, nativních souborů, modelování,
sestav, výkresů a skic.

Lokální protokoly ověření:

- `build/named-view-final-build.log`
- `build/named-view-final-focused-tests.log`
- `build/named-view-full-regression.log`
