# XPGAnalyser 1.12.19 : la règle des cases, partout, et les rappels et génériques

Livrée le 21/10/2026 au matin. Elle suit la 1.12.18 du 20/10 (`docs/LIVRAISON-1.12.18.md`).

Elle répond à deux demandes. La première :

> « le but ça va être d'harmoniser et vérifier les syntaxes de tous les champs de textes modifiables, paramètres modifiables, fx etc il faut vérifier que les syntaxes s'utilisent partout pareil et que visuellement ça soit partout pareil, modal, paramètres, variable, valeur etc exactement et toujours la même règle »

L'audit a donné cinq lots, U1 à U5. La seconde demande est venue en cours de route :

> « ajoute les CALLBACK et TEMPLATE génériques aux ST, C++, C et intègre les partout, utilisables partout fais une maquette et ensuite reprend U3 U4 U5 »

La maquette a 15 planches : https://claude.ai/artifact/FkuyTs9y8wsHHU4NgQVyoe. Ses 12 propositions ont été acceptées telles quelles (« ok continue le code pour tout avoir pour demain matin ») :
1. les mots : `CALLBACK`, `LAMBDA … END_LAMBDA`, `TEMPLATE <T>` ;
2. une LAMBDA voit les variables IHM et les paramètres tels quels ; elle copie les locales à sa création (comme `[=]` en C++) ;
3. appeler un rappel vide (NULL) ne fait rien, rend la valeur par défaut et le dit une fois dans la Console ;
4. une LAMBDA dans une propriété est permise si elle ne modifie rien ;
5. `COMPARAISON#`, `OPERATION#` et `TRANSFORMATION#` restent à côté des rappels ;
6. un nom de fonction en texte (`'Carre'`) est accepté, avec un avertissement ;
7. les symboles et les popups génériques, T déduit de la liaison ;
8. les contraintes de T : les génériques `ANY_*` des natives ;
9. une variable rappel rémanente garde le nom de sa fonction ; une LAMBDA ne se garde pas ;
10. le C et le C++ : l'en-tête du projet et une vérification plus large, toujours pas exécutés ;
11. pas de débogueur pas à pas pour l'IHM ;
12. « Voir les usages » pour toutes les fonctions.

La partie 7 ci-dessous dit ce qui est fait de cette liste et ce qui reste.

Les deux applications changent : **XPGAnalyser IHM** surtout, **XPGAnalyser API** aussi (le forçage, les points d'arrêt, les fenêtres du projet).

## 1. La règle des cases (U1, U2)

Une case se lit partout de la même façon : l'inspecteur, les volets (Actions, Contenu, alarmes, paramètres, arguments d'une popup), les variables et leurs membres, les déclarations, la valeur initiale, les recettes, l'essai d'une fonction.

- **Une expression commence par =** : `=Niveau * 2`. Elle s'écrit en chasse fixe et garde son `=` à l'affichage.
- **Une valeur fixe s'écrit comme elle se lit**, sans préfixe, et se lit dans le type de la case :
  - un nombre : `2,5` devient `2.5` ;
  - un booléen : `vrai`, `oui`, `on`, `1` ;
  - une durée : `5 s`, `1 h 30`, `01:30` ;
  - un texte : `Azote` devient `'Azote'` ;
  - une énumération : `Auto` devient `T_MODE#Auto`.
- Un entier se refuse s'il a une décimale ou sort de ses bornes. Avant, `12.7` était arrondi et `40000` replié dans un INT.
- **Un nom connu tapé seul devient une expression** : `Niveau` devient `=Niveau`.
- **Vide, la case dit sa valeur par défaut** : « 12 · valeur par défaut ».
- **Une saisie refusée ne se perd plus.** Une ligne rouge sous la case dit pourquoi. « Reprendre » rend ce qui avait été tapé ; la valeur d'avant reste.
- Les listes s'écrivent `(a;b;…)` et les intervalles `a..b`, partout.
- Les durées se lisent partout pareil. Avant, `T#5s` valait 5 ms dans deux forçages.

## 2. Les cellules de tableau (U3)

- Un refus se dit **sous la ligne**, comme dans l'inspecteur : la raison en rouge et « Reprendre ». Échap ou un clic dans la bande la retire.
- Une formule s'édite en chasse fixe, pendant qu'on la tape aussi.
- **Ctrl+Espace** dans une case Valeur d'une grille de déclarations propose les variables, les fonctions et les déclarations voisines.
- La valeur se montre comme partout : `=Niveau * 2`, `Azote` sans apostrophes, une liste telle quelle.
- Deux défauts vus sur l'exe Windows sont corrigés :
  - la raison d'un refus en bas d'un inspecteur court sortait de la vue ;
  - le refus d'une borne disait « (max) » au lieu de « Maximum ».

## 3. Les fenêtres (U4)

