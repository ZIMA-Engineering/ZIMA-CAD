# Návrh nového příkazu Kóta ve výkresu (2026-09-10)

Stav: dohodnutý návrh následující funkce, dosud neimplementováno.
Stávající experimentální ruční kótování lze nahradit. Tento návrh je oddělený
od dokončených oprav parametrických anotací a Show / Erase.

## Vstupy → prostředky → výstupy

Vstupy: body a geometrie výkresového pohledu, kamera pohledu, požadovaný
typ a styl kóty a samostatný způsob napojení každé vynášecí čáry.
Prostředky: uložené reference ZIMA a jejich promítnutá geometrie, společné
výběrové řízení, interní vlastnosti a společná prezentace/úchopy kót.
Výstupy: asociativní měřicí kóta v rovině výkresu, která zobrazuje hodnotu
průmětu a nemění parametry modelu. Výběr a úprava nevyvolávají OCCT.

## Zadání a vazby

Příkaz otevře společné interní okno vlastností. Přednastavený způsob napojení
půjde měnit pro každou jednotlivou vazbu na konci vynášecí čáry. Zadávání
podporuje bod/geometrii, střed (C), tečnu (T) a průsečík. Průsečík musí nést
obě zdrojové reference, nikoli jen kopii souřadnice.

Při zadání úsečky jako prvního vstupu se směr měření nastaví kolmo na tuto
úsečku. Druhým vstupem může být bod nebo rovnoběžná úsečka. Kótovací čáru
půjde také vázat rovnoběžně s jinou geometrií. Nabízené kandidáty musí
omezovat společný výběrový kontrakt daného kroku.

Vlastnosti obsahují společný styl kóty, její typ a uložené geometrické vazby.
Každou ztracenou vazbu lze jednotlivě znovu napojit; zbylé reference,
vzhled a umístění se zachovají. Nevyřešený rozměr se nesmí vydávat za platný.

## Typy a řetězec

První rozsah: běžná lineární, poloměrová, průměrová a řetězová kóta.
Běžnou kótu půjde rozšířit na řetězovou. Jako začátek řetězce lze zvolit
první nebo druhý konec dosavadní kóty a pokračovat ze zvoleného konce
novými referencemi. Původní kóta si ponechá vazby i prezentační nastavení.
Jednotlivé úseky řetězce měří vzdálenosti mezi sousedními referencemi.

## Změna pohledu a ovládání

Lineární měření zůstává v rovině výkresu a po změně kamery přepočítá hodnotu
z aktuálního průmětu. Nejde o převzetí skutečné prostorové délky modelu.
Kontrolní příklad: úsečka délky L skloněná o úhel α od roviny pohledu má
ve vhodném směru průmět L·cos(α).

R/Ø je viditelná pouze při kruhovém průmětu. Jakmile se kružnice po natočení
promítne jako elipsa, kóta se skryje; její vazba i umístění zůstanou uložené.
Po návratu do kolmého pohledu se opět zobrazí. Otočení v samotné rovině
výkresu není důvodem ke skrytí. Nahrazuje to původně navržené označení
R/Ø za neplatnou při eliptickém průmětu.

Fialové úchopy, umístění textu a pomocné čáry, tažení šipek i RMB přecvaknutí
při držení LMB mají stejné chování jako parametrické kóty. Úchopy mění
prezentaci. Hodnota se odvozuje pouze od geometrických vazeb a projekce.
OK a dvojklik prostředním potvrzují, Cancel vrací rozpracované změny.

## Body k ověření při implementaci

- Volba strany tečny a průsečíku při více platných výsledcích.
- Stabilita referencí, přepočtu a úchopů při změně pohledu a měřítka.
- Převod na řetězec z obou konců a oprava jedné ztracené vazby.
- Skrytí a obnovení R/Ø bez ztráty referencí nebo ručního umístění.
- Uložení/otevření a úplné zrušení rozpracované operace.
