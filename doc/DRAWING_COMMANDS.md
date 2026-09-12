# Výkresy přes společné příkazy

Výkresové příkazy zpřístupňují listy, šablony, tvorbu a vlastnosti pohledů,
uložené reference a výslovnou regeneraci. Katalog má 151 příkazů. Dotazy na modelové anotace a Show/Erase jsou sdílené.
Další anotace, zdrojové styly šraf a příkazové exporty DXF/JPG
zatím nejsou kompletně pokryté.

| Příkaz | Argumenty | Výsledek |
| --- | --- | --- |
| `drawing.sheet.list` | `[document]` | Uložené listy v pořadí dokumentu |
| `drawing.sheet.get` | `sheet`, `[document]` | Parametry a počty vložených objektů |
| `drawing.sheet.create` | `[name]`, parametry níže, `[document]` | Nový list s vlastním stabilním ID |
| `drawing.sheet.set` | `sheet`, parametry níže, `[document]` | Částečná změna parametrů |
| `drawing.sheet.delete` | `sheet`, `[document]` | Odstranění listu s jeho pohledy a kótami |
| `drawing.frame.load` | `sheet`, `path`, `[document]` | Vložená geometrie souboru `.frmz` |
| `drawing.frame.clear` | `sheet`, `[document]` | Odstranění vloženého formátu |
| `drawing.title_block.load` | `sheet`, `path`, `[document]` | Vložená geometrie souboru `.tblz` |
| `drawing.title_block.clear` | `sheet`, `[document]` | Odstranění vloženého razítka |

`document` označuje otevřený výkres, výchozí je aktivní dokument. Změny vyžadují
aktivní výkres a uzavřenou předchozí editaci. `sheet` je ID z výsledku vytvoření
nebo seznamu listů, nikoli jméno či pořadové číslo. Dotazy vracejí uložená data,
nekopírují projekční geometrii a nepotřebují zdrojové soubory.

Parametry vytvoření a úpravy listu:

- `name`: neprázdný jednořádkový název, nejvýše 256 bajtů UTF-8.
- `format`: `A4`, `A3`, `A2`, `A1`, `A0`; rozměry v mm vrací dotaz.
- `projection`: `first_angle` nebo `third_angle`.
- `scale`: desetinné měřítko 0,001 až 1000; `0.5` znamená 1:2 a `2` znamená 2:1.
- `thick_line_mm`, `thin_line_mm`, `red_line_mm`: tloušťky 0,05 až 2 mm.
- `locale`: neprázdný jednořádkový kód jazyka, nejvýše 32 bajtů.

Vytvoření používá A4, první kvadrant, měřítko 1, tloušťky 0,5/0,25/0,7 mm
a jazyk `cs`. Neuvedené položky při editaci zůstávají beze změny. Identická
editace a odstranění již prázdné šablony nevytvoří krok historie.

```json
{"command":"drawing.sheet.create","arguments":{"name":"Detail","format":"A3","scale":2}}
{"command":"drawing.sheet.set","arguments":{"sheet":"ID-LISTU","scale":0.5,"locale":"cs"}}
{"command":"drawing.frame.load","arguments":{"sheet":"ID-LISTU","path":"config/formats/ZE-A3.frmz"}}
```

Měřítko listu se přenese pouze do pohledů se zapnutým `use_sheet_scale`.
U řezů se při této výslovné editaci obnoví projekce a šrafování z dostupné
vypočtené sítě; rozteč šraf zůstává v milimetrech papíru. Otevřený zdroj má
přednost před souborem a kontroluje se jeho identita. Není-li nutný zdroj
dostupný, neprovede se žádná část změny. Samotný zdroj ani jeho sestavy se
neregenerují, OCCT se pro změnu listu nepoužívá. Změna metody promítání mění
nastavení listu; existující projekční kamery aktualizuje výslovná regenerace
pohledů, stejně jako dosavadní GUI.

Změna formátu odstraní všechny části dosavadního formátu a razítka včetně
kružnic, obrázků a opakovaných oblastí. Pohledy a jejich kóty zachová.
Odstranění razítka zachová lokální parametry a uložené řádky BOM, které
nejsou jeho kresbou. Formát `.frmz` musí odpovídat formátu listu. Šablony se
vloží do `.drwz`; po uložení není původní soubor šablony nutný. Chybný import
ponechá původní list beze změny. Poslední list nelze odstranit; odmítne se také
smazání listu, na jehož pohled závisí pohled na jiném listu.

