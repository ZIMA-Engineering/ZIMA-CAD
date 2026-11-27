# Assemblage

## Avant de commencer

Préparez des pièces ou des sous-assemblages natifs, puis créez un Assemblage (`.asmz`).
Chaque occurrence insérée possède sa propre identité et son propre placement.

## Étapes

1. Utilisez **Insérer un composant** pour ajouter une pièce ou un assemblage imbriqué. Vérifiez
   son occurrence dans l’arbre.
2. Ouvrez les **Propriétés** de l’occurrence. Activez un champ de référence du
   composant, sélectionnez sa géométrie, puis la référence correspondante de
   l’assemblage propriétaire.
3. Choisissez la liaison proposée et sa valeur. Ajoutez des références
   indépendantes jusqu’à ne conserver que les libertés de translation et de
   rotation souhaitées.
4. Inspectez le placement et confirmez avec **OK**. **Annuler** rétablit l’état
   antérieur aux modifications en cours, y compris les placements dépendants.
5. Utilisez **Actif** pour modifier sa source. Une pièce expose les
   outils de modélisation ; un sous-assemblage expose ses propres outils
   d’assemblage. Utilisez **Retour à l’assemblage** pour revenir.

## Points à vérifier

Seul l’assemblage propriétaire immédiat positionne un composant. Activez un
sous-assemblage avant de modifier ses placements internes. La scène de niveau
supérieur reste visible comme contexte, et la géométrie externe sert de référence
en lecture seule.

La géométrie actuellement calculée d’une pièce apparaît dans ses occurrences.
**Régénérer** résout explicitement les liaisons et les opérations
appartenant à l’assemblage ; changer d’onglet ne le fait pas. Les nouvelles
fonctions de matière sont créées dans les pièces, pas au niveau de l’assemblage.

Voir le [déroulement du travail en assemblage](../../USER_MANUAL.md#basic-assembly-workflow)
et les [références d’assemblage](../../ASSEMBLY_REFERENCES.md).
