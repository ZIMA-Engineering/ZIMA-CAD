# Rendu

## Avant de commencer

Calculez les corps solides sources dans une pièce. Définissez leurs couleurs et
leur apparence, puis préparez une vue initiale.

## Étapes

1. Activez la racine de la pièce et choisissez **Créer un rendu**.
2. Conservez **Corps visibles**, ou utilisez **Corps sélectionnés** pour choisir
   explicitement les sources. Définissez les dimensions de l’image et la qualité
   d’échantillonnage, puis confirmez avec **OK**.
3. Ajoutez ou modifiez les caméras et les lumières selon les besoins. Sélectionnez
   la caméra active dans les propriétés du corps de rendu.
4. Choisissez **Calculer le rendu**. L’onglet Rendu distinct affiche la
   progression et le résultat ; **Galerie de rendus** rouvre cet onglet.
5. Exportez l’image terminée en PNG ou JPEG. Utilisez **Enregistrer dans la pièce**
   pour conserver également l’image dans la pièce native.

## Points à vérifier

Le rendu utilise un instantané de la géométrie déjà calculée. Modifier la source
ne réécrit pas une ancienne image de rendu. Fermer l’onglet Rendu laisse la tâche
en cours ; **Arrêter le rendu** l’annule, et fermer le document propriétaire arrête
la tâche.

Un plus grand nombre d’échantillons peut réduire le bruit visible. Les images
conservées restent des enregistrements historiques et augmentent la taille du
document. Le moteur de rendu initial prend en charge les scènes de solides de
pièces ; les scènes d’assemblage, l’animation, les textures et la réfraction du
verre sont hors de ce périmètre.

Voir le [rendu](../../RENDER.md).
