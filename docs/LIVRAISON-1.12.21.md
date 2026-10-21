# API Studio et IHM Studio 1.12.21 : les fenêtres se déplacent et s'agrandissent, et le sélecteur de types propose T et K

Livrée le 21/10/2026. Elle suit la 1.12.20 du même jour (`docs/LIVRAISON-1.12.20.md`).

Elle répond à une demande, faite sur la capture du sélecteur de types ouvert pour le membre `Name` de la structure générique `T_Extractor<T, K>` :

> « comment choisir le type T ??? Vector T ??? Array de T ??? Callback de T ??? pareil avec K etc »

> « fait en sorte que tous les modals puissent etre deplacé, agrandit etc »

## 1. Le sélecteur de types propose T et K

Pour un membre d'une structure générique, ou un paramètre d'un symbole ou d'une popup générique, **Choisir un type…** connaît maintenant les génériques de ce qui porte le type.

- **T et K sont en tête de la liste**, dans la puce **Génériques**. Leur provenance dit d'où ils viennent (« Générique de T_Extractor<T, K> ») et leur ligne ce qu'on en fait.
- **Ils se composent comme un type** avec une forme dessous :
  - **Vecteur** donne `VECTOR OF T`, **Liste** `LIST OF T`, **Tableau** `ARRAY[0..9] OF T` ;
  - **Dictionnaire (MAP)** donne `MAP[K] OF T` : on écrit K dans la case **Clé** ;
  - **Composer l'élément…** va plus loin, et le second sélecteur connaît aussi T et K : `MAP[K] OF VECTOR OF T`.
- **Les rappels de T sont tout faits**, dans la puce **Rappels** :
  - `CALLBACK (x : T) : BOOL` (un filtre), `CALLBACK (x : T) : T` (une transformation), `CALLBACK (a : T; b : T) : BOOL` (un ordre), `CALLBACK (x : T)`, `CALLBACK () : T` ;
  - les mêmes pour K ;
  - avec deux génériques, `CALLBACK (k : K; v : T) : BOOL` (le filtre d'une `MAP[K] OF T`, comme `MAP_REMOVE_IF`) et `CALLBACK (x : T) : K` (une clé tirée d'une valeur).
- **Un type écrit dans la recherche** se lit aussi : `map[k] of vector of t` devient `MAP[K] OF VECTOR OF T`. Un nom qui n'est pas un générique d'ici (`MAP[K] OF U`) est refusé, avec la raison.
- **La vérification** met un type de la contrainte de chaque générique à sa place : `MAP[K] OF T` se vérifie comme `MAP[INT] OF INT`. Le type choisi reste écrit avec T et K.
- **Rouvert** sur `MAP[K] OF T`, le sélecteur retrouve la forme, la clé K et la ligne T.

Un défaut corrigé en passant : **un membre `MAP[K] OF T` était refusé.** T se vérifiait avec REAL, et une clé REAL n'est pas permise. L'exemple d'un générique sans contrainte est maintenant INT, qui va partout où va un type, la clé d'une MAP comprise ; `ANY_REAL` garde REAL. Les messages qui donnent un exemple disent donc `T_Mesure<INT>`.

## 2. Les fenêtres se déplacent et s'agrandissent

Toutes les fenêtres modales : Choisir un type, Choisir une fonction, la valeur initiale, les questions et les formulaires, Renommer, Disposition, Dupliquer, l'adressage, l'explorateur de fichiers, les nouveautés, l'éditeur d'icônes, les thèmes…

