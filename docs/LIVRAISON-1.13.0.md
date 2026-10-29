# API Studio et IHM Studio 1.13.0 : les objets, le débogueur des scripts, les unités, les tests unitaires, les bibliothèques

Livrée le 29/10/2026. Les exe et l'installateur datent du 26/10, la date que donnent les notes de version. Elle suit la 1.12.31 (`docs/LIVRAISON-1.12.31.md`).

Elle répond à :

> « y'a pas des concepts de programmation pas assez faits ou meme absent ?
> fait tout sauf les grafcets et ajoute ceux qui sont pas finis, envoie des captures et une maquette interactive code tout sans pause merci
> si c'est impossible de faire les requetes supprime complemente l'import export html merci
> ok code tout et estime la duree de codage avant livraison »

Les huit concepts absents et les deux pas finis de la liste du 24/10 sont dans la 1.13.0, sauf les machines à états côté IHM (les grafcets, laissés de côté à ta demande).

**Ce qui n'est pas fait :**
- **Les requêtes HTTP, MQTT ou SQL depuis un script.** Les scripts lisent et écrivent du JSON et fouillent les textes au motif (regex), en local, à partir d'un fichier ou d'une ressource.
- **Les requêtes Modbus depuis une page web.** Un navigateur ne peut pas parler Modbus TCP à un appareil. L'export et l'import en page web sont donc **retirés**, comme tu l'as demandé.

## 1. Ce qui change

### Les types objets : méthodes, privé, héritage, interfaces

Un type IHM porte maintenant ses **méthodes**. Dans Types IHM, la section **Méthodes** est sous les membres du type. **Ajouter une méthode…** demande :
- le nom ;
- le retour ;
- les paramètres ;
- Privée ;
- Redéfinissable (virtuelle).

Le corps s'écrit dans un éditeur, avec sa légende SELF et sa marge de points d'arrêt.

- **L'appel** : `Pompe1.Demarrer()` s'écrit dans un script, une fonction ou une fx, et aussi sur une liste : `Pompes[i].EnDefaut()`. Une méthode voit l'instance par `SELF` et ses membres par leur nom.
- **Privé** : la colonne **Visibilité** (cadenas, ou clic droit › Rendre privé) cache un membre ou une méthode aux scripts du dehors. Générer refuse un privé lu du dehors.
- **Héritage** : **Hérite de…** (EXTENDS) donne un parent au type ; ses membres et ses méthodes viennent avec. **Redéfinir** réécrit une méthode virtuelle héritée, et `SUPER` appelle celle du parent.
- **Interfaces** : **Nouveau type ▾ › Interface…** crée un contrat (des signatures de méthodes). Plusieurs types le respectent (la catégorie **Implémente**). Une `LIST OF REF_TO I_Organe` garde ensemble des pompes et des vannes, et chacune est appelée selon son type réel. `DYNAMIC_CAST` redescend vers le type réel. La page de l'interface liste les types qui l'implémentent, conformes ou non.
- **Générer refuse** : un privé lu du dehors, une interface non satisfaite, la redéfinition d'une méthode non virtuelle et un cycle d'héritage.
- **L'arbre** montre les méthodes de chaque type, en « Méthodes (n) », et dit « hérite de » à côté du type.

### Le débogueur des scripts IHM

- Un **point d'arrêt** se pose dans la marge d'un script, d'une fonction, d'une méthode ou d'un opérateur, d'un clic à gauche des numéros de ligne. Le clic droit de la marge propose une **condition** (`P < 5.0`), et un nom inconnu dans la condition est signalé tout de suite. `IHM_POINT_ARRET([condition])` est un point d'arrêt écrit dans le code.
- **En simulation, le script s'arrête vraiment** sur la ligne, au milieu de son exécution, et l'IHM se fige en attendant. La barre du haut dit « IHM en pause (débogage) ».
- **Simulation › IHM › Débogage** montre :
  - où et pourquoi le script s'est arrêté ;
  - la **pile** des appels ;
  - les **variables locales**, qui se déplient ;
  - les valeurs de la ligne ;
  - les **espions**.

  On avance avec **Continuer** (F5), **Suivant** (F10), **Entrer** (F11) dans la fonction appelée et **Sortir** (Maj+F11). Suivant enjambe un WAIT, et l'IHM respire pendant l'attente.
