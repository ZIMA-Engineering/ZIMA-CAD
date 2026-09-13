# Obnova referencí ve vlastněných skicách

Regenerace Partu dříve procházela samostatné skici a vložené skici Sweep2D.
Vnitřní skici Sweep3D, Helical Sweep, Hole a Thread z tohoto průchodu vypadávaly.
Stejná mezera byla v obnově kontextových referencí, hledání kontextových
závislostí před regenerací Assembly a jejich souhrnu pro otevřené Party.

## Společný průchod

`visit_document_sketches` nyní přijímá také přímo kandidáta Part/Assembly.
Lze tak zkontrolovat jeho samostatné skici, všechny podporované vlastněné
profily a skici řezů bez vytvoření kopie Workspace. Čtení může skončit před
načtením nesouvisejících profilů. `update_document_sketches` používá stejný
rozsah pro soukromý kandidát explicitního výpočtu a zapisuje jen změněná data.

Extrusion/Revolution používají dosavadní skici v seznamu dokumentu. V Assembly
jsou podporovány právě tyto dva druhy objemového řezu; nevzniká nová podpora
sestavového tažení nebo otvoru. Skica řezu v multibody Partu může obnovit
reference k původním objektům všech jeho těles.

Kontextová obnova dál čte již vypočtené zdroje a sdílí jejich načtení mezi
skicami stejného kontextu. Nevyvolává výpočet tělesa ani řešení vazeb. Přepisuje
pouze změněné profily. Souhrn závislostí otevřených Partů nově zahrnuje také
vnitřní skici a řezy, takže je neztratí při Undo aktualizace reference.

## Ověření

- Původní implementace nezvládla nový test: vnitřní kontextové reference
  Sweep3D byly přeskočené (0/1 za 0,13 s).
- Nový test prochází všechny uložené profily Sweep2D, Sweep3D, Helical Sweep,
  Hole a Thread. U racionální čtvrtkružnice kontroluje 257 bodů proti rovnici
  kružnice po posunu zdroje o 0,01 mm, identity trimu a parametry offsetu.
- Ztráta hrany zachová poslední křivku a označí referenci jako přerušenou;
  obnovení původní identity vazbu opraví. Testuje se i obnova referencí ve
  skicách řezů, samostatné nativní skici sestavového řezu, nedotčený sousední
  profil a zachování sestavové závislosti při Undo příkazového refresh.
- Skutečný CLI proces otevře Assembly, aktivuje Part se skutečně vypočtenou
  šroubovicí, regeneruje Assembly a uloží Part. Nově načtené `.prtz` obsahuje
  aktualizované přesné póly reference uvnitř základní skici šroubovice.
  Part nemá žádnou kořenovou skicu; objem tělesa zůstane zachovaný.
- Rozšířená cílená sada prošla **4/4 za 21,13 s**. Následně prošlo
  celé sestavení GUI/CLI a **116/116 testů za 501,40 s**, včetně kontroly
  nedotčeného sousedního profilu. Protokoly jsou v pracovních souborech
  `build/owned-reference-all-build.log` a `build/owned-reference-full-tests.log`.

Kontextová tvorba a odpojení reference se společným potvrzením Partu a závislostí
Assembly stále zbývá. Tento krok také ještě neřeší bezpečné odstraňování
souhrnných závislostí sdílených s uzavřenými zdrojovými Party.
