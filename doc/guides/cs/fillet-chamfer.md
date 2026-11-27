# Zaoblení a sražení

## Než začnete

V aktivním upravitelném tělese dílu mějte vypočtený objemový model. Tyto příkazy
vybírají hrany skutečného vstupního tělesa v příslušné pozici historie.

## Postup

1. Zvolte **Zaoblení** nebo **Sražení**. Vytváření a pozdější úpravy používají
   stejné okno vlastností daného příkazu.
2. Klikáním na požadované vstupní hrany je přidávejte nebo odebírejte a kontrolujte seznam vybraných hran.
3. U zaoblení zvolte konstantní poloměr R nebo proměnlivé R1 → R2 a ověřte směr. U sražení zvolte A × A, A × B nebo A + úhel; u nesymetrických hodnot ověřte referenční stranu.
4. Zvolte **ZIMA** nebo **OCCT** a zkontrolujte automaticky doplněné tečné úseky. Odebrání řádku odebere pouze daný úsek hrany; ZIMA při neúspěšném výpočtu nepřepne na OCCT.
5. Potvrzením **OK** spusťte výpočet. **Zrušit** zahodí nepotvrzenou změnu.

## Co zkontrolovat

Výsledek má sledovat požadované hrany a stranu. Znovuotevření vlastností zobrazí
předchozí ostrý vstupní tvar, aby šlo hrany opravit nebo nahradit. Dvojklik na zaoblení zobrazí jeho parametrické kóty.

První neúspěšné potvrzení OK oznámí chybu a ponechá rozpracovanou definici
otevřenou. Druhé potvrzení OK beze změn může uložit strukturálně platnou definici
jako nevyřešenou; tím se její geometrie nestává platnou. Opravte ji ve vlastnostech.

Viz [úpravy hran](../../USER_MANUAL.md#fillet-and-chamfer) a
[pravidla parametrů](../../EDGE_TREATMENT_COMMANDS.md).
