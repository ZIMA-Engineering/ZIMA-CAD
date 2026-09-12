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
3D dráhy uvnitř parametrických modelovacích prvků, tvorba a změny geometrie 3D křivek,
mazání a zadávání referencí zůstávají dalšími etapami. Tvorbu bodů, os a rovin
a obecné vlastnosti popisuje následující část. Číselné umístění
také zpřístupňují [placement.get/set](PLACEMENT_COMMANDS.md). Nativní schéma ani start šablony se nemění.
Konstrukční příkazy používají společnou transakci Vlastností a nativní řešení referencí.

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

Původní ověření používalo alternativní testovací GUI kvůli běžícímu CADu.
Po jeho zavření jsou běžné GUI i CLI sestavené a prošly závěrečnou kontrolou
startu a konzole **3/3**, viz [PLACEMENT_COMMANDS.md](PLACEMENT_COMMANDS.md).
Nejde o distribuční balíček.


## Tvorba a změna vlastností

`construction.create` vytváří absolutní `point`, `axis` nebo `plane`.
`construction.set` upravuje uloženou konstrukci podle jejího ID; zachovává
identitu, rodiče a reference. Oba příkazy používají potvrzovací transakci
stejného okna Vlastnosti jako GUI. V Partu vkládá tvorba konstrukci na aktivní
pozici historie aktivního tělesa, v Assembly do jejího vlastního dokumentu.

| Argument | Tvorba | Změna a význam |
| --- | --- | --- |
| `kind` | Povinný: `point`, `axis`, `plane` | Druh existující konstrukce se nemění |
| `construction` | ID přidělí model | Povinné ID existujícího kontejneru |
| `name` | Povinný neprázdný název | Volitelné přejmenování |
| `values` | Volitelný objekt čísel | Stejné klíče, jednotky a omezení jako `placement.set` |
| `direction_axis` | Pro osu: `x`, `y`, `z`; výchozí `y` | Vybraná lokální osa |
| `display_size_mm` | Pro osu: výchozí 100 mm | Délka zobrazení od 0,001 do 1 000 000 mm |
| `base_plane` | Pro rovinu: `xy`, `xz`, `yz`; výchozí `yz` | Rovina lokálního počátku |
| `offset_mm` | Pro rovinu: výchozí 0 mm | Odsazení entity podél normály, ±1 000 000 mm |
| `document` | Volitelný aktivní dokument | Jiný než aktivní dokument je odmítnut |

Rozšířené argumenty posílejte v JSON. Textový zápis používá poziční argumenty,
například `construction.create point "Měřicí bod"`. Volitelné argumenty
nepoužívají syntaxi `klíč=hodnota`.

```json
{"command":"construction.create","arguments":{"kind":"plane","name":"Montážní rovina","base_plane":"xy","offset_mm":12.5,"values":{"x":10,"rotation_x":90}}}
{"command":"construction.set","arguments":{"construction":"ID_Z_PŘEDCHOZÍHO_VÝSLEDKU","name":"Montážní rovina 2","offset_mm":15}}
```

Výsledek odpovídá `construction.get` a přidává `changed`. Změna více polí
je jedna Undo transakce. Chybný parametr, nevhodný druh vlastnosti, zamčená
hodnota nebo nevyřešitelná reference odmítne celý návrh. Shodné hodnoty
vracejí `changed: false` bez změny historie a cache. Zámky délek a odsazení
z Vlastností i zámky/omezení umístění se respektují; příkazy je neodemykají.
Změnu umístění lze přidat do stejné transakce přes `values`.

Vlastnosti platí v lokálním rámci uvedeném ve výsledku. Odsazení roviny
posouvá její entitu; neposouvá počátek kontejneru. Směr osy před potvrzením
připravuje stejná čistá funkce pro GUI i příkaz, v pořadí rotací X, Y, Z.
Nativní solver pak vyřeší případné geometrické reference. Tuto přípravu
používá i stávající `placement.set` pro samostatné osy.

Editace vyžaduje aktivní vlastnící těleso; odvozené těleso je chráněné.
Rozpracovaný editační dialog blokuje mutaci z konzole. Konstrukce se řeší
bez OCCT výpočtu tělesa a zachovávají poslední vypočtenou geometrii, stejně
jako dosavadní Vlastnosti. Navázaná tělesa a vazby se přepočítají výslovnou
regenerací. Nativní formáty a start Part/Assembly šablony se nemění.

Název a umístění lze upravit také u existující samostatné 3D křivky a jejích
bodů. Tato etapa netvoří nové 3D křivky, nevyměňuje reference ani seznam bodů,
nemění tečny/zaoblení a nemaže konstrukce; to je navazující rozsah.

Při čtení nativních konstrukcí se nyní obnoví i odvozená poloha entity
roviny z jejího uloženého počátku, normály a odsazení. Dříve ji samotný
deserializátor ponechal nulovou. Obnova neřeší reference, nepřepisuje
jejich diagnostiku a nevolá OCCT; platí také pro poslední polohu roviny
s chybějící referencí. Formát souboru se nemění.

Editace bodu existující 3D křivky ověřuje před potvrzením celou vlastnící
dráhu stejnou nativní kontrolou jako dialog. Přesunutí bodu na sousední bod
a jiné neplatné definice dráhy nezanechají částečný zápis ani přes
`construction.set`, ani přes společné `placement.set`.


## Ověření tvorby a vlastností

Celá Windows Release sada prošla **88/88** (389,12 s),
`build/construction-edit-full-tests.log`; sestavení všech programů odpovídá
`build/construction-edit-full-build.log` a závěrečnému
`build/construction-edit-final-build.log`. GUI i CLI jsou běžné spouštěcí
programy, není potřeba alternativní testovací EXE. Po úspěšném běhu se
produkční kód neměnil.

Modelové regrese nezávisle kontrolují směry os po otočení kolem Y/Z,
polohu odsazené roviny po změně lokální roviny a rotace i bod v tělese
otočeném o 90 stupňů. Hlídají identitu vypočteného tělesa, jednu Undo
transakci, no-op, typy/rozsahy argumentů, zámky, platné i ztracené reference,
aktivní těleso/dokument, rozpracovanou editaci a bod 3D křivky včetně
odmítnutí kolapsu na souseda. Uložení/načtení Partu a Assembly ověřuje
identitu, parametry a poslední polohu roviny i při neplatné referenci.

Skutečný CLI proces tvoří/ukládá/otevírá rovinu, edituje ji s Undo/Redo
přes JSON a tvoří osu přes textový stdin. GUI konzole vytvoří rovinu,
otvírá stejné Vlastnosti a ověřuje Cancel, OK, blokování souběžné změny,
Undo a pozdější zobrazení hodnoty nastavené příkazem. Úplná sada zahrnuje
i původní modelování, výkresy, skicář, import/export, překlady a dialogy.
