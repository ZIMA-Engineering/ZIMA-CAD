# Konstrukční geometrie v konzoli a CLI

`construction.list` a `construction.get` čtou současný model stejného otevřeného
Partu nebo Assembly, který používají vlastnosti konstrukcí. Jsou to dotazy:
nespouštějí OCCT, řešení referencí, načítání závislostí ani regeneraci a nemění
historii, aktivaci, výběr či vypočtenou geometrii. Mohou se použít i při otevřeném
editačním dialogu; vracejí potvrzený model, nikoli nepotvrzený návrh dialogu.

## Příkazy

```text
construction.list
construction.list point
construction.get <construction-ID>
```

Volitelné pojmenované argumenty se zadávají JSONem:

```json
{"command":"construction.list","arguments":{"parent":"<curve-ID>","offset":0,"limit":100}}
{"command":"construction.get","arguments":{"construction":"<point-ID>","document":"<open-document-ID>"}}
```

`list` přijímá `kind` (`point`, `axis`, `plane`, `curve3d`), `parent`,
`document`, `offset` a `limit`. Bez filtrů zahrnuje konstrukce dokumentu a jejich
vlastní body. Filtr `parent` vybere pouze přímé děti: ID tělesa vrací jeho
konstrukce, ID 3D křivky vrací její body. Pořadí je uložené pořadí konstrukcí,
s body každé křivky bezprostředně za vlastníkem v pořadí dráhy; nejde o řazení
podle názvu ani projekci widgetů stromu.

Seznam vrací stabilní `construction`, `entity`, `entity_parent` a `origin`,
`kind`, `name`, `parent`, `body`, `parent_construction`, `reference_valid`,
`suppressed`, `parent_suppressed` a `child_count`. `parent_suppressed` značí
potlačení nadřazené konstrukce, nikoli skrytí tělesa nebo výskytu sestavy.
Seznam nekopíruje souřadnice, reference ani všechny body každé křivky.
`total`, `more` a `next_offset` umožňují pokračovat další stránkou.

`get` přijímá ID konstrukčního kontejneru nebo jeho vnořeného bodu, nikoli
ID jeho entity či počátku. Vrací navíc uložené souřadnice, natočení, zámky,
definici a přesné reference včetně cesty výskytu, klíče, offsetu a zámku.
Osa uvádí směr; rovina základní rovinu, pracovní offset a oddělenou polohu
vlastní rovinné entity. Křivka uvádí typ, přepínač zaoblení a ID svých bodů;
vnořený bod uvádí poloměr a řízení tečny. Neplatná reference zůstane neplatná
a dotaz vrátí poslední uložený stav bez pokusu o opravu.

`limit` má u obou příkazů výchozí hodnotu 500 a rozsah 1–5000. V `get` omezuje
zvlášť reference a ID dětí, s příznaky `references_truncated` a
`children_truncated`. Všechny děti lze stránkovat přes `list` s `parent`.
`offset` seznamu má rozsah 0–100000000. Změní-li se `revision` mezi stránkami,
klient má seznam načíst znovu. Velikosti a offsety musí být celá čísla.

## Souřadnice a vlastnictví

Hodnoty s příponou `_mm` jsou v milimetrech, `_degrees` ve stupních, nezávisle
na zobrazovacích jednotkách dokumentu. Reference používají existující nativní
pole `offset` v mm. `coordinate_system` a `coordinate_owner` rozlišují:

| Soustava | Vlastník |
| --- | --- |
| `document` | Dokument, například kořenová Assembly |
| `body` | Vlastnící těleso Partu |
| `parent_construction` | Nadřazená 3D křivka; body jsou v jejím lokálním rámci |

Dotazy nepřevádějí uložené souřadnice do soustavy View. Umístění tělesa lze
přečíst `body.get`, původní referenční geometrii přes `reference.get`.
Názvy nejsou identitou; dvě stejně pojmenované konstrukce se rozlišují ID.

Explicitní `document` může označit jiný již otevřený zdroj bez jeho aktivace.
Dotazy neprocházejí konstrukce vložených komponent ani neotevírají jejich
zdroje. Vnořený Part se čte přes jeho otevřený zdrojový dokument; jeho
opakované výskyty nejsou samostatnými vlastníky konstrukcí.

## Rozsah a ověření

Tato etapa pokrývá `document.constructions` a body jejich 3D křivek. Vložené
3D dráhy uvnitř parametrických modelovacích prvků, tvorba, obecné vlastnosti a mazání jsou další etapy. Číselné umístění
následně zpřístupňují [placement.get/set](PLACEMENT_COMMANDS.md). Nativní schéma ani start šablony se nemění.
Sdílený kód umístění zůstává beze změn.

Samostatný modelový test ověřuje všechny čtyři druhy, nezaměnitelnost
kontejneru/entity, vlastnictví tělesa i bodu, přesný lokální rámec při posunutém
a otočeném tělese, neplatnou referenci s poslední polohou, zámky, omezení
výstupu, chyby, neaktivní dokument a nativní uložení/načtení Partu i Assembly.
Kontrola revize, generace a identity vypočtené cache chrání čisté čtení.
Skutečný CLI proces čte Part přes JSON a Assembly přes textový stdin; konzole
GUI čte stejnou nativní geometrii a ověřuje nezměněný stav dokumentů.

Závěrečná sada prošla **6/6** (26,86 s),
`build/construction-query-final-tests.log`: konstrukční dotazy, katalog hostu,
skutečný CLI proces, původní reference, překlady a konzole GUI. Předchozí běh
měl 5/6; GUI test posílal prázdné argumenty jako JSON `null` místo objektu.
Opravena byla pouze tato testovací zpráva, následně prošla celá dotčená sada.

CLI a testy jsou sestavené běžným CMake postupem. Uživatelský CAD stále běžel;
GUI regrese používá `zima-cad-construction-validation.exe`, slinkovaný z
aktuálních objektů a knihoven. Běžný `zima-cad-cpp.exe` zůstává zamčený,
po zavření CADu zbývá jeho běžný link. Nejde o distribuční balíček.
