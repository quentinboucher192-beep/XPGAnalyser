# XPGAnalyser 1.12.14 : les types imbriqués, la valeur d'une structure partout, l'aide qui mène au type attendu, XPGAnalyser IHM sans automate

Livrée le 16/10/2026 en fin de matinée. Elle suit la 1.12.13 livrée le matin même (`docs/LIVRAISON-1.12.13.md`).

Elle répond aux demandes du 16/10 :

> « aucune section de la tache en ST dans la partie IHM, à supprimer stp, tout ce qui est lié ou dans le theme de l'automate pareil »

> « pourquoi je peux pas faire de Map[type spécifique] OF VECTEUR OF Type_spécifique, etc etc, pareil dans les tuples, array, pouvoir faire par recurrence les types imbriqués etc / refait ca sur la capture c'est horrible »

> « Générer et l'aide à la saisie ne savent pas suivre un accès imbriqué comme m['k'][2].Membre. On ne peut pas écrire la valeur initiale d'une liste de structures, faute de syntaxe pour une structure. »

> « dans l'aide à la saisie, et selecteur de variable, scripts etc, quand on met INT, je veux aussi avoir dans les aides à la saisie, les structures qui ont des INT dedans, avec recursivité, profondeur infinie, genre BATEAU[0].Taille avec Taille en INT je veux BATEAU[0] apparaisse aussi dans les completions, faire pour tout, fonctions, systemes, types, enum etc »

Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API ne change pas, à part son numéro de version : ce qui démarrait démarre toujours, et sa simulation prépare l'automate comme avant.

## 1. Les types imbriqués : le sélecteur à la place de la longue liste

La liste de la capture n'existe plus : DWORD, REAL…, les types du projet, puis Tableau…, Liste…, Vecteur…, Dictionnaire (MAP)…, Tuple…, Choisir un type…

La case **Type** ouvre maintenant directement le **sélecteur de types**, partout où un type se choisit :
- une variable IHM ;
- un membre d'un type IHM ;
- une déclaration (les grilles VAR des scripts et des fonctions) ;
- le retour d'une fonction ;
- un paramètre de popup ou de symbole.

Dans le sélecteur :
- une **forme** se coche autour de l'élément : Tableau, Liste, Vecteur, Dictionnaire (MAP), Tuple… ;
- **Composer l'élément…** ouvre un **second sélecteur** pour l'élément lui-même, et celui-là peut composer à son tour. On construit ainsi `MAP[STRING] OF VECTOR OF T_Cuve`, `LIST OF TUPLE(INT, T_Cuve)` ou `MAP[STRING] OF TUPLE(STRING, LIST OF T_Four)`, à toute profondeur ;
- l'élément composé vient en tête de la liste (« Composé ici ») ; cocher une autre forme l'enveloppe (Liste : `LIST OF VECTOR OF T_Four`, Tableau : `ARRAY[0..9] OF VECTOR OF T_Four`) ;
- un **type entier écrit dans la recherche** (`MAP[STRING] OF LIST OF TUPLE(INT, T_Four)`) se prend tel quel. S'il ne se lit pas, **Choisir** se grise et la raison est dite ;
- un type **rouvert garde tous ses niveaux** : la forme du dehors cochée, l'élément composé prêt à reprendre ;
- un paramètre écrit garde ses parenthèses et ses crochets intérieurs : `TUPLE(INT, TUPLE(A, B))` et `STRING[20]` ne perdent plus leur fin.

## 2. La valeur d'une structure, partout

`(Nom := 'A', Niveau := 2.5)` est la valeur d'une structure. Elle s'écrit maintenant dans une LIST, un VECTOR, une MAP, un TUPLE ou un tableau, à toute profondeur :
- `Cuves : LIST OF T_Cuve` → `[(Nom := 'C1', Niveau := 1.5), (Nom := 'C2')]` ;
- `Parc : MAP[STRING] OF VECTOR OF T_Cuve` → `['a' := [(Nom := 'A', Niveau := 2.5), (Nom := 'B', Niveau := 4)]]` ;
- `LIST OF TUPLE(STRING, T_Four)` → `[('a', (Consigne := 3))]`.

Un membre absent garde la valeur du type (`(Nom := 'C2')` : Niveau à 0).

La même écriture vaut **dans un script** : `Ajout := [(Nom := 'S1', Niveau := 7.5)];` ou `Parc['b'] := [(Nom := 'Q', Niveau := 3)];`. Générer et Compiler ne prennent plus `Nom` et `Niveau` pour des variables inexistantes.

