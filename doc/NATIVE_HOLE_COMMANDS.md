# Nativní Otvor v GUI a CLI

`hole.create`, `hole.get` a `hole.set` pracují s `FeatureKind::Hole`.
`opening.*` nadále obsluhuje samostatný současný Otvor `FeatureKind::Thread`
s katalogovými rozměry a závitovou plochou. Nejde o změnu formátu souboru.

## Příkazový kontrakt

```json
{"command":"hole.create","arguments":{"diameter_mm":10,"bore_length_mm":10,"placement":{"z":-20}}}
{"command":"hole.set","arguments":{"container":"<id>","diameter_mm":8,"entrance_chamfer_mm":1}}
{"command":"hole.get","arguments":{"container":"<id>"}}
```

- `type`: `plain`, `metric`, `pipe`, `whitworth`. Závitový typ zapíná závitový
  drát. Hladký typ může také výslovně mít `thread_enabled`; odpovídá GUI.
- Rozměry v mm: `diameter_mm`, `bore_length_mm`, `entrance_chamfer_mm`,
  `exit_chamfer_mm`, `thread_diameter_mm`, `thread_pitch_mm`, `thread_length_mm`.
  Úhel špičky je `drill_point_angle_degrees` ve stupních.
- Přepínače: `drill_point_enabled`, `exit_chamfer_enabled`, `thread_enabled`,
  `left_handed`. Špička vypne výstupní sražení stejně jako dialog.
- `bore_end`: `length`, `through_all`, `up_to`. `thread_end`: `length`, `through_all`;
  u závitu druhá volba kopíruje uloženou jmenovitou délku válcové části.
  Neukončuje závit na automaticky nalezené výstupní ploše průchozího vrtání.
- `name`, `placement`, `document`: společný kontrakt názvu, editovatelných
  rozměrů umístění a aktivního dokumentu. Číselná pole jsou JSON čísla.

Výsledek obsahuje parametry, ID kontejneru a prvku, vlastnící těleso,
identity tří vnitřních skic a kružnice, revizi a zámky. Změny vracejí `changed`.
Dotaz `get` čte také již uložené cílové reference bez výpočtu tělesa.
Nemění revizi ani alokaci vypočtených dat.

`set` vyžaduje změnový parametr. Shodné parametry nepřepočítávají model.
Zámky respektují stávající klíče, včetně `pitch` (alias `thread_pitch`).
Cílem musí být aktivní, zapisovatelné těleso Partu. GUI šablony, Assembly,
Drawing, cizí typ prvku a neaktivní těleso se odmítnou.

## Vlastní skici a transakce

`document::update_hole_profiles` upraví existující kružnici a body obou
axiálních profilů. Nevytváří náhradní identity ani neprochází OCCT.
Stejnou funkci používá dialog při přípravě hodnot i `workspace::commit_hole`.
Geometrii profilu připravuje v soukromé kopii. Vlastní axiální rámec má
kladnou hloubku ve směru vrtání; běžná XZ skica má kladnou druhou osu směrem
-Z a její slepé použití dříve posílalo sražení/špičku proti vrtání.
Sdílený solver umístění a obecné rámce skic se nemění.

Potvrzení ověří vlastnictví profilů, kružnici, body a konstrukční osy,
zachová identity, použije stávající FRONT normalizaci a explicitní výpočet
Partu. Publikace používá `commit_part_document`, včetně souhrnů externích
referencí. Neplatný návrh nezmění živý model ani Undo. Dialog používá stejnou
transakci pro vytvoření a pozdější úpravu. Nedotčená čísla zachovávají plnou
uloženou přesnost; Cancel nepotvrzuje a shodné OK nevytváří další historii.

## Meze této etapy

Cílové zakončení vrtání `up_to` je podporované podle níže uvedeného kontraktu.
Nativní závitový drát nadále přijímá jen `length` a `through_all`; toto
rozšíření nezavádí samostatnou cílovou referenci jeho délky.
Špička a výstupní sražení vyžadují pevnou délku vrtání; kombinace s průchozím
ukončením nebo cílem se odmítne, protože profil potřebuje skutečnou koncovou
hloubku. Závit této varianty je drát a neodebírá další objem.
Katalog a závitové plochy patří do `opening.*` / `shaft_thread.*`.

Přípony `.prtz`, `.asmz`, `.drwz` a struktura uložených polí zůstávají stejné;
nepřibyla externí geometrie ani povinná cache. Start šablony se touto etapou
nemění; překlady všech nových textů jsou v celém sledovaném `config`.

## Ověření

Vstupem je kvádr 40 × 40 × 40 mm a vlastní parametry otvoru. Prostředkem
jsou existující operace Extrusion a Revolution sloučené v jeden odečet.
Požadovaný výstup je platné těleso se stálými identitami a přesným objemem.
Nezávislá kontrola odebraného objemu:

- válcové vrtání: `pi * r^2 * L`;
- náběhové či výstupní sražení 45° o šířce w: navíc
  `pi * (r*w^2 + w^3/3)`;
- špička s vrcholovým úhlem a: navíc
  `pi * r^2 * (r/tan(a/2)) / 3`;
- průchozí vrtání kolmým kvádrem: délka rovná tloušťce kvádru;
- závitový drát nemění objem.

Regrese zahrnují úhly 60°, 118° a 150°, FRONT referenci, změnu průměru a
hloubky, oba způsoby sražení, tři druhy závitu, všech osm zámků,
neaktivní těleso, neplatné vstupy, Undo/Redo a nativní uložení i studený
výpočet. Procesový test vytváří a mění Hole v samostatné CLI aplikaci.
GUI test ověřuje strom, společné Properties, OK/Cancel, přesnou nedotčenou
hloubku a objem uloženého výsledku.

