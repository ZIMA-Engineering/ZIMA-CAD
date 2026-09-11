# Dotazy na původní reference

`reference.list` a `reference.get` čtou uložené původní referenční pakety Partu
nebo sestavy. Nevolají OCCT, neaktivují dokument, neupravují výběr a nenačítají
novější obsah závislostí. Výsledná topologie tělesa se do původních referencí
nedoplňuje. Dotaz lze provést také při otevřené editaci.

## Rozhraní

| Příkaz | Argumenty |
| --- | --- |
| `reference.list` | `[owner kind instance_path document offset limit]` |
| `reference.get` | `kind owner key [instance_path document limit]` |

`kind` je `face`, `edge`, `point` nebo `axis`. Identitu tvoří druh, stabilní
`owner`, sémantický `key` a přesná neprůhledná `instance_path`. Cestu vrácenou
seznamem předejte beze změny; není to název komponenty ani cesta k souboru.
Dva výskyty stejného dílu mají různé cesty. Vynechaná cesta znamená prázdnou
cestu dokumentu, nikoli automatický výběr některého výskytu.

Pro volitelné filtry je vhodný JSON:

```json
{"command":"reference.list","arguments":{"kind":"face","owner":"<ID prvku>","limit":50}}
{"command":"reference.get","arguments":{"kind":"face","owner":"<ID prvku>","key":"<klíč ze seznamu>","instance_path":"<cesta ze seznamu>"}}
```

Seznam vrací `items`, `total`, `offset`, `more`, `next_offset`, ID dokumentu a
jeho revizi. Výchozí limit je 500, maximum 5000; index je nezáporný a nejvýše
100 000 000. Stránkování je stabilní pro tentýž stav dokumentu. Shodná reference
opakovaná u několika trojúhelníků se v seznamu objeví pouze jednou.

## Geometrie a přesnost

Detail vrací data v souřadnicích dotazovaného dokumentu, délky v mm a plochu
v mm². Obsahuje jen údaje skutečně přítomné v uloženém paketu:

- Plocha: trojúhelníkové vzorky, celkový počet trojúhelníků, naměřená plocha
  a přesná analytická rovina, válec nebo kužel, pokud jsou uložené. Úhel kužele
  `semi_angle_radians` je v radiánech.
- Hrana: fragmenty s uloženými body, naměřenou délkou, příznaky švu a nekonečné
  přímky. Přesný uložený spline obsahuje stupeň, póly, váhy a uzly.
- Bod: poloha. Osa: bod a směr.

`null` u analytického povrchu, délky nebo spline znamená, že údaj není v paketu.
Vzorky hrany se nevydávají za přesnou křivku a z trojúhelníků se zpětně neodhaduje
nová analytická plocha. Pro spline se neprovádí fit ani nová aproximace.

Detail má výchozí `limit:256`, povolené hodnoty 1–10000. U ploch limituje počet
trojúhelníků, u hran celkový počet vzorků a fragmentů. Úplná spline data se vrátí
jen pokud jejich součet pólů, vah a uzlů vejde do samostatného stejného limitu.
Omezení je výslovné v `samples_truncated`, `segments_truncated` a případném
`spline_omitted_by_limit`. Omezení odpovědi nemění model ani jeho přesnost.

Seznam prochází původní geometrii přímo ze sdílených snapshotů; nevytváří kopii
celé zobrazované sestavy. Pomocné počátky a konstrukce vznikají z uloženého
modelu stejnými datovými funkcemi jako pro GUI. Při dotazu na jedinou referenci
se transformují pouze její vracená data. Analytické povrchy mají ve vnořeném
snapshotu zdrojový rámec, zatímco vzorky a spline póly už zahrnují vnitřní
umístění; dotaz pro obě reprezentace používá odpovídající uložený řetězec výskytů.

Dotaz není příslib, že reference může být použita každým příkazem: vlastnictví,
pořadí historie, aktivní těleso a závislosti ověřuje přijímající operace. Lze číst
i zachovanou geometrii skrytého či potlačeného bezprostředního komponentu;
`source_occurrence_visible` a `source_occurrence_suppressed` popisují tento
bezprostřední komponent, nikoli stav každého vnitřního prvku.

## Ověření

Modelové testy pokrývají původní plochy kvádru, stránkování, omezení odpovědí,
původní body, přesné spline, vyloučení výsledné topologie, opakované a vnořené
výskyty včetně rotace, neměnnost revizí a snapshotů a čtení sestavy po změně
otevřeného zdrojového Partu. GUI ověřuje zachování potvrzeného výběru; CLI
proces ověřuje reference ze skutečně uloženého a znovu otevřeného `.prtz`.

Cílená Windows Release regrese: **4/4 prošlo**, 10,14 s,
`build/reference-integration-tests.log`. GUI a CLI jsou sestavené ze stejného
zdroje (`build/reference-integration-build.log`). Předchozí celá regrese historie
prošla **61/61**; tato etapa přidala samostatný test čtení referencí a ověřila
všechny dotčené příkazové adaptéry. Nespouští ani nemění žádný geometrický výpočet.
