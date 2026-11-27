# FAO

## Avant de commencer

Préparez dans une pièce les corps calculés du brut et de la cible, ainsi que les
corps des dispositifs de bridage si nécessaire. Désactivez le corps courant pour
accéder à **Créer une FAO**.
Pour fraiser une poche ou un contour, préparez une esquisse de profil native antérieure.

## Étapes

1. Choisissez **Créer une FAO**. Sélectionnez le brut, la cible et les dispositifs
   de bridage, puis placez le repère de travail de la configuration.
2. Ouvrez **Machine** et **Outils FAO**. Vérifiez les limites de course, la
   géométrie des outils, les conditions de coupe et le décalage de l’origine de travail.
   Si nécessaire, définissez les esquisses de contact et du dessus des mors,
   ainsi que leurs côtés orientés, dans **Esquisses de serrage**.
3. Dans **Opérations d’usinage**, choisissez **Ajouter un surfaçage**, **Ajouter une poche**, **Ajouter un contour** ou **Ajouter une opération manuelle**.
   Pour une poche ou un contour, cliquez sur le champ de référence **Profil d’usinage** et sélectionnez une esquisse antérieure dans l’arbre ou la vue.
   Définissez l’outil, les niveaux supérieur et inférieur, les profondeurs de
   passe, les surépaisseurs, le dégagement et le sens de la trajectoire de l’outil.
   Définissez **Pas latéral** pour une poche ; pour un contour, choisissez
   **Intérieur** ou **Extérieur**. Vérifiez l’ordre des opérations.
   **OK** dans l’éditeur reporte les changements dans la configuration en cours ;
   confirmez avec **OK** dans les propriétés de la configuration pour les valider.
4. Choisissez **Calculer la FAO**. Inspectez le **Programme CN générique**, le
   mouvement d’outil associé et le brut restant calculé.
5. Vérifiez les rapports de collision et de bridage. Utilisez **Exporter le texte CN générique…** en respectant sa portée déclarée de programme générique.

## Points à vérifier

Les trajectoires jaunes représentent l’avance de coupe ; les trajectoires rouges
en pointillés représentent les déplacements rapides. Le calcul explicite vérifie
les limites de la machine et simule la matière balayée par l’outil ; parcourir
le résultat pas à pas ne recalcule pas la matière.

Le fraisage de poches et de contours est un usinage 2,5D à trois axes de régions
fermées d’esquisses natives parallèles au plan XY de la configuration, y compris
les îlots et les boucles disjointes. Il nécessite une partie coupante cylindrique
à bout plat. L’entrée se fait par plongée en coupe, et les déplacements de liaison
remontent à la hauteur de dégagement. **Tolérance de trajectoire** borne
l’approximation des courbes non circulaires ; les lignes et les arcs de cercle
conservent des trajectoires analytiques.

Les esquisses de serrage définissent des zones orientées d’exclusion ou de contact
et les vérifications du dessus des mors. Les corps réels des dispositifs de
bridage restent des obstacles supplémentaires. Une intersection de la partie
coupante ou de la queue avec un dispositif ou un volume d’exclusion est une
collision. L’enlèvement de matière sous le dessus des mors et la perte de contact
produisent des avertissements distincts ; un contact géométrique restant ne
prouve pas une force de maintien suffisante. Les résultats présentant des
collisions ne peuvent pas servir de brut à une configuration ultérieure.

**Annuler** dans la configuration abandonne les modifications en cours. Rouvrir
un résultat à jour ne relance pas la simulation. Les opérations manuelles prennent
en charge les mouvements linéaires et circulaires dans XY. Les postprocesseurs
de commande numérique, le tournage, les mouvements à 4/5 axes, l’entrée adaptative
ou en rampe, le placement automatique de dispositifs de bridage depuis la
bibliothèque et la conversion de solides arbitraires en outils restent indisponibles.
L’export CN générique n’est pas un code machine prêt à être exécuté par une commande numérique.

Voir le [déroulement du travail en FAO](../../USER_MANUAL.md#first-stage-cam-workflow)
et le [périmètre actuel du fraisage de profils](../../CAM_BODY_DESIGN.md#profile-milling-stage-2026-11-26).
