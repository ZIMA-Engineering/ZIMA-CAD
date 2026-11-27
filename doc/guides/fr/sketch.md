# Esquisse

## Avant de commencer

Activez un corps modifiable. Ouvrez **Esquisse** depuis les propriétés de la
fonction qui la contient, après avoir choisi son placement et son plan de travail.

## Étapes

1. Choisissez l’outil de géométrie : par exemple **Rectangle**, **Cercle** ou
   **Polyligne**. Confirmez les points de définition avec le bouton gauche de la souris.
2. Utilisez les points existants et les contraintes proposées pour établir des
   connexions exactes. Marquez la géométrie d’aide comme géométrie de construction
   afin qu’elle ne fasse pas partie du profil.
3. Ajoutez des cotes. Après avoir placé une nouvelle cote pilotante, saisissez
   sa valeur et appuyez sur Entrée. Les cotes de référence et les cotes
   verrouillées restent en lecture seule.
4. Sélectionnez un point et faites-le glisser pour vérifier la liberté restante.
   Modifiez à nouveau une cote pour confirmer que le profil se comporte comme prévu.
5. Cliquez sur **OK** pour revenir aux propriétés de la fonction, puis à nouveau sur **OK** pour la valider. **Annuler** abandonne la session de modification de l’esquisse.

## Points à vérifier

Le profil doit convenir à la commande qui l’utilise : un solide nécessite
généralement une zone fermée, tandis que les opérations de paroi mince prises en
charge peuvent utiliser une chaîne ouverte. Les références externes restent en
lecture seule. Les contraintes ou les cotes contradictoires sont rejetées ; une
géométrie entièrement contrainte ne peut pas se déplacer librement.

Voir le [mode Esquisse](../../USER_MANUAL.md#sketch-mode) et les
[cotes et la sélection](../../USER_MANUAL.md#dimensions-and-selection).