- Le poste d'exploitation ne s'arrête jamais. Les points d'arrêt ne sont pas enregistrés dans le projet.

### Lever ses propres erreurs, attendre une condition

- `LEVER('E_Niveau', 'niveau hors plage : {n:0.0} %')` lève ton erreur, avec un code et un message. `CATCH e` la reçoit (`e.Code`, `e.Message`, `e.Ligne`), et `RELEVER` la renvoie au TRY englobant. Elle traverse les appels de fonctions et de méthodes.
- Les erreurs du moteur ont aussi leur code : E_DIVISION, E_BORNE, E_CONVERSION, E_CLE, E_ASSERT, E_ATTENTE, E_MOTEUR. Hors d'un TRY, le script s'arrête et la Console dit le code et le message.
- `WAIT_UNTIL(Niveau_Cuve >= Consigne_Niveau, T#2m)` attend qu'une condition devienne vraie, dans un script détaché.
  - En instruction, un délai dépassé lève E_ATTENTE.
  - Lue comme un BOOL (`IF WAIT_UNTIL(Pression_Etablie, T#10s) THEN`), elle rend FALSE au bout du délai, sans erreur.

### JSON et expressions régulières dans les scripts

- **JSON** :
  - `JSON_LIRE` lit un texte JSON (un fichier par FILE_READ, une ressource par RESOURCE_READ) en une valeur ANY, qu'on interroge par clé et par indice (`v['gaz']`, `v[0]`, `.Count`) ;
  - `JSON_NOMBRE`, `JSON_TEXTE`, `JSON_BOOL` et `JSON_ENTIER` lisent une case avec une valeur par défaut ;
  - `JSON_ECRIRE` écrit une MAP, une liste, une structure ou une valeur ANY.
- **Expressions régulières** : `REGEX_TESTE`, `REGEX_TROUVE`, `REGEX_REMPLACE`, `REGEX_DECOUPE` et `REGEX_VALIDE` fouillent un texte selon un motif (une trame, un nom à nettoyer).
- Un JSON ou un motif illisible lève une erreur, que TRY…CATCH rattrape.

### Les tests unitaires des fonctions

L'onglet **Tests** d'une fonction IHM garde ses cas de test. Cela vaut pour une fonction de projet, de vue, de popup ou de symbole. Un cas de test contient :
- les arguments ;
- le résultat attendu ;
- une tolérance pour un REAL ;
- des valeurs posées avant l'appel et vérifiées après ;
- ou l'erreur attendue.

**Nouveau cas…** pré-remplit le cas d'après la signature. **Lancer ce cas** et **Lancer tout** les jouent. Chaque cas a son verdict (réussi, échec, erreur) et le détail d'un échec.

Les cas se rejouent tout seuls à chaque Compiler et à chaque Démarrer :
- les verdicts vont aux Sorties, dans la catégorie Tests (un double-clic mène au cas) ;
- l'arbre compte les tests rouges ;
- **Configuration › Tests des fonctions** choisit si un échec empêche de démarrer.

Le rapport des tests s'écrit en Excel, en PDF ou en CSV.

### Les fonctions génériques partout

Une fonction IHM porte ses propres types génériques : la propriété **Génériques**, par exemple `T`, `T : ANY_NUM`, `K`, `V`. Elle se règle dans son inspecteur comme dans Nouvelle fonction. Un paramètre, le retour ou une locale peut alors être de type T.

À l'appel, T se déduit des arguments (`Max(P_A, P_B)`) ou se donne entre chevrons (`Max<INT>(Rang, 0)`). Sa contrainte refuse un type hors de sa famille.

### Les unités de mesure dans les types

Une variable, un membre de type, un paramètre, une locale ou une constante peut avoir une unité : bar, mbar, degC, m3/h, %… Elle se règle dans la colonne **Unité** des grilles. Tu tapes le symbole (Ctrl+Espace le propose), ou tu le choisis d'un double-clic dans le **sélecteur d'unités**.

- **La compilation** refuse d'additionner ou de comparer des bar et des mbar, et propose la conversion d'un clic `EN(x, 'bar')`.
  - L'affectation d'une variable lue telle quelle convertit d'office ce qui est compatible : 500 mbar donnent 0,5 bar.
  - Un calcul, un RETURN ou un argument demandent `EN`.
  - La case **Unités strictes** exige `EN` partout.
