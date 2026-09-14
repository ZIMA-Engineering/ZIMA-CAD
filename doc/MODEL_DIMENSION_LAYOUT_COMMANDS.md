# Vlastnosti 3D kót přes CLI

Názvy příkazů, JSON parametrů a chybových kódů jsou anglické a nezávislé
na jazyku aplikace. Překládají se popisy, nápověda a zprávy pro uživatele.

`dimension.layout.list`, `dimension.layout.get` a `dimension.layout.set`
pracují s nativními kótami otevřeného Partu nebo Assembly. GUI Vlastnosti
kóty i CLI potvrzují vzhled stejnou datovou operací. Aktivní editace skici
používá své dosavadní příkazy a rozpracovanou transakci.

```json
{"command":"dimension.layout.list","arguments":{"owner":"BOX_ID"}}
{"command":"dimension.layout.get","arguments":{"reference":{"owner":"BOX_ID","key":"parameter:length"}}}
{"command":"dimension.layout.set","arguments":{"reference":{"owner":"BOX_ID","key":"parameter:length"},"reset":true}}
```

`list` nabízí identity parametrů, včetně nulových či právě nezobrazených
kót. Přijímá volitelné `owner`, `document` a `limit` (1–10000, výchozí 2000).
Vrací `items` a celkový počet odpovídajících položek `total`. Položky nejsou
seznamem viditelné geometrie a příkaz kvůli nim nevytváří scénu.

`get` vrací `reference`, jméno vlastníka, `has_override`, uložené `layout`
nebo výchozí nastavení, revizi dokumentu a `body_calculated: false`.
`text_style: null` znamená převzetí původního stylu kóty; samostatné CLI
kvůli zjištění tohoto stylu nevytváří zobrazovanou kótu.

Při změně převezměte objekt `layout` z `get`, změňte požadované hodnoty
a odešlete jej celý do `set`:

```json
{"command":"dimension.layout.set","arguments":{"reference":{"owner":"BOX_ID","key":"parameter:length"},"layout":{"plane_quarter_turns":0,"envelope_offset":8,"text_along":4,"text_outward":0,"radius_rotation_degrees":0,"arrows_reversed":false,"line_offset":0,"radius_center_line_hidden":false,"text_style":null}}}
```

Délky jsou v modelových milimetrech, natočení poloměru ve stupních.
`plane_quarter_turns` je celé číslo 0–3; `envelope_offset` je nezáporná
vzdálenost nebo `null` pro volné umístění. Všechny číselné hodnoty musí být
konečné. Vlastní `text_style` je úplný objekt s poli `prefix`, `suffix`,
`text_override`, `decimals` (0–12), `tolerance_mode`, `symmetric_tolerance`,
`single_tolerance`, `upper_tolerance`, `lower_tolerance`.
Režim tolerance je prázdný řetězec, `symmetric`, `single_deviation` nebo
`deviations`; textová pole mají nejvýše 2048 bajtů UTF-8.

`reset: true` odstraní vlastní vzhled. Nesmí se kombinovat s `layout`.
Stejná hodnota, prázdný reset ani nastavení nezměněného výchozího vzhledu
nevytváří nový Undo krok. Změna vzhledu neovlivní hodnotu rozměru,
umístění objektu, vypočtené těleso ani jeho původní reference. Je uložena
jednou transakcí a podporuje Undo/Redo. Neprobíhá OCCT, načítání zdrojů,
řešení vazeb ani regenerace.

Zápis vyžaduje aktivní dokument, příslušný aktivní Body a zavřené editory.
Vlastnosti kót samotného Body lze upravit i mimo jeho aktivaci, stejně jako
v GUI. Nezadaný `instance_path` přebírá přesný aktivní výskyt; zadaný musí
souhlasit. V aktivovaném dílu uvnitř sestavy se vzhled uloží do zdrojového
dílu. Jeho opakované výskyty tak sdílejí vzhled; příkaz nemění vlastnosti
ani umístění nadřazené sestavy. Dotaz na jiný otevřený dokument používá
jeho místní prázdnou cestu.

Existující formát `dimension_layouts` uvnitř `.prtz` a `.asmz` zůstává
stejný. Nepřibývají pomocné soubory ani změna šablon.

Modelové testy kontrolují objem a skutečnou vypočtenou geometrii kvádru,
historii, chybné vstupy, Body, opakované výskyty a opětovné otevření obou
nativních formátů. Procesní test používá samostatný CLI program. GUI test
otevírá skutečné Vlastnosti kóty, kontroluje přenos stylu z příkazu,
Cancel, OK, Undo/Redo a nativní uložení.
