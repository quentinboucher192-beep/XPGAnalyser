# API Studio et IHM Studio 1.12.27 : plusieurs fenêtres, Pourquoi ?, l'aide F1 rangée

Livrée le 23/10/2026 dans la nuit. Elle suit la 1.12.26 de la veille (`docs/LIVRAISON-1.12.26.md`).

Elle répond à :

> « l'outil 'Pourquoi ?', gérer le clic droit sur le menu principal, gérer la touche SUPPR, DEL dans les listes (a chaque fois je dois cliquer sur la poubelle), tableaux, contenues, actions, le CTRL + D ne dupplique pas dans les listes / range l'aide F1 c'est le bordel / dans les styles, ajouter un appercu / pouvoir ouvrir plusieurs instances de IHM en meme temps comme ca on peut copier coller des choses de projets en projets, faire l'option glisser deposer aussi entre deux fenetres IHM Studio / améliore l'installateur, met de la couleur, gif, images, polices personnalisés (commence par ca envoie des screens) mais en meme temps soit plus générique sur les procédés étapes etc »

## 1. Ce qui change

### Plusieurs fenêtres d'IHM Studio

- **Projet › Nouvelle fenêtre** (**Ctrl+Maj+N**), ou le clic droit du menu principal › Nouvelle fenêtre : une deuxième fenêtre, où tu ouvres l'autre projet. Sur la carte d'un projet, **Ouvrir dans une nouvelle fenêtre** l'ouvre à côté de celui en cours. Relancer IHM Studio (son icône, un double clic sur un projet) ramène toujours la fenêtre déjà ouverte : une autre fenêtre se demande.
- **Copier dans une fenêtre, coller dans l'autre** : Ctrl+C sur des objets, Ctrl+V dans une vue de l'autre projet (ou clic droit › Coller ici). Ce dont ils ont besoin les suit : les symboles de leurs instances, les popups que leurs actions ouvrent, leurs images, leurs styles nommés, les variables IHM qu'ils lisent ou écrivent, leurs types et les fonctions IHM appelées. Ce que le projet a déjà, du même nom, reste le sien. La barre d'état dit ce qui est ajouté et ce qui est gardé ; **un seul Ctrl+Z** annule le collage entier.
- **Glisser d'une fenêtre à l'autre** : tire les objets choisis hors de la fenêtre et lâche-les sur une vue de l'autre ; ils s'y posent au point du lâcher, comme un collage. La première fenêtre ne bouge pas (c'est une copie). Échap annule le glisser. Lâchés sur Excel ou le Bloc-notes, les objets donnent leur liste (nom, genre, place, taille, valeur).
- **Le même projet dans deux fenêtres** : une question d'abord, parce que deux fenêtres qui enregistrent le même projet effacent les changements l'une de l'autre. « L'ouvrir quand même » reste possible.

### Suppr et Ctrl+D dans les listes

Plus besoin de cliquer la corbeille.
- Dans l'onglet **Contenu** (les lignes et les colonnes d'un tableau, les plumes d'une courbe, les états…) et l'onglet **Actions** : **Suppr** retire l'élément choisi ; **Ctrl+D** le duplique tout de suite, juste après lui, ses indices suivis ; **Ctrl+Maj+D** ouvre la fenêtre des copies multiples. Le clic droit propose Dupliquer et Dupliquer….
- Dans **les tableaux des volets de l'IHM** (styles, alarmes, recettes, utilisateurs, rapports, scripts…) : Suppr, Ctrl+D, **Inser** (ajouter) et **Alt+↑ / Alt+↓** (déplacer), comme les boutons de leur barre ; un bouton grisé, une touche sans effet.
- Dans l'**Explorateur d'objets**, Ctrl+D duplique la sélection.
- Après un clic sur la barre d'une liste, le clavier revient à la liste.

### L'aperçu des styles

**IHM › Styles** montre, au-dessus de la fiche, le style choisi sur un bouton, un texte, un rectangle et un voyant allumé puis éteint, dessinés comme dans une vue, sur le fond d'une vue du projet. L'aperçu suit chaque changement. Dans la liste, chaque style a sa pastille : son remplissage, son contour, la couleur de son texte et celle d'allumé.

### Le clic droit sur le menu principal et sur la barre du haut

- **Le menu principal (l'accueil)** : hors des cartes, le menu de la page - Nouveau projet, Ouvrir un projet, Nouvelle fenêtre, Ouvrir le dossier des projets, Afficher, Trier, Thème, Chercher un projet, Didacticiel, Réglages, Dossiers de l'application, Aide. Sur une carte, ou sur le panneau du projet choisi : le menu du projet. **Maj+F10** l'ouvre au clavier (sans carte choisie : le menu de la page).
- **La barre du haut et ses menus** : le clic droit sur une partie (Projet, Annuler, Simuler, la cloche…) ou sur une entrée d'un menu ouvert : son nom, Exécuter, **Épingler dans Aller à** (ou l'en retirer), Copier le nom, Raccourcis clavier, Aide.
- **Aller à (Ctrl+K)** montre les commandes épinglées en tête, avant les derniers choix (douze au plus, gardées d'une séance à l'autre).

### Pourquoi ? en simulation

Clic droit sur un objet de la vue en marche › **Pourquoi ?**. L'onglet **Pourquoi ?** de la simulation dit ce que l'objet montre, et pourquoi, suivi à chaque image :
- chaque propriété calculée, sa formule et sa valeur ;
- la valeur de **chaque terme** : les deux côtés d'une comparaison, les opérandes d'un AND ou d'un OR, la condition d'un `? :`, les conditions des états, chaque nom lu (« forcée » s'il l'est) ;
- **l'état retenu** : allumé et sa couleur, la condition vraie et l'état d'un multi-états, la zone d'un bargraphe, caché.

Double clic sur un nom : le forcer. Clic droit : Forcer, Relâcher, Suivre sur la courbe, Ajouter aux expressions, Copier la valeur ou le rapport, Aller à l'objet dans l'éditeur. Si la vue change, le dernier rapport reste affiché et l'en-tête le dit.

### L'aide F1 rangée

L'aide de l'IHM avait 115 sujets d'un bloc dans La bibliothèque d'objets, et des chapitres mêlés. Elle est rangée en chapitres et sous-chapitres :

| Chapitre | Ce qu'il range |
| --- | --- |
| Démarrer | les gestes de base : l'IHM dans le projet, le menu Aide, le didacticiel, les onglets, les raccourcis clavier, Aller à, les dossiers, l'historique, les versions, Excel, plusieurs fenêtres, les plantages |
| Concevoir les vues | Les vues et l'éditeur ; Modèles, popups et symboles ; Aller plus vite ; Langues, unités, affichage |
| Communiquer | nouveau : la communication avec les équipements - le principe, les équipements, les esclaves simulés, les outils du réseau |
| La bibliothèque d'objets | une famille par famille de la palette : Formes, Commandes, Affichage, Graphiques, Alarmes, Production, Régulation, Synoptique, Navigation et structure, Communication, Médias, Utilisateurs |
| Programmer | Variables et types ; Scripts et fonctions ; Variables système et d'instances (avant : un chapitre à part) |
| Superviser | les alarmes, les recettes, les historiques ; les notifications et les rapports y viennent |
| Ressources et données | inchangé |
| Tester et livrer | Tester ; Livrer et exploiter |
| Les nouveautés | nouveau : les pages des nouveautés, sorties de Démarrer |

Dans le centre d'aide (F1), chaque niveau se replie d'un clic, et le fil d'Ariane dit où l'on est (L'IHM › La bibliothèque d'objets › Synoptique › Vanne). F1 sur un objet ouvre toujours sa page ; seul son chemin change. « La vanne 3 voies » s'appelle maintenant comme sa tuile, « Vanne 3 voies ».

Les sujets ne perdent rien : 234 avant, 234 après, chacun avec son texte. Le texte source de l'aide (`tools/guide-ihm/guide-ihm.txt`) a été remis à jour depuis le programme, avec un outil qui vérifie que les deux disent la même chose (`tools/guide-ihm/depuis_cpp.py`).

### Le menu du clic droit de l'accueil s'affiche

Celui d'une carte de projet (Ouvrir, Ouvrir le dossier, Renommer…, Retirer de la liste, depuis la 1.12.18) s'ouvrait sans se dessiner : rien ne paraissait. Il s'affiche maintenant, comme le nouveau menu de la page.

### L'installateur

Il a son habillage : à gauche, aux couleurs des deux logos, le nom, la version et les étapes de l'installation (faites, en cours, à venir) ; sa police (Barlow) ; des bandeaux d'accueil et de fin ; une icône par étape. Pendant la copie : une animation, une barre en dégradé avec son pourcentage, et des astuces. Les étapes sont décrites à un seul endroit du script de l'installateur, pour en ajouter ou en retirer sans toucher au reste. Il pose toujours les deux applications et met à jour celle qui est installée, sans toucher à tes données.

## 2. Les essais

80 suites sur 80. Ce qui change dans les essais :
- `hmi` (6009 contrôles) : l'aide rangée - dix chapitres, chaque sujet à sa place (un objet dans la famille de sa tuile, les variables système dans Programmer), les sujets d'un chapitre qui se suivent ; Pourquoi ? (le moteur : les termes, les noms lus, l'état retenu de chaque genre d'objet).
- `hmieditor` (8262 contrôles) : l'arbre du centre d'aide à plusieurs niveaux (une famille dépliée, les autres repliées, le fil d'Ariane), la liste et le fil du volet Aide IHM ; Suppr et Ctrl+D dans les listes ; l'aperçu des styles ; Pourquoi ? dans la vue en marche ; plusieurs fenêtres (la présence, la copie publiée, le collage d'un projet à l'autre en un seul Ctrl+Z, le glisser hors de la fenêtre et le lâcher) ; les notes (404) et les versions (60).
- `hminatives` (2767 contrôles) : inchangé.
- L'aide : le texte source et le programme disent la même chose, sujet par sujet (`tools/guide-ihm/depuis_cpp.py --verifier`).

## 3. Les captures

Prises avec l'exe Windows livré, lancé sous Wine.
- `1227_01_accueil.png` : l'accueil de la 1.12.27 ;
- `1227_02_accueil_clic_droit.png` : le clic droit sur le vide de l'accueil, le menu de la page ;
- `1227_03_menu_projet_nouvelle_fenetre.png` : le menu Projet, avec Nouvelle fenêtre (Ctrl+Maj+N) ;
- `1227_10_aide_bibliotheque_synoptique.png` : F1, la page de la Vanne : L'IHM › La bibliothèque d'objets › Synoptique, les autres familles repliées ;
- `1227_11_aide_programmer.png` : Programmer › Scripts et fonctions › Fonctions IHM ;
- `1227_12_aide_communiquer.png` : Communiquer › Les équipements ;
- `1227_20_styles_apercu.png` : IHM › Styles, l'aperçu du style choisi et la pastille de chaque style ;
- `1227_30_pourquoi_menu.png` : le clic droit sur un voyant de la vue en marche (la démonstration de l'affaire) ;
- `1227_31_pourquoi.png` : l'onglet Pourquoi ? : la formule de la valeur et sa valeur, chaque terme, chaque nom lu, l'état retenu (allumé, #1E8E46), puis la couleur éteinte et sa formule ;
- `installateur/` : les neuf écrans de l'installateur, de la bienvenue à la fin, et l'animation de la copie (GIF).

Deux défauts vus sur les captures et corrigés avant la livraison :
- **le menu du clic droit de l'accueil ne s'affichait pas** : celui d'une carte de projet (depuis la 1.12.18) et le nouveau menu de la page s'ouvraient sans se dessiner. Ils s'affichent (`1227_02`) ;
- le chapitre « Communiquer avec les équipements » était coupé dans l'arbre de l'aide : il s'appelle « Communiquer », comme Démarrer, Programmer et Superviser.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.12.27.exe` : l'installateur, qui pose les deux applications ;
- `API-Studio-1.12.27-portable.zip` et `IHM-Studio-1.12.27-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.12.27.zip`) ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.12.27.exe`. Il met à jour la version installée (la 1.12.26 ou une plus ancienne) au même endroit, et garde tes données. Les projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.12.27.exe` (32,8 Mo, 34 409 365 octets) :
`088E0FB114B0EA531DC86963D00F4D8407C5F8A5B4960107A91530119FE0600A`

Les zips portables (151 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.12.27-portable.zip` (24,2 Mo) :
  `8F4CAE3F10776A71D39D7FDBA6E87DC5B9C5D242ED4F50A02EC16764F4FEB7F9`
- `IHM-Studio-1.12.27-portable.zip` (24,2 Mo) :
  `6CBC0FA315F74FF2A7F2BEBF6DEED65CA6A2DEBF598FF1E49B4C1B06E6BF8C31`

Les exe sont plus légers qu'en 1.12.26 (52 Mo au lieu de 64) : celui de la 1.12.26 avait gardé sa table des symboles ; ceux-ci l'ont retirée, comme toutes les versions d'avant. Le contenu est le même, en plus des nouveautés.

## 5. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine : `--version` répond `API Studio 1.12.27` et `IHM Studio 1.12.27` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB). Deux séances scriptées ont pris les captures ci-dessus : l'accueil et ses menus, l'aide F1, les styles, la simulation de la démonstration de l'affaire et Pourquoi ?.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.26 (installée avant : `IHM Studio 1.12.26`) : « Installation process succeeded » ; le dossier d'installation a `API-Studio.exe`, `IHM-Studio.exe` et le désinstalleur ; le menu Démarrer garde son dossier **API IHM Studio** ; les deux exe installés sont identiques à ceux qui ont été testés et répondent `1.12.27`.

**Pas fait :** le glisser-déposer d'une fenêtre à l'autre et deux fenêtres ouvertes en même temps n'ont pas pu être joués sous Wine (les essais couvrent le paquet publié, le lâcher et le collage, pas le glisser du système entre deux vraies fenêtres). Ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici) ; il liste bien tous les fichiers (484 .cpp, comme CMake).

## 6. La suite

- Le glisser d'une fenêtre à l'autre et la question du même projet ouvert deux fois, à essayer sur un vrai Windows : sous Wine, un seul glisser-déposer entre deux fenêtres ne se rejoue pas.
- Communiquer les adresses de MAESTRO et de CIMVIEW ; reporter les adresses dans l'AF § 6 à sa prochaine révision.
- En attente : les fonctions IHM génériques (TEMPLATE) partout ; les raccourcis clavier avancés des éditeurs.
