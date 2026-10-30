# API Studio et IHM Studio 1.14.1 : vannes à fins de course, calculatrice, unités, explorateur de fichiers, bibliothèque d'images

Livrée le 30/10/2026. Elle suit la 1.14.0 (`docs/LIVRAISON-1.14.0.md`).

Elle répond à :

> « ok dans les vannes 2 vois 3 voies, je veux faire une variante pour des vannes avec plusieurs fin de courses et pas une seule valeur, pareil pour registre Ouvert/fermé et les vannes ouvertes/fermées, jouer les sons sur enregistrements
> quand on peut étirer, déplacer mettre la bonne fléche windows »
>
> « ajouter un objet calculatrice / calculatrice scientifique / calculatrice programmeur
> dans unités de programmation, ajouter un petit onglet pour faire des conversions entre les unités pour faire des tests
> pareil ajouter un objet pour convertir d'unité en unité
> faire un objet qui permet de gérer des zips ou des dossiers, pour afficher le contenu avec des infos en tooltip »
>
> « ah oui aussi dans les objets classiques, ajouter une bibliothèques d'images donc sous onglet images avec possibilité d'importer dans l'appli dans la partie resources, ajoute bcp d'images standard classique trie par catégorie »

Tout est fait.

## 1. Ce qui change

### Les vannes et les registres commandés par leurs fins de course

La **Vanne**, la **Vanne 3 voies**, le **Registre**, le **Registre multi-lames** et le **Clapet coupe-feu** ont une variante dont l'état vient de leurs fins de course, comme sur le terrain, au lieu d'une seule valeur.

- **Dans l'inspecteur** : la case **Commande de l'état** (pour la vanne 3 voies : **Commande de la position**) passe de « une valeur » à « fins de course ». Apparaissent alors :
  - vanne et registres : **Fin de course ouvert**, **Fin de course fermé** ;
  - vanne 3 voies : **FC voie 1-2**, **FC voie 1-3**, **FC voie 2-3**, et **Fin de course fermé** (facultative) ;
  - facultatifs : l'**Ordre** (d'ouverture, de position) et le **Temps de manœuvre** ; les **Voyants des fins de course** (O et F) à cacher ou montrer.
  - Les cases sans effet dans ce mode (la Valeur, En mouvement) sont grisées et le disent.
