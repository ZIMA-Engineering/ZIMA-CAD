# Balayage

## Avant de commencer

Activez un corps de pièce modifiable. Choisissez **Balayage 2D**, **Balayage 3D**
ou **Balayage hélicoïdal** selon la trajectoire nécessaire ; ce sont des outils distincts.

## Étapes : balayage 2D

1. Placez le conteneur et sélectionnez son plan de trajectoire **XY / XZ / YZ**.
2. Ouvrez l’**Esquisse de trajectoire…**. Dessinez une trajectoire ouverte connexe ou une chaîne fermée prise en charge. Une trajectoire ouverte peut commencer hors de l’origine ; son sens défini détermine la première section. Terminez l’esquisse.
3. Ouvrez l’**Esquisse** de la première station et définissez un profil non vide.
   Son plan est perpendiculaire à la tangente à la trajectoire.
4. Ajoutez les profils suivants selon les besoins. Une section ultérieure vide reprend le profil précédent ; la section de fermeture d’une trajectoire fermée utilise le premier profil. Un profil différent crée une transition.
5. Sélectionnez **Corps**, **Fonction à paroi mince** ou **Face**, vérifiez l’opération
   de matière et confirmez avec **OK**. **Annuler** abandonne les modifications en cours.

## Points à vérifier

Un résultat à paroi mince nécessite une épaisseur et un côté ; un résultat surfacique ne ferme pas les extrémités et autorise uniquement l’ajout. Dans Balayage 3D, utilisez **Ordre des points** pour faire correspondre les contours des profils. Balayage hélicoïdal
nécessite son cercle de base et son point de départ, un guide radial, un pas et
une section transversale.

**Précision personnalisée** modifie la tolérance d’approximation du corps en
millimètres, pas seulement la qualité d’affichage. Préservez la précision exigée
par votre modèle.

Voir le [balayage 2D](../../SWEEP_2D.md), le [balayage 3D](../../3D_CURVE_AND_SWEEP.md)
et le [balayage hélicoïdal](../../HELICAL_SWEEP.md).
