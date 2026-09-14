# Unicode cesty v nativních souborových příkazech

## Opravený rozdíl GUI a CLI

Na Windows přijímá úzký konstruktor `std::filesystem::path` řetězec
v systémovém kódování. `QString::toStdString()` poskytuje UTF-8.
Přímé předání mezi nimi proto může změnit český název souboru nebo adresáře.
Opačným směrem také nelze vydávat `path.string()` za UTF-8.

Společná příkazová vrstva již používá `std::filesystem::u8path` a
`document::path_to_utf8`. Stejný převod nyní používají následující adaptéry GUI:

- počáteční pracovní adresář okna, zadaný přímo i převzatý z konfigurace;
- Nový dokument, Otevřít a titulky průběhu otevření;
- Uložit jako pro Part, Assembly a Drawing, včetně automatické kopie
  vlastního výkresu;
- volba pracovního adresáře;
- JPG/DXF z Uložit jako ve výkresu;
- přepnutí mezi modelem a jeho výkresem, pokud je nutné otevřít zdrojový soubor.

Běžné Uložit a titulky tabů byly opravené v etapě
[NATIVE_FILE_RENAME.md](NATIVE_FILE_RENAME.md).
Tato změna nezasahuje do výpočtu modelu, sdíleného umístění ani schématu
nativních souborů. Start šablony se nemění. Nový příkaz nepřibyl;
katalog zůstává na **288 příkazech**, registrováno je **154 testů**.

## Ověření skutečnými soubory

Scénář `verify_save_copy_ui` nyní vytváří dočasný adresář s diakritikou
a ověřuje GUI společně s konzolí:

1. Počáteční pracovní adresář je přesně ten, který byl předán oknu.
2. Part se skutečně vypočteným tělesem a jeho výkres se uloží pod českými
   názvy. GUI Uložit jako vytvoří obě kopie s novými identitami a přesměruje
   výkres na nový Part. Původní dokument, jeho tab i aktivace se nezmění.
3. GUI export výkresu vytvoří čitelný JPG a úplný DXF. Příkaz `export.image`
   načte tentýž český zdroj a vytvoří čitelný obrázek.
4. GUI volba pracovního adresáře odpovídá adresáři vrácenému příkazem `context`.
5. GUI Nový, Uložit a Uložit jako proběhnou pro všechny tři nativní typy.
   Kontrola skutečných uložených dokumentů ověří jejich ID. CLI následně
   každou vytvořenou kopii otevře.
6. Testovací dokumenty se zavřou s výslovným zahozením změn, bez ručního dialogu.

Volba souboru v tomto testu má opakovanou obsluhu a kontroluje nečekaná
chybová hlášení.

Windows Release sestavil obě aplikace a všechny testovací cíle
(`build/unicode-native-gui-final-build.log`).
Cílený scénář prošel **1/1 za 4,21 s** s
`ZIMA_VERIFY_SAVE_COPY_ONLY=1`
(`build/unicode-native-gui-focused-final-tests.log`).
Navazující regrese prošla **5/5 za 258,44 s**: nativní dokumenty,
dokumentové operace, skutečný proces CLI, GUI konzole a celý test
pracovního okna (`build/unicode-native-gui-regression-tests.log`).
Oba GUI běhy proběhly bez ručních potvrzení.

První běh nového přípravku skončil na chybějícím povinném argumentu
`sheet` pro `export.image`. Přípravek nyní předává skutečné ID uloženého listu;
rozhraní exportu se neměnilo. Úspěšné výsledky výše jsou až po této opravě.

Jde o ověření uvedených nativních pracovních postupů. Není tím prokázaná
správnost každého zbývajícího převodu cesty v celé aplikaci ani externích
spouštěčů.
