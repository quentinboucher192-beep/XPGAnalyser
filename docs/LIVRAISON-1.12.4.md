# XPGAnalyser 1.12.4 : les types objets dans les membres et les paramètres, une seule ligne Type, l'aperçu de la Disposition

Livrée le 11/10/2026 dans la matinée.

Elle répond à ces demandes du 11/10 :

> « quand on ajoute un membre dans un type IHM, on voit pas les nouveaux types, tuple, list, vector, map, pareil dans paramètres de symboles, pareil paramètres des popup »

> « quand tu vois Type et Tableau concernant une variable, il faut garder que type, type donne l'info de si c'est un tableau ou non »

> « dans disposition ajoute un apercu dans le menu »

Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API gagne seulement l'aperçu dans sa fenêtre Disposition.

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.4.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.4-portable.zip` et `XPGAnalyser-IHM-1.12.4-portable.zip` : chaque application, sans installation ;
- ce document ;
- les captures `1124_ihm_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

| | Avant (1.12.3) | Maintenant (1.12.4) |
|---|---|---|
| Le type d'un membre d'un type IHM | un type simple, une énumération, une structure, un tableau | aussi une **liste**, un **vecteur**, un **dictionnaire (MAP)**, un **tuple** |
| Le type d'un paramètre de popup ou de symbole | un type simple, une structure, un tableau | aussi `LIST`, `VECTOR`, `MAP`, `TUPLE`, en référence, en valeur ou en copie |
| Le type d'un paramètre dans l'inspecteur | deux lignes, Type et Tableau | **une seule ligne Type** : `ARRAY[1..10] OF REAL`, `LIST OF STRING`… |
| Projet › Disposition… | la liste des réglages | la liste et **l'aperçu de la fenêtre** (le brouillon, avant Appliquer) |
| `JOIN`, `LIST_COUNT`… dans un texte à trous | Générer disait « fonction inconnue » ; la simulation les calculait | Générer les accepte |

## 2. Les membres objets des types IHM

Dans **Programmation générale › Types IHM**, la case Type d'un membre propose maintenant **Liste…**, **Vecteur…**, **Dictionnaire (MAP)…** et **Tuple…**. Chacun ouvre le sélecteur de types sur cette forme. Exemple : `T_Recette` a les membres `Etapes : LIST OF STRING` et `Stock : MAP[STRING] OF INT`.

Un tel membre vit dans la mémoire de l'IHM. Il n'a pas de place Modbus : sa colonne Décalage montre « — » et sa Taille « objet (IHM) ». Les autres membres gardent leur place, et leur liaison avec elle. Sa valeur initiale est un littéral (`['Peser', 'Pétrir']`, `['farine' := 3]`, `(1, 'a')`), que Générer vérifie dans le type.

On s'en sert comme d'une variable objet, dans les scripts et dans les expressions :
- `Recette.Etapes[0]`, `Recette.Etapes.Count`, `Recette.Stock['farine']` ;
- `LIST_ADD(Recette.Etapes, 'Servir')`, et `Recettes[i].Etapes` dans un tableau de recettes ;
- `FOR EACH x IN Recette.Etapes` ;
- `R2 := Recette`, une copie entière, objets compris ;
- une variable locale `VAR r : T_Recette; END_VAR` ;
- un texte à trous `{JOIN(Recette.Etapes, ' / ')}`.

Un type qui se contiendrait par une liste est refusé (`T_Noeud.Enfants : LIST OF T_Noeud`). L'écran des variables donne une ligne par membre objet, l'arbre de la simulation les montre, et renommer un type le suit jusque dans `LIST OF T`.

## 3. Les paramètres objets des popups et des symboles

Un paramètre de popup ou de symbole peut être une `LIST`, un `VECTOR`, une `MAP`, un `TUPLE`, ou une structure qui en porte. Selon son mode :
- **en référence**, il reçoit une variable de l'appelant, membre objet compris (`L := Recette.Etapes`). La popup la lit et l'écrit en place. Une clé de `MAP` peut être donnée dans l'argument (`Stock['vis']`) ;
- **en valeur**, il reçoit un littéral (`[1, 2]`, `['a' := 1]`, `(7, 'sept')`) ou un calcul. La popup reçoit alors son propre objet ;
- **en copie** et en mode « les deux », il reçoit une copie profonde. **Appliquer** recopie l'objet entier sur la référence ; pour une structure, ses cases et ses objets.