## Historie a GUI

Výkresová `DrawingState` má společné Undo/Redo dostupné v CLI i hlavním GUI.
Každý potvrzený krok má vlastní revizi; nová větev zruší Redo. Historie
uchovává nativní výkresové snímky bez opětovné projekce, načítání souborů nebo
OCCT. Přidělená čísla kót se neztrácejí při Undo a později se nepoužijí znovu.
Dokončení staršího asynchronního uložení nesmí označit mezitím změněný výkres
za uložený. Formát `.drwz` se nemění a historie není povinný vedlejší soubor.

GUI vytvoření a odstranění listu, dialog vlastností, spodní lišta, import i
odstranění formátu/razítka používají stejné modelové operace. Dialog při
neplatných hodnotách zůstane otevřený; Cancel nezapisuje změny. Otevřená
editace blokuje souběžné příkazové změny a hlavní GUI Undo/Redo. Zvětšovací
měřítko se ve spodní liště správně zobrazí například jako 2:1.

## Ověření

Cílená Windows Release sada prošla **7/7** (21,42 s),
`build/drawing-sheet-integration-tests.log`. Pokrývá skutečný CLI proces,
GUI dialog a historii, nativní uložení/znovunačtení, chyby importu a změny
formátu. Nezávislá geometrická kontrola ověřila rozteč šrafování 1,2 mm na
papíře při měřítku 2, zachování vlastního měřítka druhého pohledu a nedotčenou
revizi zdrojového dílu. Další testy ověřují zmizelý zdroj, nesprávnou identitu
souboru, přidělování čísel kót a uložení během změn historie.

Celá Windows Release sada této etapy prošla **79/79** (407,77 s),
`build/drawing-sheet-full-tests.log`, včetně obou výsledných programů GUI a CLI.

## Pohledy, reference a regenerace

| Příkaz | Argumenty | Význam |
| --- | --- | --- |
| `drawing.view.list` | `[sheet]`, `[limit]`, `[document]` | Uložené pohledy a jejich dostupná metadata |
| `drawing.view.get` | `view`, `[document]` | Jeden uložený pohled včetně skutečné kamery |
| `drawing.view.references` | `view`, `[kind]`, `[limit]`, `[document]` | Původní měřicí křivky a body uložené s projekcí |
| `drawing.view.delete` | `view`, `[document]` | Odstranění pohledu a jeho projekčních potomků |
| `regenerate` | `[document]` | Výslovná regenerace všech pohledů aktivního výkresu |

`view` a `sheet` jsou stabilní ID z dotazů. `limit` je 1 až 10000, výchozí
2000; `total` zahrnuje i položky nad limitem. Dotazy neotvírají soubory,
neprojektují geometrii a nemění potvrzený výběr ani historii. Vrací vlastnosti
pohledu, zdrojové ID/cestu, vazbu na rodičovský pohled, skutečnou kameru,
orientaci, polohu v mm papíru, měřítko, styly čar, řez, nastavení popisků a
vodítek, zámky a počty projekčních a měřicích prvků.

`drawing.view.references` přijímá `kind` s hodnotou `all`, `curve` nebo `point`.
Každý řádek obsahuje původní `owner`, sémantický `key` a přesnou `instance_path`.
Výsledek uvádí také kořenový `source_document` pohledu. Nejde o pořadová čísla
OCCT ani o novou identitu z projekčních čar. Křivka poskytuje příznak přímky,
počet uložených vzorků, první/poslední bod a případná kruhová data. Bod poskytuje
souřadnici. Souřadnice jsou v mm souřadného systému zdrojového modelu
(`source_model_mm`); vektory směru jsou bezrozměrné. Celá vzorkovaná křivka se
neposílá při běžném seznamovém dotazu.

Regenerate načte dostupné vypočtené zdroje, anotace, aktuální definice vybraných
řezů a jejich tras a obnoví projekce, měřené kóty a uložený kusovník. Projekční
potomci se počítají po rodiči bez ohledu na pořadí v souboru. Chybějící rodič,
nesouhlas zdrojové identity, cyklus, chybějící vybraný řez nebo nedostupný zdroj
zamítne celou změnu. Hloubka projekčního řetězce je omezena na 256. Prázdný
výkres bez pohledů nevytvoří krok historie.