- **Les natives** : `EN(x, 'unité')` convertit, `UNITE(x)` donne le symbole, `SANS_UNITE(x)` l'enlève.
- **En marche**, l'unité de la variable est l'unité affichée par défaut : afficheur, jauge, tableau, saisie d'une consigne, Réglage PID, rapports, `{X:u}`, alarmes.
- **Configuration › Unités de mesure** ajoute les tiennes.
- Sans unité, rien ne change.

### Les espaces de noms et les bibliothèques

- Range tes types, tes fonctions et tes scripts généraux dans un **espace de noms** (la propriété Espace de noms : `Gaz`, `Gaz.Vannes`). Appelle-les par leur nom complet (`Gaz.T_Vanne`, `Util.Moyenne(a, b)`), ou par leur nom court après `USING Gaz;`. Deux noms pareils dans deux espaces ne se gênent plus.
- Le volet **Espaces et bibliothèques** :
  - **Publier** un espace en bibliothèque versionnée (`.xpgbib`) ;
  - la **Relier** à un autre projet : son code reste dans le fichier, en lecture seule, et ses unités propres l'accompagnent ;
  - **Mettre à jour** montre ce qui change avant d'appliquer, et dit quand une version plus récente est disponible.
- Un projet sans espace s'enregistre comme avant.

### La page web, retirée

**Projet › Exporter en page web** et **Importer depuis une page web** sont retirés, dans IHM Studio comme dans API Studio. Un navigateur ne peut pas dialoguer en Modbus avec les appareils. L'installateur retire aussi le moteur de la page (le dossier `web\` posé par la 1.12.31).

Ce qui reste :
- les archives `.xihm` et `.xapi` ;
- l'accès web en direct.

L'aide de la page web dit « Retiré en 1.13.0 ».

### L'aide

Six nouveaux sujets F1 :
- Les types objets ;
- Déboguer les scripts IHM ;
- JSON et expressions régulières ;
- Les tests unitaires d'une fonction ;
- Les unités de mesure ;
- Espaces de noms et bibliothèques.

Chacun a ses exemples en ST, C et C++. Dans les sujets qui existaient déjà (Scripts généraux et scripts de vue, Fonctions IHM, Les erreurs des scripts…), les 16 passages nouveaux sont marqués « nouveau en 1.13.0 ». Les notes de version 1.13.0 (10 notes) résument tout.

## 2. Les essais

**92 suites sur 92**. La 1.12.31 en avait 86 : les huit nouvelles s'ajoutent, et les deux de la page web retirée (`web_moteur`, `web_page`) disparaissent. Les huit nouvelles suites :
- `objets` (141 contrôles) : méthodes, privé, héritage, interfaces ;
- `hmidebug` (363) : le débogueur ;
- `erreurs` (115) : LEVER, RELEVER, CATCH e, WAIT_UNTIL, et une erreur qui traverse les appels ;
- `donnees` (76) : JSON et regex ;
- `testsunitaires` (115) : les tests des fonctions ;
- `generiques` (59) : les fonctions génériques ;
- `unites` (175) : les unités ;
- `bibliotheques` (175) : les espaces de noms et les bibliothèques.

`hmieditor` (8434 contrôles) compte les nouveautés : 438 notes, 65 versions, 254 sujets d'aide (L'IHM 230). Le guide de l'aide et son C++ disent la même chose. Le projet Visual Studio (`XpgAnalyzer.vcxproj`) a tous les nouveaux fichiers.

**Le chemin jusqu'ici** :
1. Huit lots faits à part, puis fusionnés.
2. Une relecture croisée de tout le code, qui a trouvé 52 défauts, corrigés en trois lots.
3. Une seconde passe sur le débogueur, les objets et les unités.
4. Une première série de captures avec les exe Windows. Ses relecteurs ont trouvé 4 défauts graves et une cinquantaine de petits ; tous sont corrigés, sauf ceux du § 6 :
   - RELEVER refusé par Générer ;
   - un LEVER dans une fonction qui arrivait en E_MOTEUR au CATCH de l'appelant ;
   - Relier qui refusait `libs/…` ;
   - une fonction générique montrée sans paramètres.