Une liste, une liste déroulante ou un tableau de la popup peut être lié au paramètre (`← L`) : il montre ses éléments. Un symbole reçoit son paramètre objet par une variable, et Générer le dit sinon. Générer vérifie aussi un littéral dans son type : `['x' := 'a']` ne va pas dans une `MAP[STRING] OF INT`. L'onglet Popups de la simulation montre et compte les copies d'objets.

## 4. Une seule ligne Type

L'inspecteur d'un paramètre de popup ou de symbole n'a plus qu'une ligne **Type**, qui donne le type entier : `ARRAY[1..10] OF REAL` dit déjà que c'est un tableau. La ligne Tableau a disparu.

La liste de cette ligne propose, dans l'ordre :
- les types courants et ceux du projet ;
- **Tableau…**, **Liste…**, **Vecteur…**, **Dictionnaire (MAP)…** et **Tuple…**, qui ouvrent le sélecteur de types sur cette forme en partant du type actuel ;
- **Choisir un type…**.

## 5. L'aperçu de la Disposition

À droite de **Projet › Disposition…**, la fenêtre est dessinée en boîtes avec le brouillon, avant **Appliquer**. On y voit :
- la barre du haut et la bande d'état ;
- l'explorateur, à gauche ou à droite, et les documents ouverts ;
- les pages du centre qui s'ouvrent seules (en onglet, côte à côte, ou en fenêtre détachée qui flotte par-dessus), et celle de devant avec ses sous-onglets : l'inspecteur, le panneau de droite de la simulation, la programmation générale ;
- le panneau du bas, dessous, à droite ou replié (alors une simple barre), avec ses sous-onglets.

Deux boutons, **En édition** et **En simulation**, montrent la fenêtre dans chaque mode. Les couleurs :
- un contour vert : seulement en simulation ;
- un contour bleu : seulement en édition ;
- en couleur : le sous-onglet choisi au départ.

Chaque changement dans la liste redessine l'aperçu tout de suite.

## 6. Générer et les fonctions des listes

En préparant la démonstration, Générer refusait `{JOIN(Recette.Etapes, ' / ')}` (« fonction inconnue : JOIN »), alors que la simulation l'affichait. Générer accepte maintenant dans une expression ou un texte à trous :
- les fonctions des listes et des vecteurs (`JOIN`, `LIST_COUNT`, `LIST_CONTAINS`…) ;
- celles des MAP (`MAP_HAS`, `MAP_SIZE`…) ;
- `SIZEOF`, `LOWER_BOUND`, `UPPER_BOUND`, `TO_UPPER` et `TO_LOWER`.

Il compte leurs arguments. Une fonction qui change son objet (`LIST_ADD`, `MAP_REMOVE`…) y est signalée : une expression lit une copie et ne change rien, cela se fait dans un script.

## Tester vous-même

1. **Programmation générale › Types IHM** : choisissez un type, **+ Membre**, puis Type › **Liste…**. Le sélecteur s'ouvre sur `LIST OF` ; choisissez `STRING`. Mettez la valeur initiale `['Peser', 'Cuire']` : la colonne Décalage montre « — ».
2. Créez une variable `Recette` de ce type, et posez dans une vue un texte `{JOIN(Recette.Etapes, ' / ')}`. Générer passe, et la simulation affiche « Peser / Cuire ».
3. Créez une popup avec un paramètre `L`, Type › **Liste…** › `LIST OF STRING`, en mode Référence. Mettez-y une Liste dont la source est `L`, et un bouton « Exécuter un script » avec `LIST_ADD(L, 'Servir');`.
4. Dans une vue, ajoutez un bouton « Ouvrir une popup » avec l'argument `L := Recette.Etapes`. En simulation, connecté avec un utilisateur qui a la permission « Scripts », le bouton ajoute « Servir » à `Recette.Etapes`.
5. **Projet › Disposition…** : choisissez « Écran large ». Avant même Appliquer, l'aperçu met le panneau du bas à droite. Cliquez **En simulation** : la page de la simulation passe devant.

## Installer