Otevřený zdrojový dokument má přednost. Uzavřený Part používá stejný nativní
snímek jako otevřený Part, včetně samostatných skic, konstrukční geometrie a
původních datumových referencí. Pomocné načtení zůstává mimo uživatelské taby.
Regenerace výkresu nepočítá tělesa a neprovádí regeneraci zdrojové sestavy ani
Partu; pracuje s jejich posledním vypočteným stavem. Relativní cesty se vyhodnotí
od souboru výkresu. Cesty uložené v kusovníku používají UTF-8 i na Windows.

Mazání odstraní celý řetězec pohledů navázaných přes `parent_view` i jejich
měřené kóty. Nezávislému řezovému pohledu pouze zruší odkaz na smazaný rodičovský
pohled trasy; jeho řez a zdroj zůstanou zachovány. Jeden krok Undo obnoví
pohledy, kóty a jejich původní ID. Původní akce GUI Regenerovat a Odstranit
používají stejné operace jako konzole.

Regrese této etapy prošly **12/12** (36,66 s),
`build/drawing-view-final-tests.log`. Obsahují změnu délky kvádru z 20 na 40 mm
a odpovídající měřenou kótu, projekční strom uložený v opačném pořadí,
Undo/Redo, zmizelé a zaměněné zdroje, anotace, řezy, kusovník, skutečné CLI,
GUI mazání i uložení do cesty s českými znaky. GUI bylo také vizuálně ověřeno
na `Projects/test/command-drawing-views.png`.

Závěrečná kontrola zdrojů a vstupů prošla **3/3** (17,21 s),
`build/drawing-view-source-tests.log`: navíc pouze skica bez tělesa, přípona
`.PRTZ`, český název a čitelně rozmístěné pohledy v GUI. Finální sestavení
odpovídá `build/drawing-view-source-build.log`.

## Vytvoření a vlastnosti pohledu

`drawing.view.create` vyžaduje `sheet` a buď `source` (ID otevřeného Partu
nebo Assembly), nebo `parent_view`. `drawing.view.set` vyžaduje `view` a mění
jen zadané vlastnosti. Oba příkazy přijímají volitelný `document` a používají
stejnou modelovou operaci jako OK v dosavadním dialogu vlastností pohledu.

- `name`: neprázdný jednořádkový název do 256 bajtů UTF-8.
- `orientation`: `front`, `back`, `left`, `right`, `top`, `bottom`, `isometric`.
  Alternativně `camera` s vektory `horizontal`, `vertical`, `depth`, každý
  jako pole tří čísel. Vektory musí tvořit jednotkovou kolmou bázi ve stejné
  konvenci jako GUI (`horizontal × vertical = -depth`). Obě možnosti současně
  se odmítnou. Nový základní pohled má přední orientaci.
- `x_mm`, `y_mm`: -10000 až 10000 v souřadnicích papíru: počátek vpravo dole,
  X roste doleva, Y nahoru. Nový základní pohled začíná na 100, 100 mm.
- `scale`: 0,001 až 1000, automaticky zapne vlastní měřítko.
  `use_sheet_scale:true` převezme aktuální měřítko listu. Současné zadání
  konkrétního `scale` a `use_sheet_scale:true` je rozporné a odmítne se.
- `display_style`: `visible_edges`, `hidden_edges`, `shaded_with_edges`, `shaded`.
  `hidden_edge_style`: `dashed`, `gray`. `tangent_edge_style`: `visible`, `thin`, `hidden`.
- `show_caption`, `show_section_label`, `show_dimension_guides`: booleany.
  `guide_offset_mm`: 0 až 1000; `guide_spacing_mm`: 0,1 až 1000.
- `value_locks`: celé pole zámků `x`, `y`, `scale`; prázdné pole je odstraní.
  Stejně jako v dialogu zámek nebrání výslovné ruční změně dané hodnoty.
- `section`: stabilní ID existujícího řezu zdroje z `model.tree`, prázdný řetězec řez zruší.
  `section_markers`: celé pole ID zobrazovaných řezových tras; prázdné pole je skryje.
  `hidden_hatch_components`: celé pole přesných klíčů komponent, které se v tomto
  pohledu nešrafují. Viditelnost patří pohledu, styl šrafování patří zdrojovému
  řezu. Příkazy zatím nemění zdrojové parametry šraf; GUI zachovává jejich
  dosavadní samostatnou zdrojovou transakci.

