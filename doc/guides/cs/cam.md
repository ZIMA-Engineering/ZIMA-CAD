# CAM

## Než začnete

V dílu připravte vypočtená tělesa polotovaru a cílového tvaru a případně také
tělesa upínek. Pro přístup k příkazu **Vytvořit CAM** deaktivujte aktuální těleso.
Pro frézování kapsy nebo kontury připravte předcházející nativní skicu profilu.

## Postup

1. Zvolte **Vytvořit CAM**. Vyberte polotovar, cíl a upínky a umístěte pracovní
   souřadný systém upnutí.
2. Otevřete **Stroj** a **Nástroje CAM**. Zkontrolujte limity pojezdů, geometrii
   nástroje, řezné podmínky a posunutí pracovního počátku. Podle potřeby zadejte
   v okně **Upínací skici** kontaktní skicu, skicu horní roviny čelistí a jejich orientované strany.
3. V okně **Obráběcí operace** zvolte **Přidat čelní frézování**, **Přidat kapsu**,
   **Přidat konturu** nebo **Přidat ruční operaci**. U kapsy nebo kontury klikněte
   na referenční pole **Profil obrábění** a vyberte předcházející skicu ve Stromu
   nebo Pohledu. Nastavte nástroj, horní a dolní úroveň, hloubky záběru, přídavky,
   bezpečnou výšku a směr dráhy nástroje. U kapsy nastavte **Boční krok**;
   u kontury zvolte **Uvnitř** nebo **Vně**.
   Zkontrolujte pořadí operací. **OK** v editoru předá změny do návrhu upnutí;
   potvrzením **OK** v nastavení upnutí je uložíte.
4. Zvolte **Vypočítat CAM**. Prohlédněte **Obecný NC program**, související pohyb
   nástroje a vypočtený zbytek polotovaru.
5. Zkontrolujte hlášení kolizí a upnutí. **Exportovat obecný NC text…** použijte
   pouze s vědomím uvedeného významu obecného programu.

## Co zkontrolovat

Žluté dráhy představují pracovní posuv, červené čárkované dráhy rychloposuv.
Výslovně spuštěný výpočet kontroluje limity stroje a simuluje materiál zasažený
pohybem nástroje; krokování výsledku materiál nepřepočítává.

Frézování kapes a kontur je tříosé 2,5D obrábění uzavřených oblastí nativní skici
rovnoběžných s rovinou XY upnutí, včetně ostrůvků a oddělených smyček. Vyžaduje
válcovou řeznou část s plochým čelem. Vstup probíhá řezným zanořením a přejezdy
se zvedají do bezpečné výšky. **Tolerance dráhy** omezuje aproximaci nekruhových
křivek; přímky a kruhové oblouky si zachovávají analytické dráhy.

Upínací skici definují orientované zakázané a kontaktní oblasti a kontroly horní
roviny čelistí. Skutečná tělesa upínek zůstávají dalšími překážkami. Průnik břitu
nebo stopky s upínkou či zakázaným objemem je kolize. Úběr pod horní rovinou
čelistí a ztráta kontaktu vytvářejí odlišná upozornění; zbývající geometrický
kontakt nedokazuje dostatečnou upínací sílu. Kolizní výsledek se nemůže stát
polotovarem dalšího upnutí.

**Zrušit** v nastavení zahodí nepotvrzené úpravy. Opětovné otevření aktuálního
výsledku neopakuje simulaci. Ruční operace podporují lineární pohyb a kruhový
pohyb v XY. Postprocesory řídicích systémů, soustružení, pohyb ve 4/5 osách,
adaptivní nebo rampový vstup, automatické umístění upínání z knihovny a převod
libovolného tělesa na nástroj zůstávají nedostupné. Obecný NC export není kód
připravený pro konkrétní řídicí systém stroje.

Viz [práce s CAM](../../USER_MANUAL.md#first-stage-cam-workflow)
a [aktuální rozsah profilového frézování](../../CAM_BODY_DESIGN.md#profile-milling-stage-2026-11-26).
