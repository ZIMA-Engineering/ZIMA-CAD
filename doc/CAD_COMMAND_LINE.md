# Samostatná příkazová řádka ZIMA-CAD

`zima-cad-cli` spouští stejné příkazy jako panel konzole v CADu. Používá
`command_host::Host`, Workspace a existující nativní operace; nevytváří
QApplication ani hlavní okno. Pro společný PDF export používá QGuiApplication
v režimu `offscreen`, Qt Gui/Svg a příslušný platformní plugin. Každé spuštění má vlastní Workspace.
Není připojením do již běžícího okna CADu.

## Sestavení a první spuštění

Windows sestavovací skript nyní sestavuje GUI i CLI:

```powershell
./tools/build-windows.ps1 -Configuration Release
& ./build/cpp-windows-release/zima-cad-cli.exe --help
& ./build/cpp-windows-release/zima-cad-cli.exe --working-directory ./Projects --command "documents"
```

Například vytvoření prázdného Partu podle start šablony a jeho uložení:

```powershell
& ./build/cpp-windows-release/zima-cad-cli.exe --working-directory ./Projects --command "new part cli_pokus" --command "save"
$LASTEXITCODE
```

Pracovní adresář musí existovat. Výchozí je pracovní adresář procesu.
`Paths/WorkingDirectory` z GUI tento explicitní kontext CLI nepřepisuje.
Příkazy `open`/`save` následně nastavují pracovní adresář na adresář dokumentu,
stejně jako společný host konzole. `new` vytváří dokument v paměti; soubor
vznikne až příkazem `save`. Existující cestu `new` odmítne.

Linuxový cíl ze stejného CMake projektu:

```bash
cmake --build build/cpp-release --target zima-cad-cli
./build/cpp-release/zima-cad-cli --working-directory Projects --command documents
```

Tato etapa byla sestavena a spuštěna na Windows. Linuxová větev používá POSIX
vstup/výstup, ale v této etapě nebyla spuštěna. Konfigurace celého CMake projektu
vyžaduje Qt. Spouštějte CLI z adresáře sestavení s dostupnými Qt, OCCT a C++
runtime knihovnami; pro příkazový grafický výstup je potřeba plugin `offscreen`.
Nejde o nový ověřený portable-release balíček.

## Režimy vstupu

Vyberte právě jeden režim:

- `--command "příkaz"`: lze opakovat; příkazy běží v uvedeném pořadí ve stejném Workspace.
- `--script soubor.txt`: UTF-8 textový soubor, jeden příkaz na řádku.
- `--stdin`: stejné řádky ze standardního vstupu, až do EOF.

Příkaz může být běžný text nebo JSON podle [konzole CADu](CAD_CONSOLE.md).
Prázdné řádky a řádky začínající po odsazení `#` se ignorují. Skript přijímá
UTF-8 BOM na začátku souboru, LF i CRLF a poslední řádek bez koncového odřádkování.
Vstupní řádky mají limit 64 KiB; příliš dlouhý řádek se odmítne jako celek,
nezačne se vykonávat jako několik příkazů. UTF-8 se ověřuje před provedením změny.

Například soubor `davka.txt`:

```text
# Cesta dokumentu je vůči pracovnímu adresáři CADu.
open "díl s mezerou.prtz"
context
tree
regenerate
save
```

Spuštění na Windows:

```powershell
& ./build/cpp-windows-release/zima-cad-cli.exe --working-directory ./Projects --script ./davka.txt
```

Cesty u přepínačů `--script`, `--config` a `--working-directory` se vyhodnocují
vůči adresáři procesu při spuštění, nezávisle na následných příkazech v dávce.
Pro složitější text/JSON a české názvy v PowerShellu je přímý UTF-8 skript
praktický: jeho obsah neprochází zpracováním uvozovek ani kódováním shellové roury.
Klient standardního vstupu musí odesílat UTF-8 a číst výstup souběžně.

## Výstup a chyby

Pro každý provedený příkaz přijde právě jeden JSON objekt zakončený LF na stdout:

```json
{"protocol":"zima-cad.commands/1","ok":true,"code":"ok","message":"","data":[]}
```

Výstup se odesílá ihned po příkazu, bez čekání na EOF nebo ukončení programu.
Nevypisují se interaktivní výzvy. Diagnostika C/C++ a OCCT jde na stderr;
stdout má oddělený deskriptor pro protokol. `--help` je výjimka: vrací čitelný
návod bez načítání configu, dokumentů či kernelu.

| Kód procesu | Význam |
| --- | --- |
| `0` | Všechny provedené příkazy uspěly, případně úspěšná nápověda/prázdná dávka. |
| `1` | Alespoň jeden příkaz selhal. Detail je v jeho JSON výsledku. |
| `2` | Neplatné spuštění, chyba configu nebo vstupu/výstupu; diagnostika na stderr. |

Výchozí chování je zastavit po první chybě. `--keep-going` pokračuje dalšími
příkazy, ale konečný kód zůstane `1`. Selhání výstupu zastaví provádění vždy,
takže po odpojení příjemce nepokračuje například další `save`.

Dokončené příkazy se při chybě celé dávky automaticky nevracejí. Dříve výslovně
uložené soubory zůstávají uložené; konec procesu nic dalšího sám neukládá.
Případné neplatné bajty diagnostiky Windows/externích knihoven se v chybové
odpovědi nahradí znakem Unicode U+FFFD. Úspěšná modelová odpověď s neplatným
UTF-8 se odmítne jako `invalid_result`; identity či názvy se tiše nepřepisují.

## Config a šablony

