# Místní vlastnosti modelové kóty ve výkresu

`drawing.annotation.get` vrací konkrétní uloženou anotaci a její místní
rozložení. `drawing.annotation.set` upravuje rozložení nebo textový styl
modelové kóty v jednom pohledu. Oba pracují s daty uloženými ve výkresu;
neotevírají zdrojový Part ani nepočítají tělesa.

```json
{"command":"drawing.annotation.get","arguments":{"view":"ID_POHLEDU","reference":{"source_document":"ID_PARTU","owner":"ID_VLASTNIKA","key":"ID_KOTY","instance_path":"CESTA_VYSKYTU"}}}
{"command":"drawing.annotation.set","arguments":{"view":"ID_POHLEDU","reference":{"source_document":"ID_PARTU","owner":"ID_VLASTNIKA","key":"ID_KOTY","instance_path":"CESTA_VYSKYTU"},"layout":{"text_along":3,"text_outward":4,"line_offset":2,"arrows_reversed":true},"style":{"prefix":"REF ","decimals":4,"tolerance_mode":"symmetric","symmetric_tolerance":"0.02"}}}
```

Reference má všechny čtyři položky, včetně `instance_path` (pro přímý Part
může být prázdná). Převzít ji lze z `drawing.annotation.list`; názvy dílů ani
pořadí kót nejsou identitou. Volitelné `document` chrání cílový dokument.
Záměna výskytu nebo pohledu nikdy nesmí upravit sousední anotaci.

`layout` je částečná změna těchto vlastností:

- `text_along`, `text_outward`, `line_offset`: posuny v modelových mm v
  soustavě kóty; při vykreslení se uplatní měřítko pohledu.
- `envelope_offset`: nezáporná vzdálenost od obálky modelu, nebo null pro
  výchozí odsazení.
- `plane_quarter_turns`: celé číslo 0–3.
- `arrows_reversed`, `radius_center_line_hidden`: boolean.
- `radius_rotation_degrees`: natočení radiální kóty ve stupních; pro ostatní
  druhy se neuplatní. Rovina úhlové kóty je určená měřenými rameny.

`style` je částečná změna textu: `prefix`, `suffix`, `text_override`,
`decimals` (0–12), `tolerance_mode`, `symmetric_tolerance`,
`single_tolerance`, `upper_tolerance`, `lower_tolerance`. Tolerance má režim
prázdný, `symmetric`, `single_deviation` nebo `deviations`. Odchylky jsou
texty jako ve Vlastnostech. Přepsání zobrazeného textu nemění měřenou hodnotu.
`set` vyžaduje alespoň `layout` nebo `style`.

Výsledek obsahuje původní `model_layout`, volitelné `view_layout`, účinné
`layout` a `style`, měřenou `value`, zobrazovaný `text`, viditelnost,
`unresolved`, `editable`, `dimension_kind`, identitu a revizi. Editace navíc vrací `changed`.
Anotace os a konstrukcí lze číst, ale tyto vlastnosti kóty se na ně nevztahují.

Vlastnosti GUI a příkaz potvrzují stejnou operaci: ověřit návrh, přepočítat
pouze projekci anotace a uložit jednu změnu výkresu. Staré ruční úchopy
v papírových souřadnicích se při tomto potvrzení vyčistí stejně jako dříve
v GUI. Hodnota, geometrická reference, modelový rozměr a výchozí modelové
rozložení se nemění. Undo/Redo vrací celou místní úpravu.

Pokud zdrojová kóta chybí, ale výkres má její poslední geometrické zobrazení,
lze upravit jeho místní vzhled. Příznak `unresolved` zůstává zachovaný.
Obnova zdroje aktualizuje skutečnou hodnotu a zachová místní rozložení.
Nejednoznačná reference, neplatné typy, neznámé položky a neplatné hodnoty
se odmítnou bez částečné změny.

Vše zůstává v existujícím `.drwz`. Formáty a startovací šablony se nemění.

Úplná Windows Release regrese prošla **146/146 za 587,41 s**,
`build/annotation-layout-full-tests.log`. Po doplnění výslovné validace
správně orientované kamery testovacího přípravku prošla závěrečná sada
**3/3 za 27,52 s** (model, samostatné CLI, skutečné GUI),
`build/annotation-layout-verified-tests.log`. Produkční kód se mezi těmito
běhy nezměnil; obě aplikace a všechny cíle jsou sestavené.