Pro nový projekční pohled se místo `source` zadá `parent_view`,
`projection_direction` a `distance_mm` (0,001 až 10000 mm podél jednotkového
paprsku). Směry jsou `right`, `top_right`, `top`, `top_left`, `left`,
`bottom_left`, `bottom`, `bottom_right`. Zdroj a kamera se odvodí od rodiče a
metody promítání listu; `source`, `orientation`, `camera`, `x_mm`, `y_mm` se
u projekčního pohledu odmítají. Pozdější `distance_mm` mění polohu na jeho
existujícím paprsku. Změna rodiče nebo směru již existujícího pohledu není
součástí této etapy.

```json
{"command":"drawing.view.create","arguments":{"sheet":"LIST","source":"PART","orientation":"front","x_mm":120,"y_mm":80}}
{"command":"drawing.view.create","arguments":{"sheet":"LIST","parent_view":"POHLED","projection_direction":"right","distance_mm":40}}
{"command":"drawing.view.set","arguments":{"view":"POHLED","scale":2,"show_caption":true}}
```

Při posunu rodiče se o stejný rozdíl posunou všichni projekční potomci.
Změna kamery nebo zdroje znovu promítne jejich řetězec; vlastní měřítka
potomků zůstávají zachována. Kóty se obnoví z původních měřicích referencí.
Neplatný parametr, nedostupný zdroj nebo chyba pozdějšího potomka zamítne celý
návrh před změnou historie. Jeden Undo obnoví celou výkresovou operaci.

Změna vlastností je výslovná projekce, stejně jako potvrzení dialogu: používá
poslední vypočtená data zdroje, nepočítá zdrojová tělesa a nespouští OCCT.
Krátkodobá `DrawingProjection` sdílí načtení zdrojů a projekci shodných kamer
v rámci operace/dialogu; odděluje různé zdroje. Stejnou projekci používá také
Regenerate. Uložené relativní cesty se zachovávají. Samotné otevření uloženého
výkresu a dotazy zůstávají bez projekce.

Etapa vytvoření a vlastností pohledů přidává `drawing.view.create/set`, celkem
**138 příkazů**. GUI a CLI sdílejí výpočet projekce, atomický návrh změny,
aktualizaci potomků a měřených kót. Pokryté jsou orientace, vlastní kamera,
měřítko, papírová poloha, styly, řezy a trasy. Opraveno je načtení kusovníku
neuloženého otevřeného dílu na Windows. Celá Windows Release sada prošla
**80/80** (410,05 s), `build/drawing-edit-full-tests.log`; oba výsledné programy
jsou sestavené. Testy zahrnují skutečné GUI i CLI, Undo, zachování přesných
referencí, chybu pozdějšího potomka bez částečného zápisu, zámky, měřítka,
neuložené zdroje, řez i nativní uložení. GUI je vizuálně ověřeno na
`Projects/test/command-drawing-views.png`.

## Modelové anotace a Show/Erase

`drawing.annotation.list` čte uložené anotace bez načtení zdrojového dokumentu
nebo výpočtu OCCT. Volitelně omezuje výsledek pomocí `view`, `kind`
(`all`, `dimension`, `axis`, `construction`), `mode` (`all`, `show`, `erase`),
`limit` (1–10000, výchozí 2000) a `document`. Vrací `items` a celkový počet
`total` před omezením. Každá položka obsahuje pohled, list, typ, stav viditelnosti,
neplatnost, text, hodnotu, počet křivek a přesnou `reference`.
Dotaz `all` zahrnuje i neplatné reference pro diagnostiku. Režim `show` nabízí
jen platné skryté položky; `erase` jen platné viditelné. Výběr používá stejné
pravidlo jako GUI, bez kopírování geometrie pro samotný dotaz.

`drawing.annotation.show_erase` přijímá povinné pole `views` (1–1000 položek)
a volitelný `document`. Každá položka obsahuje:

- `view`: existující ID pohledu, v dávce nejvýše jednou;
- `mode`: `show` (výchozí) nebo `erase`;
- `selection`: `keep_selected` (výchozí) nebo `remove_selected`;
- `kinds`: volitelné pole typů `dimension`, `axis`, `construction`, výchozí všechny;
- `selected`: povinné pole přesných referencí z dotazu.

