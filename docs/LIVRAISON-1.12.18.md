# XPGAnalyser 1.12.18 : le clic droit, partout, et l'adresse de toutes les cases d'un tableau de structures

Livrée le 20/10/2026. Elle suit la 1.12.17 du 19/10 au soir (`docs/LIVRAISON-1.12.17.md`).

Elle répond à trois demandes. La première :

> « que proposes tu de faire pour améliorer le clic droit dans le projet ? genre avec des menus contextuels ? liste tout ce que tu peux améliorer sur ce point »

L'inventaire a donné une maquette de 21 planches, avant et après. La deuxième demande l'a complétée :

> « j'aimerais faire en sorte d'optimiser l'adressage %MW %M des structures comme ceci avec par exemple une base et on fait clic droit 'modifier pour tout le monde', et on aurait un sorte de modal avec soit des index calculés à partir d'une formule avec 'i' l'indice dans [i], soit on prend une adresse et on dit on prend toutes les suivantes et on override si l'adresse est deja utilisée ailleurs sans supprimer de l'autre coté ajoute à la maquette »

La troisième a validé la maquette : « ok go fait tout ca ». Les points « à décider » ont été tranchés comme la maquette les proposait :
- Ctrl+D duplique tout de suite ; Ctrl+Maj+D ouvre « Dupliquer… » ;
- Ctrl+G groupe, Ctrl+Maj+G dégroupe ;
- Ctrl+Alt+C et Ctrl+Alt+V copient et collent le style ;
- l'adresse se calcule par une formule ;
- dans la vue en marche, Maj+clic droit ouvre le menu quand le projet lit le bouton droit (SYS.MouseRight ou SYS.MouseButtons) : le clic droit seul reste au projet, et la barre d'état le dit ;
- une seule courbe pour suivre une variable : celle de Simulation › Courbes ;
- sur le poste d'exploitation, le menu n'a que « Acquitter », et seulement pour qui en a le droit.

