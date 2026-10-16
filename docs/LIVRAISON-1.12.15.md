# XPGAnalyser 1.12.15 : le résultat final à chaque niveau du sélecteur, le déballage à toute profondeur

Livrée le 16/10/2026 en début d'après-midi. Elle suit la 1.12.14 de la fin de matinée (`docs/LIVRAISON-1.12.14.md`).

Elle répond à la demande du 16/10, faite sur une capture du sélecteur de types de la 1.12.14 :

> « c'est bien il faut juste ajouter un resultat final a chaque composition sinon on peut se perdre sur les types comme ca un peu dynamique, les lister et dans les listes de variabels quand on les deballe ajouter les paramètres public accessible genre 'size', 'maxsize', 'length' etc pour chaque profondeur, on peut deballer sous deballer avec pour les maps par exmeple le cote first, second etc »

Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API ne change pas, à part son numéro de version.

## 1. Le résultat final à chaque niveau du sélecteur

« Composer l'élément… » ouvre un second sélecteur, qui peut en ouvrir un troisième, et ainsi de suite. Chacun montre maintenant deux lignes :
- **Résultat** : ce qu'on choisit à ce niveau ;
- **Résultat final** : le type complet qui sortira du premier sélecteur. La part choisie à ce niveau est mise en évidence et suit chaque choix.

Exemple, pour la variable de la capture :
- au premier niveau, on coche Dictionnaire (MAP), clé STRING, puis on clique « Composer l'élément… » ;
- au niveau 2, sur Vecteur, la ligne dit : Résultat final `MAP[STRING] OF VECTOR OF T_Cuve`. Cocher Liste la change aussitôt en `MAP[STRING] OF LIST OF T_Cuve` ;
- au niveau 3, la ligne dit `MAP[STRING] OF LIST OF …` avec ce qu'on y choisit.

Le titre dit le niveau : « Choisir un type · « éléments de Type de var2 » · niveau 2 ».

## 2. Le déballage à toute profondeur

Dans **Variables IHM**, une variable LIST, VECTOR, SET, RING, MAP ou TUPLE se déplie maintenant, et chaque partie se déplie à son tour. Un membre de ces types dans une structure se déplie aussi. Avant, il restait une seule ligne.

À chaque niveau, on voit d'abord les **propriétés publiques**, celles qu'un point lit. Elles sont en gris, en lecture seule, avec leur valeur au démarrage quand la valeur initiale la donne. Leurs autres noms sont écrits à côté :

| Type | Propriétés |
| --- | --- |
| LIST, VECTOR, SET | `.Count` (aussi Size, Length), `.Empty` (aussi IsEmpty), `.First`, `.Last`, `.Low`, `.High` |
| RING[N] | les mêmes, plus `.Capacity` (aussi **MaxSize**) et `.Full` |
| MAP | `.Count` (aussi Size, Length) |
| ARRAY | `.Length` (aussi Size, Count, MaxSize), `.Low`, `.High` ; à deux dimensions, aussi `.Rows`, `.Columns`, `.Dimensions` |
| structure IHM (une variable) | `.Bytes`, `.Words`, après ses membres |

Ensuite viennent les **éléments** :
- ceux de la valeur initiale, chacun avec sa valeur : `Parc['a']`, `Parc['a'][0]`, `Parc['a'][0].Nom`… ;
- sans valeur initiale, un élément type qui montre la forme : `Cuves[i]` (« chaque élément, i de 0 à Count - 1 ») ou une entrée `Parc['…']`. Il se déplie lui aussi, et les indices suivants sont `j`, `k`… ;
- une **entrée de MAP** est une paire, comme dans un débogueur. Dépliée, elle montre **first (la clé)** et **second (la valeur)**. La valeur se déplie à son tour.

Exemple avec `Parc : MAP[STRING] OF VECTOR OF T_Cuve := ['a' := [(Nom := 'A', Niveau := 2.5), (Nom := 'B')]]` :

```
Parc                       MAP[STRING] OF VECTOR OF T_Cuve
  Parc.Count               DINT      1          propriété · aussi Size, Length
  Parc['a']                STRING → VECTOR OF T_Cuve
    first (la clé)         STRING    'a'
    second (la valeur)     VECTOR OF T_Cuve
      Parc['a'].Count      DINT      2
      Parc['a'].First      T_Cuve
      …
      Parc['a'][0]         T_Cuve
      Parc['a'][1]         T_Cuve
        Parc['a'][1].Nom     STRING  'B'
        Parc['a'][1].Niveau  REAL
```

Une ligne du déballage choisie montre sa fiche à droite : son chemin, son type, sa valeur au démarrage et ce qu'elle est.

Ces propriétés apparaissent aussi ailleurs :
- **sous les grilles des déclarations et des locales d'un calcul**, le déballage d'une valeur montre les mêmes propriétés en tête de chaque niveau (`.Count = 2`, `.Length = 3`…). Une entrée de MAP s'y déplie aussi en first et second ;
- le **sélecteur de valeur** propose ces propriétés à chaque niveau. On peut choisir directement `Parc.Count`, `Parc['a'].Count`, `Tampon.Capacity` ou `Seuils.Length`. Filtré sur DINT, il les garde.

**MaxSize** se lit maintenant aussi sur un RING : `Tampon.MaxSize` vaut `Tampon.Capacity`, comme pour un tableau. Générer, Compiler et le moteur l'acceptent.

## Tester vous-même

