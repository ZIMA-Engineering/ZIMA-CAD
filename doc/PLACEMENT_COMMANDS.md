# Sdílené umístění v GUI a příkazech

Přesun společné editace umístění a výpočetní transakce z GUI do modelové
vrstvy výslovně schválil uživatel v tomto úkolu odpovědí „ano povoluji.“.
Pravidla řešení referencí v nativním modelu se nemění. Příkaz a inline editace
ve View používají společné přiřazení číselných hodnot; vlastnosti konstrukcí
a inline editace sdílejí také potvrzovací transakci.

## Příkazy

```text
placement.get <object-ID>
```

```json
{"command":"placement.set","arguments":{"object":"<feature-ID>","values":{"x":12.5,"y":-3,"rotation_z":90}}}
{"command":"placement.set","arguments":{"object":"<body-ID>","values":{"reference_offset:2":25}}}
```

`get` přijímá `object` a volitelně `document`. Čte aktuální uložené umístění
bez OCCT, přepočtu referencí, načtení závislostí a změny aktivace. Výsledek
obsahuje `kind`, vlastnící `body`, `coordinate_system`, `coordinate_owner`,
`placement`, `revision`, délkovou jednotku mm a úhlovou jednotku degrees.
Pole `placement` používá existující nativní pojmenování: `x/y/z`,
`rotation_x/y/z`, `absolute_rotation_x/y/z`, `rotation_offset_x/y/z`,
reference, zámky a stav platnosti. U konstrukcí jsou zámky umístění převedeny
z prefixu `placement:` na stejné klíče jako u těles a prvků.

`set` přijímá `object`, neprázdný objekt `values` a volitelně `document`.
Hodnoty musí být konečná čísla JSON. Podporované klíče jsou:

| Klíč | Význam a jednotka |
| --- | --- |
| `x`, `y`, `z` | Volná souřadnice v místním rámci, mm |
| `rotation_x`, `rotation_y`, `rotation_z` | Hodnota odpovídajícího úhlového pole GUI, stupně |
| `reference_offset:N` | Offset N-té vyplněné poziční reference, mm |

Reference se číslují od nuly v pořadí vyplněných pozičních řádků; prázdné
řádky a řádky pouze pro orientaci se nepočítají. Jde o stejné adresování
parametru jako v existujících kótách View, nikoli pořadí geometrie OCCT.
Přesná identita reference je uložena jako vlastník, klíč a cesta výskytu.

Úhel se interpretuje stejně jako v Properties: osa řízená orientační
referencí upravuje korekci, volná osa absolutní úhel. Například samotná
FRONT reference řídí RX/RZ, zatímco RY zůstává absolutní. Příkaz nepřepisuje
souřadnici řízenou referencí ani zamčenou hodnotu (včetně samostatného
zámku aktivní korekce z Properties); v takovém případě upravte
příslušný odemčený referenční offset. Odemykání, přidání a výměna referencí
nejsou součástí této etapy.

Výchozí těleso je připojeno třemi rovinami k počátku Partu: offsety 0/1/2
odpovídají Z/Y/X. Orientaci řídí samostatné FRONT/TOP reference. Proto jeho
přímé `x/y/z` nejsou volná pole. Tato pravidla jsou stejná jako v GUI.

## Transakce a rámce

Jedno `set` mění všechny hodnoty společně. Neplatný klíč, zámek, řízená
souřadnice nebo odmítnutý výpočet nezanechá první část změny v dokumentu.
Úspěšná změna vytvoří jednu Undo transakci; shodné hodnoty vrátí
`changed: false` bez výpočtu a nové historie. Upravovaný dokument musí být
aktivní a bez rozpracovaného editačního dialogu. Modelovací prvek nebo
konstrukce v Partu vyžaduje aktivní vlastnící těleso; odvozené těleso je
řízené svou definicí a přímá editace je odmítnuta.

Souřadnice tělesa jsou v Partu, jeho prvku/konstrukce v tělese a bodu 3D
křivky v křivce. Společný zdroj referenční geometrie vyjadřuje reference ve
stejném rámci jako upravovaný objekt. U konstrukčních bodů tím sjednocuje
inline kontrolu volných os s dosavadními zobrazovanými referencemi i při
otočeném tělese nebo křivce.

Změna tělesa nebo prvku výslovně vypočítá Part přes dosavadní řešení referencí.
Zachová se přenos offsetu vlastněného profilu a kontrola přesných výpočetních
vstupů ShaftThread. Změna konstrukce používá původní transakci Properties:
vyřeší konstrukční reference a obnoví externí reference skic, přičemž zachová
poslední vypočtené těleso. Přepočet navázaných těles nebo vazeb nadřazených
sestav neprobíhá skrytě při čtení, aktivaci ani přepnutí tabu.

Podporovány jsou tělesa a modelovací kontejnery Partu, samostatné konstrukce
Partu/Assembly a jejich vnořené body. Vložené dráhy uvnitř modelovacích prvků,
Assembly komponenty, řezy a změna samotných referencí zůstávají další etapou.
Nativní formát ani start Part/Assembly šablony se nemění.


## Ověření

