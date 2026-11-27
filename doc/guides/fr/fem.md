# FEM

## Avant de commencer

Préparez un corps solide calculé dans une pièce et attribuez-lui des propriétés
de matériau adaptées. Choisissez une analyse correspondant au modèle physique
pris en charge.

## Étapes

1. Désactivez le corps courant pour atteindre la racine de la pièce et choisissez **Créer un FEM**. Sélectionnez le corps source et l’analyse dans **Propriétés du corps FEM**, puis confirmez avec **OK**.
2. Définissez ses conditions : blocages mécaniques, forces ou pression pour un
   cas mécanique ; conditions thermiques prises en charge pour la conduction thermique.
3. Générez explicitement le maillage volumique. Vérifiez les diagnostics du
   maillage avant la résolution.
4. Lancez l’analyse adaptée dans le panneau FEM : **Statique linéaire**, **Analyse thermique stationnaire**, **Analyse thermomécanique**.
5. Sélectionnez les résultats de déplacement, de contrainte ou de température
   selon le cas. Inspectez les extrema et les points de mesure placés ; comparez
   les résultats après raffinement du maillage.

## Points à vérifier

L’analyse mécanique est une analyse statique élastique linéaire isotrope en petits
déplacements. L’analyse thermique est stationnaire. La dilatation thermique
optionnelle est un couplage unidirectionnel en petites déformations ; le mode
combiné inclut également les forces et la pression.

L’amplification de la déformée modifie l’affichage, pas le déplacement calculé.
L’animation illustre un cas statique, pas un comportement dynamique. Le contact,
la plasticité, le chauffage transitoire et les propriétés dépendant de la
température sont hors de ce périmètre.

Modifiez les définitions dans les propriétés et recalculez lorsque les données
d’entrée changent. L’enregistrement natif conserve les définitions FEM et les
références de mesure prises en charge. La réussite du maillage ou de la résolution
ne suffit pas à démontrer la précision du calcul d’ingénierie.

Voir les [capacités et limites du FEM](../../FEM_STATUS.md).