5. Une relecture contradictoire de ces corrections. Elle a trouvé 3 nouveaux défauts graves : une erreur de fonction qui disparaissait sans trace sous un TRY, quand la fonction était appelée d'un texte à trous ou par une native à rappel. Ils sont corrigés à leur tour et contre-vérifiés.

Les essais ont tourné sur le tout, après chaque étape.

## 3. Les captures

Dans `captures-1.13.0.zip` (109 images), prises sous Wine avec les exe livrés, à 1920 × 1200, sur une copie du projet de démonstration. Un dossier par sujet :
- `1-objets-espaces/` (39) :
  - un type avec ses méthodes, un membre privé et son cadenas ;
  - Nouvelle méthode, le corps et sa marge de points d'arrêt ;
  - Hérite de…, la redéfinition, l'interface héritée cochée et grisée ;
  - la page d'une interface et Nouveau type › Interface… ;
  - l'arbre des types ;
  - un script avec USING et Plus_Grand<INT> ;
  - la faute « privé » de Générer ;
  - la fonction générique Plus_Grand<T> et Nouvelle fonction avec Génériques ;
  - les espaces de noms ;
  - le volet Espaces et bibliothèques : Publier, Relier avec `libs/…`, Mettre à jour et ce qui change ;
  - l'aide à la saisie « T · déduit de a » ;
  - une fonction Max qui cache MAX, et Nouvelle fonction qui refuse ce nom.
- `2-debogueur-scripts/` (37) :
  - JSON et regex ;
  - LEVER, CATCH e et RELEVER, rattrapés ou non ;
  - un LEVER dans une fonction, rattrapé par le CATCH de l'appelant ;
  - WAIT_UNTIL, détaché ou non ;
  - les points d'arrêt dans la marge, la condition, la pause réelle ;
  - le volet Débogage : pile, locales, espions, infobulles ;
  - Entrer et Sortir, une méthode en pause ;
  - la Console des arrêts et des pas ;
  - l'arrêt sur modification ;
  - RELEVER hors d'un CATCH refusé.
- `3-unites-tests/` (33) :
  - la colonne Unité, le sélecteur d'unités, Configuration › Unités de mesure ;
  - la faute « bar + mbar » et sa correction d'un clic, Unités strictes ;
  - l'unité affichée en marche ;
  - l'onglet Tests, Nouveau cas…, les verdicts, les Sorties, l'arbre « 1 test rouge » ;
  - Configuration › Tests des fonctions, un démarrage refusé par un échec ;
  - le rapport des tests ;
  - le menu Projet sans page web, l'aide F1 des nouveaux sujets, les notes de version.

Chaque défaut vu par la première série de captures a été revérifié à l'écran avec ces exe. Les 52 traités sont corrigés, sept seulement en partie (§ 6).

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.13.0.exe` : l'installateur des deux applications ;
- `API-Studio-1.13.0-portable.zip` et `IHM-Studio-1.13.0-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.13.0.zip`) ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.13.0.exe`. Il met à jour la version installée (la 1.12.31 ou une plus ancienne) au même endroit et garde tes données. Il retire le dossier `web\` de la 1.12.31, qui ne sert plus.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Les projets s'ouvrent tels quels. Un projet enregistré par la 1.13.0 passe au format 33 (méthodes, unités, cas de test, espaces) : une version plus ancienne ne l'ouvre plus.

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.13.0.exe` (37,9 Mo, 39 700 993 octets ; 2,2 Mo de moins que la 1.12.31, sans le moteur de la page web) :
`93E3CCEFFB3DE0F3915A069D561BEF80123897FD4F87FAC3D8A2CAECBDC126F4`

