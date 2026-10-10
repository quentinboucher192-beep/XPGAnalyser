# XPGAnalyser 1.12.2 : les types objets partout, la liste et le tableau qui suivent des variables, les raccourcis de Visual Studio

Livrée le 10/10/2026 dans l'après-midi. C'est la **livraison intermédiaire** annoncée : elle contient tout ce qui est terminé et testé. Le reste de la liste du 10/10 (Disposition, multi-états, multi-sélection, simulation et poste) arrive en 1.12.3 (§ 9).

Elle répond à vos demandes du 10/10 :

> « dans tous les endroits qui proposent de créer des variables, il faut pouvoir mettre ARRAY, ARRAY de 2 dimensions, MAP etc, d'ailleurs j'aimerais ajouter LIST, TUPLE, VECTOR etc dans les types reconnus en natif (+ fonctions liées aux types concernés) et intégrer ca dans le créateur de variables […] attention aux types avec taille dynamique, ils seront impossibles à allouer dans une mémoire de taille fixe genre les équipements, bloquer le selecteur 'Equipement', 'Adresse' pour eux […] y'a encore bcp d'endroits avec écrit 'Automate' ou 'API' corrige / L'objet 'liste' et 'liste déroulante' difficiles à configurer (qu'il puisse s'adapter à plusieurs types) / objet 'tableau' pas hyper pratique pour taille dynamique »

> « Il faut virer l'onglet 'Variables employées' aussi dans programmation générale »

> « les types de retours des fonctions doivent pouvoir utiliser tous les nouveaux types, dans les creation des fonctions pouvoir mettre un type spécifique »

Elle contient aussi deux points de la liste du matin : les **raccourcis de Visual Studio** dans les éditeurs, et les **pièges de l'éditeur**.

Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API ne change pas, sauf ses éditeurs de code, qui gagnent les raccourcis (§ 2).

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.2.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.2-portable.zip` et `XPGAnalyser-IHM-1.12.2-portable.zip` : chaque application, sans installation ;
- ce document ;
- les captures `1122_ihm_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

| | Avant (1.12.1) | Maintenant (1.12.2) |
|---|---|---|
| Types d'une variable IHM | simple, tableau, structure, énumération | en plus : tableau 2D, `LIST OF T`, `VECTOR OF T`, `MAP[K] OF T`, `TUPLE(T1, T2…)` |
| Où les choisir | le sélecteur de types, à la main | partout où une variable se crée, avec une **Forme** : Simple, Tableau, Tableau 2D, Liste, Vecteur, Dictionnaire (MAP), Tuple |
| Retour d'une fonction IHM | un type simple | toutes les formes, et un type précis à la création |
| Liste, liste déroulante, boutons radio, sélecteur | des éléments écrits à la main (`a;b;c`) | aussi **Éléments depuis** : une énumération, une variable LIST, VECTOR, MAP, un tableau, une expression |
| Liste | un texte à lire | une commande : un clic écrit la ligne, la ligne choisie se surligne, elle défile |
| Tableau | des cases ou un fichier | aussi **Lignes depuis** une variable : une ligne par élément, il défile et s'exporte entier |
| Éditeurs de code | les raccourcis de la 1.12.1 | ceux de Visual Studio, avec les accords `Ctrl+K, …` |
| Programmation générale | une branche Variables employées | retirée |
| XPGAnalyser IHM | parlait encore de l'« automate » | dit « équipement », « variable », « IHM » |

## 2. Les raccourcis de Visual Studio

Ils marchent dans tous les éditeurs de code : scripts, fonctions, opérateurs, script d'une action, et les sections ST dans XPGAnalyser API.

| Accord ou touche | Ce qu'il fait |
|---|---|
| `Ctrl+K, Ctrl+C` / `Ctrl+K, Ctrl+U` | commenter / décommenter les lignes |
| `Ctrl+K, Ctrl+D` | mettre en forme |
| `Ctrl+K, Ctrl+S` | entourer de `IF`, `FOR`, `WHILE`, `CASE`, `TRY` |
| `Ctrl+K, Ctrl+K` puis `Ctrl+K, Ctrl+N` / `Ctrl+K, Ctrl+P` | poser un signet, aller au suivant / au précédent |
| `Ctrl+D` / `Alt+↑` / `Alt+↓` | dupliquer la ligne, la monter, la descendre |
| `Ctrl+L` / `Ctrl+Maj+L` | couper / supprimer la ligne |
| `Ctrl+C` sans sélection | copier la ligne entière |
| `Ctrl+/` (`Ctrl+:` en AZERTY) | commenter ou décommenter, selon la ligne |
| `Ctrl+U` / `Ctrl+Maj+U` | minuscules / MAJUSCULES |
| `Ctrl+M, Ctrl+M` / `Ctrl+M, Ctrl+L` | plier ou déplier le bloc / tout |
| `Ctrl+]` (`Ctrl+$` en AZERTY) | aller au bout du bloc |
| `Ctrl+F`, `Ctrl+H`, `Ctrl+G`, `F3` | rechercher, remplacer, aller à la ligne, suivant |
| `F12` / `Maj+F12` / `F2` (ou `Ctrl+R, Ctrl+R`) | aller à la définition / trouver les références / renommer |
| `Ctrl+.` | la correction proposée par la faute |