Modelový test používá nezávislé očekávané meze kvádru po otočení o 90 stupňů,
posunu prvku a následném posunu/otočení vlastnícího tělesa; objem zůstává
6000 mm³. Ověřuje jednu Undo transakci pro více hodnot, odmítnutí částečného
zápisu při zamčené další hodnotě, chybná čísla/indexy, chybějící reference,
aktivní těleso a zachování původního vypočteného tvaru při úpravě konstrukce.
Samotná FRONT reference ověřuje oddělené absolutní RY a korekční RX/RZ,
včetně samostatného zámku korekce. V otočené křivce musí rovina XZ tělesa
fixovat lokální X=-60 mm a ponechat lokální Y volné. Nativní Part/Assembly,
skutečný proces CLI a obousměrná konzole/Properties ověřují stejná data.

První úplná Windows Release regrese prošla **87/88** (382,41 s),
`build/placement-full-tests.log`. Selhal nový test konzole, který hledal
souřadnicové pole pod obecným názvem, přestože ho dialog přejmenovává.
Další běh **8/9** (66,87 s), `build/placement-final-tests.log`, potvrdil ostatní
cesty po odstranění nadbytečného kopírování dokumentu. Test pole X nyní
používá jeho skutečný sémantický zámek `valueLock:placement:x`.
Dodatečná ochrana korekčního zámku prošla cíleně **1/1** (0,17 s),
`build/placement-lock-tests.log`.


Závěrečná dotčená sada prošla **9/9** (81,10 s),
`build/placement-verified-tests.log`: umístění, konstrukční dotazy, tělesa,
katalog, skutečný CLI proces, konzole GUI, inline kóty, aktualizace sestav
a vlastněné profily. Odpovídá `build/placement-verified-build.log` a
`build/placement-verified-gui-link.log`. Po tomto běhu se produkční kód neměnil.

GUI adaptéry už nekopírují celý Part jen kvůli návrhu jednoho konstrukčního
objektu; kopie dokumentu vzniká až uvnitř společné potvrzovací transakce.
Konstrukční referenční geometrie se nepřipravuje při editaci jiného druhu
parametru, který touto větví jen prochází.

Po uživatelském zavření CADu je dokončené i běžné Windows Release sestavení
`zima-cad-cpp.exe` a `zima-cad-cli.exe` (`build/placement-normal-build.log`).
Ověření běžného EXE prošlo **3/3** (29,51 s): start samostatných instancí,
GUI konzole a skutečný CLI proces (`build/placement-normal-tests.log`).
Předchozí alternativní testovací EXE už pro spuštění této etapy není potřebné.
Jde o místní vývojové sestavení, nikoli distribuční balíček.


## Výslovný souhlas se sdíleným zadáváním referencí (2026-09-13)

Uživatel výslovně schválil přesun datové části
`ContainerPlacementSection::set_reference` do společné funkce pro GUI a CLI
odpovědí „Ano, schvaluji tento přesun“. Souhlas se týká zachování rozdělení
pozičních polí a FRONT/TOP, kontroly duplicit, naměřené zamčené vzdálenosti
a automatického doplnění orientace. Platí pro všechny dialogy používající
sekci a vyžaduje modelové i GUI regrese. Předchozí automatická kontrola
přesun odmítla; před tímto souhlasem nebyl kód změněn. Řešič umístění,
geometrické rovnice a perzistenční kontrakt se tím nemění. Push zůstává
odložený podle samostatného uživatelova pokynu.


## Společné přiřazení referenčního pole (2026-09-13)

`assign_placement_reference` v dokumentové vrstvě přebírá čistě datovou část
`ContainerPlacementSection::set_reference`. GUI si ponechává popisky,
překlady, zvýraznění a oznámení změny. Společná část pracuje pouze s návrhem
pozičních/orientačních řádků a zámků; nevytváří geometrii ani historii.
Zachovává přesnou cestu instance, nezávislé FRONT/TOP, případný automatický
přenos roviny do orientace a jednorázové zachycení zamčené vzdálenosti.

Nový test odhalil dosavadní chybu při opakovaném zadání stejné poziční
reference: kontrola automaticky doplněné orientace porovnávala zdroj až
po přesunu jeho řetězců do cílového řádku. Mohla proto doplnit stejnou
referenci znovu do TOP. Kontrola nyní drží původní trojici cesta/vlastník/klíč
před přesunem. Výměna zdroje tak nepřidá druhou automatickou kopii.
Tuto konkrétní chybu ověřuje datový i skutečný widgetový test.

První regrese ji reprodukovala (**0/1 za 0,13 s**); oprava prošla
**1/1 za 0,12 s**. Obě aplikace i testovací programy se sestavily a širší
regrese prošla **11/11 za 90,45 s**, včetně vlastností, Windows zámků,
modelových umístění, konstrukcí, konzole a referencí vložených profilů.
Logy: `build/placement-reference-assignment-tests.log`,
`build/placement-reference-assignment-fixed-tests.log`,
`build/placement-reference-assignment-integration-build.log`,
`build/placement-reference-assignment-integration-tests.log`.

Tato etapa připravuje společnou datovou cestu; samostatný příkaz zadávání
referencí ještě nepřidává. Nativní formát se nemění.

## Přiřazení reference

`placement.reference.set` doplňuje společný vstup pro Body, samostatné
konstrukce a primitiva. Přesný rozsah, pravidla zdrojů a příklady:
[PLACEMENT_REFERENCE_COMMANDS.md](PLACEMENT_REFERENCE_COMMANDS.md).
