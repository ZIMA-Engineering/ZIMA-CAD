# Číselné zámky v GUI a CLI

`value_lock.list` a `value_lock.set` čtou a mění stejné uložené zámky jako
zámeček ve Vlastnostech a kontextová akce kóty ve View. Společná transakce
je v `workspace/value_lock_operations`. Nevolá OCCT, solver referencí ani
řešení vazeb sestavy. Vypočítaná geometrie se zachová; změna vytvoří jeden
krok Undo. Opakované nastavení stejného stavu je beze změny revize.

```json
{"command":"value_lock.list","arguments":{"object":"ID-KONTEJNERU"}}
{"command":"value_lock.set","arguments":{"object":"ID-KONTEJNERU","key":"length","locked":true}}
{"command":"value_lock.set","arguments":{"object":"ID-KONTEJNERU","key":"placement:x","locked":false}}
{"command":"value_lock.set","arguments":{"object":"ID-TELESA","key":"placement:reference_offset:2","locked":true}}
```

`object` je konkrétní ID objektu v otevřeném Partu nebo Assembly. Výpis
přijímá volitelné `document` také pro jiný otevřený dokument; změna smí
cílit pouze aktivní dokument a vyžaduje ukončený dialog/skicář. V Partu
musí být aktivní vlastnící Těleso, kromě zámků samotného umístění Tělesa.
Odvozené Těleso se touto operací přímo neupravuje. V sestavě se používá ID
bezprostředně vlastněného výskytu: dvě vložení téhož zdroje mají nezávislé
zámky. Zámek zdrojového Partu nemění rodičovská sestava.

Výsledek `.list` obsahuje `document`, `object`, `revision` a `items`.
Každá položka má `key`, `locked` a `editable`. Poslední příznak označuje,
zda daný typ reference vůbec umožňuje editovat odsazení; neobchází pravidla
aktivního dokumentu/Tělesa. Nulové a právě skryté rozměry se vypisují také.
`.set` přidává `changed`; `locked` musí být skutečný JSON boolean.

Klíče odpovídají konkrétním polím Vlastností, například `length`, `radius`,
`primary`, `secondary`, `profile_offset`, `pitch`, `base_offset` nebo
`placement:x`. Platné klíče se vždy zjišťují výpisem pro konkrétní objekt.
Neznámý klíč nesmí vytvářet libovolná metadata. Kóty skic a výkresů mají
své již existující příkazy; pole odvozených kopií se řeší v jejich etapě.

Umístění Partu/konstrukce má samostatné `placement:rotation_x/y/z` a
`placement:rotation_offset_x/y/z`. Komponenta Assembly má pouze běžné
rotace a polohu. GUI nadále překládá nabízenou kótu na její skutečný uložený
zámek podle `value_lock_key` ve vieweru. CLI zadává konkrétní klíč výslovně.

`placement:reference_offset:N` je současná adresa pole reference od nuly:
u Partu přeskočí prázdné a čistě orientační reference, u komponenty označí
její uložený řádek. Není to index OCCT geometrie. Odsazení osové/bodové
shodnosti nemá uživatelsky měnitelný zámek. Neplatný nebo přetečený index
se odmítne porovnáním s existujícími klíči. Nové geometrické identity se
nevytvářejí.

Sdílený převod přijímá i stávající adresy vieweru `parameter:...`, zkrácené
`x/y/z`, `rotation_...` a `reference_offset:...`. Starší názvy polí `size`,
`included_angle` a `thread_nominal_diameter` znamenají `primary`, `angle`
a `thread_diameter`. Katalogová kóta `thread_designation` používá skutečný
zámek `nominal_diameter`, `thread_pitch` používá `pitch`. Jde o názvy
aktuálního UI, nikoli migraci souborů.
Sestavová adresa `placement-reference:ID-VYSKYTU:N` se převádí na stejný
řádek příslušné komponenty.

Při otevřených Vlastnostech kliknutí na zámeček zůstává pouze v dialogu.
OK uloží hodnotu i zámek, Cancel obojí zahodí. Příkazové nastavení v této
situaci vrátí `editing_in_progress`. Samotné přepnutí zámku mimo dialog
aktualizuje historii a značku změněného dokumentu bez obnovy modelové scény.

Nativní formát ani start šablony se nemění: používají se existující
`value_locks` a `offset_locked` uvnitř `.prtz` a `.asmz`.

## Ověření této etapy

První sestavení nového testu vyžadovalo opravit pomocnou cestu na měnitelnou
hodnotu podle existujícího konstruktoru Host. První modelový běh došel
k chybějícím názvům pomocných konstrukcí; testovací vstupy byly doplněny.
Revize odstranila z výpisu komponent neexistující korekční úhly a přidala
kontrolu jejich odmítnutí. Cílený modelový test prošel **1/1** (0,19 s),
`build/value-lock-model-final-build.log`, `build/value-lock-model-final-tests.log`.

Test měří nezměněný vypočítaný tvar a otisk, čtení a no-op bez mutace,
jediné Undo/Redo, odmítnutí přepisu zamčené hodnoty, nulové umístění,
korekční úhel, původní offsety, lokální bod 3D křivky, aktivní Těleso,
neplatné klíče a typy, přesné výskyty, sdílenou geometrii komponent,
nezměněnou polohu/vazby a nativní ukládání. Skutečné CLI procesy a GUI
regrese prošly v úplné sadě **102/102** (479,54 s),
`build/value-lock-all-build.log`, `build/value-lock-full-tests.log`.

Závěrečná kontrola doplnila společnou adresu katalogové velikosti závitu
a stoupání. Model ověřuje atomické odmítnutí M10 → M12 při zámku,
odemčení a úspěšnou změnu. GUI test používá skutečnou kótu katalogu ve View
a kontroluje, že při zamčení neotevře inline editor velikosti.

Po tomto doplnění jsou oba programy znovu sestavené; závěrečná dotčená
sada prošla **7/7** (162,86 s), včetně skutečného CLI, konzole, Vlastností
a inline katalogu ve View. Logy: `build/value-lock-alias-build.log`,
`build/value-lock-alias-tests.log`. Úplný běh 102 testů výše předcházel
této poslední úpravě adresování.
