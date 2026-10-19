# XPGAnalyser 1.12.17 : un tableau entier refusé là où une valeur seule est attendue, les natives rangées, les conteneurs expliqués, Mes fonctions et Rend, les performances dans le temps, le journal des lenteurs, la Simulation dépliée

Livrée le 19/10/2026 au soir. Elle suit la 1.12.16 du même jour (`docs/LIVRAISON-1.12.16.md`).

Elle répond à deux demandes. La première, après la maquette validée (« ok go ») :

> « dans natives, dans types, il manque les types genre 'tuple', 'list', 'vector', 'map', 'ring' etc, avec des explications et exemple / reorganise un peu mieux les natives pour que ce soit plus logique et facile à naviguer / dans le sélecteur de fonction, il faut mettre toutes celles qui sont aussi publics créés par l'utilisateur bien rangés dans un dossier en bas en violet et avec onglets, sous onglets, deballage etc / ajouter un champ pour filtrer les fonctions par retour, just des void etc / dans 'Simulation' ajoute des deballage, dans vue d'ensemble, amene les performances en mieux et plus liées au temps pas juste à l'instantannée, stock les actions effectuées qui ont durées anormalement longtemps »

La seconde, un bug signalé le soir :

> « y'a un petit bug, quand je mets dans une plume une variable de type tableau REAL par exemple ecrite comme ca 'TABLEAU' au lieu de 'TABLEAU[0]' j'ai aucune erreur alors que je vise un tableau et pas un élément, à corriger, à verrifier les autres cas de figure qui ont aussi ce bug »

Tout se passe dans **XPGAnalyser IHM**. XPGAnalyser API ne change pas, à part son numéro de version.

## 1. Un tableau entier là où une valeur seule est attendue (la correction)

**Avant :** une plume `TABLEAU` (un `ARRAY[0..9] OF REAL`) passait Générer sans un mot, et ne traçait rien.

**Maintenant :** Générer et Compiler le disent en **erreur**, à sa ligne, avec ce qu'il faut écrire :

> plume TABLEAU : un nombre est attendu, et TABLEAU est un tableau entier (ARRAY[0..9] OF REAL) - choisis un élément : TABLEAU[0]

La même règle vaut partout où une valeur seule est attendue. Voici les cas vérifiés :

| Où | Refusé | Le message propose |
| --- | --- | --- |
| une plume d'une courbe, et sa condition **Visible si** | `TABLEAU`, `BOOLS` | `TABLEAU[0]`, `BOOLS[1]` |
| une mesure d'un graphique (barres, secteurs, radar, XY), une ligne d'un chronogramme, une consigne d'un radar, la variable X d'un XY | `TABLEAU`, `L` (une liste) | `TABLEAU[0]`, `L[0] ou L.Count` |
| une variable archivée (Historique) | `TABLEAU` | `TABLEAU[0]` |
| la variable d'un objet qui montre une valeur (jauge, afficheur, voyant, bargraphe…) | `TABLEAU` | `TABLEAU[0]` (avant : un simple avertissement) |
| la variable qu'écrit un objet (une commande, un curseur, un champ de saisie, un programmateur) | `CUVE` (une structure) | `CUVE.Niveau` (avant : rien pour un champ de saisie ou un programmateur) |
| une condition, une couleur, une valeur pilotée (fx) | `=BOOLS` | `BOOLS[1]` |
| un calcul | `TABLEAU * 2`, `ABS(TABLEAU)`, l'alarme `TABLEAU > 5` | `TABLEAU[0]` |
| la cible d'une action : Mettre à 1, Mettre à 0, Basculer, Incrémenter, Décrémenter, `:=` | `BOOLS`, `TABLEAU := 5`, `X := TABLEAU` | `BOOLS[1]`, `TABLEAU[0]` |

Est « entier » : un tableau (à une ou deux dimensions), une LIST, un VECTOR, un SET, un RING, une MAP, un TUPLE ou une structure (un type IHM qui a des membres). Le message dit quoi écrire à la place : un élément (`TABLEAU[0]`, `MATRICE[0, 0]`), une propriété (`L.Count`, `R.Last`), une entrée (`M['clé']`) ou un membre (`CUVE.Niveau, CUVE.Actif`).

