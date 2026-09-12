# Parametry a nastavení dokumentu přes GUI a CLI

`document.parameters.get/set` a `document.settings.get/set` používají společné
operace `workspace::metadata_operations`. Stejná data a validace jsou v oknech
Parametry a Nastavení souboru. Vytvoření, odstranění a změna pořadí parametrů
jsou vyjádřeny náhradou celé uspořádané tabulky. Nastavení přijímá dílčí patch.

## Parametry

```json
{"command":"document.parameters.get","arguments":{}}
{"command":"document.parameters.set","arguments":{"parameters":[
  {"key":"NUMBER","values":{"":"ZE-100"},"labels":{"cs":"Číslo","en":"Number"}},
  {"key":"NAME","values":{"cs":"Držák","en":"Bracket"}}
]}}
```

Pořadí pole je pořadím řádků dialogu. `key` je jedinečný neprázdný klíč bez
okolních mezer a řídicích znaků, nejvýše 256 bajtů UTF-8. `labels` a `values`
jsou objekty jazykových klíčů a textových hodnot; vynechaný objekt je prázdný.
Prázdný jazykový klíč v `values` znamená sdílenou hodnotu použitelnou v relacích.
Jazykové varianty zůstávají oddělené a samy nevytvářejí sdílený číselný parametr.

Podporováno je nejvýše 4096 parametrů, 128 jazykových variant na položku a
65536 bajtů na textovou hodnotu. Celkový limit řádku CLI protokolu zůstává
64 KiB. Neznámá pole a chybné typy se odmítají, nic se tiše nevynechává.

Stávající fyzikální relace zůstávají autoritou pro své cíle. Například
startovní šablona obsahuje `mass = model.mass`; tento parametr se po potvrzení
znovu odvodí z uložených fyzikálních údajů, i když v předané tabulce chyběl.
Výsledek příkazu vrací skutečnou výslednou tabulku včetně takových řádků.
Opakování stejného požadavku nevytváří další krok Undo. Úprava samotných
relací patří do další příkazové etapy.

## Jednotky a přesnost

```json
{"command":"document.settings.get","arguments":{}}
{"command":"document.settings.set","arguments":{"units":{"Length":"cm"},"precision":{"mesh_deflection":2,"decimal_places":6}}}
```

`get` vrací `units`, číselné `precision` a `unit_choices`. Povolené jednotky
sdílí příkazovka s dialogem: Length mm/cm/m/in, Angle deg/rad, Mass kg/g/t/lb,
Time s/min, Temperature C/K/F a Stress Pa/kPa/MPa/GPa/psi. Neznámé veličiny
či jednotky jsou odmítnuty před změnou dokumentu.

`linear_tolerance` a `angular_tolerance` přijímají konečné hodnoty 0 až 1000000,
`mesh_deflection` 0,000000001 až 1000000 a `decimal_places` celé číslo 0–12.
Lineární tolerance a odchylka sítě jsou v mm. `angular_tolerance` se nyní
pouze uchovává jako existující nastavení; tato etapa nezavádí nové použití
v geometrickém jádře. Změna jednotek či desetinných míst nepřepočítává geometrii.
Změna lineární tolerance či odchylky sítě porovná výpočetní požadavky; pokud
se skutečně liší, potvrzení OK nebo tento příkaz výslovně přepočítá dotčený
Part přes existující společné řešení. U sestavy přepočítá pouze její vlastní
řezy, pokud nějaké má, bez obnovy zdrojů či rodičů. Vlastní nastavení
importovaného prvku má nadále přednost.

To je nutné také pro konzistentní uložení: nativní soubor ověřuje, že cache
odpovídá parametrům a přesnosti výpočtu. Původní dialog mohl ponechat cache
se starým otiskem a následné Save selhalo. Potvrzení nyní ukládá nastavení
a odpovídající výsledek v jednom kroku Undo. Neúspěšný výpočet zachová původní
dokument. Nemění se formát souborů ani umístění kontejnerů.

Změna mm na cm převádí zobrazované fyzikální hodnoty: kvádr o objemu
6000 mm³ zůstává geometricky stejný a jeho objem se zobrazí jako 6 cm³.
Samotná změna jednotek a počtu desetinných míst obnoví související fyzikální
relace z již vypočtených měr, bez volání OCCT.

## Transakce a rozhraní

Všechny čtyři příkazy mají volitelné `document`. Čtení může určit otevřený Part
či Assembly; změna musí mířit na právě aktivní dokument podle běžné ochrany
příkazové vrstvy. Výkresové parametry zdroje se z GUI nadále otevírají přes
původní explicitní postup; CLI si příslušný zdroj otevře/aktivuje samostatně.

Výsledek obsahuje `document`, `revision` a u změny také `changed`; nastavení
navíc vrací `calculated`, zda proběhl potřebný geometrický výpočet. Potvrzení
validuje celý požadavek i závislé fyzikální relace před změnou živého dokumentu.
Chyba například při dělení nulou zachová i generaci dat. Úspěšná skutečná změna
má jeden krok Undo; shodná výsledná data žádný. Běžná metadata zachovávají vypočtený
B-Rep a Assembly sdílené snímky komponent; rodiče se neobnovují.

GUI nadále používá společné okno PropertiesSubWindow s OK/Cancel. Cancel
nezapisuje rozpracovaná data. Po chybě potvrzení se hodnoty ponechají v dialogu
pro opravu. Změna počtu desetinných míst se promítne do vlastnosti okna stejně
přes dialog i přes konzoli. Uložení do `.prtz`/`.asmz` zůstává výslovné.

## Ověření

Modelová regrese ověřuje jazykové varianty, pořadí, odvozený parametr `mass`,
rozpoznání shodných dat, Undo/Redo, neplatné typy a jednotky, chybu relace,
převod 6000 mm³ na 6 cm³ bez změny B-Rep, potřebný výpočet při změně
geometrické přesnosti, zachování snímků sestavy a rodiče a nativní uložení. Procesová regrese spouští skutečné CLI a znovu otevírá
soubor. GUI regrese čte CLI hodnoty v dialogu, mění je přes OK, ověřuje Cancel
a upravuje jednotky a přesnost skutečnými ovládacími prvky.

Katalog má 112 příkazů. Celá Windows Release sada prošla **74/74**
(405,58 s); `build/metadata-full-tests.log`.
