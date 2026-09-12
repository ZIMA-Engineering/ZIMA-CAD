# Příkazy samostatné vrtací špičky

`drill_point.create/get/set` ovládají současný nativní **DrillPoint**.
Jedna operace odebere vrtací špičky z jednoho nebo více kruhových den otvorů;
každé dno poskytne vlastní průměr, střed a směr do materiálu. Vrcholový úhel
je společný. GUI Vlastnosti a CLI používají `commit_drill_point`.

## Příklad

Původní reference dna získáte z dat modelu (`reference.list/get`). Je nutné
zadat konkrétní ID vlastníka a sémantický klíč; názvy položek stromu ani
pořadí ploch nejsou identifikátory.

```json
{"command":"drill_point.create","arguments":{"faces":[{"owner":"ID-OTVORU","key":"KLIC-PUVODNIHO-DNA"}],"angle_degrees":118}}
{"command":"drill_point.get","arguments":{"container":"ID-SPICKY"}}
{"command":"drill_point.set","arguments":{"container":"ID-SPICKY","angle_degrees":120}}
{"command":"drill_point.set","arguments":{"container":"ID-SPICKY","faces":[{"owner":"ID-JINEHO-OTVORU","key":"KLIC-PUVODNIHO-DNA"}]}}
```

`create` vyžaduje neprázdné pole `faces`. `set` vyžaduje `container` a alespoň
jeden měněný parametr. Volitelné `name` je neprázdný název; `document`
identifikuje aktivní Part. `get` vrací ID dokumentu, kontejneru, prvku a
tělesa, název, úhel, všechny reference, zámky a revizi, bez výpočtu geometrie.

`angle_degrees` je JSON číslo od 1 do 179 stupňů, výchozí 118. Reference
obsahuje textové `owner` a `key`, volitelně prázdné `instance_path`. Příkaz
přijímá lokální původní plochy před daným prvkem. Každé dno smí být v seznamu
jen jednou, maximálně je podporováno 10000 referencí. Vstup neobsahuje
náhradní analytickou geometrii.

`faces` při editaci nahradí celý seznam: umožňuje přidávání, výměnu,
přeuspořádání a odebírání den. Prázdný seznam u existujícího prvku zachová
jeho identitu a vypne úběr, stejně jako odebrání všech den ve Vlastnostech.
Vytvoření prázdného prvku se odmítá. Neplatná vybraná plocha nezanechá
částečný úběr pouze na ostatních dnech; celé potvrzení se odmítne.

Zámek vrcholového úhlu používá stejný klíč `angle` jako Vlastnosti. Odvozené
těleso se přímo neupravuje; při editaci musí být aktivní těleso špičky.
Úspěšná změna má jeden krok Undo, shodné nastavení nepřepočítává model.
Náhledové změny v GUI se ukládají až po OK, Cancel je zahodí.

## Identity vytvořené geometrie

Identita plochy/obvodové hrany vrtací špičky obsahuje jejího vlastníka,
sémantickou roli a přesnou původní referenci dna. Nevychází z pořadí den
ani z pořadí nalezených OCCT ploch. Odebrání prvního dna proto nepřejmenuje
špičku druhého dna. Stejné pravidlo se používá u koncových ploch Sweep.

Klíče mají tvar `drill-point:ROLE:from:DELKA:VLASTNIK:KLIC-DNA`.
`DELKA` je délka ID vlastníka v bajtech; klíč dna může sám obsahovat
oddělovače. Role je `side`, `base` nebo `base-circle`.
`kernel::drill_point_source` obnoví rodičovskou referenci z uloženého klíče
bez OCCT. Číselné klíče založené na pořadí se při novém výpočtu nevytvářejí.

Struktura nativních dokumentů ani jejich přípony se nemění. Start šablony
neobsahují vrtací špičky a nepotřebují převod geometrie. Regenerace při ztrátě
zdrojů používá dosavadní pravidla; explicitní editace přijímá jen dostupná
a platná dna. Samostatný Hole a další operace zůstávají v celkovém plánu CLI.

## Ověření

Modelový test používá dvě slepé díry různých průměrů. Úběr porovnává se
součtem objemů kuželů a kontroluje body kuželových ploch, původ geometrie,
změnu úhlu, pořadí/odebrání/vyprázdnění seznamu, zámky, atomické chyby,
Undo/Redo a nativní uložení se studeným výpočtem. První běh prošel **1/1**
(0,47 s), `build/drill-point-command-model-build.log` a
`build/drill-point-command-model-tests.log`.

Procesní a GUI testy přidávají skutečnou CLI tvorbu/editaci, čtení uloženého
modelu, přechod do Vlastností, OK/Cancel a odebrání jednoho dna v dialogu.

Sestavení obou programů a související sada prošly **11/11** (88,78 s),
`build/drill-point-command-full-build.log` a
`build/drill-point-command-related-tests.log`. Zahrnují základní geometrii,
3D tažení, stávající otvory/závity, GUI, překlady, zámky a skutečné CLI.
Otisk výpočtu špičky nově zahrnuje verzi její topologické identity, aby
explicitní výpočet nemohl převzít odvozenou cache s pořadovými klíči.
Zobrazení uloženého modelu tím nevyvolává výpočet.


Závěrečné sestavení obou programů a úplná sada prošly **97/97** (458,94 s),
`build/drill-point-command-final-build.log` a
`build/drill-point-command-full-tests.log`, bez opakování testů. Dodatečný
modelový scénář s dnem vytvořeným současným `opening.create` prošel **1/1**
(0,53 s), `build/drill-point-opening-build.log` a
`build/drill-point-opening-tests.log`. Kontroluje skutečný úběr a vazbu nové
kuželové plochy na původní dno Otvoru. Po úplném běhu se změnil pouze test.