Reference má čtyři povinná pole: `source_document`, `owner`, `key`,
`instance_path`. První tři nejsou prázdná; prázdná cesta je platná pro kořenový
zdroj. Identita zahrnuje cestu výskytu, takže dva výskyty stejného dílu nelze
zaměnit. Neznámé klíče, duplicity a nenabídnuté reference se odmítají.

V rámci právě nabídnutých položek `keep_selected` zobrazí vybrané a skryje
ostatní; `remove_selected` vybrané skryje a ostatní zobrazí. Ostatní typy,
nenabídnuté anotace a neplatné reference se nemění. Samotný přepínač `mode`
určuje nabídku, stejně jako ve stávajícím dialogu Show/Erase.

```json
{"command":"drawing.annotation.list","arguments":{"view":"POHLED","kind":"dimension","mode":"show"}}
{"command":"drawing.annotation.show_erase","arguments":{"views":[{"view":"POHLED","mode":"show","selection":"keep_selected","selected":[{"source_document":"PART","owner":"SKICA","key":"KOTA","instance_path":"VYSKYT"}]}]}}
```

Všechny pohledy se ověří před zápisem; chyba poslední položky neprovede ani
první. GUI potvrzuje přes stejnou atomickou operaci viditelnosti. Změny více
pohledů tvoří jedno Undo/Redo, návrh a Cancel nevytvářejí historii. Prázdná
změna rovněž nevytváří krok historie. Aktivní GUI dialog brání konkurenčnímu
zápisu z konzole. Geometrie, hodnoty a rozložení anotací se nepřepisují;
viditelnost se ukládá v existujícím `.drwz`, bez změny formátu.

Sestavení GUI i CLI a integrační ověření prošlo **6/6** (21,27 s),
`build/drawing-annotation-tests.log`: nativní model, přesné výskyty, atomická
dávka, Undo/Redo, skutečný CLI proces, GUI konzole, blokace zápisu během
náhledu a stávající dialog Show/Erase včetně více pohledů a Cancel.

## Dotazy a mazání měřených kót

`drawing.dimension.list` přijímá volitelné filtry `sheet`, `view`, `limit`
(1–10000, výchozí 2000) a `document`. Vrací `items` a `total` před omezením.
`drawing.dimension.get` vyžaduje přesné ID `dimension`, volitelně `document`.
Oba dotazy čtou výhradně data výkresu; neotevírají zdroje, nevolají OCCT,
nezapisují historii a nepřepisují poslední platnou prezentaci.

Každá kóta uvádí `dimension`, `sheet`, `view`, typ `kind` (`linear`, `radius`,
`diameter`, `chain`, `angular`) a stav `state`:

- `resolved`: platné měření z uložené geometrie pohledu;
- `hidden`: měření se v tomto průmětu nezobrazuje, například šikmý průmět kružnice;
- `unresolved`: chybějící či neplatná reference, s poslední hodnotou, pokud existuje.

Pole `measurements` obsahuje jednotlivé úseky (`segment`), hodnotu `value`,
jednotku `unit`, výsledný `text`, příznak poslední platné hodnoty `last_valid`
a použití místních úhlových odkazů `angular_leaders`. Skrytá kóta může mít
prázdné pole; neplatná kóta bez předchozí platné hodnoty rovněž. Poslední
hodnota není aktuální měření a nesmí se za ně vydávat. Řetězec zůstává jednou
kótou s více úseky, z nichž každý má vlastní stabilní ID.

`get` navíc vrací `attachments`, `resolved_attachments`, `direction`,
`direction_resolved`, `parallel_reference`, `anchor_attachment`, `style`
a `segments`. Napojení mají pojmenovaný `kind` (`point`, `curve_point`,
`line`, `center`, `tangent`, `intersection`), přesné `reference` a
`other_reference` (`owner`, `key`, `instance_path`), parametr na křivce
`parameter` a volbu strany `side`. Segmenty obsahují uložené `layout`,
`last_presentation` a `last_angular_leaders`. Tyto diagnostické údaje
nejsou rozhraním k nevalidovanému zápisu serializovaného dokumentu.