1. Lancez `XPGAnalyser-Setup-1.12.4.exe`. Il met à jour la version installée : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.4.exe` (28,2 Mo, 29 581 011 octets) :
`67951A6899874AA351D05E943F0D335B7C357F51CB4BFA1FCE26DCB25FB7E498`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.4` ou `XPGAnalyser-IHM-1.12.4`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.4-portable.zip` (21,1 Mo) :
  `C43F10CC3A530EB0040664F6C03C848CD7512B725C8EABF82D75DBB212A6A8B7`
- `XPGAnalyser-IHM-1.12.4-portable.zip` (21,1 Mo) :
  `F883F45B87A2A0365977C8B94D3D48E73B6353BAD0E06A14D7E5FC32EF9ED46A`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 70 sur 70. Elle comprend les 6 826 contrôles de `hmieditor`, les 4 899 de `hmi`, les 1 842 de `hminatives`, les 258 de `hmireperes` et les 133 de `hmitypes`. Chaque nouveauté a ses essais :
- les membres objets dans le moteur (`membresObjets1124`) : l'aplatissement, les places Modbus des autres membres, la lecture et l'écriture en place, un tableau de recettes, la copie entière, `FOR EACH`, une locale, une expression de vue et `{JOIN(…)}`, le type refusé, et Générer (`JOIN` accepté, `LIST_ADD` signalé, le nombre d'arguments) ;
- les membres objets à l'écran (`membresObjetsEcran1124`) : l'écran des types, ses colonnes Décalage et Taille, l'écran des variables ;
- les paramètres objets (`parametresObjets1124`) : référence, valeur, copie et « les deux », Appliquer, la clé d'une MAP, les listes et tableaux liés, un membre objet passé en référence (`L := Recette.Etapes`), et Générer ;
- la ligne Type unique de l'inspecteur, sans ligne Tableau, et les formes qu'elle propose ;
- l'aperçu de la Disposition (`apercuDisposition1124`) : les boîtes en édition et en simulation, les pages détachées, le panneau du bas replié ou à droite.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.4` et `XPGAnalyser IHM 1.12.4` ;
- `XpgAnalyzer-API.exe --cli MAST.XPG` analyse le projet d'essai (891 variables, 29 POU), comme avant.

**La session sous Wine** a tourné avec l'exe livré, sur Armoire_Gaz. Un petit outil y a ajouté `T_Recette`, la variable `Recette`, la popup `Pop_Etapes` et deux objets dans Vue_Listes. Chaque ligne de la session a réussi :
- la fenêtre Disposition et son aperçu, en édition (capture `1124_ihm_01`) puis en simulation (`02`) ;
- `T_Recette` dans l'écran des types, avec Etapes et Stock en membres objets (`03`) ;
- le paramètre `L` de Pop_Etapes sur une seule ligne Type, sans ligne Tableau (`04`), et la liste de cette ligne avec ses formes (`05`) ;
- la simulation : le build réussit (365 éléments générés, 293 compilés, 0 erreur). Vue_Listes affiche « Recette.Etapes : Peser / Pétrir / Cuire · 3 étapes » (`06`) ;
- Étapes… ouvre Pop_Etapes avec ses trois étapes (`07`), et « + Servir » en ajoute une quatrième (`08`). La popup fermée, la vue affiche « Peser / Pétrir / Cuire / Servir · 4 étapes » (`09`).

La simulation s'est faite connectée en « admin ». L'utilisateur de départ du projet, « operateur », n'a pas la permission « Scripts » : chez lui, « + Servir » est refusé, et c'est voulu.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.3 :
- « Installation process succeeded » ; il a repris l'installation de la 1.12.3 (même dossier, son journal de désinstallation complété) ;
- avant comme après : `XpgAnalyzer-API.exe`, `XpgAnalyzer-IHM.exe`, et les raccourcis « XPGAnalyser API » et « XPGAnalyser IHM » ;
- les deux exe installés sont identiques à ceux qui ont été testés ; installés, ils répondent `XPGAnalyser API 1.12.4` et `XPGAnalyser IHM 1.12.4`.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). La section « Tester vous-même » donne le parcours à refaire chez vous.

## La suite

- Dans XPGAnalyser API, la fenêtre **Nouvelle variable…** (les variables de l'automate) a encore Type, Tableau et Dimensions. Elle peut passer à une seule ligne Type comme l'IHM, si vous le voulez là aussi ;
- `DATE`, `TOD`, `DT`, `CHAR` et `WSTRING` comme variables IHM, avec leur place Modbus ;
- la source du guide Word, à remettre à jour depuis la 1.11.2.