- **La règle** :
  - une seule fin de course vraie donne l'état ;
  - aucune : **en mouvement** (l'ambre qui clignote ; une petite flèche vers l'ordre s'il y en a un) ;
  - les deux à la fois, ou un ordre pas suivi après le temps de manœuvre : **discordance** (le rouge qui clignote et une pastille ≠) ;
  - une seule fin de course reliée : l'autre position s'en déduit ;
  - le Défaut et le déclenchement du clapet passent avant.
- **Une vanne réglante** (ou un registre modulant) garde son ouverture en % ; FCF force 0 %, FCO 100 %. Entre les deux, sans ordre, elle se montre à son ouverture, sans clignoter.
- **Autour** : une alarme par défaut **Discordance** (son délai : le temps de manœuvre) ; **Pourquoi ?** dit quelle fin de course décide ; **Générer** signale une fin de course manquante, une qui n'est pas un booléen, un temps illisible ; les exemples animés de l'aide montrent la variante ; chercher « fins de course », « FCO » ou « FCF » dans la bibliothèque trouve les cinq objets.
- Un projet 1.14.0 s'ouvre et se réenregistre à l'identique (en mode « une valeur »).

### La bonne flèche Windows

Le pointeur prend la forme de ce qu'on peut faire :
- **les séparateurs** des panneaux et des groupes d'onglets : la double flèche ↔ ou ↕ ;
- **le bord d'une colonne** d'un tableau : ↔ ;
- **les champs et les éditeurs de code** : la barre en I sur le texte (la flèche sur la marge et les barres de défilement) ;
- **l'éditeur de vues** : les 8 poignées d'un objet (tournées avec lui), la main sur la poignée de rotation, « déplacer » sur le corps de l'objet choisi (la flèche sur un objet verrouillé), les règles et les guides, la croix pour poser un objet, les sommets d'une ligne ;
- **en marche** : les curseurs A et B d'une courbe, la fenêtre de temps qu'on tire, la barre de titre d'une popup, la page d'une visionneuse, la barre de progression d'une relecture.

La forme reste pendant tout le glisser. Corrigé au passage : un séparateur ou une colonne lâché hors de son panneau restait pris et suivait la souris au retour.

### Entendre les sons à la relecture

Cinq défauts empêchaient d'entendre (ou de bien entendre) les sons d'un enregistrement :
1. le son du tout premier instant (une alarme déjà là au départ) n'était jamais rejoué ;
2. au-delà de ×2, plus aucun son : maintenant, à ×4 et ×8, **les sons d'alarme** se rejouent ;
3. la pause, un saut ou la sortie laissaient jouer ce qui jouait : tout se tait maintenant, et ce qui est sauté ne se rejoue pas ;
4. un son qui n'est plus une ressource du projet était ignoré en silence : la Console le dit une fois ;
5. le canal Relecture coupé (ou tout le son) est respecté par le moteur lui-même.

**Le bouton Sons** de la barre de relecture (touche **S**) coupe ou rétablit le canal Relecture : le même réglage que le M de la piste Relecture du Mélangeur et que SYS.MuteReplay. Il dit « Coupés » quand le canal est coupé, « Alarmes » à ×4 et ×8. Configuration › Enregistrements : la case « Sons (pour les entendre à la relecture) » ; un enregistrement fait sans les sons le dit à la relecture.

### La Calculatrice

Un objet de la famille **Commandes**, en trois tuiles déjà réglées : **Calculatrice** (standard), **Scientifique**, **Programmeur**.
- **Standard** : les quatre opérations, %, ±, 1/x, x², √x, C, CE, ⌫, la mémoire (MC, MR, M+, M−).
- **Scientifique** : en plus les parenthèses, sin, cos, tan et leurs inverses (touche 2nde), DEG/RAD, log, ln, 10^x, e^x, x^y, ʸ√x, n!, π, e, mod, |x|, Exp.
- **Programmeur** : la valeur en HEX, DEC, OCT et BIN à la fois (un clic sur une ligne la rend active), la taille du mot (BYTE à LWORD) et le signe, AND, OR, XOR, NOT, NAND, NOR, <<, >>, ROL, ROR, SWAP, et **la grille des bits** cliquable.
- L'ordre des opérations est respecté (2 + 3 × 4 = 14) ; la virgule à la française ; « Erreur » avec sa raison.
- **Le clavier** marche quand on a cliqué dessus.
- **Avec l'IHM** : la **Valeur de départ** (une expression lue à l'ouverture de la vue) ; **Résultat vers** (le bouton Envoyer écrit le résultat dans une variable, avec le niveau d'accès et les règles d'écriture) ; `Vue.Calc.Result` lit le résultat.

### Convertir des unités

- **Configuration › Unités de mesure › Convertir** (un onglet à côté de la Bibliothèque) : une valeur, l'unité de départ, l'unité d'arrivée (seulement celles de la même dimension), le résultat à chaque frappe, ⇄ pour inverser, les décimales, la formule (« 1 bar = 1000 mbar »), l'expression à copier `EN(Valeur, 'mbar')`, l'historique de la séance. Le calcul est celui de EN.
- **L'objet Convertisseur d'unités** (Commandes) : la même chose en marche, pour l'opérateur. Sa **Valeur de départ** peut suivre une mesure (et prendre son unité) ; **Résultat vers** ajoute un bouton Écrire.
- Les nombres montrés sont à la française (34,084 psi) ; un refus dit l'unité comme on la lit (°C).

### L'Explorateur de fichiers

Un objet de la famille **Médias** qui montre le contenu d'un **dossier** ou d'un **.zip** :
- une ligne par élément : l'icône de son genre, le nom, la taille, la date ; un clic sur un titre trie ;
- le fil d'Ariane, « .. » et Remonter, sans jamais sortir de la source ; un double-clic ouvre un dossier ou un zip (même un zip dans un zip) ;
- **l'infobulle** au survol : le chemin, le genre, la taille (et dans un zip, la taille compressée et le taux), les dates, le nombre d'éléments d'un dossier, les dimensions d'une image, les premières lignes d'un texte ;
- un filtre d'extensions (`*.csv;*.pdf`), « Nom contient », Rafraîchir, **Choix vers** (le chemin de la ligne choisie dans une variable), les actions au double-clic d'un fichier ;
- **en lecture seule** ; « Extraire » une entrée d'un zip est décoché d'office, n'écrase jamais rien et n'écrit jamais dans le projet ; un zip piégé (chemins qui remontent, tailles énormes) est signalé et ne casse rien.

