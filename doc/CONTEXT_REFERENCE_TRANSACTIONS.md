# Společná transakce kontextové reference

Skica v aktivním Partu ukládá identitu zdrojového dokumentu, původního objektu
a přesného výskytu. Závislost mezi bezprostředními větvemi jejich společné
Assembly je odvozený souhrn těchto referencí. Obě změny patří k jednomu
potvrzení Partu; samostatný krok Undo v Assembly by mohl odpojit souhrn od
skutečně uložené reference.

## Příprava a publikace

`commit_part_document` porovná závislosti všech samostatných skic, vnitřních
profilů prvků a skic řezů. Pokud se závislosti nemění, použije běžné potvrzení
Partu. Při změně ověří přesný kontext a zákaz cyklů, připraví potřebné stavy
Assembly a kapacitu seznamu dokumentů. Teprve pak potvrdí Part a bez další
alokace zveřejní připravené souhrny. Neúspěšná validace Partu nezveřejní ani
nově načteného vlastníka. Příprava neřeší vazby ani nepočítá těleso.

Part Undo/Redo poskytuje předem čitelný cílový dokument. Závislosti se ověřují
před přesunem historie; pozdější změna jiného Partu proto může cyklické Redo
odmítnout, aniž by přesunula historii nebo změnila sestavu.

Odstranění jedné reference nemaže automaticky celou závislost větve. Kontrola
zahrnuje ostatní otevřené i zavřené nativní Party ve větvi a všechny jejich
vlastněné skici. Otevřený dokument má přednost. Chybějící nebo zaměněný zdroj
není důkaz, že existující souhrnná závislost už není potřebná. Takovou hranu
kontrola konzervativně zachová. Po doloženém odstranění poslední reference ji
může odstranit. Native loader zatím čte celý dokument; nejde o nový lehký
formát ani vnější cache.

## GUI a CLI

`sketch.reference.create` přijímá přesnou cestu zdroje v kontextu aktivního
Partu. Vlastní výskyt Partu používá dosavadní pravidlo dřívější geometrie.
Jiný výskyt stejného zdrojového Partu nesmí vytvořit závislost sám na sobě.
Part drží jediný konzistentní kontext i napříč vnitřními profily.

GUI výběr používá stejnou přípravu. Vlastněný rozpracovaný profil drží referenci
ve svém dočasném návrhu; výběr nemění souhrn Assembly. Až OK vlastnícího prvku
potvrdí obě strany. Odpojení přes `sketch.reference.delete` zachová nativní
promítnutou křivku. Ukládají se pouze dosavadní soubory prtz/asmz/drwz.

## Ověření a hranice etapy

- Původní příkaz odmítl přesný kontextový zdroj (`invalid_reference_source`).
- Čtyři základní regresní testy prošly za 0,80 s. Rozšířený test kontroluje
  neúspěšnou validaci Partu, soukromě připraveného zavřeného vlastníka,
  opakovaný výskyt, cyklické Redo, sdílenou závislost zavřeného sourozence
  a zachování neověřitelné závislosti při chybějícím souboru.
- Další záměrný test odhalil, že chybějící první sourozenec mohl skrýt novou
  závislost kandidáta. Nové dvojice větví se nyní připravují samostatně a
  průchod pokračuje přes ostatní dostupné sourozence. Pamatuje si pouze malé
  množiny identit referencí, nikoli celé vypočtené nativní Party.
- Skutečný proces CLI vytváří a ukládá referenci i souhrn společné Assembly,
  potom je v novém procesu odpojí a znovu uloží. U ponechané racionální
  čtvrtkružnice ověřuje 257 bodů proti analytické rovnici.
- GUI má šest scénářů: běžný Part a Part v Assembly, pokaždé reference,
  promítnutá křivka s Cancel a reference s OK. Návrh ani Cancel nesmějí změnit
  živý stav Assembly; OK ukládá přesný kontext i její souhrnnou závislost.
- GUI regrese navíc odhalila, že sestavová větev rollbacku vůbec nevkládala
  aktivní skicu do scény. Nyní používá stávající vstupní mesh a renderer
  návrhu skici, včetně správného umístění tělesa a výskytu. Profil se kontroluje
  ve scéně ještě před výběrem reference.

Po opravě prošly oba cílené testy GUI a skutečného CLI za 38,82 s a následně
celá sada **118/118 za 513,98 s**. Závěrečný test navíc přímo vytváří všechny
čtyři druhy kontextové reference a porovnává projekci s nezávisle spočtenými
souřadnicemi. Další regrese opravila předání výslovně zvoleného neaktivního
Partu do `history.can_move`; dotaz nemění aktivaci, revize ani vypočtená data.
Po této poslední úpravě prošlo nové sestavení obou aplikací a **9/9 cílených
testů za 53,72 s**, včetně GUI, skutečného CLI, historie a profilů.

Etapa zahrnuje společnou transakci skic, vlastněných profilů, Part historie
(včetně odstranění tělesa) a skic řezů. Navazující sjednocení při Assembly Undo
a explicitní regeneraci popisuje [ASSEMBLY_REFERENCE_SUMMARIES.md](ASSEMBLY_REFERENCE_SUMMARIES.md).
Odpojení při zavřeném původním kontextu se nepokouší odhadnout cestu Assembly
podle názvu. Její odvozený souhrn se ověří po zpřístupnění skutečné hierarchie
při explicitní regeneraci. Nevyžaduje to změnu formátu ani sidecary.
