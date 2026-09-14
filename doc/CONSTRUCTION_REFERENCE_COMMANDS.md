# Původní reference konstrukčního kontejneru přes CLI

`construction.reference.set` přiřadí původní referenci existujícímu nezávislému
bodu, ose, rovině, celému kontejneru 3D křivky nebo jejímu vlastnímu bodu. Používá schválený společný
helper pozičních polí a FRONT/TOP a stejnou transakci `commit_construction`
jako OK konstrukčních Vlastností. U samostatných konstrukcí se tělesa
nepřepočítávají. Bod vlastněný Sweep3D potvrzuje celé tažení přes jeho
společnou transakci; viz [SWEEP_POINT_REFERENCES.md](SWEEP_POINT_REFERENCES.md).

```json
{"command":"construction.create","arguments":{"kind":"plane","name":"Navázaná rovina","base_plane":"xy"}}
{"command":"construction.reference.set","arguments":{"construction":"ID_KONSTRUKCE","index":0,"reference":{"owner":"ID_DOKUMENTU:origin","key":"origin:plane:xy"},"offset_mm":7}}
{"command":"construction.get","arguments":{"construction":"ID_KONSTRUKCE"}}
{"command":"placement.set","arguments":{"object":"ID_KONSTRUKCE","values":{"reference_offset:0":13}}}
```

`index` 0–2 označuje poziční pole, 3 znamená FRONT a 4 TOP. `reference`
obsahuje `owner`, `key` a případně `instance_path`. Jde o skutečné původní
identifikátory z uložené geometrie. Bod konstrukce používá svůj uložený
počátek (pole `origin` výsledku `construction.get`) a klíč `point`;
rovinná entita konstrukce používá pole `entity` a klíč `plane`.
Názvy ve stromu ani pořadová čísla ploch nejsou reference.

Part přijímá místní reference. V sestavě `instance_path` rozlišuje konkrétní
výskyt a je relativní ke zdrojové sestavě, která konstrukci vlastní. Nesmí
se zaměnit dvě instance stejného Partu ani ztratit vnořené úrovně cesty.
Lookup používá uloženou geometrii scény, bez OCCT a bez změn zdrojového Partu.

`offset_mm` je konečné podepsané odsazení rovinné poziční reference.
Orientace a reference bez odsazení přijímají jen nulu. `flip` zachovává
stávající pravidlo orientačního přetočení z GUI. `derive_orientation` je
výchozí true: rovinné poziční reference doplňují samostatná pole FRONT/TOP.
Poziční řádek sám nezískává další rotační význam. Po odebrání všech
translačních stupňů volnosti může další poziční pole určit směr podle
stávajícího kontraktu Vlastností.

Při náhradě zamčeného pozičního pole se zachová skutečná změřená vzdálenost.
Přechodné `measured_offset` se po přiřazení odstraní; trvalé `offset` a jeho
zámek zůstávají součástí reference. První rovinná reference roviny zvolí
základní rovinu XZ stejně jako GUI. Explicitní FRONT/TOP zůstávají nezávislé.

Vlastní a pozdější konstrukce, pozdější modelový prvek, chybějící zdroj,
duplicitní reference, neplatné pole nebo neřešitelné umístění se odmítnou
před změnou dokumentu. Neaktivní a odvozené těleso zachovává své ochrany.
Rozpracovaný GUI dialog příkaz zablokuje. `document` je volitelná ochrana
proti změně aktivní karty.

Jedna změna je jeden krok Undo. Bezezměnové přiřazení nepřidá historii;
`body_calculated` je false. Vrácené vlastnosti odpovídají `construction.get`
a navíc obsahují `changed`. Změny jsou uložené v současném `.prtz` / `.asmz`;
formát ani přípony se nemění.

Bod uvnitř samostatné 3D křivky se zadává svým vlastním ID, získaným z
`construction.get`. Souřadnice jsou místní vůči rodičovské křivce; vrácené
`coordinate_owner` je její ID. Rodičovský rám, bod, osa nebo rovina dřívějšího
bodu jsou přípustné zdroje. Výsledná hrana celé křivky, vlastní bod/rám ani
pozdější bod přípustné nejsou. Také reference na předcházející geometrii
Partu a přesné výskyty v Assembly zůstávají dostupné.

Při řešení se staré rámy vlastních bodů nejprve odstraní z pracovních
podkladů. Po vyřešení každého bodu se zveřejní celý jeho aktuální místní
rám (bod, osy a roviny) pro následující body. Stejný postup používá vložená
dráha Sweep3D. Chybějící zdroj ponechá poslední uloženou polohu a nastaví
neplatnost reference; nepoužije se starý rám pozdějšího bodu.

Příkazové přiřazení referencí bodům vložené dráhy je implementované.
Stejný příkaz přijímá ID vlastněného bodu; skutečná změna přepočítá tažení
a vrátí `body_calculated: true`. Hodnoty bodů se zadávají přes `sweep3d.set`
a úplný seznam `path.points`. Obecné `placement.get/set/reference.set`
zůstávají pro umístění tažení a samostatné konstrukce.
Odebrání existujících referencí pokrývá
[placement.reference.remove](PLACEMENT_REFERENCE_REMOVAL.md), včetně bodů
samostatných i vložených drah. Formát souborů se nemění.

## Ověření (2026-09-14)

Modelový test Part/Assembly ověřuje bod, osu, rovinu, kořenovou 3D křivku,
zámek vzdálenosti, FRONT/TOP, chyby bez změny, Undo/Redo a nativní uložení.
Samostatný test opakovaných a vnořených výskytů prošel **1/1 za 0,26 s**;
výměna stejné plochy mezi výskyty posune konstrukci o nezávisle známých 30 mm.
Související integrace prošla **11/11 za 106,48 s**, včetně skutečného CLI
procesu a GUI Vlastností s Cancel/OK, Undo/Redo a uložením. Kontroluje se
zachování vypočtených těles a jejich otisků. Obě aplikace a všechny cíle
jsou sestavené.


## Body křivek a oprava aktuálních rámů (2026-09-14)

Původní chybu reprodukoval test změny odsazení prvního bodu z 2 na 4 mm:
navázaný čtvrtý bod nesprávně zůstával na 2 mm. Opravený resolver předává
čerstvý úplný rám každého vyřešeného bodu dalším bodům. Ověření zahrnuje
řetězy přes bod, osy a roviny, natočení rodiče a dítěte, natočené těleso,
odmítnutí vlastních/dopředných zdrojů a zachování poslední polohy při
chybějící referenci. Pro vložený Sweep ověřuje skutečný objem válcového
tažení 4π√909 mm³, nativní uložení, načtení a opakované vyřešení.

GUI test upravuje dítě uvnitř dialogu křivky: Cancel vrací původní stav,
OK dítěte potvrzuje pouze návrh rodiče a až OK rodiče mění dokument.
Undo/Redo a uložení zachovávají místní souřadnice i původní identity.
Test opakovaných výskytů zahrnuje také dítě natočené křivky ve vnořené sestavě.

Obě aplikace a všechny testovací cíle jsou sestavené ve Windows Release.
Úplná regrese prošla **155/155 za 630,24 s**, včetně CLI procesu, GUI
konzole a hlavního pracovního okna. Katalog zůstává na **288 příkazech**;
rozšiřuje se působnost existujícího přiřazení. Logy:
`build/curve-reference-final-build.log` a `build/curve-reference-full-tests.log`.
