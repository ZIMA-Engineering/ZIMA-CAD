# Congé et chanfrein

## Avant de commencer

Disposez d’un solide calculé dans un corps de pièce actif et modifiable. Ces
commandes sélectionnent les arêtes du corps d’entrée réel à leur position dans
l’historique.

## Étapes

1. Choisissez **Congé** ou **Chanfrein**. La création et les modifications
   ultérieures utilisent la même fenêtre de propriétés de la commande.
2. Cliquez sur les arêtes d’entrée souhaitées pour les ajouter ou les retirer, puis vérifiez la liste des arêtes sélectionnées.
3. Pour un congé, choisissez un rayon constant R ou variable R1 → R2 et vérifiez le sens. Pour un chanfrein, choisissez A × A, A × B ou A + angle ; vérifiez le côté de référence pour des valeurs asymétriques.
4. Sélectionnez **ZIMA** ou **OCCT** et vérifiez les segments tangents ajoutés automatiquement. Supprimer une ligne retire seulement ce segment d’arête ; ZIMA ne bascule pas vers OCCT après un échec de calcul.
5. Confirmez avec **OK** pour calculer. **Annuler** abandonne la modification en cours.

## Points à vérifier

Le résultat doit suivre les arêtes et le côté prévus. Rouvrir les propriétés
affiche la géométrie d’entrée antérieure, aux arêtes vives, afin de réparer ou
de remplacer les arêtes. Un double-clic sur un congé affiche les cotes de ses paramètres.

Un premier clic sur OK qui échoue signale l’erreur et laisse la définition en
cours ouverte. Un deuxième clic sur OK sans modification peut conserver une
définition structurellement valide à l’état non résolu ; cela ne rend pas sa
géométrie valide. Réparez-la dans les propriétés.

Voir l’[utilisation des traitements d’arêtes](../../USER_MANUAL.md#fillet-and-chamfer)
et les [règles des paramètres](../../EDGE_TREATMENT_COMMANDS.md).
