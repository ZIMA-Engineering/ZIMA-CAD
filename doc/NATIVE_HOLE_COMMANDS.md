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
- `bore_end`: `length`, `through_all`. `thread_end`: `length`, `through_all`;
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

CLI zatím nenastavuje cíle `up_to`; vrací `unsupported_end_condition`.
Zůstává práce na společné přípravě, obnově a ověření cílových referencí.
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