Ces écritures restent acceptées, parce qu'elles sont voulues :
- un texte à trous montre le tableau entier : `{TABLEAU}` affiche `[1, 2, 3]` ;
- `Copie := TABLEAU` copie un tableau dans un autre du même type ;
- le joker `TABLEAU[*]` d'une courbe trace une plume par élément ;
- la liste d'une liste déroulante (« Éléments depuis ») prend une liste entière ;
- un type qui a ses opérateurs ou ses conversions se calcule, se compare et se convertit entier : `V0 + V1`, `V0 < V1`, `TO_REAL(V0)` ;
- dans un script, `ASCII_TO_STRING(Nom)` et `STRING_TO_ASCII` prennent un tableau entier (les cases d'une chaîne). Dans une fx, le moteur ne le fait pas : c'est une erreur.

**Pour la simulation**, c'est une erreur qui bloque le démarrage, où qu'elle soit (une plume, une action, une alarme comme la variable d'un objet). Elle est rouge dans Diagnostics, le build compte « 1 erreur », et un double-clic mène à l'objet. Le reste ne change pas : une plume dont la variable n'existe pas reste un avertissement non bloquant, comme avant.

## 2. Les natives, rangées par ce qu'on veut faire

**Programmation générale › Natives › Fonctions** : huit thèmes, Nombres, Textes, Dates et heures, Couleurs, Conteneurs, Conversions, L'IHM (IHM_), Références et échanges.
- Au-delà de 20 fonctions, un thème se range en sous-dossiers par geste. Par exemple :
  - Nombres › Calcul, Trigonométrie, Choisir et borner, Interpoler, Entiers et saturation, Constantes et limites, Aléatoire, Bits et octets ;
  - Conteneurs › Listes (› Ajouter et retirer, Lire et chercher, Trier et ranger, Calculer, Copier et convertir), Vecteurs, Tuples, MAP (› Lire, Écrire, Parcourir), Ensembles (SET), Tampons (RING), Tableaux (ARRAY), Cumuls et pliages ;
  - L'IHM › Navigation et vues, Popups, Utilisateurs, Alarmes, Journal et Console…
- Les conversions (`TO_…` et `X_TO_Y`) sont réunies dans **Conversions**.
- Les **énumérations** sont rangées par usage ; l'en-tête C++ d'une fonction (`<numeric>`) est en étiquette.
- **De A à Z** les liste toutes, par ordre alphabétique.
- Un ancien lien vers une catégorie (« map ») mène à son nouveau dossier.

**Natives › Types**, en cinq dossiers : Nombres et bits ; Textes, durées et dates ; **Conteneurs** ; Références ; Génériques.
- **Conteneurs** a une fiche pour ARRAY, LIST, VECTOR, MAP, SET, RING et TUPLE. Chacune dit quand le choisir, comment le déclarer, le lire et l'écrire, ses propriétés, comment le parcourir, ses fonctions, où il vit et ses notations, avec **un exemple joué par le moteur** et ce qu'il vaut. **Lequel choisir ?** les compare.
- Les références et les génériques (ANY_NUM…) ont aussi leur fiche : 41 fiches en tout.
- Dans un script, **F1** sur LIST, RING, MAP, POINTER ou ANY_NUM ouvre la fiche. **Insérer la déclaration** l'écrit dans le script.

## 3. « Choisir une fonction… » : Mes fonctions et Rend

Le sélecteur (Ctrl+I, ou la dernière ligne de l'aide à la saisie) a deux nouveautés.

**Mes fonctions**, en bas, en violet :
- Programmation générale › Fonctions IHM ; Vues, Popups, Modèles › la vue › Fonctions ; Symboles › le symbole › Fonctions ;
- le dossier d'où l'on vient est marqué « ici » et déplié. Une fonction privée ne se voit que chez elle. Les dossiers ouverts ou fermés le restent ;
- l'appel est écrit tel qu'il s'écrit d'ici : `Vue_Cuves.Niveau_Moyen()`, `Pop_Reglage.Monter(c)`. Une fonction de symbole se déplie sur ses instances : `Vanne_1.Ouvrir()` dans sa vue, `Vue_Cuves.Vanne_2.Ouvrir()` ailleurs, `Ouvrir()` dans le symbole ;
- la fiche d'une de vos fonctions dit où elle est, sa portée, l'appel d'ici et ses premières lignes. **Ouvrir sa définition** (Ctrl+Entrée) ouvre son sous-onglet Fonctions sur elle.

**La liste Rend** filtre par ce que la fonction rend : Tout, Ce qui convient ici, Sans retour, BOOL, Entiers, Réels, Textes, Durées et dates, Mots, Conteneurs, Références, Comme son argument, et vos types.
- Chaque choix montre son nombre, recompté quand on cherche.
- Dans une case fx, Sans retour est grisé : une fx doit rendre une valeur.
- 25 fonctions qui lisent un conteneur (LIST_AVG, LIST_SUM, MAP_GET, JOIN…) s'écrivent maintenant aussi dans une case fx.

## 4. Les performances dans le temps

**Simulation › Vue d'ensemble**, bloc **Performances**. Avant, il ne montrait que l'instant ; il garde maintenant une mesure par seconde.
- La période se choisit : **1 min**, **10 min**, **1 h** ou **depuis le lancement**. Au-delà d'une heure, une mesure toutes les 10 s, jusqu'à 8 h.
- Quatre cases : le cycle IHM en moyenne (son p95, son maximum et à quelle heure), la charge de la période, les dépassements (un cycle plus long que sa période), les lenteurs.
- La courbe du cycle, seconde par seconde : la médiane, et une barre jusqu'au maximum. Un point rouge marque un dépassement, un point orange une lenteur (un clic l'ouvre). Le survol dit la seconde.
- Quatre petites courbes : le dessin de la vue, les expressions, les écritures, les échanges avec les équipements.
- **Où passe le temps** : chaque script, les alarmes, les courbes et archives, le reste, avec leur part du cycle.
- **Remettre à zéro** repart d'une page blanche sans arrêter l'IHM. **Exporter en CSV** donne une ligne par mesure.

L'onglet Performances de l'IHM en marche garde son tableau ; son lien **Dans le temps ›** mène ici. Rien n'est écrit dans le projet.

## 5. Le journal des lenteurs

Juste en dessous, **Lenteurs** garde ce qui a duré anormalement longtemps.
- **Une lenteur**, c'est plus de 50 ms, ou plus de 5 fois le temps habituel et au moins 10 ms. Le temps habituel est la médiane des 100 dernières fois. Un cycle plus long que sa période est toujours une lenteur.
- Sont suivis : un clic et ce qu'il déclenche, un script, l'affichage d'une vue, l'ouverture d'une popup, un échange lent avec un équipement.
- Chaque ligne donne l'heure à la milliseconde, la durée, le temps habituel, où et qui. Elle s'ouvre sur **ce qui a pris le temps dedans** (« Ouvrir Pop_Reglage », puis son script d'ouverture). **Aller à** ouvre l'objet, le script ou la popup.
- Des filtres : Tout, Actions, Scripts, Vues et popups, Cycles, Équipements, chacun avec son nombre.
- Les seuils se règlent pour le projet. Un clic sur « 50 ms » passe au suivant (20, 50, 100, 200, 500 ms) ; un clic sur « 5 × » aussi (3, 5, 10, 20).
- **Vider…** demande d'abord une confirmation. **Exporter en CSV** donne une ligne par lenteur, avec ce qui a pris le temps dedans.
- Les 500 dernières sont gardées d'une marche à l'autre, jusqu'à Vider ou la fermeture du projet. Le Journal de la simulation les montre aussi (filtre Lenteurs).

## 6. La Simulation dépliée dans l'arbre

Le dossier **Simulation** de l'arbre se déplie :
- **Vue d'ensemble** › Performances (le cycle de la dernière minute et son p95), Lenteurs (combien, la dernière), À surveiller ;
- **IHM** › Affiché (la vue et les popups ouvertes, depuis quand), Scripts (leur temps moyen), Alarmes, Utilisateur ;
- **Équipements** › chacun (son état, ses échanges par seconde), avec ses Lues et ses Écrites ;
- **Journal** › une ligne par source.

Les valeurs se mettent à jour une fois par seconde. L'IHM arrêtée, sa dernière marche reste, en gris. Un clic mène au bon endroit : le bloc de la Vue d'ensemble, le script, la page Équipements, le Journal filtré. Ce qui est déplié le reste d'une marche à l'autre.

## Tester vous-même

1. Dans une vue, une courbe : mettez une plume sur une variable tableau sans indice (`Consignes`, un `ARRAY[0..9] OF REAL`), puis **Générer**. L'erreur dit « choisis un élément : Consignes[0] ». Écrivez `Consignes[0]` : elle disparaît.
2. Programmation générale › Natives › Types › Conteneurs › **LIST** : la fiche et son exemple joué. Puis **Lequel choisir ?**.
3. Dans un script, **Ctrl+I** : en bas, en violet, Mes fonctions. Puis la liste Rend › **Sans retour**.
4. Simulez (F5) et naviguez un peu, puis ouvrez Simulation › **Vue d'ensemble** : Performances sur 1 min puis 10 min, et Où passe le temps. Plus bas, Lenteurs : baissez le seuil à 20 ms pour en voir.
5. Dans l'arbre, dépliez Simulation › IHM › **Affiché** : la vue ouverte, et depuis quand.

## Les captures

Prises avec l'exe Windows livré, lancé sous Wine, sur le projet de démonstration Armoire_Gaz :
- `1217_05_plume_tableau_entier.png` : la plume `Consignes` (un `ARRAY[0..9] OF REAL`) d'une courbe de Vue_Supervision, puis Générer. L'erreur rouge en tête des Diagnostics (« choisis un élément : Consignes[0] »), la vue marquée en faute dans l'arbre, « Build échoué : 1 erreur » ;
- `1217_06_simulation_bloquee.png` : Démarrer l'IHM sur ce projet : « Démarrage bloqué : 1 erreur bloquante » ;
- `1217_01_onglet_performances.png` : l'onglet Performances de l'IHM en marche, et son lien « Dans le temps › » ;
- `1217_02_dans_le_temps_arbre.png` : la Vue d'ensemble sur 10 min, et l'arbre Simulation déplié (Performances, Lenteurs, À surveiller ; Affiché, Scripts, Alarmes, Utilisateur ; les équipements) ;
- `1217_03_une_minute.png` : la même sur 1 min ;
- `1217_04_lenteurs.png` : le journal des lenteurs (seuil baissé à 20 ms : aucune lenteur sous Wine).

## Installer

Fichiers livrés :
- `XPGAnalyser-Setup-1.12.17.exe` : l'installateur, qui pose les deux applications ;
- `XPGAnalyser-API-1.12.17-portable.zip` et `XPGAnalyser-IHM-1.12.17-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lancez `XPGAnalyser-Setup-1.12.17.exe`. Il met à jour la version installée, la 1.12.16 ou une plus ancienne : même identité d'installation, données reprises sans être déplacées. Il pose les deux applications et leurs deux raccourcis. Vos projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.12.17.exe` (30,6 Mo, 32 116 425 octets) :
`5FA2BB9AEE3CBAA2E414508CB002287D5E95AABBF9A7E0EAE2FA2B1E2EA01168`

**Les zips portables.** Chacun contient une application et ce qu'il lui faut, dans un dossier `XPGAnalyser-API-1.12.17` ou `XPGAnalyser-IHM-1.12.17`. Décompressez, puis lancez l'exe.
- `XPGAnalyser-API-1.12.17-portable.zip` (22,8 Mo) :
  `C98BF36931298CAB9059A28553B07026715A7E830850846DEF8786F4E4797162`
- `XPGAnalyser-IHM-1.12.17-portable.zip` (22,8 Mo) :
  `3984ED5F332D3B8D80C08C1DA76F55F0A4064C7EC301A86366FA0EA571009B71`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe, 76 sur 76 (une de plus qu'à la 1.12.16 : `simperf`). Elle comprend :
- 7 725 contrôles dans `hmieditor` (7 638 à la 1.12.16), 5 760 dans `hmi` (5 706), 2 447 dans `hminatives`, 1 025 dans `hmidecl`, 380 dans `hminatives1216`, 235 dans `hmibuild` et 38 dans `simperf` ;
- ceux d'avant, qui passent toujours.

Les nouveaux essais couvrent :
- **le tableau entier** :
  - la plume `TABLEAU` (l'erreur, `TABLEAU[0]` proposé), une liste (`L[0] ou L.Count`), une MAP (`M['clé'] ou M.Count`), une structure (ses membres proposés) ; Visible si `BOOLS` ;
  - la variable d'une jauge (une erreur, plus un avertissement), `TABLEAU * 2`, `visible = BOOLS`, les mesures d'un graphique en barres, la variable X d'un XY ;
  - Mettre à 1 `BOOLS`, Incrémenter `TABLEAU`, `TABLEAU := 5`, `X := TABLEAU`, `ABS(TABLEAU)`, l'alarme `TABLEAU > 5`, les variables archivées ;
  - un champ de saisie, un programmateur et un curseur reliés à un tableau, une liste ou une structure entiers ;
  - ce qui passe toujours : `TABLEAU[2]`, `CUVE.Niveau`, `L.Count`, le joker `TABLEAU[*]`, `{TABLEAU}` dans un texte, `COPIE := TABLEAU`, Mettre à 1 `BOOLS[2]`, un champ de saisie sur `TABLEAU[3]`, les éléments d'une liste déroulante depuis une liste entière ;
  - chaque faute est dite une fois, pas deux ; elle bloque partout (la plume, l'action, l'alarme, la jauge, le champ de saisie), et rien d'autre ne devient bloquant ;
  - le build (`hmibuild`) : la plume `TABLEAU` le bloque, `TABLEAU[0]` le laisse passer, une plume à la variable inexistante reste un avertissement ;
  - pas de faux positif : les opérateurs d'un type (`V0 + V0`, `V0 < V0`) et sa conversion (`TO_REAL(V0)`), `ASCII_TO_STRING(NOM)` dans un script ; dans un script, `X := TABLEAU * 2` est dit ;
  - le projet de démonstration n'a aucun constat du tableau entier ;
- **les natives** : les 8 thèmes et les 5 dossiers des types, dans l'ordre de la maquette ; aucun dossier vide ; toutes les fonctions rangées, `X_TO_Y` compris ; au-delà de 20, des sous-dossiers (`LIST_ADD` dans Conteneurs › Listes › Ajouter et retirer, `INT_TO_REAL` dans Conversions) ; l'ancienne catégorie « map » mène au dossier MAP ; De A à Z dans l'ordre, sa fiche, Insérer ; les 7 fiches des conteneurs ; les énumérations, chacune une fois, en 5 dossiers ; F1 sur ANY_NUM ;
- **Choisir une fonction…** : Mes fonctions (une fonction de vue sous Vues › Vue_Cuves › Fonctions, `Pop_Reglage.Monter(c)` sous Popups, une fonction du projet par son nom seul, le dossier d'où l'on vient marqué « ici ») ; la fiche et ses premières lignes ; les dossiers des natives ; la liste Rend (aucune native ne rend rien, Comme son argument, vos types) ; `RETURN LIST_AVG(Niveaux);` ; les 25 lectures de conteneurs évaluées dans une case fx ;
- **les performances et les lenteurs** :
  - une lenteur à plus de 50 ms, ou plus de 5 fois l'habituel et au moins 10 ms ; pas d'habituel avant 3 fois ; les 100 dernières seulement ;
  - trois secondes sautées donnent trois mesures vides, et une heure infinie ne fait rien ;
  - depuis le lancement : les cycles, les dépassements, le maximum et son heure ;
  - « dedans » : Ouvrir Pop_Reglage, puis son script d'ouverture en retrait ; la popup et son script ne sont pas notés à part ;
  - où passe le temps : le script, les alarmes, les courbes, le reste toujours en bas ;
  - la Vue d'ensemble (`simperf`, 38 contrôles) : les deux blocs, les cases, 10 min par défaut, les périodes, l'export, Vider et sa confirmation ;
- **la Simulation dépliée** : Vue d'ensemble et IHM sans ligne restent des feuilles ; Lenteurs avec son icône et sa pastille ; Affiché › la vue et la popup avec leur heure ; une valeur qui change met la ligne à jour sans refaire l'arbre ; ce qui est déplié le reste ; l'IHM arrêtée, la dernière marche en gris ; Journal › IHM, Lenteurs.

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine :
- `--version` répond `XPGAnalyser API 1.12.17` et `XPGAnalyser IHM 1.12.17` ;
- ils importent exactement les mêmes DLL que ceux de la 1.12.16 (ADVAPI32, IPHLPAPI, KERNEL32, SDL3, SHELL32, USER32, WS2_32, msvcrt, ole32) ;
- deux sessions scriptées (les captures ci-dessus) : le projet de démonstration se génère sans erreur et la simulation démarre ; avec la plume `Consignes`, le build échoue sur cette seule erreur et le démarrage est bloqué.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.16 :
- avant, la 1.12.16 livrée le 19/10 était installée (`XPGAnalyser IHM 1.12.16`) ;
- « Installation process succeeded ». Il a repris l'installation en place : même dossier, les deux raccourcis et le dossier Maintenance du menu Démarrer gardés, et son journal de désinstallation complété ;
- les deux exe installés sont identiques à ceux qui ont été testés. Installés, ils répondent `XPGAnalyser API 1.12.17` et `XPGAnalyser IHM 1.12.17`, et l'API installée analyse `MAST.XPG` comme avant (891 variables, 29 POU).

**Les zips portables** ont chacun 151 fichiers, dont l'exe testé et le manifeste de la 1.12.17.

**Pas encore fait :** ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici).

## Limites connues

- **Les performances dans le temps** vivent avec la marche : rien n'est écrit dans le projet. Les lenteurs sont gardées jusqu'à la fermeture du projet, pas d'un lancement de l'application à l'autre.
- **Le tableau entier** est jugé là où le type est connu : une variable, un membre, un paramètre de vue ou de popup, une constante. Un appel d'une de vos fonctions qui rend une liste (`=Mes_Mesures()` dans une plume) n'est pas encore dit.

## La suite

- Essayer sur un vrai Windows, avec la section « Tester vous-même ».
