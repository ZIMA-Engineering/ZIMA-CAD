# Editor šablon výkresu přes CLI

`template.new/get/open/save/sketch.edit` obsluhují stejné rámečky `.frmz`
a razítka `.tblz` jako GUI. Jde o stávající formáty editoru konfigurace;
načtené rámečky, razítka a obrázky se nadále vkládají do nativního výkresu.
Nevzniká nový formát ani povinný vedlejší soubor modelu.

```json
{"command":"template.new","arguments":{"kind":"title_block","name":"Moje razítko"}}
{"command":"template.sketch.edit","arguments":{"operations":[{"command":"sketch.segment.create","arguments":{"first":[0,0],"second":[-20,0]}},{"command":"sketch.text.create","arguments":{"value":"&name","position":[-5,3],"height_mm":2.5}}]}}
{"command":"template.save"}
{"command":"template.save","arguments":{"path":"Kopie.tblz","copy":true}}
{"command":"template.open","arguments":{"path":"Moje razítko.tblz"}}
{"command":"template.get"}
```

Druhy jsou `drawing_format` a `title_block`. Tvorba nic nepočítá ani ihned
nezapisuje. Otevření již otevřeného souboru zachová jeho neuložené změny.
Uložení bez `path` použije současnou cestu. Nový cíl se existujícím souborem
vyžaduje `overwrite: true`; otevřený cizí dokument nelze přepsat. `copy`
nemění zdrojovou cestu ani stav uložení původního dokumentu. Výsledek ukládání
obsahuje `paths` se zapsaným souborem. Cesty ke stejnému fyzickému souboru
se nepovažují za dva samostatné dokumenty.

Dávka obsahuje 1 až 1000 editačních příkazů skici bez argumentů `sketch`
a `document`. Použije pracovní kopii a jediné potvrzení, nebo při chybě
neprovede žádnou změnu. `operation_index` označuje neúspěšný příkaz od nuly.
Externí modelové reference nejsou ve šabloně povolené. Text používá stejné
nativní písmo a souřadnice jako editor; výchozí text je výkresový.
`body_calculated` je vždy false. `changed: false` nepřidává historii.
Obvyklé `undo`, `redo`, `close` a `activate` fungují i v klidové šabloně.

GUI dovolí tyto příkazy pouze v klidovém editoru šablony. Aktivní kreslení,
tažení a dialog je odmítnou. Modelová operace jiného pracovního prostoru
se tím nezpřístupňuje. Stejnou detekci aktivního skicového příkazu používá
panel nástrojů i konzole.

Samostatné vlastnosti obrázků a oblastí kusovníku obsluhují příkazy popsané
v [TEMPLATE_OBJECT_COMMANDS.md](TEMPLATE_OBJECT_COMMANDS.md). Existující metadata
rámečku a razítka se při otevření a uložení zachovávají.

Ověření: cílené modelové/GUI testy **2/2 za 75,55 s** a úplná regrese
**140/140 za 563,84 s**, včetně skutečného CLI procesu, překladů a editoru
šablon. Sestaveny obě aplikace a všechny testovací programy. Katalog obsahuje
251 příkazů. Testovací záznam: `build/template-lifecycle-full-tests.log`.
Samostatné obrázky a oblasti kusovníku následují; kompletní CLI tím ještě
není uzavřené. Push zůstává odložený podle posledního pokynu uživatele.
