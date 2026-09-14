# Snímek aktuálního View

`export.view` uloží právě zobrazený interaktivní 3D pohled do PNG nebo JPEG.
Používá aktuální kameru, viditelnost i vykreslené značky. Geometrii nepočítá,
nezmění dokument ani Undo/Redo a neprovádí Fit.

```json
{"command":"export.view","arguments":{"path":"pohled.png"}}
{"command":"export.view","arguments":{"path":"pohled.jpg","quality":90,"overwrite":true}}
```

- `path`: cesta s příponou `.png`, `.jpg` nebo `.jpeg`; relativní cesta se
  vyhodnotí vůči pracovnímu adresáři. Cílový adresář musí existovat.
- `quality`: celé číslo 0–100, výchozí 95; ovlivňuje JPEG. PNG je bezeztrátové.
- `overwrite`: výchozí false. Existující soubor se přepíše pouze při true.
- `document`: volitelná kontrola ID aktivního dokumentu.

Výstup obsahuje `document`, `displayed_document`, `path`, `bytes`,
`width_px`, `height_px`, `camera` a `model_changed: false`. Rozměry odpovídají
skutečnému framebufferu v pixelech; nepřidává se domyšlená tisková přesnost.
Při aktivaci Partu v sestavě zůstává snímanou scénou celá zobrazená sestava.
ID aktivního dílu a zobrazené sestavy jsou proto uvedena odděleně.

Hostitel bez 3D View (včetně samostatného dávkového CLI) vrátí
`view_unavailable` a obrázek nevytvoří. Výkres používá vlastní `export.image`
pro list či výřez; `export.view` neexportuje skryté 3D pozadí pod výkresem.
Rozpracovanou GUI editaci chrání obvyklá blokace konzolových operací.

Snímek vzniká na vlákně View. Zápis pracuje s neměnnou kopií obrázku a smí
proběhnout na pracovním vlákně. Stejný atomický zapisovač používá GUI export
3D pohledu: nejdříve dokončí dočasný soubor, teprve potom zveřejní výsledek.
Při chybě nebo nepovoleném přepisu původní soubor zůstává zachovaný.
Nevznikají nové povinné soubory dokumentu ani změny nativních formátů.

## Testované smlouvy

Regrese kontrolují PNG po jednotlivých pixelech, čitelný JPEG, rozměry,
UTF-8 cestu, přepis, chyby snímání/zápisu, oddělení vláken, přesný kontext
aktivovaného výskytu a nedotčenou vypočtenou geometrii i historii.
Samostatný CLI proces ověřuje chybu při chybějícím View. GUI test srovnává
skutečný framebuffer s uloženým PNG a ověřuje, že se kamera nezměnila.

Ověřeno všech šest dotčených testů. První integrační běh potvrdil překlady,
CLI proces, ostatní exporty, hostitele a skutečné GUI (82,15 s).
Nový modelový přípravek nejdříve použil nesprávný typ rozměru a nekódovanou
cestu výskytu; po opravě vstupů prošel samostatně **1/1 za 0,56 s**.
Logy: `build/view-export-tests.log`, `build/view-export-model-tests.log`.
Obě aplikace a všechny cíle jsou sestavené. Katalog má **266 příkazů**,
sada **146 testů**. Úplný běh nové sady zatím neproběhl.