Výchozí regrese selhala na `unknown_command` (0/1; 0,10 s).
První implementace prošla 2/3 testů a objemová regrese odhalila obrácený
axiální rámec. Po jeho opravě prošel modelový test 1/1 za 0,34 s.
Úplné sestavení obou aplikací a testovacích programů prošlo.
Rozšířená integrační sada prošla **11/11 za 179,53 s**, včetně GUI konzole
(51,20 s), procesu CLI (22,32 s) a spuštění/překladů (95,23 s).
Doplňková kontrola obnovy všech vlastněných referencí prošla **1/1 za 0,22 s**.
Celá sada nyní obsahuje 121 testů; v této etapě byla spuštěna uvedená dotčená
podmnožina, nikoli všech 121. Katalog má **213 příkazů**.

Logy: `build/native-hole-integration-build.log`,
`build/native-hole-integration-tests.log`, `build/native-hole-owned-reference-tests.log`.


## Zakončení vrtání k původní ploše nebo rovině

```json
{"command":"hole.set","arguments":{"container":"<hole-id>","bore_end":"up_to","bore_targets":[{"owner":"<original-owner-id>","key":"<original-semantic-key>","label":"Cílová plocha"}]}}
```

`bore_targets` lze použít v `hole.create` i `hole.set`. Je to pole nejvýše
jedné reference; aktivní `up_to` vyžaduje právě jednu. Reference obsahuje
povinné texty `owner/key` a volitelně `kind` (`face` nebo `plane`), `label`
a prázdnou `instance_path`. Souřadnice, náhradní trojúhelníky ani výsledná
topologie Partu nejsou vstupem příkazu. Formát vstupu je společný s
`opening.*`; oba typy používají jeden parser.

Příprava pro GUI i CLI ověří původní geometrii a vlastníka před potvrzením.
Přípustné jsou předchozí objekty a dostupné počátky stejného Partu včetně
původního objektu jiného tělesa. Těleso se svými vlastními souřadnicemi
zůstává samostatné; cílová reference se převede existujícím kontraktem.
Sebereference, pozdější prvek, chybějící zdroj, bod, neprázdná cesta výskytu,
více cílů či podvržené odvozené souřadnice se odmítnou bez zápisu do historie.

Nativní výpočet obnoví aktivní datumovou rovinu z původních ZIMA dat.
Původní rovinná plocha solidu zůstává referencí na tento solid; její
geometrii při výslovném výpočtu nalezne OCCT v aktuálním původním tělese.
Nesmí se změnit na nezávislou datumovou rovinu jen kvůli svému rovinnému
tvaru. Změna zdrojového tělesa se tak uplatní i při studeném výpočtu nativního
souboru nebo použití uložené cache. Chybějící aktivní cíl nesmí použít staré
souřadnice jako platnou náhradu.

Obnova cílových dat používá společnou funkci pro Opening a nativní Hole.
Uložené souřadnice i identity nedostupného cíle se zachovají pro opravu,
ale aktivní výpočet s chybějícím cílem selže. Neaktivní uložený cíl
neovlivňuje pevnou délku ani průchozí vrtání. Opakované přiřazení téže
normalizované reference nepřepočítává těleso a nepřidává historii.

Tato etapa mění pouze cílové zakončení vrtání. Nativní Hole má závitový
drát s uloženou jmenovitou délkou; nezavádíme mu novou cílovou vlastnost.
Zakončení závitové plochy u samostatného `opening.*` popisuje
[OPENING_COMMANDS.md](OPENING_COMMANDS.md). Špička vrtáku a výstupní sražení
stále vyžadují pevnou délku. Formát a přípony souborů ani start šablony se nemění.

Výchozí regrese selhala na chybějícím argumentu `bore_targets`
(**0/1 za 0,14 s**, `build/hole-target-baseline-tests.log`). Po implementaci
byla opravena chyba testovacího vstupu `box.set`, který vyžaduje rozměrový
řetězec. Rozšířená geometrická regrese prošla **1/1 za 0,49 s**
(`build/hole-target-expanded-tests.log`). Ověřuje nezávisle spočtené objemy,
změnu roviny a zdrojového solidu, původní identity, neplatné vstupy,
Undo/Redo, nativní uložení, různě umístěná tělesa a studený přírůstkový
výpočet. Dosavadní testy Hole a cílových referencí Opening také prošly.

Úplné sestavení obou aplikací a všech testovacích programů prošlo. Celá
Windows Release sada skončila **124/127 za 489,43 s**. Tři neúspěšné testy
obsahovaly neplatné vstupy: dva používaly `origin:plane:XY` místo platného
`origin:plane:xy`; starší geometrický test deklaroval neexistující rovinu
`datum:test-plane`. Nahrazení skutečnou rovinou počátku zachovalo nezávisle
ověřovaný objem. Po opravě těchto vstupů a novém sestavení prošlo všech
**7/7 dotčených testů za 68,55 s**, včetně CLI procesu, GUI konzole,
Hole a původních cílů Opening. V této opravě se produkční kód neměnil.

GUI regrese ověřuje přechod CLI → vlastnosti Hole → OK: cílová rovina,
identity vlastní skici i revize při nezměněném potvrzení zůstávají stejné.
Modelová sada navíc ověřuje ztrátu původní roviny, pozdější cíl a neaktivní
uloženou referenci. Logy: `build/hole-target-full-tests.log`,
`build/hole-target-fixtures-build.log`, `build/hole-target-fixtures-tests.log`.
Katalog zůstává na **226 příkazech**, celá sada obsahuje **127 testů**.


Dotazy a samostatné odstranění volitelných částí: [OPENING_COMPONENT_COMMANDS.md](OPENING_COMPONENT_COMMANDS.md).
