# Uložit jako: kopie dokumentu a výkresu

Příkaz **Uložit jako** ukládá aktuální rozpracovaný stav pod novým názvem.
Původní dokument zůstává otevřený a aktivní, se stejným ID, cestou a stavem
neuložených změn. Operace jej nepřejmenovává ani neoznačuje za uložený.

Nový Part nebo Assembly dostane nové ID dokumentu. Identity prvků, těles,
zdrojových křivek a topologie zůstávají v jeho vlastním jmenném prostoru.
Reference na vlastní dokument a jeho počátek se přesměrují na novou identitu.
Vložené díly a podsestavy Assembly zůstávají odkazy na původní zdroje;
nevytváří se automatická kopie celé závislostní struktury.

Spolu s modelem se kopírují jeho navázané výkresy: aktuální otevřené dokumenty
mají přednost před soubory na disku. Zavřené výkresy se hledají ve složce
zdrojového modelu a v aktuální pracovní složce. Hlavní sourozenecký výkres
má stejný nový základ názvu a příponu `.drwz`; další výkresy mají číselný
příznak `_2`, `_3`, atd. Výkresové kopie dostanou vlastní nová ID a jejich
odkazy na model, cesty i pohledy se přesměrují na kopii modelu. Samostatné
Uložit jako nad výkresem vytvoří pouze výkresovou kopii se stejným zdrojovým
modelem.

Relativní cesty se při kopii do jiné složky ukotví ke zdrojové složce.
Uložená geometrie včetně B-Rep a mezivýsledků těles se převezme bez OCCT.
Kontrolní otisky uložených výsledků se přizpůsobí změně identity/cesty,
aniž by se přepočítávala geometrie.

Celá sada cílových názvů se kontroluje před zápisem. Existující nebo otevřený
cílový soubor se nepřepisuje. Kopie se nejdřív zapíší a znovu načtou v dočasné
složce vedle cíle; až po ověření se zveřejní pod cílovými názvy. Zveřejnění
používá atomické vytvoření pevného odkazu bez přepsání existujícího souboru;
dočasné odkazy se následně odstraní. Při chybě se odstraní pouze nové soubory
vytvořené touto operací. Originály se nemění.

Ověření pokrývá neuložený stav modelu a výkresu, zavřený výkres, současné
otevření originálu a kopie, kolizi cílového výkresu, vazby Assembly,
samostatnou kopii výkresu a přesun kopie zmrazeného STEP do jiné složky.
GUI test spouští skutečný příkaz Uložit jako a potvrzuje zachování původní
aktivní záložky.