`drawing.dimension.delete` vyžaduje `dimension` a volitelně `document`.
Maže pouze měřenou výkresovou kótu, včetně případných úseků řetězce.
Modelové anotace spravuje samostatné Show/Erase. Klávesa Delete a kontextové
menu měřené kóty v GUI používají stejnou operaci. Odstranění má jedno Undo;
obnovení zachová reference, poslední prezentaci i identitu neplatné kóty.
Chybějící ID se odmítá bez kroku historie. Přidělená čísla kót se nerecyklují.

```json
{"command":"drawing.dimension.list","arguments":{"view":"POHLED"}}
{"command":"drawing.dimension.get","arguments":{"dimension":"KOTA"}}
{"command":"drawing.dimension.delete","arguments":{"dimension":"KOTA"}}
```

Nativní formát se nemění. Tvorba a vlastnosti jsou popsány v navazující části.

Ověřeno sestavením GUI i CLI a cílenou sadou **6/6** (23,53 s),
`build/drawing-dimension-command-tests.log`. Testy zahrnují hodnotu 60°,
původní výskyt, poslední hodnotu neplatné kóty, skrytý průmět rádiusu,
filtry, mazání přes CLI i skutečnou klávesu Delete v GUI, Undo/Redo,
uchování přidělených čísel a nativní uložení.

## Tvorba a vlastnosti měřených kót

`drawing.dimension.create` vyžaduje `view` a pole `attachments`.
`kind` má výchozí hodnotu `linear`, další možnosti jsou `radius`, `diameter`,
`chain`, `angular`. R/⌀ mají jedno napojení, lineární a úhlová kóta dvě,
řetězec nejméně dvě (nejvýše 4096). Nové identity kóty a úseků přiděluje ZIMA
před výpočtem. List se určí podle pohledu, není možné vytvořit kótu pohledu
z jiného listu. ID výsledku je v `dimension`.

`drawing.dimension.set` vyžaduje `dimension`; ostatní parametry jsou částečný
patch. Podporuje i přepojení na `view` ve stejném listu. Změna typu `kind`
vyžaduje explicitně nové `attachments`; resetuje rozložení a starou prezentaci,
přizpůsobí výchozí příponu mm/° a zachová ID kóty i přeživších úseků.
Samotné nahrazení reference zachová rozložení. Prázdný patch platné kóty
nevytváří historii. Neplatnou kótu lze tímto příkazem opravit nahrazením
přesných referencí; nepotvrzené napojení se nikdy nenahradí blízkou hranou.

Každé napojení má `kind` a `reference` (`owner`, `key`, `instance_path`).
Volitelné jsou `other_reference` pro průsečík, `parameter` (výchozí 0) a
`side` (1 nebo -1, výchozí 1). Parametr je normalizovaný podél původní křivky;
průsečíky ho používají také pro volbu větve. Úhel přijímá dvě různé přímé
reference s `kind: line`. Střed, tečna a průsečík používají stejnou nativní
geometrii a ověření jako dialog Kóta.

Společné volitelné parametry vytvoření a editace:

- `direction`: `automatic`, `horizontal`, `vertical`, `parallel`;
- `parallel_reference`: původní přímá reference pro paralelní směr;
- `anchor_attachment`: index počátečního napojení měřicího směru;
- `style`: částečný objekt `prefix`, `suffix`, `text_override`, `decimals`
  (celé 0–12), `tolerance_mode` a textové `symmetric_tolerance`,
  `single_tolerance`, `upper_tolerance`, `lower_tolerance`;
- `layouts`: pole částečných rozložení, přesně jedno na každý úsek; přijímá
  `text_along`, `text_outward`, `line_offset`, `arrows_reversed`,
  `radius_rotation_degrees`, `radius_center_line_hidden`;
- `placements`: pole bodů `[x,y]` v rovině průmětu pohledu, přesně jeden na
  úsek; `null` nechá úsek na místě. Používá stejné umístění jako myš, včetně
  volby menšího/doplňkového sektoru úhlu;
- `document`: cílový otevřený výkres.

Souřadnice a délkové posuny jsou v milimetrech promítnutého modelu, X doprava,
Y nahoru; nejde o absolutní polohu na papíru. Rotace rádiusu a úhlové hodnoty
jsou ve stupních. `layouts` se aplikuje před `placements`. Režimy tolerance
jsou prázdný řetězec, `symmetric`, `single_deviation`, `deviations`; odchylky
jsou text, stejně jako v GUI, a mohou obsahovat například desetinnou čárku.