À la saisie de la valeur initiale, une faute est refusée, et la raison nomme le membre : un membre inconnu (`[(Pression := 1)]` pour une `LIST OF T_Four`), une valeur du mauvais type (`[(Consigne := 'chaud')]`), une structure dans une `LIST OF REAL`.

## 3. Générer et l'aide à la saisie suivent les accès imbriqués

`Parc['a'][1].Niveau` se lit à toute profondeur : par Générer, par le moteur, par l'aide à la saisie, dans un script, une case fx ou un texte à trous (`{Parc['a'][0].Nom}`).

Une vraie faute est dite au fond, là où elle est :
- `Parc['a'][1].Pression` : T_Cuve n'a pas de membre Pression ;
- `Total := Cuves[0].Nom` : un STRING dans un REAL.

Après `Parc['a'][1].`, l'aide propose les membres de T_Cuve.

## 4. L'aide à la saisie mène au type attendu

Là où un type est attendu, la liste propose d'abord ce qui est de ce type, puis **ce qui y mène**, à toute profondeur. Un type est attendu après `n := ` pour un INT, dans l'argument d'une fonction, après `IF m = `, ou dans une case fx.

Exemple, pour un INT :
- `Compte` (un INT) en tête ;
- puis `BATEAU[0]` avec « → BATEAU[0].Taille : INT » ;
- puis `Capitaine.` (→ `.Age : INT`) et `Flotte['…'][0].Taille` ;
- puis le reste, plus bas.

Choisir `BATEAU[0].` rouvre la liste sur les membres de T_Bateau, Taille (INT) en tête.

Ce classement vaut pour tout ce qui a un type :
- les variables IHM, les variables locales, les membres ;
- les variables système et les variables d'instances ;
- les fonctions du projet, les fonctions internes et les natives, par le type qu'elles rendent ;
- les valeurs d'énumérations : `IF m = ` propose les valeurs de T_MODE d'abord.