`--config soubor.ini` určuje základní config. Bez něj se hledá ve stejném pořadí
jako u GUI: `config/config.ini` pod adresářem procesu, pod adresářem programu,
a `../../config/config.ini` vůči programu v sestavení. Projektový `config.ini`
v počátečním pracovním adresáři přepíše neprázdné hodnoty základní vrstvy.
Relativní cesta patří k adresáři toho configu, který ji dodal. Nastavení se
načte jednou při spuštění a příkazy ho nepřepisují.

CLI čte pouze potřebné řetězcové hodnoty:

- `Application/Language`;
- `Paths/Templates`, `Paths/Localization`;
- `Templates/Part`, `Templates/Assembly`;
- `Units/Length`, `Angle`, `Mass`, `Time`, `Temperature`, `Stress`.

Ostatní nastavení GUI ignoruje. Podporuje UTF-8 INI s běžnými řetězci,
uvozovkami a escapovanými zpětnými lomítky. Používejte přenositelné cesty
s `/`, případně řetězce uložené editorem configu. Qt typované hodnoty nejsou
řetězcovým nastavením CLI. Config a katalogy se pouze čtou. Překlady příkazů
pocházejí ze stejné sekce `QtTranslations` jako u GUI; technické chyby CLI a
nápověda přepínačů jsou anglicky.

Bez nalezeného základního configu lze stále číst existující dokumenty;
`new part`/`new assembly` ovšem vyžadují dostupné odpovídající start šablony.
Nativní `.prtz`, `.asmz`, `.drwz` ani start šablony touto etapou nemění formát.
Nevznikají žádné povinné externí soubory geometrie, revizí nebo cache.

## Rozsah a ověření

Katalog je společný s konzolí: dokumenty, context/tree, new/open/save,
regenerate, undo/redo, fit a příkazy create/get/set všech šesti základních primitiv. Bez View vrací `fit` chybu. `context` nevymýšlí
výběr, hover ani kameru. Kvádr, válec, koule, kužel, jehlan a klín mají rozměry
výslovně v mm a používají stejné transakce jako GUI. Přenos do běžícího GUI,
AI poskytovatel a hlas nejsou součástí tohoto kroku.

`zima_cpp_cli_process_tests` spouští skutečné CLI procesy. Ověřuje config uložený
přes QSettings, projektovou vrstvu, šablony/jednotky, české cesty, všechny tři
nativní typy, text/JSON, skripty a stdin, okamžitý výstup před EOF, zastavení
při chybě, keep-going, neplatné UTF-8, dlouhé řádky a návratové kódy. Test
otevře kvádr, výslovně ho regeneruje a uloží a porovná objem 6000 mm³ i přesné
identity jeho původních ploch. Ztráta výstupu se ověřuje také přes adaptér
zapisování výsledků s kontrolou, že další Save neproběhl.

Zdrojové části jsou `cpp/cli/main.cpp` (platformní vstup/výstup),
`runner.cpp` (argumenty, dávka, společný host) a `settings.cpp` (čtení configu).
Modelovací logika zůstává v existujících modulech.


Ověření: Windows Release, **57/57 testů prošlo** (422,09 s),
`build/cli-full-tests.log`. Po posledním doplnění překladových kontextů a
ověření zpětných lomítek v configu byl znovu sestaven CLI cíl a celý jeho
procesový test prošel samostatně (`build/cli-final-focused-tests.log`).
Při této původní etapě `dumpbin /dependents` potvrdil nepřítomnost Qt DLL
(`build/cli-dependencies.log`). Od doplnění společného PDF exportu CLI používá
Qt Gui/Svg v bezokenním režimu; původní údaj již nepopisuje aktuální runtime.

### Modelování kvádru

```powershell
.\build\cpp-windows-release\zima-cad-cli.exe --working-directory C:/CAD/pokus `
  --command "new part kvadr" --command "box.create 10 20 30" --command "save"
```

Vznikne skutečný kvádr 10 × 20 × 30 mm (objem 6000 mm³) v `kvadr.prtz`.
Výsledek `box.create` vrací stabilní ID kontejneru. Při pozdějším spuštění
otevřete Part příkazem `open` a použijte `box.get ID` nebo `box.set ID 15`.
Stejný proces může změnu vrátit pomocí `undo`, opakovat pomocí `redo` a uložit
příkazem `save`. CLI si historii Undo nepřenáší mezi procesy.

Přesná syntaxe, jednotky, zámky a pravidla transakce jsou v
[popisu příkazů kvádru](CAD_CONSOLE.md#společná-operace-kvádru-2026-09-11).

Stejné ovládání mají `cylinder`, `sphere`, `cone`, `pyramid` a `wedge`.
Například `cylinder.create 3 6` vytvoří válec s poloměrem 3 mm a výškou 6 mm;
`cone.create 4 0 6` ostrý kužel. Úplné pořadí parametrů je v
[katalogu základních primitiv](CAD_CONSOLE.md#všechna-základní-primitiva).


### PDF výkresu bez okna

```text
open vykres.drwz
export.pdf vykres.pdf
```

Příkaz exportuje všechny listy bez regenerace. Již existující PDF vyžaduje
explicitní JSON argument `overwrite: true`. Stejný renderer používá GUI;
rozměry listů, kóty, šrafy a písmo nejsou přibližnou samostatnou implementací
pro CLI. Qt se spouští s platformou `offscreen` i při jiném nastavení
`QT_QPA_PLATFORM`; nevytváří se QWidget ani okno. `--help` grafické prostředí
neinicializuje. CLI stále neovládá jiný spuštěný CAD a při ukončení automaticky
neukládá nativní dokumenty. Podrobnosti: [DRAWING_COMMANDS.md](DRAWING_COMMANDS.md).