`drawing.dimension.extend` vyžaduje `dimension` a jediné `attachment`.
Volitelné `at_first: true` přidá začátek, výchozí `false` konec řetězce.
`position: [x,y]` umístí jen nový úsek. Příkaz přijímá lineární nebo řetězovou
kótu; zachová identity a rozložení všech původních úseků, včetně posunutí
indexu kotvy při vložení na začátek. Přidání na začátek se proto provádí tímto
příkazem, nikoli přeřazením pole napojení přes `set`.

```json
{"command":"drawing.dimension.create","arguments":{"view":"POHLED","kind":"angular","attachments":[{"kind":"line","reference":{"owner":"PRVEK","key":"HRANA_A","instance_path":""},"parameter":0.5},{"kind":"line","reference":{"owner":"PRVEK","key":"HRANA_B","instance_path":""},"parameter":0.5}],"placements":[[10,5]]}}
{"command":"drawing.dimension.set","arguments":{"dimension":"KOTA","style":{"prefix":"A=","decimals":2},"layouts":[{"arrows_reversed":true}]}}
```

GUI OK i CLI sdílejí atomické ověření a potvrzení. Chybné reference, parametry
nebo poslední položka pole nezanechají částečný zápis. Potvrzení neplatné kóty
bez opravy reference se odmítá stejně jako v GUI; skrytý platný radiální průmět
je dovolený. Výpočet měření používá uložená ZIMA data pohledu, nikoli OCCT
nebo otevření zdroje. Změny mají jeden krok Undo/Redo a ukládají se výhradně
v existujícím `.drwz`, bez změny formátu.

Integrační sada nových příkazů prošla **6/6** (21,36 s),
`build/drawing-dimension-edit-integration-tests.log`; následně celá
Windows Release regrese **82/82** (376,97 s),
`build/drawing-dimension-edit-full-tests.log`. GUI i samostatné CLI jsou
sestavené. Testy ověřují tvorbu, 60°/120°, opravu reference, oba konce
řetězce, identity, parametry a atomické zamítnutí, Undo/Redo, nativní
uložení a skutečné zobrazení kóty vytvořené z konzole. Snímek
`Projects/test/command-drawing-views.png` prošel vizuální kontrolou.

## Razítko a parametry zdrojů kusovníku

`drawing.bom.list` vrací uložené řádky, jejich přesné `row`, `source_document`,
`source_path`, množství a metadata. Volitelné jsou `sheet`, `document` a
`limit` 1–10000 (výchozí 2000). Dotaz neotevírá zdroje a neregeneruje kusovník.

`drawing.title.get` vyžaduje `sheet`. Vrací pole `fields` s identifikátorem
`field`, výrazem, zobrazenou hodnotou, příznaky `writable` a `write_back`.
Zahrnuje také parametry použité v prostých textech razítka a respektuje
pořadí a jazykové názvy zdrojových parametrů. Volitelné `bom_row` je přesné
`row` z dotazu na kusovník; určuje zdrojový díl/sestavu, nikoli pořadí řádku.
Bez něj se upravuje zdroj hlavního pohledu, případně zdroj výkresu bez pohledů.
Otevřený zdroj je autoritativní. Zavřený zdroj se čte z nativního dokumentu,
bez jeho otevření v pracovním prostoru a bez OCCT.

`drawing.title.set` vyžaduje `sheet` a objekt `values` (ID pole → text).
Volitelné `expected_values` obsahuje původní hodnoty měněných polí z `get`;
při jejich změně se celý požadavek odmítne. Stejnou kontrolu používá dialog
razítka automaticky. `bom_row` a `document` mají stejný význam jako u dotazu.
Systémové a vypočítané parametry, složené výrazy a nezapisovatelná pole
nelze přepsat. Všechna pole se ověří před zápisem. Beze změny nevzniká historie.

```json
{"command":"drawing.bom.list","arguments":{"sheet":"LIST"}}
{"command":"drawing.title.get","arguments":{"sheet":"LIST","bom_row":"PRESNE_ROW_Z_DOTAZU"}}
{"command":"drawing.title.set","arguments":{"sheet":"LIST","values":{"NAME":"Nový název"},"expected_values":{"NAME":"Původní název"}}}
```

