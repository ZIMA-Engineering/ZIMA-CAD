# Části otvoru v GUI a CLI

`opening.components` a `hole.components` vypíší aktuálně zobrazené části
stejně jako strom. Každý řádek obsahuje stabilní `role` a příznak `removable`;
identitu tvoří vlastnický `container` a tato role. Dotaz nic nepočítá.

```json
{"command":"opening.components","arguments":{"container":"ID-OTVORU"}}
{"command":"opening.component.remove","arguments":{"container":"ID-OTVORU","role":"chamfer"}}
{"command":"hole.component.remove","arguments":{"container":"ID-HOLE","role":"thread"}}
```

U současného Otvoru (`opening`, nativní Thread) lze odstranit `thread`,
`chamfer` a `tip`. Povinný `bore` nelze odebrat samostatně. U nativního
Hole lze stejně jako v GUI samostatně odebrat pouze `thread`. Ostatní
části, jejich konstrukční roviny a vlastněné skici se mění přes vlastnosti
celého Hole. Tyto dvě varianty zachovávají své dosavadní modelové významy.

GUI i CLI používají `remove_opening_component` a společnou datovou změnu
`disable_opening_component`. Otvor se potvrdí existující transakcí
`commit_opening`; odebrání závitové plochy zachová průměr vrtání. Závit
nativního Hole tvoří dráty odvozené z parametrů, proto jeho odebrání
potřebuje pouze potvrzení dokumentu se stávajícím vypočteným tělesem.

Výsledek vrací vlastnosti otvoru, `component`, `changed` a `body_calculated`.
Jeden krok Undo obnoví úplný původní stav. Opakované odebrání již vypnuté
volitelné části vrací `changed: false` bez výpočtu nebo další historie.
Neznámá či povinná role se odmítne. Nelze měnit odvozené nebo neaktivní
těleso ani zasáhnout do otevřené nedokončené editace. Identifikátory všech
vlastněných profilů zůstávají zachovány. Nativní formát se nemění.


Ověření (2026-09-14): modelová sada **4/4 za 2,64 s** a finální integrace
**12/12 za 128,16 s** prošly. Zahrnut původní geometrický test s analytickými
objemy, původní koncové reference, skutečný CLI proces, GUI konzole a obě
kontextové nabídky. Kontrolovány no-op, zákazy neaktivního tělesa, zachování
profilů a úplný návrat jedním Undo i při vytvoření jiné větve historie.
Sestaveny obě aplikace a všechny testovací programy. Logy:
`build/opening-components-model-tests.log`,
`build/opening-components-integration-build.log`,
`build/opening-components-integration-tests.log`.
