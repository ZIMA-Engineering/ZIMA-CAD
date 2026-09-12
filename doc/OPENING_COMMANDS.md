# Příkazy Otvoru

`opening.create/get/set` ovládají současný příkaz **Otvor**: hladký nebo
závitový otvor, předvrtání, vstupní sražení a vrtací špičku. V nativním modelu
jde o `FeatureKind::Thread`. Závit má technologickou plochu; příkaz neřeže
závitovou šroubovici do B-Rep. Jde o stejné chování jako v GUI.

`get` je čtení bez výpočtu. Tvorba a editace používají společné OK s dialogem:
validují, vyřeší dosavadní umístění, vypočítají těleso a vytvoří jeden krok Undo.
Chyba nic nepotvrdí. Numericky nezměněný prvek nepřepočítávají.

## Příklad

```text
new part otvor
box.create 40 40 40
```

```json
{"command":"opening.create","arguments":{"type":"metric","designation":"M10","bore_length_mm":20,"thread_length_mm":10,"chamfer_enabled":false,"drill_point_enabled":false,"placement":{"z":-20}}}
```

Z odpovědi použijte stabilní `container`:

```json
{"command":"opening.get","arguments":{"container":"ID-OTVORU"}}
{"command":"opening.set","arguments":{"container":"ID-OTVORU","bore_length_mm":25}}
```

## Parametry

`create` vkládá nový otvor do aktivního tělesa; `set` vyžaduje `container`.
Oba přijímají nepovinný `document` jako kontrolu cílového dokumentu a stejné
vlastnosti. Číselné hodnoty JSON jsou čísla; délky jsou v mm a úhly ve stupních.

| Parametr | Význam |
| --- | --- |
| `name` | Neprázdný název prvku |
| `type` | `plain`, `metric`, `whitworth`, `pipe` |
| `designation` | Přesná velikost z `thread.catalog`, například `M10` nebo `G 1/2` |
| `nominal_diameter_mm` | Průměr hladkého otvoru; u závitu jej určuje katalog |
| `custom_bore_diameter` | Povolení vlastního průměru předvrtání |
| `bore_diameter_mm` | Vlastní průměr předvrtání; vyžaduje zapnutou předchozí volbu |
| `bore_length_mm` | Délka válcové části, bez vrtací špičky |
| `thread_length_mm` | Délka válce závitu od počátku, bez výběhu |
| `bore_end` | `length`, `through_all`, případně `up_to` s již uloženým cílem |
| `thread_end` | `length`, případně `up_to` s již uloženým cílem |
| `direction` | `forward` nebo `reverse` |
| `chamfer_enabled`, `chamfer_depth_mm`, `chamfer_angle_degrees` | Vstupní sražení; úhel je vrcholový |
| `drill_point_enabled`, `drill_point_angle_degrees` | Vrtací špička slepého otvoru; úhel je vrcholový |
| `runout_pitch_factor` | Délka výběhu jako násobek stoupání, 0–100 |
| `left_handed` | Levý závit |
| `placement` | Číselný patch existujícího umístění: `x/y/z`, `rotation_x/y/z` a dostupná referenční odsazení |

Délky mají rozsah 0,001–1000000 mm, oba úhly 1–179°. Zamčené rozměry nelze
změnit ani nepřímo volbou jiné velikosti. Průměr předvrtání musí být menší než
jmenovitý průměr závitu. Závit včetně výběhu se musí vejít do slepého otvoru.
Změna zakončení na průchozí nebo Až k vypne špičku stejně jako dialog; její
výslovné zapnutí v takové kombinaci se odmítne.

Výběr katalogové velikosti přebírá jmenovitý průměr, stoupání a automatický
průměr předvrtání; vlastní předvrtání zachová. U délkového zakončení může
zvětšit výchozí hloubku pro závit s výběhem. Výslovné `bore_length_mm` však
určuje konečnou hloubku a musí projít kontrolou. Samotná změna délky závitu
hloubku sama nezvětšuje. Při přepnutí normy bez označení se zachová existující
velikost, pokud existuje; jinak se vybere nejbližší jmenovitý průměr stejně
jako v GUI. Konkrétní výsledek vždy vrátí odpověď.

`get` vrací také `feature`, `body`, `revision`, zámky a uložené
`bore_targets/thread_targets`. `bore_diameter_mm` je uložené předvrtání závitu;
u hladkého otvoru skutečný průměr určuje `nominal_diameter_mm`.

## Hranice této etapy

Příkazové přidávání a výměna původních cílových referencí bude následovat.
Tato etapa je čte a zachovává, ale nenahrazuje je souřadnicemi ani výslednou
plochou tělesa. Příkazová editace vnějšího závitu, samostatné vrtací špičky
a samostatného nativního Hole patří do dalších kroků celkového CLI.
Nativní formát a start šablony se nemění.

## Ověření

Modelová regrese porovnává nezávislé objemy hladkého, metrického, Whitworthova
a trubkového otvoru. Kontroluje tři úhly sražení, vrtací špičku, směr, průchozí
otvor, katalogové/vlastní předvrtání, neplatné vstupy, zámky, přesné Undo,
identity profilů a nativní uložení i studený výpočet. Procesový test tvoří a
mění otvor skutečným CLI. GUI scénář přechází z CLI tvorby do Vlastností,
kontroluje pending změny, OK/Cancel, Undo a další katalogovou velikost.

Úplné sestavení nejprve odhalilo chybějící závislost Part harnessu na Workspace.
Čisté pravidlo katalogového výběru bylo přesunuto do `document_core`, který
harness již používá. Sestavení obou programů a související sada poté prošly
**11/11** (66,97 s), `build/opening-command-full-build.log`,
`build/opening-command-related-tests.log`. Po doplnění překladu názvu Otvor,
Undo tvorby, nepřímého zámku katalogu a smyslu závitu prošlo **3/3** (16,96 s),
`build/opening-command-final-tests.log`.

Závěrečná kontrola transakce přesunula také existující volání normalizace
FRONT z GUI do společného potvrzení. Algoritmus normalizace ani řešení
umístění se nemění. Modelový test navíc kontroluje otvor vázaný na rovinu
původního počátku a přesné obnovení jeho umístění přes Undo.

Po tomto posledním přesunu prošlo znovu sestavení obou programů a **5/5**
(57,25 s), `build/opening-command-placement-build.log`,
`build/opening-command-placement-tests.log`: Otvor, umístění, profily,
CLI proces a skutečné GUI potvrzení. Katalog má **177 příkazů**.