- Une fenêtre dit un refus **sous son champ** et reste ouverte. C'est le cas de Nouvelle variable, Modifier, Créer la variable, Nouveau script et Forcer une variable IHM.
- Une seule règle de nom : un mot réservé, `SYS.` ou une variable déjà là sont refusés dès la création.
- Le `=` de tête est accepté dans toute case qui n'attend qu'une expression : Maths, la formule d'adresse, l'expression surveillée, la réceptivité GRAFCET.
- Échap ferme d'abord la liste d'aide ouverte, pas la fenêtre.
- La galerie lit Largeur et Hauteur comme toute case de nombre.
- Les fenêtres du projet de l'API sont en français.
- Le tutoiement est partout.

## 4. La simulation et l'API (U5)

- **Forcer** (Simulation › Automate) vérifie la valeur à la frappe, lue dans le type de chaque case. Le refus est dit en rouge sous le champ et la fenêtre reste ouverte.
- La condition d'un point d'arrêt accepte `=x > 3`. Une condition illisible ne se valide plus. Ctrl+Espace propose les variables de l'automate.
- Le fichier des forçages s'écrit `nom := valeur`, comme en ST. L'ancien `nom = valeur` se relit toujours.
- La colonne Expression de la simulation IHM montre le `=` d'une formule.
- Le Journal cherche comme toutes les listes : des mots qui doivent tous y être, `-mot` pour exclure.
- Nouvelle variable de l'API lit la valeur initiale dans son type.

## 5. L'adresse de toutes les cases : les chevauchements ne bloquent plus

Deux cases à la même place (une formule `%MW(1000)` pour dix cases) laissent Appliquer actif. L'aperçu les montre en ambre, et rien n'est effacé chez l'autre variable.

## 6. Les rappels et les génériques (R1 à R7)

### Le langage des scripts de l'IHM

- **`CALLBACK`** : une locale, un paramètre, une variable IHM ou un paramètre de popup peut viser une fonction :
  - `f : CALLBACK (x : REAL) : BOOL` déclare le rappel ;
  - `f := Est_Valide` y range une fonction ;
  - `f(Niveau)` l'appelle comme une fonction.
- **`LAMBDA`** s'écrit sur place :
  - sur une ligne : `LAMBDA (x : REAL) : BOOL => x > Seuil` ;
  - sur plusieurs : `LAMBDA … END_LAMBDA`, avec ses locales et `RETURN`.

  Elle copie les locales du script à sa création : `k := 3; f := LAMBDA () : INT => k * 10; k := 5;` puis `f()` rend 30.