Le **sélecteur de valeur** (le bouton d'une case) s'ouvre aussi à toute profondeur : `BATEAU[0]`, `BATEAU[0].Taille`, `Flotte['cle'][0].Taille`. Filtré par type, il garde les structures et les listes qui mènent à une valeur de ce type.

## 5. XPGAnalyser IHM ne prépare plus l'automate

Simulation › Vue d'ensemble ne dit plus « La simulation ne se prépare pas — aucune section en ST de la tâche MAST » : XPGAnalyser IHM n'a pas d'automate, et sa simulation ne le prépare plus.

Ce qui démarre la simulation démarre l'IHM :
- **Simuler** et **F5** ;
- le clic droit sur le dossier Simulation ;
- les courbes.

La barre de la Vue d'ensemble a **Démarrer l'IHM** et **Arrêter l'IHM**.

Plus rien ne parle de l'automate dans XPGAnalyser IHM :
- la Vue d'ensemble : son bandeau, ses cartes, sa chaîne, ce qui mérite ton attention, la pastille du dossier ;
- le journal ;
- les forçages : plus d'outils de l'automate ;
- les courbes ;
- la barre du haut et le menu du dossier Simulation.

Dans XPGAnalyser API, rien ne change.

## Autres corrections

- L'arbre de l'IHM dit le déclencheur d'une action de Tableau (Clic sur une case, Ligne choisie…). Depuis la 1.12.13, il disait « ? ».

## Tester vous-même

1. Dans un projet IHM, créez un type IHM `T_Cuve` (Nom : STRING, Niveau : REAL).
2. Nouvelle variable IHM `Parc`, cliquez sa case **Type** : le sélecteur s'ouvre. Cochez **Dictionnaire (MAP)**, clé STRING, puis **Composer l'élément…** : dans le second sélecteur, cochez **Vecteur** et choisissez `T_Cuve`. Le type devient `MAP[STRING] OF VECTOR OF T_Cuve`.
3. Rouvrez la case Type : la MAP est cochée et l'élément composé est là, en tête (« Composé ici »).
4. Valeur initiale de `Parc` : `['a' := [(Nom := 'A', Niveau := 2.5), (Nom := 'B', Niveau := 4)]]`. Essayez aussi `['a' := [(Pression := 1)]]` : la saisie est refusée et la raison nomme Pression.
5. Dans un script, tapez `Total := Parc['a'][1].` : l'aide propose Nom et Niveau. Générer ne dit rien sur `Total := Parc['a'][1].Niveau;`, et dit `Parc['a'][1].Pression`.
6. Avec une variable `BATEAU : ARRAY[0..3] OF T_Bateau` (Taille : INT) et une variable `Compte : INT`, tapez `Compte := ` : la liste propose les INT, puis `BATEAU[0]` avec « → BATEAU[0].Taille : INT ».
7. Lancez la simulation (F5). La Vue d'ensemble ne parle que de l'IHM : plus de section ST, plus de MAST.

## Installer

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.14.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.14-portable.zip` et `XPGAnalyser-IHM-1.12.14-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lancez `XPGAnalyser-Setup-1.12.14.exe`. Il met à jour la version installée, la 1.12.13 du matin ou une plus ancienne : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.14.exe` (29,7 Mo, 31 182 817 octets) :
`92F006AC29E7E4D666E66CE5F94D7F8011271E6BC9B74175D72FA8FFFAEA2006`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.14` ou `XPGAnalyser-IHM-1.12.14`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.14-portable.zip` (22,2 Mo) :
  `0A82978BE33809CCAFD61C8B36DE1726561D570013913A86B39DC09B4EDB886A`
- `XPGAnalyser-IHM-1.12.14-portable.zip` (22,2 Mo) :
  `8500EFA71D545C4BF1C82D8416DA1A1891BCBEAE5CCEA19E817B6490ABC6A44E`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 74 sur 74. Elle comprend :
- 7 343 contrôles dans `hmieditor` (7 314 à la 1.12.13) et 5 654 dans `hmi` (5 647) ;
- ceux d'avant, qui passent toujours.

Les nouveaux essais couvrent :
- le sélecteur : MAP[STRING] OF VECTOR OF T_Four composé en deux sélecteurs, l'élément composé en tête, les formes autour (Liste, Tableau), TUPLE(STRING, LIST OF T_Four) et LIST OF TUPLE(INT, T_Four), un type écrit dans la recherche (juste, puis faux), un type rouvert ;
- la valeur d'une structure dans une LIST, une MAP, un ARRAY, une MAP de VECTOR, un TUPLE ; les refus (membre inconnu, mauvais type, structure dans une LIST OF REAL) ;
- Générer, Compiler et le moteur sur `Parc['a'][1].Niveau`, `Cuves[1].Nom`, l'écriture `Parc['a'][0].Nom := 'Z'`, les littéraux de structures affectés dans un script ; les fautes au fond ;
- l'aide typée : l'ordre (INT, puis ce qui mène à un INT, puis le reste), le détail « → … : INT », le point qui rouvre la liste, les valeurs d'énumération d'abord ; le sélecteur de valeur à toute profondeur, filtré par type ;
- la Vue d'ensemble de XPGAnalyser IHM : aucun mot de l'automate (bandeau, cartes, chaîne, attention), et celle de XPGAnalyser API inchangée.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.14` et `XPGAnalyser IHM 1.12.14` ;
- ils importent exactement les mêmes DLL que ceux de la 1.12.13 (ADVAPI32, IPHLPAPI, KERNEL32, SDL3, SHELL32, USER32, WS2_32, msvcrt, ole32).

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.13 :
- avant, la 1.12.13 était installée (`XPGAnalyser IHM 1.12.13`) ;
- « Installation process succeeded ». Il a repris l'installation en place : même dossier, et son journal de désinstallation complété ;
- avant comme après : `XpgAnalyzer-API.exe`, `XpgAnalyzer-IHM.exe`, et les raccourcis « XPGAnalyser API » et « XPGAnalyser IHM » ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `XPGAnalyser API 1.12.14` et `XPGAnalyser IHM 1.12.14`, et l'API installée analyse `MAST.XPG` comme avant (891 variables, 29 POU).

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). Pas de session Wine avec captures pour cette version : la section « Tester vous-même » donne le parcours à refaire chez vous.

## Limites connues

- **L'aide typée** classe ce qu'elle connaît par son type. Une native qui rend un type générique (ANY_NUM, T) n'est pas classée, faute de savoir ce qu'elle rendra.
- **Une clé de MAP** dans l'aide se dit `['…']` : l'aide ne devine pas les clés présentes en marche.
- Comme pour les versions d'avant : ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici).

## La suite

- Essayer sur un vrai Windows, avec la section « Tester vous-même ».