Les zips portables (167 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.13.0-portable.zip` (29,4 Mo) :
  `A3C53AD87485F089D7C20F34E675C99C82AB0AEC7651716D18D21B87F60EEFDF`
- `IHM-Studio-1.13.0-portable.zip` (29,4 Mo) :
  `2D7AF18C5399FA68AFD9B3ADCAC134DCC7CE59F19B9EEEEAD8743232FAEFA237`

## 5. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine : `--version` répond `API Studio 1.13.0` et `IHM Studio 1.13.0`. `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant : 891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, lancé en silencieux pour tous les comptes, par-dessus la 1.12.31 officielle (installée avant : `IHM Studio 1.12.31`, avec son dossier `web\`) : « Installation process succeeded ».
- Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été testés.
- Le dossier `web\` de la 1.12.31 a disparu.
- Le manifeste compte 167 fichiers.
- Les associations `.xihm` et `.xapi` sont toujours là.

**Les captures** (§ 3) ont été prises sous Wine avec les exe livrés.

**Pas fait (Wine seulement)** : un vrai écran Windows, et Windows PowerShell 5.1 lui-même (les étapes de l'installateur ont tourné avec PowerShell 7.4).

## 6. Les limites, et la suite

**Les limites connues :**
- **Les objets** :
  - une LAMBDA lancée plus tard (par une minuterie, IHM_SUR_CHANGEMENT) ne voit pas SELF : passe-lui l'objet en paramètre ;
  - Générer ne voit pas un accès privé dans certains chemins très imbriqués ; le moteur le refuse alors en marche ;
  - depuis la pile du débogueur, aller au code d'une méthode d'une bibliothèque reliée n'ouvre rien.
- **Le débogueur** :
  - les scripts écrits en C ou en C++ ne se déboguent pas pas à pas ;
  - une LAMBDA lancée par une minuterie ne s'arrête pas sur un point ;
  - il n'y a pas de commande « Exécuter jusqu'à la ligne ».
- **Les erreurs** : une erreur levée dans une fonction appelée depuis un texte à trous (`'{F(x)}'`) ou une expression (fx, alarme) n'arrive pas au CATCH du script. Le trou affiche `###`, la Console dit l'erreur, et le script est compté en échec. Appelée directement, `F(x)`, ou passée à une native à rappel, elle arrive bien au CATCH.
- **Les expressions régulières** : la version livrée (MinGW) prend l'alternative la plus longue et refuse les références arrière (`\1`).
- **Les unités** : l'affichage ne convertit pas. Une autre unité d'affichage que celle de la variable est une erreur de Générer.
- **Les tests unitaires** : un cas de test d'une fonction générique ne peut pas fixer T (pas de `T :=`) ; T se déduit des arguments.
- **Une fonction du projet nommée comme une fonction standard** (Max, Min…) passe avant la standard. Nouvelle fonction refuse ce nom et Générer le signale. Compiler contrôle pourtant encore `MAX(…)` comme la standard (le nombre d'arguments).
- **L'écran** :
  - le compteur « Build : N avertissement(s) » ne compte que les éléments refaits par le dernier build ;
  - les boutons radio sont dessinés en carrés, partout dans l'application ;
  - quand l'arbre est élargi et le panneau du bas ouvert, la page Types IHM manque de hauteur ;
  - la Console garde les niveaux d'IHM_LOG en anglais (TRACE, INFO, WARNING…).

**Les restes vus aux captures finales** (mineurs, pour une 1.13.1) :
- **Les fonctions génériques** :
  - l'onglet Paramètres d'une fonction générique marque à tort ses paramètres de type T en faute (badge rouge) ; la fonction se compile et s'appelle normalement ;
  - « Appelée par » ne compte pas un appel écrit avec le type donné (`Plus_Grand<INT>(…)`).
- **La correction d'un clic** : après « Appliquer la correction » depuis Diagnostics, l'éditeur ne prend pas le clavier. Clique dans le code avant de taper.
- **Les tests unitaires** :
  - le bilan d'un échec reste orange (⚠) dans le bandeau de l'onglet Tests et la barre d'état, alors que les Sorties le disent en Erreur ;
  - au démarrage, deux bilans s'écrivent pour une seule fonction testée.
- **La note « Retiré : » de la page web** propose encore « Tutoriel », qui ne lance rien.
- **Le débogueur** :
  - après « Tout retirer », la barre du volet des scripts garde le message de condition du point retiré ;
  - une erreur non rattrapée d'une fonction nomme la fonction et sa ligne, pas le script qui l'a appelée ;
  - en trois colonnes, des valeurs restent coupées sur place : le survol les donne en entier.
- **Une interface** garde une grille Membres vide.
- **Les textes coupés** : à la largeur par défaut, des libellés de la grille Propriétés et quelques titres de colonnes se coupent (« Hérite… », « Re… »).

**Pas fait :**
- les requêtes HTTP, MQTT ou SQL depuis un script ;
- les machines à états (grafcets) côté IHM, laissées de côté à ta demande.

**En attente** : les raccourcis clavier avancés des éditeurs.