### La bibliothèque d'images

La bibliothèque de l'éditeur de vues a trois onglets : **Objets | Images | Variables**.
- **320 images dessinées pour l'application** (vectorielles, nettes à toute taille ; aucune image tierce), en 12 catégories repliables : Flèches et navigation, Interface, Sécurité et signalisation, Industrie et process, Énergie et fluides, Bâtiment et infrastructure, Réseau et informatique, Véhicules et logistique, Météo, Personnes, Drapeaux, Formes et pastilles.
- La recherche de la bibliothèque y lit le nom, la catégorie et des mots-clés ; trois tailles de vignettes ; l'infobulle dit le nom, la catégorie et la taille.
- **Poser une image** : un clic puis un clic dans la vue (ou la glisser, ou un double-clic : au centre). L'objet Image est créé et l'image entre dans **Ressources › Images** ; un seul Ctrl+Z défait les deux. La même image déjà là est réutilisée.
- **Importer** (le bouton, ou le clic droit) l'ajoute aux ressources sans la poser.
- Les dessins d'une couleur prennent la couleur choisie (« Auto » : celle qui se lit sur le fond de la vue).
- **Mes images** : Ressources › Ma bibliothèque (ou le bouton +, ou un fichier lâché sur l'onglet) y ajoute tes propres images, gardées hors du projet, pour tous tes projets.

### L'aide et les notes

Trois nouveaux sujets F1 (260 → 263) : la Calculatrice, le Convertisseur d'unités, l'Explorateur de fichiers ; des sections nouvelles dans les vannes et registres (Commande par les fins de course), les enregistrements (les sons à la relecture), Unités de mesure (l'onglet Convertir), la bibliothèque d'objets (la bibliothèque d'images), les onglets (le pointeur). Les passages nouveaux sont marqués « nouveau en 1.14.1 ». 14 notes de version 1.14.1.

## 2. Les essais

**93 suites sur 93**, sur le tout fusionné. `hmieditor` passe de 9420 à 9847 contrôles, `hmi` de 6391 à 6840, `hmi_recording_test` compte 328 vérifications, `unites` 180. 477 notes, 68 versions, 263 sujets d'aide ; le guide et son C++ disent la même chose.

Les essais ajoutés, lot par lot :
- **vannes** : chaque cas des fins de course (2 et 3 voies), l'ordre et le temps, une vue en marche, Pourquoi ?, Générer, l'alarme, le fichier d'un projet 1.14.0 réécrit à l'identique, le code brut, Dupliquer, les exemples animés ; à l'écran : l'inspecteur, la bibliothèque, les couleurs et voyants de chaque état ;
- **pointeur** : 75 contrôles - le séparateur, la colonne, les champs et le code, les groupes d'onglets, les 8 poignées (droites, à 90°, à 45°), le verrou, les règles et guides, poser, Espace, les sommets, la courbe en marche, les dialogues (non-régression) ;
- **sons à la relecture** : un faux haut-parleur qui note chaque son - les trois sources, l'instant 0, la pause, la reprise, un saut, chaque vitesse, Relecture coupé, quitter, une ressource manquante, un enregistrement sans les sons ; la barre et le bouton Sons ;
- **Calculatrice** : 171 contrôles du moteur (priorités, fonctions, erreurs, mémoire, bases, mot, signe, débordement, bits) et de l'objet en marche (Envoyer, refus), 28 à l'écran (souris, clavier, bits) ;
- **unités et explorateur** : les conversions comparées à EN pour toutes les paires, les refus, l'objet en marche ; un faux dossier avec un zip, un zip dans un zip et un zip piégé - la liste, le tri, la navigation (et le refus de sortir de la source), l'infobulle, le filtre, Choix vers, l'extraction permise puis refusée ;
- **images** : les 320 images une par une (chacune se lit et se dessine), l'onglet, la recherche, poser (un Ctrl+Z), réutiliser, Importer, Mes images.

**Le chemin jusqu'ici** :
1. Six lots faits à part, en parallèle : vannes (N1), pointeur (N2), sons (N3), Calculatrice (N4), unités et explorateur (N5), images (N6). Chacun a rendu des aperçus dessinés avec la vraie police, regardés avant la fusion.
2. Des défauts vus sur les aperçus et corrigés avant la fusion : dans le Convertisseur, des nombres avec un point à côté d'autres à la virgule, « degC » au lieu de « °C » dans un refus, un vide dans le fil d'Ariane de l'Explorateur ; dans la Calculatrice, des diagonales parasites sur les touches.
3. La fusion des six lots, puis les 93 suites. La Calculatrice et le Convertisseur ont chacun une propriété « Résultat vers » : chacun garde la sienne (la Calculatrice écrit un nombre ou un texte, le Convertisseur un nombre).
4. Les captures finales, faites avec l'exe Windows sous Wine, ont trouvé **un plantage** et **treize défauts**, tous corrigés avant la livraison :
   - **le plantage** : en marche, un clic d'une calculatrice à une autre fermait l'appli. Le clic ailleurs rafraîchit la vue, et l'objet cliqué était lu dans l'ancienne vue, déjà libérée. L'objet cliqué est maintenant copié avant ; le même piège valait pour l'Enregistreur, les alarmes et les panneaux. Valgrind voyait des lectures invalides avant la correction, plus aucune après ;
   - **vannes et registres** : « 4 s » refusé dans le Temps de manœuvre (« n'est pas un nombre ») : une durée s'écrit maintenant comme partout (4 s, 250 ms, T#30s, 1 min) ; une case grisée ne disait pas pourquoi : sa raison est en tête de son infobulle ; les voyants O et F du clapet coupe-feu passaient sous sa marque CF : ils sont à droite du déclencheur ; la pastille de la vanne 3 voies en discordance avait un cadre tireté : elle est en haut à droite comme les autres ; Pourquoi ? coupait l'état retenu : il prend toute la largeur, chaque ligne en infobulle ; le texte de l'exemple animé de l'aide sortait de son cadre : il passe à la ligne ;
   - **Explorateur de fichiers** : en marche, un chemin comme `Z:\tmp\nouveau` perdait ses `\t` et `\n`, lus comme une tabulation et un retour à la ligne : une valeur se montre maintenant telle quelle (dans un Texte, un afficheur, l'Explorateur ; le texte tapé dans l'éditeur garde ses `\n`) ; le refus d'extraction était coupé avant l'essentiel : il le dit d'abord, et la ligne d'état a une infobulle ;
   - **unités** : l'inspecteur du Convertisseur montrait « degC » : il montre °C ; Générer laissait passer des dimensions incompatibles en simple avertissement : c'est une erreur bloquante, comme EN() mal employé ;
   - **images** : Mes images ne voyait pas une image ajoutée par Ressources › Ma bibliothèque : chaque onglet Images ouvert se rafraîchit ; la ligne du bas parlait d'une image cachée par la recherche : elle revient au mode d'emploi ;
   - **relecture** : la note « Le projet a changé depuis l'enregistrement » était coupée au milieu : elle se dit en entier ou en bref, et en entier dans son infobulle.
5. Une seconde passe de captures, sur l'exe corrigé, a confirmé ces corrections (plus aucun plantage, même en cliquant sans précaution d'une calculatrice à l'autre) et trouvé **cinq défauts de plus**, corrigés aussi :
   - en marche, l'infobulle d'une ligne de l'Explorateur restait affichée quand la souris quittait la vue (et pouvait même apparaître pour une ligne jamais survolée) : les infobulles de la vue en marche ne se montrent plus que sous la souris ;
   - le voyant éteint du clapet coupe-feu prenait la couleur « fermé » (orange) et paraissait allumé : il est gris, comme celui des registres ;
   - le Temps de manœuvre tapé « T#30s » ou « 1 min » devenait « 30 » ou « 60 » : il reste tel qu'on l'a tapé ;
   - le message vert de la Calculatrice (« chargée : P_Reseau », « envoyé : … ») restait quelques secondes et cachait le calcul suivant : la touche suivante l'efface ;
   - dans l'inspecteur du Convertisseur, l'Unité de départ et l'Unité d'arrivée ne se tapaient pas (rien ne se passait, sans le dire) : un symbole se tape (« Taper un nom… », le dernier choix de la liste), une unité inconnue est gardée et dite en rouge sous la case.

## 3. Les captures

Dans `captures-1.14.1.zip` (117 images), prises sous Wine à 1920 × 1200, sur une copie du projet de démonstration (elle n'est pas dans le dépôt). Les dernières l'ont été avec les exe livrés. Un dossier par sujet :
- `1-vannes/` (33) :
  - l'inspecteur : la Vanne en « fins de course » (« 4 s » accepté, « 1 min » et « T#30s » gardés), la Valeur grisée et sa raison au survol, la vanne 3 voies (FC par voie, Ordre de position), le registre multi-lames, le clapet coupe-feu ;
  - la recherche « fins de course » dans la bibliothèque ;
  - en marche : une vanne en mouvement, en discordance après le temps de manœuvre, puis ouverte ; le clapet coupe-feu ouvert puis fermé (le voyant éteint gris), un clapet étroit (la pastille ≠ sous ses voyants) ; Pourquoi ? (la vanne, la vanne 3 voies, le registre, le clapet, l'état retenu en infobulle) ;
  - les exemples animés de l'aide : vanne, vanne 3 voies, registre, clapet (en veille, déclenché), et la page de l'aide.
- `2-relecture-sons/` (11) : Configuration › Enregistrements (la case Sons) ; la barre de relecture avec le bouton Sons actif et un son qui joue, puis « Coupés » (touche S), « Alarmes » à ×4 et ×8 ; un enregistrement de la 1.14.0 : la note « Projet changé depuis l'enregistrement » et son infobulle.
- `3-calculatrice/` (16) : la bibliothèque (les trois tuiles, la recherche « calcul ») ; les trois calculatrices en marche avec un calcul en cours ; la grille des bits ; l'ordre des opérations ; Envoyer vers une variable ; le clavier ; Charger la Valeur de départ (le message effacé à la touche suivante) ; une division par zéro ; des clics croisés rapides d'une calculatrice à l'autre, sans plantage ; les inspecteurs.
- `4-unites/` (24) : Configuration › Unités de mesure, onglet Convertir (une conversion, sa formule, EN(…), l'inverse, °C en °F, l'historique, un refus) ; l'inspecteur du Convertisseur (m³/h, °C) ; des dimensions différentes ; Générer qui s'arrête (erreur bloquante) ; une unité tapée (« Taper un nom… », furlong/h inconnue en rouge, degC et degF) ; l'objet en marche (il suit P_Reseau, la liste Vers, Écrire).
- `5-explorateur/` (17) : un dossier, un zip, l'intérieur du zip et son sous-dossier, avec une infobulle à chaque fois ; Extraire, puis le refus d'écraser ; l'infobulle de la ligne d'état ; « Nom contient » ; Choix vers avec le chemin entier et tel quel ; l'infobulle qui disparaît quand la souris quitte la vue ; les inspecteurs ; la bibliothèque (Médias).
- `6-images/` (11) : l'onglet Images (Mes images, Flèches, Sécurité ouverte), l'infobulle d'une vignette, le clic droit, la recherche « vanne », une vue avec des images posées, Ressources › Images, Ressources › Ma bibliothèque, Mes images à jour aussitôt, Retirer, les grandes vignettes en bleu.
- `7-notes/` (5) : les 14 notes de version 1.14.1 dans l'aide.

Le pointeur Windows n'y est pas : SDL ne le dessine pas dans une capture.

Les captures ont été prises trois fois. La première série a montré le plantage et treize défauts, la deuxième cinq de plus, tous corrigés puis repris à l'écran (voir § 2, étapes 4 et 5). La troisième n'a rien trouvé de nouveau.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.14.1.exe` : l'installateur ;
- `API-Studio-1.14.1-portable.zip` et `IHM-Studio-1.14.1-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.14.1.zip`) ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.14.1.exe`. Il met à jour la version installée au même endroit, garde tes données et reprend ton choix d'applications.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Le format des projets ne change pas (33) : les projets de la 1.14.0 s'ouvrent tels quels. Les images de la bibliothèque sont dans l'exe : l'installateur ne gagne aucun fichier.

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.14.1.exe` (39,3 Mo, 41 214 587 octets) :
`18B2945B1AF91A0FCB073CEA96A287D36A0E3FEB97ED452898C64F07BC277E26`

Les zips portables (167 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.14.1-portable.zip` (30,5 Mo) :
  `5C7D73460A48D33ECFF0D8955112B2ADD1CF18529DCEB877776D169556885DD9`
- `IHM-Studio-1.14.1-portable.zip` (30,5 Mo) :
  `7645E95B25D424AD54F48E3EA7E7DE65228538EF134D03D7322881385E633D26`

## 5. Les vérifications sous Windows

**Les deux exe Windows** sont compilés sous Linux avec MinGW-w64 et lancés sous Wine.
- `--version` répond `API Studio 1.14.1` et `IHM Studio 1.14.1`.
- `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant : 891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB.

**L'installateur** est compilé par Inno Setup 6.4.1 sous Wine. Il a été lancé en silencieux, pour tous les comptes, par-dessus la 1.14.0 livrée : « Installation process succeeded ».
- Le profil repris est « les deux » (« Profil : deux (avant : deux, version 1.14.0) »).
- Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été testés ; le manifeste compte 167 fichiers.
- Les associations `.xihm` et `.xapi` sont là.
- La copie de secours de la 1.14.0 est faite (168 fichiers, 123,2 Mo) et gardée : deux copies au plus, comme avant.

**À regarder sur un vrai Windows** (ce que Wine et les essais ne montrent pas) :
- le pointeur lui-même (aucune capture ne le montre) ;
- les sons de la relecture, entendus pour de vrai, et la pause qui les coupe net ;
- les voyants O/F et la pastille ≠ sur une petite vanne, à 100 % ;
- le défilement de l'onglet Images, le glisser d'une image jusqu'à la vue, un fichier lâché depuis l'Explorateur Windows sur l'onglet ;
- la racine √ et les petits exposants de la Calculatrice avec la police de Windows ; le clavier AZERTY (pavé numérique) ;
- la date de création des fichiers dans l'Explorateur de fichiers, et des chemins accentués.

## 6. Les limites et les choix

- **Les nombres à la virgule** : le Convertisseur et la Calculatrice écrivent 34,084 ; la Valeur numérique et la colonne Conversion de l'onglet Bibliothèque des unités écrivent encore 34.084, comme avant.
- **La Calculatrice** respecte l'ordre des opérations même en mode standard (la calculatrice standard de Windows enchaîne de gauche à droite). Quand elle a le clavier, Échap remet à zéro au lieu de fermer la popup.
- **La relecture à ×4 et ×8** ne rejoue que les sons d'alarme.
- **La bibliothèque d'images** : un nom déjà pris dans les Ressources devient `pompe_2.svg` (la règle des Ressources) ; une icône importée en rouge est une ressource à part (`fleche-haut-rouge.svg`).
- **L'Explorateur de fichiers** : une source vide montre les ressources du projet ; les listes s'arrêtent à 2000 éléments (réglable) et le disent ; Extraire ne prend qu'un fichier à la fois.
- **Les vannes** : la variante se choisit dans l'inspecteur (pas de tuile séparée dans la bibliothèque).
- **Les durées** : seul le Temps de manœuvre garde l'unité tapée (« 1 min », « T#30s »). Les autres cases de durée (période, fenêtre, maintien…) l'écrivent, comme avant, dans leur unité (« 1,5 s » devient 1500 dans une case en ms).
- **Les voyants éteints** : celui du clapet coupe-feu est toujours gris ; ceux de la vanne et du registre prennent leur Couleur fermé (grise par défaut), donc une autre couleur si tu la changes.
- **Taper une unité** dans l'inspecteur du Convertisseur passe par le dernier choix de la liste, « Taper un nom ou un repère… », comme pour toutes les listes de l'inspecteur : taper directement dans la liste ouverte ne fait rien.

**En attente**, comme avant :
- les raccourcis clavier avancés des éditeurs ;
- les requêtes HTTP, MQTT ou SQL depuis un script ;
- les grafcets côté IHM.
