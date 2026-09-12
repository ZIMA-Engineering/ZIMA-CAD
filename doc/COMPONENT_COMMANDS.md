# Komponenty sestavy přes společné příkazy

První sada příkazů komponent poskytuje čtení uložené hierarchie a vložení
otevřeného Partu či Assembly. GUI menu vložení používá stejnou operaci.
Katalog nyní obsahuje 150 příkazů; příkazy komponent ještě nepokrývají editaci vazeb,
umístění, odstranění komponent ani vložení do vnořeného aktivního kontextu.

| Příkaz | Argumenty | Význam |
| --- | --- | --- |
| `component.list` | `[recursive:Boolean]`, `[limit:Integer]`, `[document]` | Uložené výskyty komponent, výchozí limit 2000 |
| `component.get` | `instance_path`, `[document]` | Jeden přesný uložený výskyt |
| `component.dependencies` | `instance_path`, `[document]` | Závislosti bránící odstranění přímé komponenty |
| `component.open` | `instance_path`, `[document]` | Otevření zdroje přesného výskytu na samostatné kartě |
| `component.insert` | `source`, `[name]`, `[document]` | Vložení otevřeného dokumentu do aktivní samostatné sestavy |

`source` je ID otevřeného Partu nebo Assembly. `document` je ID vlastnící sestavy.
Vložení vyžaduje, aby tato sestava byla aktivní na své samostatné kartě.
Při aktivaci vnořeného Partu/Assembly se operace odmítne, takže nevloží komponentu
omylem do zobrazeného rodiče. Vnořenou sestavu lze nyní upravit na její vlastní
kartě; příkazová aktivace přesného vnořeného kontextu zůstává další etapou.

```json
{"command":"component.insert","arguments":{"source":"ID-OTEVRENEHO-DILU","name":"Šroub 1"}}
{"command":"component.list","arguments":{"recursive":true,"limit":1000}}
```

Výsledek vložení obsahuje nové `occurrence`, přesné `instance_path`,
`source_document`, `document`, `revision`, `changed`. `instance_path` přebírejte
z výsledku; sestavuje ji nativní `InstancePath`, není to název dílu ani pořadové
číslo. Opakované vložení stejného zdroje má jiné ID a cestu výskytu. Part
používá sdílený vypočtený snímek, bez opětovného výpočtu jeho tělesa.
Vložení podsestavy zachovává existující nativní chování: na soukromém návrhu
obnoví její řetězec dostupných závislostí a případné řezy. Necommitne tím změny
do zdrojové podsestavy ani do rodičů cílového dokumentu.

`component.list` bez `recursive` vrací přímé komponenty. Rekurzivní varianta
vrací i vnořené výskyty a jejich přesné cesty. `total` udává celkový počet
nalezených položek, `items` respektuje výstupní limit 1 až 10000.
`component.get` vrací například `parent_path`, `owning_document`,
`source_document`, `name`, `kind`, `direct`, `visible`, `effective_visible`,
`suppressed`, `effective_suppressed`, `grounded`, `derived`, `placement` a počet
potomků. Souřadnice umístění jsou v mm, úhly ve stupních, v lokálním rámci
bezprostředního vlastníka.

Přímý výskyt má navíc uloženou zdrojovou cestu, vazbové reference, zámky,
uložený objem v mm³ a plochu v mm². Řádek vazby používá pro `offset` mm,
u `plane_angle` stupně. Vnořený snímek neobsahuje celý původní dokument;
neposkytuje proto údaje, které v něm uložené nejsou.

Dotazy čtou vypočtený/persistovaný snímek cílové sestavy. Nečtou novější otevřené
zdroje, neotvírají soubory a nevolají OCCT. Fungují i po zavření zdrojů nebo
odstranění jejich souborů. Změnu zdroje do existujícího výskytu přenáší výslovná
regenerace sestavy. Skrytí či potlačení předka se promítne pouze do odpovídající
větve výskytů, nikoli do ostatních výskytů stejného zdroje.

Před vložením se ověřuje cyklus přes podsestavy a externí reference skic,
včetně vložených profilů a řezů. Otevřené dokumenty mají přednost před soubory.
Uzavřené závislosti se čtou podle uložených zdrojových cest a kontrolují se jejich
ID; načtené pomocné dokumenty se neotevírají v uživatelském Workspace.
Pokud chybí cesta externího zdroje, musí být tento zdroj před vložením otevřený,
aby šlo jeho závislosti ověřit. Hloubka kontroly je omezena na 256 dokumentů;
pomocné načtené dokumenty se po kontrole jednotlivé větve uvolní.