- **Déplacer** : on la prend par sa barre de titre (le curseur devient une croix fléchée) et on la lâche où on veut. Sa barre de titre reste toujours à l'écran.
- **Agrandir** : on tire un bord ou un coin. La **poignée** dessinée en bas à droite (trois petits traits) est la plus commode ; le bord pris se colore et le curseur montre le sens.
- **En grand** : un double clic sur la barre de titre la met en grand, un autre la rend à sa place. Tirée par son titre, une fenêtre en grand reprend sa taille sous la souris.
- **Jamais plus petite que sa taille d'origine** : son contenu est placé pour cette taille. Une fenêtre qui grandit d'elle-même (une liste qui s'allonge) grandit toujours.
- **Retenue** : rouverte pendant la séance, une fenêtre revient là où on l'a laissée, à la taille choisie, tant qu'elle demande la même taille d'origine.
- **Ce qui reste cliquable dans la barre de titre** : la croix qui ferme, les champs et les boutons, les onglets de l'éditeur d'icônes.

Le code : un module commun, `ui/DialogFrame`. Chaque fenêtre calculait son cadre centré ; elle le passe maintenant à `place()`, qui rend celui à employer. C'est une ligne par fenêtre (34 cadres, dont celui que partagent les questions et les formulaires) ; le reste de la fenêtre se place depuis ce cadre, comme avant.

## 3. Les essais

77 suites sur 77. L'essai nouveau de cette version :
- `hmieditor` (8048 contrôles) : `generiquesFenetres11221` :
  - un membre de `T_Extractor<T, K>` peut être `T`, `VECTOR OF T`, `ARRAY[0..9] OF T`, `MAP[K] OF T`, `MAP[K] OF VECTOR OF T`, `CALLBACK (x : T) : BOOL`, `CALLBACK (k : K; v : T) : BOOL` ;
  - le sélecteur : T et K en tête, la puce Génériques, Vecteur et Tableau, les rappels tout faits, la recherche (`map[k] of vector of t`, le refus de U), Choisir, la réouverture sur `MAP[K] OF T` ;
  - la fenêtre : le cadre centré ; la barre de titre déplace, la croix et la recherche non ; déplacée par le titre (et son contenu avec) ; agrandie par le coin, puis par le bord gauche ; jamais plus petite que 900 × 640 ; en grand d'un double clic, rendue d'un autre ; fermée puis rouverte, à la même place.
- Mis à jour : l'exemple d'un générique sans contrainte (`T_Mesure<INT>`, `LIST OF INT`) dans `hmi` (5929 contrôles) ; les notes (379) et les versions (54) dans `hmieditor`.

## 4. Les captures

Prises avec l'exe Windows livré, lancé sous Wine, sur une copie du projet de démonstration Armoire_Gaz à laquelle la session ajoute la structure générique `T_Extractor<T, K>` (membres `Name : STRING` et `Valeurs : MAP[K] OF T`) :
- `1221_01_accueil.png` : l'accueil d'IHM Studio, la version 1.12.21 ; en bas, « Nouveautés de la 1.12.21 : 2 à découvrir ».
- `1221_02_selecteur_generiques.png` : Types IHM › T_Extractor › la case Type du membre Name : le sélecteur a T et K en tête (puce Génériques 2, « Générique de T_Extractor<T, K> »), les rappels de T et K comptés dans la puce Rappels (18) ; T et la forme Vecteur donnent `VECTOR OF T`.
- `1221_03_fenetre_deplacee.png` : la même fenêtre tirée par son titre (250 à gauche, 50 plus haut) puis agrandie par son coin (1200 × 740 au lieu de 900 × 640) ; la souris sur la poignée, le cadre est en couleur ; la liste et la ligne de T montrent leur texte en entier.
- `1221_04_guide_fenetres.png` : le guide, Les onglets et les raccourcis : la section « Les fenêtres : déplacer, agrandir (1.12.21) ».
- `1221_05_guide_generiques.png` : le guide, Rappels et génériques : la section des structures, symboles et popups génériques, avec la puce du sélecteur qui propose T et K.

## 5. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.12.21.exe` : l'installateur, qui pose les deux applications ;
- `API-Studio-1.12.21-portable.zip` et `IHM-Studio-1.12.21-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.12.21.exe`. Il met à jour la version installée (la 1.12.20 ou une plus ancienne) en place : même identité d'installation, même dossier, données reprises sans être déplacées. Les projets s'ouvrent tels quels : le format des fichiers ne change pas.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.12.21.exe` (31,5 Mo, 33 036 296 octets) :
`B1B7C13368C78B5847556D8A15BD94984F87CAFE0DB9A028DCAFC00A85DF19A3`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `API-Studio-1.12.21` ou `IHM-Studio-1.12.21`. Décompresse, puis lance l'exe.
- `API-Studio-1.12.21-portable.zip` (23,5 Mo) :
  `8D92951835301EA0D56F8A6DA15770A96D7AA357D5601E7D99E6CEDFDE4C1B13`
- `IHM-Studio-1.12.21-portable.zip` (23,5 Mo) :
  `26145C8EB3514BB2765929C9B8B75DFCD6FB7626271E6F70661B514E3D73086B`

## 6. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `API Studio 1.12.21` et `IHM Studio 1.12.21` ;
- ils importent exactement les mêmes DLL que ceux de la 1.12.20 (ADVAPI32, IPHLPAPI, KERNEL32, SDL3, SHELL32, USER32, WS2_32, msvcrt, ole32) ;
- `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- une session scriptée sur le projet de démonstration : les cinq captures ci-dessus. Le sélecteur y rend bien `VECTOR OF T`, et la fenêtre se déplace et s'agrandit à la souris (des glissers envoyés comme ceux d'une vraie souris).

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.20 :
- avant, la 1.12.20 était installée (`IHM Studio 1.12.20`) ;
- « Installation process succeeded ». Il a repris l'installation en place : même dossier, journal de désinstallation complété ;
- le dossier d'installation a `API-Studio.exe`, `IHM-Studio.exe` et le désinstalleur ; le menu Démarrer garde son dossier **API IHM Studio** (API Studio, IHM Studio, Maintenance) ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `API Studio 1.12.21` et `IHM Studio 1.12.21`, et l'API installée analyse `MAST.XPG` comme avant.

**Les zips portables** ont chacun 151 fichiers, dont l'exe testé.

**Le projet Visual Studio** a les mêmes 468 fichiers .cpp et 423 en-têtes que CMake (`outils/verifier_vcxproj.py`) : `ui/DialogFrame` y est ajouté.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows (les curseurs de déplacement et d'agrandissement ne se voient pas sur une capture), et le projet Visual Studio n'a pas été compilé (pas de MSVC ici).

## 7. La suite

- Essayer sur un vrai Windows : déplacer et agrandir les fenêtres à la souris (les curseurs), la mise à jour par-dessus la 1.12.20.
- Le sélecteur ne propose pas encore les génériques d'une fonction TEMPLATE dans la grille de ses déclarations (on y écrit `T` à la main, comme avant).