- Quand vous tapez `Ctrl+K`, la barre d'état attend la seconde touche, comme Visual Studio. Une combinaison inconnue est signalée.
- Chaque commande s'annule en une fois.
- Le profil **classique** (les raccourcis de la 1.12.1, sans accords) se choisit par **Aller à**. Le choix est gardé.
- La page d'aide des raccourcis les dessine tous (capture `1122_ihm_08`).

## 3. Les types objets partout

**Les formes.** Un type se compose d'une forme et d'un élément :

| Forme | Exemple | Taille |
|---|---|---|
| Simple | `REAL` | fixe |
| Tableau | `ARRAY[0..9] OF REAL` | fixe |
| Tableau 2D | `ARRAY[0..3, 0..9] OF INT` | fixe |
| Liste | `LIST OF REAL` | variable |
| Vecteur | `VECTOR OF REAL` | variable |
| Dictionnaire (MAP) | `MAP[STRING] OF REAL` | variable |
| Tuple | `TUPLE(REAL, STRING, BOOL)` | fixe, mais sans place mémoire |

**Où les choisir.** Partout où une variable se crée :
- **Nouvelle variable IHM** et **Créer la variable** (depuis une case d'inspecteur) : un champ **Forme**, puis le type des éléments, les bornes ou la clé, et le type écrit, qui se met à jour ;
- **les Variables IHM** : les boutons Liste…, Vecteur…, Dictionnaire (MAP)…, Tuple…, et le sélecteur de types, qui a lui aussi sa Forme ;
- **les déclarations** des scripts, des fonctions et des opérateurs : locales, paramètres et constantes ;
- **Tableau…** : il garde les formes à taille fixe (une structure ne peut pas contenir une liste).

**La taille variable.** Une liste, un vecteur ou un dictionnaire ne tient pas dans la mémoire fixe d'un équipement. Ces variables vivent dans la mémoire de l'IHM :
- **Équipement** et **Adresse** sont bloqués pour elles, et la bulle dit pourquoi. La rémanence aussi.
- Si vous donnez ce type à une variable déjà liée à un équipement, elle est **déliée** : son équipement, son adresse, sa mise à l'échelle et sa rémanence sont retirés, et le message le dit. Ctrl+Z la remet comme avant.
- Le tuple n'a pas de place mémoire : il suit la même règle.

**Les valeurs.** Les littéraux s'écrivent comme en ST :

```
Recettes : LIST OF STRING := ['Pain', 'Brioche', 'Baguette']
Stock    : MAP[STRING] OF INT := ['farine' := 120, 'sel' := 8]
Point    : TUPLE(INT, STRING) := (1, 'a')
Grille   : ARRAY[0..1, 0..1] OF INT := [[1, 2], [3, 4]]
```

Pour une constante ou une valeur initiale, le bouton **Éléments…** ouvre un éditeur, une valeur par ligne : plus besoin d'écrire les crochets. Compiler refuse une valeur qui ne va pas dans le type : trop de valeurs, un texte dans une liste de nombres, une clé du mauvais type.

**Les fonctions natives.** 28 nouvelles fiches dans **Natives**, chacune avec son exemple exécuté par les essais :
- listes : `LIST_ADD`, `LIST_INSERT`, `LIST_REMOVE_AT`, `LIST_REMOVE`, `LIST_CLEAR`, `LIST_COUNT`, `LIST_CONTAINS`, `LIST_INDEX_OF`, `LIST_FIRST`, `LIST_LAST`, `LIST_GET`, `LIST_SORT`, `LIST_REVERSE`, `LIST_SUM`, `LIST_MIN`, `LIST_MAX`, `LIST_AVG` ;
- vecteurs : `VECTOR_PUSH`, `VECTOR_POP`, `VECTOR_INSERT`, `VECTOR_ERASE`, `VECTOR_CLEAR`, `VECTOR_SIZE`, `VECTOR_RESIZE`, `VECTOR_FRONT`, `VECTOR_BACK` ;
- textes : `JOIN(L, ';')` et `SPLIT('a;b;c', ';')`.

Dans un script, `L[0]`, `L.Count`, `L.First`, `t.Item1`, `FOR EACH x IN L` et `L1 + L2` marchent aussi. Le contrôle des scripts et l'aide à la saisie les connaissent.

## 4. Le retour des fonctions

La fenêtre **Nouvelle fonction** a une **Forme du retour** (Aucun, Simple, Tableau, Liste, Vecteur, Dictionnaire, Tuple), le type des éléments, les bornes ou la clé, et le **Type écrit**. Vous pouvez y taper un type précis, par exemple `LIST OF T_Recette` ou `TUPLE(STRING, REAL)`. Une fonction peut donc rendre une liste ou un tuple :

```
FUNCTION Dizaines : LIST OF INT
...
L := Dizaines(3);      (* [10, 20, 30] *)
```

## 5. La liste, la liste déroulante et le tableau

**Éléments depuis.** La Liste, la Liste déroulante, les Boutons radio et le Sélecteur ont une nouvelle propriété, **Éléments depuis**. Elle remplace la liste écrite à la main, et l'inspecteur propose les sources du projet :

| Source | Ce qui s'affiche | Ce qui s'écrit dans la variable |
|---|---|---|
| une énumération (`T_Mode_Four`) | ses textes (Arrêt, Préchauffe, Cuisson…) | son nombre |
| une variable `LIST` ou `VECTOR` | ses éléments | l'élément, tel quel |
| un tableau IHM | ses cases | la valeur de la case |
| une `MAP` | ses clés | la clé |
| une expression qui rend `a;b;c` | les morceaux | le rang |

Les lignes suivent la variable en marche : un `LIST_ADD(Recettes, 'Fougasse')` ajoute une ligne. Compiler signale une source inconnue.

**La Liste devient une commande.** Elle a une **Variable écrite** : un clic sur une ligne l'écrit, et la ligne choisie se surligne. Quand tout ne tient pas, deux bandes fléchées et la molette la font défiler. Sans variable, elle se lit seulement.

**Le tableau dynamique.** Le Tableau a une propriété **Lignes depuis** : une variable `LIST`, `VECTOR`, `MAP` ou un tableau de l'IHM.
- Il montre une ligne par élément, autant que la variable en a.
- Ses colonnes viennent du type : les membres d'une structure, `Item1`, `Item2`… d'un tuple, `Clé` et `Valeur` d'une MAP, sinon `Valeur`. Dans l'éditeur, il les montre déjà.
- Il défile à la molette ou par sa barre à droite.
- L'export du tableau prend toutes les lignes, pas seulement celles qui sont affichées.

## 6. Les pièges de l'éditeur

- Une action de **vue** ne propose plus Clic, Double clic ni Appui long : un clic se fait sur un objet, donc ces actions ne partaient jamais. Une ancienne action réglée ainsi le dit dans l'inspecteur.
- Lier une variable rémanente à un équipement retire sa rémanence : c'est l'équipement qui garde sa valeur. La case affichait « oui » sans effet.
- La colonne Public / Privé des déclarations des scripts est retirée : elle ne faisait rien.
- Le **Profil utilisateur** d'un objet quitte l'inspecteur : rien ne le lisait. **Niveau d'accès** fait ce travail.
- **Projet › Nouveau** dans XPGAnalyser IHM propose les créations de l'IHM (vue, popup, symbole, script, fonction, variable IHM).
- **Aller à** ne propose plus la Table des adresses, qui est cachée dans l'IHM. Le chronogramme n'a plus de case Couleur sans effet.

## 7. Variables employées

La branche **Variables employées** quitte **Programmation générale**. Pour savoir où une variable sert :
- **IHM › Rechercher** ;
- **Maj+F12** sur son nom dans un éditeur.

Le filtre « Employées » de la Configuration marche toujours.

## 8. XPGAnalyser IHM sans « automate »

Dans XPGAnalyser IHM, il n'y a pas d'automate : les équipements en tiennent lieu. Les textes disent maintenant ce qui existe vraiment :
- **l'inspecteur** : « Variable » au lieu de « Variable API », la Valeur « reliée à une variable », le compteur de production, l'état de la liaison (« l'équipement répond ») ;
- **les bulles** des actions, du contenu, des alarmes, des fonctions, des opérateurs, des types et des essais, et les fenêtres Forcer, Nouvelle fonction et Essayer ;
- **la Communication** ne montre plus d'« automate du projet » : ni sa fiche, ni sa ligne d'état, ni son ping, ni sa carte mémoire. Sans équipement, elle le dit ;
- **le diagnostic système** et l'état du moteur n'ont plus de groupe « Automate » ;
- **le guide** et l'aide des paramètres (F1, bulles de l'inspecteur) sont dits avec les mots de l'IHM. Un essai vérifie qu'il n'y reste plus le mot « automate ».

XPGAnalyser API garde ses mots.

## 9. Ce qui viendra en 1.12.3

Ce sont les points de la liste du 10/10 qui ne sont pas dans cette livraison. Ils sont décrits dans la maquette interactive :
- **Projet › Disposition…** : quels sous-onglets et panneaux montrer, quand, où et comment, gardé avec le projet ;
- **l'éditeur des états** des objets multi-états (texte multi-états, voyant multi-états…) : en table, et la formule sur plusieurs lignes ;
- **la sélection multiple** dans l'explorateur d'objets, et la modification des propriétés communes ;
- **la simulation et le poste** : les courbes enregistrées dès le démarrage, les écritures forcées, l'état du poste gardé.

## Tester vous-même

1. Lancez **XPGAnalyser IHM**. Dans **Variables IHM**, créez une variable `Recettes` : Forme **Liste**, éléments `STRING`. Dans sa valeur initiale, **Éléments…** : tapez `Pain`, `Brioche` et `Baguette`, un par ligne.
2. Essayez de lui donner un **Équipement** : la case est grisée, et la bulle dit pourquoi.
3. Posez une **Liste** dans une vue. Dans l'inspecteur, **Éléments depuis** : `Recettes`. **Variable écrite** : une variable `STRING`.
4. Lancez la simulation et cliquez sur « Baguette » : la ligne se surligne et la variable vaut `Baguette`.
5. Posez un **Tableau**. **Lignes depuis** : une variable `LIST OF TUPLE(STRING, INT)` avec une dizaine d'éléments. En simulation, la molette le fait défiler.
6. Dans un script, tapez `Ctrl+K` puis `Ctrl+C` sur deux lignes : elles sont commentées. `Ctrl+K`, `Ctrl+U` les décommente.

## Installer

1. Lancez `XPGAnalyser-Setup-1.12.2.exe`. Il met à jour la version installée : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.2.exe` (28,1 Mo, 29 443 642 octets) :
`1F5AD4C17E01544A8A33F2D0C19D8437C25C34C45F4CCBDD13EBB75254EE4315`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.2` ou `XPGAnalyser-IHM-1.12.2`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.2-portable.zip` (21,0 Mo) :
  `93062682C402CB50E108410EF260DDC78F30696833857D147FD2C3209FE4D937`
- `XPGAnalyser-IHM-1.12.2-portable.zip` (21,0 Mo) :
  `1522651E4BEE72A0FA50E7ECAABF4D6557BB947D8EBEE3EBC30D12CB24796595`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 70 sur 70. Elle comprend les 6 699 contrôles de `hmieditor`, les 4 806 de `hmi`, les 1 842 de `hminatives` (les 28 nouvelles fiches, chaque exemple exécuté) et les 258 de `hmireperes`. Les nouveautés ont leurs essais : les collections du langage (`scriptsCollections1122`), les objets Liste, Liste déroulante, Boutons radio, Sélecteur et Tableau (`objetsListes1122` : chaque source, le clic, le défilement, l'export, Compiler), et le guide de l'IHM sans « automate » (`edition`).

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.2` et `XPGAnalyser IHM 1.12.2` ;
- `XpgAnalyzer-API.exe --cli MAST.XPG` analyse le projet d'essai (891 variables, 29 POU), comme avant ;
- une session sous Wine avec l'exe livré : Armoire_Gaz reçoit une vue Vue_Listes (`Recettes` en `LIST OF STRING`, `Stock` en `MAP[STRING] OF INT`, `Lots` en `LIST OF TUPLE(STRING, INT, REAL)`, une énumération `T_Mode_Four`). La simulation démarre (build réussi, 0 erreur), un clic sur la liste écrit `Baguette`, la liste déroulante de l'énumération écrit `2`, les boutons radio de la MAP écrivent `farine`, le tableau défile à la molette. Captures `1122_ihm_01` à `08`.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.1 :
- « Installation process succeeded » ;
- avant comme après : `XpgAnalyzer-API.exe`, `XpgAnalyzer-IHM.exe`, et les raccourcis « XPGAnalyser API » et « XPGAnalyser IHM » ;
- les deux exe installés sont identiques à ceux qui ont été testés.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). La section « Tester vous-même » donne le parcours à refaire chez vous.

## La suite

- la 1.12.3 (§ 9) ;
- `DATE`, `TOD`, `DT`, `CHAR` et `WSTRING` comme variables IHM, avec leur place Modbus ;
- la source du guide Word, à remettre à jour depuis la 1.11.2.