Modelové parametry patří zdrojovému Part/Assembly. Zápis do zavřeného zdroje
jej otevře v pracovním prostoru, zachová vypočítanou geometrii a nezmění aktivní
výkres. Soubor zdroje se automaticky neukládá; je třeba jej výslovně aktivovat
a uložit. Lokální `drawing.*` parametry a doslovné texty patří výkresu.
Úprava zdrojových parametrů osvěží odpovídající uložená metadata řádků ve všech
listech tohoto výkresu, ale nemění množství ani nepřepočítává nadřazenou sestavu.
Změněné složení kusovníku vyžaduje explicitní regeneraci výkresu.

Historie zůstává podle vlastníka: Undo ve výkresu vrací jeho lokální změny
a uložené řádky, Undo ve zdrojovém dílu vrací jeho parametry. Nejde o společné
Undo více dokumentů. GUI i CLI používají stejnou modelovou operaci; během
otevřeného editačního dialogu konzole zápis odmítá. Nativní formáty ani startovní
šablony se tímto krokem nemění.

Ověření etapy: **70/70** nezávislých testů (112,97 s),
`build/drawing-title-independent-tests.log`, a **13/13** scénářů hlavního GUI
(269,82 s), `build/drawing-title-gui-tests.log`. Dohromady všech **83 testů**.
Závěrečná cílená sada po doplnění kontroly duplicitních polí prošla **6/6**
(9,83 s), `build/drawing-title-tests.log`. Ověřené jsou přesné zdroje řádků,
opakované výskyty, vypočítané parametry, odmítnutí zastaralých a rozporných
hodnot, výsledky parametrických vztahů v kusovníku, Undo/Redo, explicitní
uložení, UTF-8 a obousměrná editace mezi konzolí a dialogem. Dialog razítka
s reálnou šablonou prošel také vizuální kontrolou.

Běžící uživatelský CAD zamykal `zima-cad-cpp.exe`. CLI a samostatné testy byly
sestaveny běžným CMake postupem. Hlavní GUI bylo pro tuto regresi slinkováno ze
stejných aktuálních CMake objektů a knihoven do `zima-cad-title-validation.exe`
ve stejném build adresáři; dočasná kopie CTest definic změnila pouze tuto cestu.
Nejde o distribuční balíček. Původní spouštěcí soubor nebyl přepsán a běžící
program nebyl ukončen; jeho běžné sestavení je potřeba dokončit po zavření CADu.


## Export PDF

`export.pdf` vyžaduje `path` s příponou `.pdf`; volitelné jsou `document`
a `overwrite` (výchozí `false`). Cílem je aktivní otevřený výkres. Exportuje
všechny listy v jejich pořadí a formátech, včetně kombinace A4/A3, bez změny
nativního dokumentu a bez regenerace. Cesty mohou obsahovat české znaky.
Výsledek uvádí `document`, `source_revision`, `path`, `pages`, `bytes`
a `model_changed: false`.

```json
{"command":"export.pdf","arguments":{"path":"výkres.pdf","overwrite":true}}
```

GUI PDF a CLI používají `drawing_render::SheetRenderer`, stejný jako plátno
výkresu. Při exportu vynechá hover, úchopy, náhledy a výběrové rámečky.
Zůstávají uložené průměty, měřené i modelové kóty, řezy, šrafy, razítka,
vložené obrázky a vykreslovací styl pohledů. Čáry a text jsou vektorové,
stínovaná výplň používá stávající omezený rastr. Export používá rozlišení
720 DPI a žádné přizpůsobení na tisknutelnou plochu; při tisku volte 100 %.
Rozměry stránky PDF mohou mít obvyklé zaokrouhlení Qt na tiskové body.

Razítko načítá aktuální parametry otevřeného zdroje i bez jeho uložení;
uzavřený zdroj používá nativní soubor. Přitom se neotevírají nové dokumenty
v pracovním prostoru. Uložené řádky kusovníku a geometrie se při exportu
neregenerují. Export je možné spustit až po ukončení rozpracované editace,
stejně jako ostatní exportní příkazy.

Zápis sdílí s modelovými exporty atomické publikování dokončeného souboru.
Chyba na kterémkoli listu zachová původní cíl, uklidí dočasná data a nemění
Undo historii. Bez `overwrite` nelze přepsat existující cíl ani při souběžném
zápisu. Formát `.drwz` ani startovní šablony se nemění.
