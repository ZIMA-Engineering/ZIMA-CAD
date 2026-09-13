# Původní geometrie v kontextu sestavy

Čtení reference aktivovaného Partu používá uložené referenční pakety
zdrojového dílu a přesné cesty výskytů v zobrazené sestavě. Nevolá
řešení vazeb, konstrukcí, odvozených kopií ani sestavových řezů.
Dřívější cesta přes `Workspace::refreshed_assembly` tyto výpočty při
přípravě referencí prováděla; zůstává dostupná pouze svým výpočetním
volajícím. Společný solver umístění se v této etapě nemění.

## Kontrakt dat

- Otevřený Part poskytuje aktuální vypočítanou geometrii včetně dosud
  neuložených změn. Zavřený zdroj se čte z jeho nativního `.prtz` a jeho
  ID se ověří proti výskytu. Čtení nepřidává živé karty.
- Opakované výskyty mají samostatné úplné cesty. Posuny a rotace se
  přebírají z právě zobrazené uložené hierarchie; dotaz nemění historii,
  generaci ani sdílenou geometrii rodičovské sestavy.
- Již vypočtená odvozená kopie poskytuje svou uloženou geometrii.
  Nesmí být nahrazena nezrcadleným zdrojovým dílem. Přímý odvozený Part
  nabízí také svůj počátek se stejnou identitou jako společný prohlížeč.
- Společný převod přenáší vzorky hran, póly přesné spline, body, osy,
  směry a analytické plochy. Stupeň, uzly a váhy spline zůstávají stejné.
  Vzorky a analytická plocha mohou mít odlišný uložený souřadný rámec.
- Filtr kopíruje jen požadované reference a použité vrcholy trojúhelníků.
  Trojúhelníky jedné plochy sdílejí jednu převedenou analytickou plochu.
  Chybné indexy a neúplné referenční trojúhelníky se odmítají.

Katalog zůstává na **209 příkazech**. Příkazové vytvoření, odpojení a
obnovení externí reference aktivovaného Partu je další etapa; tento
záznam ji neoznačuje za dokončenou. Formát ani startovací šablony se nemění.

## Ověření

Modelová sada prošla **4/4** (1,02 s),
`build/context-reference-geometry-tests.log`. Obsahuje nezávislé
matematické kontroly 257 bodů racionální čtvrtkružnice po posunu a
otočení, analytickou rovinu, osy, body, sdílení ploch a řídký převod
indexů. Zkouší neuložený zdroj, zavřený nativní zdroj, chybnou identitu,
přesný vnořený výskyt a zrcadlený Part s jeho počátkem.

Regresní sestava obsahuje řez s prázdným profilem: výslovný výpočet
prokazatelně selže, ale čtení referencí uspěje beze změny jejího stavu.
První verze tohoto testovacího vstupu neměla platnou definici řezu;
byla opravena před hodnocením produkčního chování.

Po sestavení všech programů (`build/context-reference-all-build.log`)
prošla širší sada **17/17** (142,93 s),
`build/context-reference-integration-tests.log`. Zahrnuje celý start GUI,
projekci vlastněných profilů, GUI offsety a aktualizaci sestavy, skutečný
CLI proces, aktivaci komponent, dotazy na reference, odvozené kopie,
explicitní výpočet modelu, přesné spline a geometrii offsetů.
