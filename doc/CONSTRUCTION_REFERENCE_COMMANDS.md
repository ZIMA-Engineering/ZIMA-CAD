# Původní reference konstrukčního kontejneru přes CLI

`construction.reference.set` přiřadí původní referenci existujícímu nezávislému
bodu, ose, rovině nebo celému kontejneru 3D křivky. Používá schválený společný
helper pozičních polí a FRONT/TOP a stejnou transakci `commit_construction`
jako OK konstrukčních Vlastností. Tělesa se při tomto příkazu nepřepočítávají.

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

Příkaz je nyní určen pro nezávislé kořenové kontejnery. Reference bodů uvnitř
3D křivky, vložených drah, umístění ostatních prvků a řezů navazují dalšími
etapami. Odstranění reference není součástí tohoto příkazu.

## Ověření (2026-09-14)

Modelový test Part/Assembly ověřuje bod, osu, rovinu, kořenovou 3D křivku,
zámek vzdálenosti, FRONT/TOP, chyby bez změny, Undo/Redo a nativní uložení.
Samostatný test opakovaných a vnořených výskytů prošel **1/1 za 0,26 s**;
výměna stejné plochy mezi výskyty posune konstrukci o nezávisle známých 30 mm.
Související integrace prošla **11/11 za 106,48 s**, včetně skutečného CLI
procesu a GUI Vlastností s Cancel/OK, Undo/Redo a uložením. Kontroluje se
zachování vypočtených těles a jejich otisků. Obě aplikace a všechny cíle
jsou sestavené.
