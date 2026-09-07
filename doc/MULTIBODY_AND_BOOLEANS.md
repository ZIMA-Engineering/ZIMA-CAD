# Vícetělesový Part a booleovské operace

Stav k 2026-09-07: **dohodnutý směr návrhu, dosud neimplementováno**.
Uživatel nyní požaduje zápis dokumentace, commit a push. Tato dohoda sama
nepovoluje zahájit změnu datového modelu. Rozpracované kontejnery ve stromu
jsou již samostatně implementované; nejsou implementací vícetělesového Partu.

## Účel a model

Vstupem jsou samostatně vytvářené geometrické větve, například polotovar
formy a nástrojový tvar dutiny. Každá větev potřebuje vlastní historii
přičítání, odebírání a dalších úprav. Výstupem je geometrie dokumentu vzniklá
z jejich výslovně určených kombinací. Prostředkem je vícetělesový Part,
parametrické booleovské operace a existující explicitní výpočet přes OCCT.

Typické příklady: licí formy, otisky, razníky, dutiny a elektrody. Kanálky
nejsou hlavním důvodem této změny; jejich organizaci mohou řešit skupiny.
Organizační skupina sama neznamená samostatný geometrický výsledek.

## Part

- Kořenem stromu bude název souboru; před uložením pracovní název dokumentu.
- Pod kořenem bude počátek dokumentu, samostatná tělesa a operace mezi nimi.
- Každé těleso bude mít vlastní lokální počátek a vlastní modelovací historii
  se současnými typy prvků. Nejde pouze o jeden primitivní nebo importovaný solid.
- Aktivní těleso určí, do které větve vzniká nový prvek. Aktivita není
  booleovská operace a samotné přidání tělesa nic automaticky nesjednotí
  ani neodečte.
- Uvnitř tělesa půjde přičítat a odebírat materiál a upravovat jeho výsledek.
  Výsledek celé větve se poté může stát vstupem dalšího Booleanu.
- Boolean bude položkou historie s explicitními vstupy a druhem operace.
  Po jeho vytvoření může práce pokračovat dalšími tělesy i operacemi.
- Kliknutí na název souboru označí celkovou výslednou geometrii dokumentu.
  Nevznikne navíc volně stojící položka „výsledné těleso“ bez jasného vlastníka.
  Boolean přesto má výpočetní výstup, na který mohou navazovat další operace.
- Výsledek dokumentu může obsahovat více nespojených těles. Výběr celého
  dokumentu není automatické geometrické sjednocení.
- Zdrojové větve zůstanou editovatelné. Viditelnost zdrojů a výsledku musí
  zabránit matoucímu překrytí, aniž by mazala zdrojová data.

Příklad zamýšlené struktury (nikoli současný implementovaný formát):

```text
Forma.prtz                         ← aktuální výsledek dokumentu
├─ Počátek dokumentu
├─ Těleso: polotovar
│  ├─ Lokální počátek
│  └─ vlastní historie prvků
├─ Těleso: nástroj dutiny
│  ├─ Lokální počátek
│  ├─ Protrusion
│  ├─ další přičtený a odečtený prvek
│  └─ zaoblení
├─ Boolean: polotovar − nástroj dutiny
├─ Těleso: další nástroj
│  └─ vlastní historie prvků
└─ Boolean: předchozí výsledek − další nástroj
```

## Ovládání: směr a otevřené volby

Poslední upřednostněná varianta je samostatný příkaz Boolean v libovolném
vhodném místě historie. Zvažované ovládání:

- „Přidat těleso“ a „Boolean“ v kontextové nabídce kořene nebo „Vložit zde“;
- aktivace a vlastnosti tělesa v jeho kontextové nabídce;
- rychlý výběr těles a příkaz Sjednotit / Odečíst / Průnik;
- stejné příkazy dostupné také na panelu nástrojů;
- vlastnosti Booleanu s cílovým a nástrojovými vstupy a jejich jednoznačným
  zvýrazněním. Vlastnosti používají společný interní dialog s OK/Cancel.

Dříve diskutovaná alternativa byla volba Samostatné / Přičíst / Odečíst
přímo ve vlastnostech tělesa s automatickým Booleanem. Je jednodušší pro
jednu lineární posloupnost, ale samotné pořadí neurčuje bezpečně cíl při
více nezávislých větvích. Není to současně požadovaný druhý režim.
Přesné rozmístění tlačítek, výchozí cíle a prezentaci větvení ještě potvrdit
před implementací; nepovažovat návrhy nabídky za již hotové UI.

## Assembly a Drawing

Kořen stromu Assembly i Drawing bude také název souboru. Pod sestavou budou
její počátek, komponenty a operace sestavy; pod výkresem listy a pohledy.
Drawing tím nezískává booleovské modelování.

V Assembly mohou booleovské operace pracovat s konkrétními umístěnými výskyty
Partů nebo podsestav, například při geometrickém návrhu tvarového vyjiskřování.
Výsledek operace patří sestavě; nesmí nevyžádaně přepsat zdrojový Part ani
zdrojovou podsestavu. Obyčejné vložení komponenty zůstává vložením samostatné
komponenty, nikoli automatickým sjednocením všech komponent.

Vlastnictví umístění zůstává u bezprostřední vlastnící Assembly. Vyšší sestava
nesmí převzít umístění vnitřních komponent podsestavy. Opakované výskyty
rozlišují stabilní instance paths. Aktualizace závislostí zůstává výslovná
přes Regenerate, bez skrytého přepočtu při přepnutí záložky nebo obnovení stromu.

## Import a reference

Požadovaným použitím je také vložit STEP nebo další `.prtz` jako nástrojovou
geometrii a odečíst ji od jiného tělesa. Propojený zdroj versus nezávislá kopie,
výběr těles z vícetělesového zdroje a jejich obsluha jsou ještě k dopracování.
Propojené zdroje mají respektovat explicitní Regenerate a zákaz cyklů.

Identity těles, operací a závislostí musí být stabilní a persistované.
Topologie a reference nadále vycházejí ze ZIMA ancestry, nikoli z pořadí
OCCT ploch nebo hran. Je nutné určit, jak navazující prvky adresují výstup
Booleanu při zachování původu zdrojové topologie.

## Implementační a ověřovací brány

Před implementací dopracovat vlastnictví prvků, adresování vstupů/výstupů,
výpočetní graf, rollback, viditelnost a serializaci. Změna datového modelu je
přijatelná; kompatibilitní větve pro staré dokumenty se nepožadují. Zásahy
do sdíleného umístění kontejnerů nadále podléhají jeho samostatnému pravidlu
schválení; tato poznámka není plošným povolením měnit jeho kontrakt.

Nejdříve vícetělesový Part a operace mezi jeho větvemi, poté Assembly.
Přesné zařazení do širší roadmapy nebylo určeno; dosavadní pořadí prací
včetně pozdějšího komplexního auditu Undo/Redo se tím automaticky nepřepisuje.

Ověřit alespoň nezávislost dvou větví, navazující Booleany, změnu zdrojového
prvku, odmítnutí cyklu, potlačení operace, OK/Cancel a obnovu po chybě výpočtu,
uložení/načtení, více nespojených výsledků a opakované výskyty v Assembly.
Pro formy testovat dutinu vytvořenou nástrojovou větví i importovaným tvarem.
Úspěch architektury nezaručuje úspěch každé geometrické operace; neplatný
nebo degenerovaný vstup musí být odmítnut bez poškození posledního výsledku.