Úspěšné vložení má jediný krok Undo. Před commitem se na soukromém návrhu
ověří také fyzikální relace. Chyba nesmí změnit revizi, generaci dat ani seznam
otevřených dokumentů. Název je jednořádkový, bez okolních mezer, 1 až 256 bajtů.
Uložení je výslovné příkazem `save`.

## Ověření

Modelová sada ověřuje opakované výskyty a sdílení snímku, hierarchii a viditelnost,
Undo/Redo, nativní uložení, dotazy bez zdrojových souborů, izolaci rodičů,
cyklus přes uzavřenou podsestavu i externí referenci a dělení nulou při vložení.
Workspace a importní regrese prošly společně **3/3** (1,39 s).
Skutečné CLI procesy uloží a znovu načtou sestavu opakovaných dílů; GUI test
použije původní menu vložení a jeho vlastnosti. Integrační sada prošla **4/4**
(17,30 s), `build/component-integration-tests.log`.

Celá Windows Release sada prošla **77/77** (402,70 s),
`build/component-full-tests.log`.

## Otevření zdroje

`component.open` přijímá přesnou cestu z `component.list/get`; výchozím
vlastníkem je zobrazená sestava. Otevře pouze vybraný zdroj, nikoli všechny
mezilehlé sestavy. U odvozené kopie dohledá její původní zdroj. Výsledek
obsahuje `document`, `path`, přesměrovanou `source_instance_path` a `opened`
(příznak nově načteného dokumentu). Zdroj se aktivuje na samostatné kartě.

Již otevřený zdroj se používá v současném stavu bez čtení souboru, takže
nezmizí neuložené změny. U uzavřeného zdroje se kontroluje ID i druh dokumentu;
soubor jiného dílu pod stejnou cestou se odmítne. Změna vlastnící sestavy, její
zavření a opětovné otevření nebo přepnutí kontextu během čtení výsledek zneplatní.
Čtení používá uložená data bez regenerace zdroje i rodičů. GUI kontextová akce
Otevřít sdílí stejnou operaci; CLI zatím zachovává obecnou ochranu aktivního
vnořeného editačního kontextu uvedenou výše.

Regrese otevírání zdrojů prošly **5/5** (27,35 s),
`build/component-source-integration-tests.log`. Zahrnují skutečné CLI a GUI
menu vnořeného dílu, zachování změn otevřeného zdroje, chybnou identitu souboru,
přesměrování kopie a změny Workspace během čtení.

## Závislosti před odstraněním

`component.dependencies` přijímá `instance_path` bezprostřední komponenty
vlastnící sestavy. Pro vnořený díl dotazujte jeho vlastnící sestavu přes
`document` a cestu relativní k ní; nadřazená sestava nesmí převzít vlastnictví
vnitřního dílu. Zdrojové jméno ani ID dílu nejsou identitou jeho výskytu.

Výsledek obsahuje `blocked` a tři seznamy stabilních ID:

- `placement_components`: komponenty, jejichž uložené řádky vazeb používají
  dotazovaný výskyt na některé straně;
- `dependent_components`: jiné komponenty s uloženou závislostí na výskytu;
- `sketches`: skici vlastnící sestavy s externí referencí na tento výskyt.

Odkaz na vnitřní geometrii podsestavy se počítá jako použití této podsestavy.
Opakovaná použití se ve výsledku neopakují; jiný výskyt stejného zdroje své
závislosti nesdílí. Kontrola používá totožnou funkci jako dosavadní GUI mazání.
`blocked: false` znamená absenci těchto překážek; není to příkaz k odstranění
ani záruka úspěchu následného výpočtu řezů. Dosavadní mazání, řešení vazeb a
přepočet řezů se tímto krokem nemění. Dotaz neotevírá soubory, nevolá OCCT,
nepřepočítává vazby a nemění historii.

```json
{"command":"component.dependencies","arguments":{"document":"VLASTNICI_SESTAVA","instance_path":"CESTA_Z_COMPONENT_LIST"}}
```

Ověřeno **4/4** modelových, procesových, katalogových a překladových testů
(7,76 s), `build/component-dependencies-tests.log`, a **2/2** testů konzole
hlavního GUI a aktualizace sestavy (18,48 s),
`build/component-dependencies-gui-tests.log`. Pokryté jsou tři druhy překážek,
přesné opakované výskyty, duplicity, vlastnící dokument, odmítnutí vnořené
cesty a chybějícího výskytu, zachování revize a dotazy bez zdrojových souborů.
GUI bylo kvůli běžícímu uživatelskému CADu ověřeno pomocí samostatně slinkované
kopie `build/cpp-windows-release/zima-cad-component-validation.exe` ze stejných
aktuálních CMake objektů a knihoven. Běžný spouštěcí soubor se nepřepisoval.