Les deux applications changent : **XPGAnalyser IHM** surtout (l'éditeur de vues, le code, la simulation, l'arbre), **XPGAnalyser API** aussi (l'arbre, les onglets, les titres de colonne, les volets Variables et Versions, l'Automate, le débogueur, l'accueil, les macros).

## 1. Le menu lui-même

- **Au clavier** : Maj+F10 ou la touche Menu ouvrent le menu de ce qui a le focus (une ligne de l'arbre, d'un tableau, un onglet, le canevas, le code). La lettre soulignée choisit l'entrée ; Début et Fin vont aux bouts ; un long menu défile.
- **En tête, ce que vise le menu** : « Moyenne_Pression · fonction IHM », « 3 variables IHM », « Colonne Type ».
- **La colonne de droite ne montre que des touches.** Les nombres sont passés dans le libellé : « Fermer tout (5) ».
- Une entrée grisée **dit pourquoi** (« une à la fois », « le travail en cours », « natif : verrouillé »). Une note grise sous un libellé dit où va l'action (« dans Simulation › Courbes »).
- Les cases à cocher, les choix, les sous-menus à plusieurs niveaux, une icône par geste.
- **Un mot, un raccourci**, le même partout : Copier Ctrl+C, Coller Ctrl+V, Dupliquer Ctrl+D, Renommer F2, Supprimer Suppr, Aller à la définition F12, Voir les usages Maj+F12.

**Les dix défauts de l'inventaire sont corrigés** :
- le menu et son action visent la même chose ;
- le clic droit ne fait plus de clic gauche, et n'agit plus sans menu ;
- Supprimer ne vise plus un autre objet ;
- le texte tapé n'est plus jeté quand le menu s'ouvre ;
- le clavier revient à l'arbre et aux onglets après le menu ;
- les sous-onglets répondent (le clic droit les choisit, puis leur menu) ;
- dans le volet Macros, avec plusieurs macros choisies, le menu montré pour l'une n'agit plus sur une autre ;
- l'historique, « Aller à », un lieu épinglé de la fenêtre de fichiers, les puces et le pied de l'arbre n'agissent plus dès le clic droit.

## 2. L'éditeur de vues (XPGAnalyser IHM)

- **Le canevas**, selon ce qu'on vise : une sélection, un objet verrouillé, le vide, un objet qu'on pose.
- **Ctrl+D** duplique tout de suite, à côté ; **Ctrl+Maj+D** ouvre « Dupliquer… ».
- **Ctrl+G** groupe, **Ctrl+Maj+G** dégroupe ; **Ctrl+Alt+C / Ctrl+Alt+V** copient et collent le style.
- L'explorateur d'objets, les calques, la bibliothèque.
- **L'inspecteur** : un menu par ligne (une couleur, une liste, une case à cocher, le nom) ; Aller à la définition et Voir les usages du nom sous le clic.
- Le contenu, les actions, les paramètres, les types, les équipements.
- Les variables : Voir les usages ; Rémanence et Interne ou attribuée en sous-menus.
- Le panneau du bas : Console, Sorties, Diagnostics (Appliquer la correction).

## 3. L'adresse de toutes les cases d'un tableau de structures

Sur une variable IHM qui est un tableau de structures (`Pompes : ARRAY[0..9] OF T_Pompe`), le clic droit sur un membre propose **Modifier l'adresse dans toutes les cases…**. Deux façons :
- **une formule de i et j** (i : l'indice de la case, j : le deuxième indice d'un tableau à deux dimensions) :
  - `[*].Retour = %MW(1000 + 10 * i)` place le membre Retour de chaque case ;
  - `[*] = %MW(2000 + 8 * i)` place les cases entières, et leurs membres suivent ;
  - un BOOL dans un mot : `%MW(551 + i / 16).(i MOD 16)` ;
- **à la suite d'une adresse** : la première case à cette adresse, les suivantes derrière, sans trou.

**L'aperçu**, case par case, montre l'adresse d'avant et celle d'après. Une adresse qu'une autre variable a déjà est signalée : l'autre variable **garde la sienne**, rien n'est supprimé de son côté. Les refus sont dits : un nom inconnu, deux cases à la même place, une adresse hors des zones de l'équipement. Tout se fait en une commande : **un seul Ctrl+Z**.

Ensuite :
- une adresse tapée sur une case l'emporte sur la formule ;
- les cases ajoutées (le tableau qui grandit) suivent la formule ;
- « Revenir aux adresses calculées » retire la formule.

## 4. Le code, les déclarations, les champs

- **L'éditeur de code** :
  - la faute sous la souris d'abord : son message, Corriger, Faute suivante, Faute précédente ;
  - puis le nom visé et ce qu'on en sait (REAL · variable IHM) : Aller à la définition (F12), ou Aide sur LIMIT (F1) pour une native ; Voir les usages ; Renommer partout… ;
  - Couper, Copier, Coller ; Choisir une fonction… ; Entourer de › (IF, FOR, WHILE…) ; Commenter les lignes ; Mettre en forme le document ;
  - le clic droit garde la sélection quand il tombe dedans ; ailleurs, il pose le curseur.
- **Un champ d'une ligne** : Couper, Copier, Coller, Tout choisir, Choisir une fonction… Ouvrir le menu ne termine plus la saisie.
- **Les grilles de déclarations** : Ajouter, Dupliquer, Monter, Descendre, **Voir les usages ›** (une entrée par utilisation, qui y mène), Valeur initiale…, Copier, Coller, Supprimer.
- **Une fiche des natives** : Insérer dans le script, Copier le nom, Copier l'exemple.
- **Les sélecteurs** (Choisir une fonction…, le type, la valeur) : Choisir, Copier le nom, Aide (F1) ou Aller à la définition (F12).

## 5. La simulation

- **La vue en marche** (XPGAnalyser IHM) : le clic droit sur un objet montre ce qu'il affiche, sa variable et sa valeur. Ses entrées :
  - Forcer…, Relâcher ;
  - Suivre (dans Simulation › Courbes) ;
  - Ajouter aux expressions ;
  - Aller à l'objet dans l'éditeur ;
  - Copier la valeur ;
  - sur une courbe : Pause, Exporter (CSV)… ; sur une alarme : Acquitter.

  Quand le projet lit le bouton droit (SYS.MouseRight ou SYS.MouseButtons), le clic droit reste au projet ; Maj+clic droit ouvre le menu, et la barre d'état de la simulation le rappelle au clic droit seul. Le projet de démonstration est dans ce cas : Vue_Clavier lit SYS.MouseButtons.
- **Les Expressions, les variables IHM simulées et les esclaves simulés** : Forcer à…, Forcer à sa valeur, Relâcher, Mouvement ›, Suivre, Copier la valeur. Entrée, Suppr et Ctrl+C agissent sur la ligne courante.
- **L'Automate** (XPGAnalyser API) : **une seule courbe**. La courbe à six plumes du volet part ; Suivre va dans Simulation › Courbes. Le menu d'une variable : Forcer…, Relâcher, Suivre, Ajouter à une table d'animation › (avec Nouvelle table…), Aller à l'écriture, Copier la valeur, Copier le nom.
- **La Vue d'ensemble** :
  - la courbe des performances : Période ›, Exporter, Remettre à zéro, Voir les lenteurs ;
  - une lenteur : Aller à, Copier la ligne, Exporter, Vider les lenteurs… ;
  - À surveiller : Tester, Aller à l'équipement.
- **Forçages** (Relâcher, Tout relâcher), **Courbes** (Retirer la plume, Figer, Exporter (CSV)…, Tout retirer), **Journal** (comme la Console : Aller à la source, Filtrer sur la source, Copier la ligne, Exporter en CSV, Effacer le journal).
- **L'arbre Simulation** : chaque ligne a son menu. Suppr retire un point d'arrêt, un forçage, une plume.
- **Le débogueur** :
  - la marge : Point d'arrêt (F9), Condition…, Désactiver, Exécuter jusqu'à la ligne (Ctrl+F10), Retirer tous les points d'arrêt ;
  - le code : le nom visé et sa valeur, Ajouter un espion, Qui a écrit ?, Aller à la définition (F12) ;
  - les points d'arrêt, la trace, les espions.

## 6. L'arbre du projet, les onglets, les tableaux et le reste

**L'arbre du projet** :
- **Chaque nœud a ses gestes**, et Épingler vient après eux, juste avant Supprimer (il était avant Ouvrir). La touche de chaque geste est dans la colonne de droite : Ouvrir (Entrée), Dupliquer (Ctrl+D), Renommer (F2), Copier le nom (Ctrl+C), Voir les usages (Maj+F12), Supprimer (Suppr). Ces touches marchent aussi dans l'arbre, sans ouvrir le menu.
- **Ce qui manquait** :

  | Nœud | Renommer | Dupliquer | Ouvrir dans une fenêtre |
  | --- | --- | --- | --- |
  | Fonction IHM | ajouté | ajouté (son corps suit son nom : `Moyenne_copie := …`) | ajouté |
  | Type IHM | ajouté | ajouté | ajouté |
  | Script | déjà là | ajouté | ajouté |
  | Variable IHM | déjà là | ajouté | — |
  | Alarme, recette | ajouté | ajouté | — |
  | Version | ajouté (son nom seulement) | — | — |
  | Tâche | dit pourquoi (MAST, FAST, AUX : des noms fixés par Control Expert) | — | — |
- **Un dossier crée ce qu'il range** : Nouvelle alarme…, Nouvelle recette…, Nouvel utilisateur…, Nouveau groupe…, Nouveau style…, Nouveau rapport…, Nouveau destinataire…
- **Plusieurs variables IHM choisies** (Ctrl+clic) : l'en-tête dit « 3 variables IHM » et leurs noms ; Ajouter à une table (3) ›, Copier les 3 noms (Ctrl+C), **Supprimer les 3…** (une question, un seul Ctrl+Z). Ce qui ne vaut que pour une ligne est grisé : « une à la fois ».
- **La racine** (le projet) : Nouveau ›, Enregistrer (Ctrl+S), Créer une version… (Ctrl+Alt+S), Ouvrir le dossier du projet, Rechercher dans le projet (Ctrl+Maj+F).
- **Le vide sous les lignes, et le rail à gauche** : Nouveau ›, Tout déplier, Tout replier.
- **Épinglés et Récents** : une ligne a tout le menu de ce qu'elle vise (Dupliquer, Renommer, Exporter…), plus Afficher dans l'arbre, et Épingler ou Désépingler.
- **Les macros** : un seul menu, le même dans l'arbre et dans le volet Macros. « Favorite » s'appelle maintenant Épingler ; Dupliquer… (Ctrl+Maj+D).

**Le reste** :
- **Un onglet du centre** : Fermer (Ctrl+W), Fermer les autres (n), Fermer ceux de droite (n), Fermer tout (n), **Rouvrir le dernier fermé** (Ctrl+Maj+T, les vingt derniers), Afficher dans l'arbre, Copier le titre, puis la disposition. **Le vide de la bande** : Rouvrir, Fermer tout, Mosaïque.
- **Un sous-onglet** (l'inspecteur, le panneau du bas, la simulation, la programmation, la communication, un symbole) : le clic droit le choisit, puis ses entrées et **Disposition…**. La Console : Effacer la console, Copier les lignes, Exporter en texte.
- **Le titre d'une colonne**, dans tous les tableaux : Trier de A à Z, de Z à A, Ajuster la largeur, Masquer la colonne, Colonnes › (les remontrer), Filtrer la colonne…, Effacer les filtres, **Exporter en CSV…** (pour Excel).
- **Les volets Variables et Versions de l'API** : le menu de la ligne, et non plus seulement la barre d'outils.
  - Variables : Voir les usages (Maj+F12), Ajouter à une table ›, Copier le nom, Renommer… (F2), Supprimer (Suppr).
  - Versions : Ouvrir (Entrée : ce qu'elle a changé), Comparer au travail, Restaurer…, Exporter, Extraire, Supprimer… (Suppr).
- **Une carte de l'accueil** : Ouvrir (Entrée), Ouvrir le dossier, Renommer… (F2), **Retirer de la liste** (Suppr), et à part, Supprimer du disque…
  - **Attention, Suppr change** : il retire la carte de la liste, le projet reste sur le disque. Supprimer du disque passe par le menu ou le bouton du panneau de droite.
- **La fenêtre de fichiers** :
  - une ligne : Ouvrir (Entrée), Ouvrir avec le programme du système, Afficher dans l'Explorateur Windows, Copier le chemin, Épingler, Actualiser (F5), Dossier parent (Retour arrière), Nouveau dossier ;
  - un lieu épinglé : Ouvrir, Désépingler (le clic droit ne désépingle plus tout de suite).
- **L'historique** (Ctrl+H) : Aller à (Entrée), Ouvrir dans une fenêtre, Revenir à cet état… (le double clic), Retirer de l'historique (Suppr), Vider l'historique. Retirer et Vider ne touchent que la liste : Ctrl+Z annule toujours.
  - Entrée va maintenant à l'endroit ; revenir à un état reste le double clic.

## Les captures

Prises avec l'exe Windows livré, lancé sous Wine, sur le projet de démonstration Armoire_Gaz :
- `1218_01_arbre_fonction.png` : l'arbre du projet, clic droit sur la fonction IHM `Moyenne_Pression` : l'en-tête dit ce qu'elle est (« Moyenne_Pression · fonction IHM »), puis Ouvrir (Entrée), Ouvrir dans une fenêtre, Essayer…, Dupliquer (Ctrl+D), Renommer… (F2), Définir l'icône…, Exporter la fonction…, Importer…, Épingler et Supprimer… (Suppr) en dernier ; dessous, le build de la fonction ;
- `1218_02_onglet.png` : clic droit sur l'onglet `Vue_Supervision` : l'en-tête (« 1 onglet »), Fermer (Ctrl+W), Fermer les autres et Fermer ceux de droite grisés avec leur raison, Fermer tout (1), Rouvrir le dernier fermé (Ctrl+Maj+T), Afficher dans l'arbre, Copier le titre, puis Diviser, Mosaïque et Ouvrir dans une fenêtre ;
- `1218_03_courbe_en_marche.png` : la simulation en marche, Maj+clic droit sur la courbe `Courbe_Pressions` : Courbe_Pressions · 2 plumes, Pause (grisé : la courbe n'est pas pilotable), Exporter (CSV)…, Aller à l'objet dans l'éditeur. Le projet de démonstration lit le bouton droit (Vue_Clavier lit SYS.MouseButtons) : le clic droit seul lui revient, et la barre d'état de la simulation le rappelle pendant 5 secondes (sur la capture, l'avertissement de la caméra injoignable l'a déjà remplacé).

## Installer

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.18.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.18-portable.zip` et `XPGAnalyser-IHM-1.12.18-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lancez `XPGAnalyser-Setup-1.12.18.exe`. Il met à jour la version installée, la 1.12.17 ou une plus ancienne : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.18.exe` (31,1 Mo, 32 628 640 octets) :
`005FD662F70A8F95B7AFFF76B6BB1D73A09E0B52EBC71BDA91D8639F277B28D4`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.18` ou `XPGAnalyser-IHM-1.12.18`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.18-portable.zip` (23,2 Mo) :
  `7313440990AD1799DB5631567E82FAAB5BF2B2869410143C35231E9CDEAE6E31`
- `XPGAnalyser-IHM-1.12.18-portable.zip` (23,2 Mo) :
  `4DBC36C17B7E92F87F0F2CDB67F7962D3D255025BB51396330544534D117A5A6`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 76 sur 76. Elle comprend :
- 7 914 contrôles dans `hmieditor` (7 725 à la 1.12.17), 5 823 dans `hmi` (5 760), 2 447 dans `hminatives`, 1 025 dans `hmidecl`, 380 dans `hminatives1216` et 235 dans `hmibuild` ;
- ceux d'avant, qui passent toujours.

Les nouveaux essais couvrent :
- **le menu lui-même** (`popup`) : deux niveaux de sous-menus, à la souris et au clavier (Droite descend, Gauche remonte, Échap ferme le plus profond) ; les lettres (une par entrée, sans accent, la suivante à la seconde frappe, soulignées au clavier seulement) ; Début et Fin ; un menu plus haut que l'écran défile ;
- **l'éditeur de vues** : le canevas (une sélection, 2 ou 3 objets, un objet verrouillé, le vide, une pose que le clic droit annule ; Coller ici ; Entrer dans le groupe ; Maj+F10), Ctrl+D, Ctrl+Maj+D, Ctrl+G, Ctrl+Maj+G ; l'inspecteur (une couleur, un choix, une case, Renommer…, Voir les usages) ; la colonne de droite n'a que des touches ;
- **l'adressage** (`hmi` et `hmieditor`) : les formules (`%MW(1000 + 10 * i)`, les cases entières, un BOOL dans un mot `%MW(551 + i / 16).(i MOD 16)`), les refus (un nom inconnu, une division par zéro, une adresse négative, deux cases à la même place, hors des zones), l'aperçu, une autre variable qui garde la sienne, l'enregistrement, Revenir aux adresses calculées, Ctrl+Z ;
- **le code et les champs** : la faute d'abord, le nom visé, Aller à la définition ou Aide (F1), le profil classique (bug 9), le clic droit qui garde la sélection (bug 6), le menu qui ne termine pas la saisie (bug 5), Maj+F10, une fiche des natives (Insérer, Copier l'exemple) ;
- **la simulation** : la jauge en marche et sa variable, la courbe qui garde Pause et Exporter quand une alarme est active ailleurs, le bandeau d'alarmes (son alarme, puis l'objet), Maj+clic droit quand une expression lit SYS.MouseRight (et la barre d'état qui le dit), les variables IHM simulées, les Expressions ; la courbe des performances et une lenteur (`simperf`) ;
- **l'arbre et le reste** : l'historique, Aller à (le clic droit choisit sans aller), le titre d'une colonne et son export CSV, une bande de sous-onglets et Disposition…, la Console, Dupliquer une fonction (son corps suit son nom) et un script, puis Ctrl+Z ; la fenêtre de fichiers (le menu d'un dossier et d'un fichier, Épingler et Désépingler, Actualiser).

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.18` et `XPGAnalyser IHM 1.12.18` ;
- ils importent exactement les mêmes DLL que ceux de la 1.12.17 (ADVAPI32, IPHLPAPI, KERNEL32, SDL3, SHELL32, USER32, WS2_32, msvcrt, ole32) ;
- `XpgAnalyzer-API.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- une session scriptée sur le projet de démonstration (les trois captures ci-dessus) : le menu d'une fonction dans l'arbre, celui d'un onglet, et la simulation qui démarre (« Projet à jour ») avec le menu de la courbe.

Cette session a trouvé trois défauts, corrigés avant la livraison :
- **le menu d'un objet de la vue en marche prenait l'alarme la plus grave du projet** (la courbe montrait « PT1_A_Vide · Acquitter » au lieu de Pause et Exporter). Seul un bandeau d'alarmes montre maintenant son alarme ; un objet qui a une alarme la montre d'abord, puis ses propres entrées ;
- **rien ne disait pourquoi le clic droit n'ouvrait pas de menu** dans un projet qui lit le bouton droit : la barre d'état le dit maintenant (Maj+clic droit ouvre le menu).
- **l'en-tête du menu de l'arbre montrait toute la signature d'une fonction**, coupée par son commentaire ; il ne montre plus que le nom (« Moyenne_Pression · fonction IHM »), comme la maquette.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.17 :
- avant, la 1.12.17 livrée le 20/10 était installée (`XPGAnalyser IHM 1.12.17`) ;
- « Installation process succeeded ». Il a repris l'installation en place : même dossier, les deux raccourcis et le dossier Maintenance du menu Démarrer gardés, et son journal de désinstallation complété ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `XPGAnalyser API 1.12.18` et `XPGAnalyser IHM 1.12.18`, et l'API installée analyse `MAST.XPG` comme avant (891 variables, 29 POU).

**Les zips portables** ont chacun 151 fichiers, dont l'exe testé et le manifeste de la 1.12.18.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici).

## Limites connues

- **Le menu de l'arbre** se vérifie par les sessions scriptées et par ses briques (l'historique, Aller à, les colonnes, les sous-onglets, Dupliquer une fonction ou un script) ; l'écran entier n'a pas d'essai unitaire.
- **Plusieurs lignes choisies dans l'arbre** : Copier les N noms et Supprimer les N… valent pour les variables IHM ; ailleurs, le menu vise la ligne du clic droit.
- **Renommer une tâche** n'existe pas : Control Expert fixe leurs noms (MAST, FAST, AUX). F2 le dit.

## La suite

- Essayer sur un vrai Windows.