1. Dans un projet IHM, créez T_Cuve (Nom : STRING, Niveau : REAL), puis une variable `var2`. Cliquez sa case Type et cochez Dictionnaire (MAP), puis « Composer l'élément… » : au niveau 2, la ligne « Résultat final » dit le type complet. Cochez Liste : elle suit. Composez encore : le titre dit « niveau 3 ».
2. Donnez à `Parc : MAP[STRING] OF VECTOR OF T_Cuve` la valeur `['a' := [(Nom := 'A', Niveau := 2.5), (Nom := 'B')]]`. Dépliez Parc : `Parc.Count` vaut 1. Dépliez `Parc['a']`, puis second, puis `Parc['a'][1]` : vous lisez Nom = 'B'.
3. Créez `Vide : MAP[STRING] OF LIST OF INT`, sans valeur. Dépliez-la : une entrée `Vide['…']`, et sous second, `.Count`, `.Empty`… puis `Vide['…'][i]`.
4. Créez `Tampon : RING[5] OF REAL := [1.5, 2.5]` et dépliez-la : Count 2, Capacity 5 (aussi MaxSize), Full FALSE.
5. Dans une case fx, ouvrez le sélecteur de valeur : `Parc.Count` et `Tampon.Capacity` y sont, sous leur variable.

## Installer

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.15.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.15-portable.zip` et `XPGAnalyser-IHM-1.12.15-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lancez `XPGAnalyser-Setup-1.12.15.exe`. Il met à jour la version installée, la 1.12.14 ou une plus ancienne : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.15.exe` (29,8 Mo, 31 216 877 octets) :
`71563DAA740AEAB7509BF1EE4ADB55700EDD8494F86825615E56430D658BD26E`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.15` ou `XPGAnalyser-IHM-1.12.15`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.15-portable.zip` (22,2 Mo) :
  `E40943D70AB5772F67FCF9F8F684A3DB5FFFC5CE9C0B8FEF02C8B8905C9C496C`
- `XPGAnalyser-IHM-1.12.15-portable.zip` (22,2 Mo) :
  `7ED1D1D8F3B2110CA9F44EB51761808D0079C2D4645237B6E68B2EE1BBF67217`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 74 sur 74. Elle comprend :
- 7 363 contrôles dans `hmieditor` (7 343 à la 1.12.14), 5 656 dans `hmi` (5 654) et 157 dans `hmitypes` ;
- ceux d'avant, qui passent toujours.

Les nouveaux essais couvrent :
- le sélecteur imbriqué : le niveau 2 reçoit `MAP[STRING] OF {}` et son niveau. Son Résultat final est `MAP[STRING] OF VECTOR OF T_Four`, puis `MAP[STRING] OF LIST OF T_Four` après Liste. Le niveau 3 reçoit `MAP[STRING] OF LIST OF {}`, avec le même champ. Le premier sélecteur n'a pas de ligne finale ;
- la liste Variables IHM :
  - Parc, son Count et son entrée `Parc['a']`, avec first = 'a' et second ;
  - `Parc['a'].Count` = 2, puis ses éléments, jusqu'à `Parc['a'][1].Nom` = 'B' ;
  - une MAP vide et son entrée type, jusqu'à `[i]` ;
  - un RING : Capacity 5 (aussi MaxSize), Full ;
  - un tableau : Length (aussi Size, MaxSize), Low, High, avant ses cases ;
  - une structure : Words après ses membres ;
  - une ligne du déballage ne s'écrit pas ;
- le sélecteur de valeur filtré sur DINT : Parc.Count, Parc['a'].Count, Tampon.Capacity, Seuils.Length ;
- les propriétés de chaque forme (LIST, RING, MAP, ARRAY à une et deux dimensions ; rien pour un tuple, une structure, un type simple) ;
- `Tampon.MaxSize + Tampon.Capacity` = 10, sans un mot de Générer ni de Compiler ;
- les déballages sous les grilles : `.Count = 2` et les entrées d'une MAP ; `.Length`, `.Low`, `.High` d'un tableau avant ses cases.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.15` et `XPGAnalyser IHM 1.12.15` ;
- ils importent exactement les mêmes DLL que ceux de la 1.12.14 (ADVAPI32, IPHLPAPI, KERNEL32, SDL3, SHELL32, USER32, WS2_32, msvcrt, ole32).

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.14 :
- avant, la 1.12.14 était installée (`XPGAnalyser IHM 1.12.14`) ;
- « Installation process succeeded ». Il a repris l'installation en place : même dossier, et son journal de désinstallation complété ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `XPGAnalyser API 1.12.15` et `XPGAnalyser IHM 1.12.15`, et l'API installée analyse `MAST.XPG` comme avant (891 variables, 29 POU).

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici). Pas de session Wine avec captures pour cette version : la section « Tester vous-même » donne le parcours.

## Limites connues

- **Les valeurs du déballage sont celles du démarrage**, lues dans la valeur initiale. En simulation, la liste Variables IHM ne suit pas les valeurs en direct d'une LIST ou d'une MAP.
- **Un élément type** (`[i]`, `['…']`) montre la forme, pas une valeur.
- **Bytes et Words** d'une structure se lisent sur une variable IHM et ses membres. Dans une LIST ou une MAP, une structure ne montre que ses membres.
- Au-delà de 200 éléments, le déballage montre les 200 premiers et dit combien il en reste.

## La suite

- Essayer sur un vrai Windows, avec la section « Tester vous-même ».