- **`NULL`** est un rappel vide. Appelé, il ne fait rien et rend la valeur par défaut. La Console le dit une fois par endroit, pas à chaque cycle.
- **`TEMPLATE <T : ANY_NUM> FUNCTION Limiter(x : T; bas : T; haut : T) : T`** :
  - T se déduit des arguments (`Limiter(Compteur, 0, 5)` : INT ; `2` et `0,5` : REAL l'emporte) ;
  - ou T se donne entre chevrons : `Limiter<REAL>(…)` ;
  - la contrainte est vérifiée : « STRING n'est pas un ANY_NUM ».
- Une fonction de la mauvaise forme est refusée et le message dit pourquoi : « Grand ne convient pas : … ».
- Les expressions fx des objets prennent aussi les rappels : `Filtre(250.0)` (une variable rappel), `LIST_COUNT_IF(Mesures, LAMBDA (x : REAL) : BOOL => x > 10.0) = 2`, `LIST_ANY(Mesures, Tres_Grand)` (une fonction passée par son nom).
- Compiler et Générer acceptent tout cela, dans les scripts comme dans les fx, et refusent un appel inconnu. Le ST de l'automate ne le connaît pas, et ne change pas.

### Les natives à rappel (R4)

- **`LIST_FILTER(L, f)`** et **`LIST_MAP(L, f)`** rendent une liste neuve.
- **`LIST_FOR_EACH(L, f)`** et **`MAP_FOR_EACH(m, f)`** appellent f sur chaque élément ou chaque paire.
- `LIST_COUNT_IF`, `LIST_FIND_IF`, `LIST_ANY`, `LIST_ALL`, `LIST_NONE` et `LIST_REMOVE_IF` prennent un rappel à la place de COMPARAISON et de la valeur.
- `LIST_SORT(L, f)` trie dans ton ordre : f(a, b) rend TRUE si a passe avant b.
- `LIST_TRANSFORM(S, D, f)` range dans D le résultat de f sur chaque élément de S.
- **`IHM_APRES(T#5s, f)`**, **`IHM_CHAQUE(T#500ms, f)`** et **`IHM_ANNULER(n)`** sont des minuteurs de l'IHM :
  - f tourne dans l'IHM, comme un script ;
  - une période manquée ne se rattrape pas ;
  - au plus 256 minuteurs attendent ;
  - l'arrêt de l'IHM les oublie.
- Chaque native a sa fiche (ST, C, C++, un exemple exécuté par l'essai), sa place dans le rangement des natives, l'aide à la saisie et Compiler.
- COMPARAISON#, OPERATION# et TRANSFORMATION# restent : rien ne change dans les projets existants.

### Les types, les variables, les popups (R2, R3)

- Le registre des types lit `CALLBACK (x : REAL; n : INT) : BOOL`, avec une clé stable `cb:(base:REAL,base:INT)base:BOOL`.
- La puce **Rappels** du sélecteur propose six formes toutes faites. Une autre forme se tape.
- **Une variable IHM rappel** vit dans la mémoire de l'IHM, sans adresse. Sa valeur initiale s'écrit `=Valider`.
- **Un paramètre de popup ou de symbole rappel** se reçoit à l'ouverture :
  - `Sur_Oui := Valider` (une fonction) ;
  - `Sur_Oui := LAMBDA () => …` ;
  - `Sur_Oui := Sur_Fin` (une variable rappel de l'appelant) ;
  - `Sur_Oui := NULL`.

  La popup l'appelle `Sur_Oui();` dans « Exécuter un script », ou par la nouvelle action **Appeler** (famille Scripts du volet Actions) : la cible (un rappel reçu, une variable rappel, une fonction de la vue ou du projet) et ses arguments, sans script. Générer refuse une action Appeler sans cible, ou qui vise une fonction inconnue.
- **Générer** vérifie un argument rappel :
  - la fonction donnée existe et a la bonne forme ;
  - une variable rappel a la même forme ;
  - un nom en texte (`'Valider'`) donne un avertissement.
- **La simulation** montre un rappel `→ Valider`. On le force par le nom d'une fonction ou `=Valider`.
- **La rémanence** garde le nom de la fonction. Une LAMBDA ne se garde pas : la variable repart de sa valeur initiale.

### Retrouver (R5)

- **« Voir les usages »** d'une fonction (clic droit dans l'arbre) cherche son nom dans tout le projet, mot entier.
- « Appelée par » (l'inspecteur d'une fonction) compte aussi les endroits où elle est passée en rappel :
  - `f := Valider` ;
  - `LIST_FILTER(L, Est_Valide)` ;
  - `IHM_APRES(T#1s, Valider)` ;
  - la valeur initiale `=Valider` d'une variable rappel ;
  - l'argument `Sur_Oui := Valider` d'une popup.
- **Renommer une fonction suit tous ces endroits.** Un nom voisin (`Valider_Total`) n'est pas touché.

### Le guide et l'aide à la saisie (R7)

- Une page **« Rappels et génériques : CALLBACK, LAMBDA, TEMPLATE »** (Programmer) : le langage, la table des natives à rappel, TEMPLATE, l'équivalent en C et en C++. F1 sur CALLBACK, LAMBDA ou TEMPLATE y mène.
- Les modèles CALLBACK, LAMBDA, LAMBDA … END_LAMBDA et TEMPLATE sont dans l'aide à la saisie.
- Les mots du langage sont colorés et réservés.
- Sept notes de version de plus : les cellules, les fenêtres, la simulation, les rappels, les génériques, les natives à rappel, les variables et paramètres rappel.

## 7. Ce qui reste de la maquette

Ces points de la maquette ne sont pas dans cette version :
- **les symboles, les popups et les structures génériques** (`Afficheur_Mesure<T>`, `T_Mesure<T>`), décision 7 ;
- **les types rappel nommés** du projet (`T_Filtre`, Types IHM › Nouveau › Rappel…) ;
- **le contrôle « sans effet »** d'une LAMBDA écrite dans une expression fx (décision 4) : la LAMBDA y marche et Compiler la vérifie, mais rien n'empêche encore qu'elle écrive une variable ;
- **le C et le C++** (décision 10) : l'en-tête du projet et la vérification plus large ;
- **la correction d'un clic** d'un nom de fonction en texte (décision 6) : l'avertissement est là, pas le bouton ;
- **l'avertissement** quand une variable rappel rémanente tient une LAMBDA (elle repart vide sans le dire).

## 8. Les essais

77 suites sur 77. Les essais nouveaux :
- `hminatives` (2532 contrôles) :
  - les natives à rappel (12 contrôles) ;
  - les minuteurs, à l'heure près ;
  - le rappel vide dit une fois ;
  - le registre (CALLBACK, la clé, les refus) ;
  - la variable IHM rappel `=Tres_Grand`, une LAMBDA dans une variable IHM ;
  - la rémanence et le forçage d'un rappel ;
  - Compiler sur 6 formes.
- `hmi` (5842 contrôles), la popup qui reçoit un rappel :
  - une fonction, une variable rappel, une LAMBDA, NULL ;
  - Générer : la bonne forme, la mauvaise, un nom en texte, un nom inconnu ;
  - « Appelée par » et Renommer suivent les rappels ;
  - l'action Appeler : une fonction inconnue et une cible vide sont refusées, `Sur_Oui` passe.
- `hmi`, les rappels dans les fx : `Filtre(250.0)`, une LAMBDA dans `LIST_COUNT_IF`, `LIST_ANY(Mesures, Tres_Grand)`, un script `f := Tres_Grand` ; Compiler ne dit rien, la simulation les montre, la Console reste vide.
- `hmitypes` (157 contrôles) : l'ordre du registre, avec les formes de rappel.
- `hmieditor` (7978 contrôles) : les notes (52 versions, 370 notes), le guide (211 sujets dans L'IHM).
