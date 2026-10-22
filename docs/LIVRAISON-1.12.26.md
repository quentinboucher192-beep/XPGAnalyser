# API Studio et IHM Studio 1.12.26 : l'historique des objets, WAIT et les fichiers dans les scripts

Livrée le 22/10/2026 au soir. Elle suit la 1.12.25 du même jour (`docs/LIVRAISON-1.12.25.md`).

Elle répond à :

> « termine l'historique continue »

> « pourquoi en haut à droite y'a écrit que l'ihm est à l'arrêt c'est faux / les lenteurs sont dues aux trames : on peut pas optimiser et regrouper par paquet pour envoyer moins de trames aux equipements ? »

> « dans les natives ajoute des fonctions pour lire des fichiers dans les resources, fonctions pour lire ecrire un peu comme filesystem / dans les scripts ajoute la possibilité de faire des Wait dedans, en ST, C, C++ les scripts seraient soit en detach soit pas en detach mais ils seraient dans ce cas là bloquant, mettre une option pour detach ou pas dans les scripts dans les propriétés ou paramètres »

Livraison rapide : les essais et les vérifications sous Windows (Wine) sont faits, mais pas de nouvelles captures d'écran.

## 1. Ce qui change

### L'historique des objets : la fenêtre et le suivi

La 1.12.25 notait déjà, à chaque Enregistrer, ce qui avait changé dans chaque objet. La 1.12.26 le montre.

- **Clic droit sur un objet › Historique des modifications…** (sur le fond de la vue : Historique de la vue…). À gauche, les enregistrements qui ont touché l'objet : la date, le PC, le compte Windows, la version. À droite, propriété par propriété, Avant et Après. On peut rechercher (une propriété, une valeur, un PC) et filtrer par PC. **Revenir à Avant** ou **Revenir à cette version** demande confirmation, et Ctrl+Z annule. Un objet supprimé se **restaure**. **Copier** et **Exporter en CSV** sont là. Ce qui n'est pas encore enregistré vient en tête, « Non enregistré ».
- **Propriétés › Suivi** : quand l'objet a été créé et modifié, et sur quel PC ; ce qui n'est pas enregistré ; le nombre d'enregistrements (le bouton … ouvre l'Historique).
- **L'Explorateur d'objets** montre la date de la dernière modification (« il y a 5 min », « hier 14:05 »). **Un point orange** marque un objet modifié et pas encore enregistré, dans l'Explorateur et sur la vue. **Le survol** d'un objet le résume dans une infobulle. Clic droit sur le fond de la vue : on peut couper l'infobulle ou le point.

### WAIT et les scripts détachés

`WAIT(T#2s)` attend dans un script ST. En C, c'est `WAIT(2000)` ; en C++, `WAIT(2s)`.

Un script général a une nouvelle propriété : **Détaché**.

- **Coché** : le script tourne en arrière-plan. WAIT n'attend que lui ; l'IHM continue (les vues, la communication, les autres scripts). Une boucle sans fin se laisse écrire, `WHILE TRUE DO … WAIT(T#1s); END_WHILE`, et un long calcul rend la main toutes les 8 ms. Une seule instance tourne à la fois : relancé pendant qu'il tourne, il ne repart pas.
- **Décoché** (par défaut) : le script tourne d'un trait, comme avant. WAIT bloque l'IHM pendant l'attente, 10 s au plus par WAIT.
- `IHM_SCRIPT_ACTIF('Nom')` dit si un script détaché tourne. `IHM_SCRIPT_ARRETER('Nom')` l'arrête, à sa prochaine instruction, ou tout de suite s'il attend.
- La Console dit le départ, la fin et sa durée, ou l'arrêt. L'arrêt de l'IHM arrête les scripts détachés, sans erreur.
- Les scripts de vue (OnOpen, OnCycle, OnClose) ne se détachent pas. Un OnOpen lance une séquence avec `IHM_APPELER('Sequence')`.

### Les fichiers et les ressources dans les scripts

29 natives, rangées dans un nouveau dossier **Fichiers** de Natives :

- **lire** un fichier : `FILE_READ`, `FILE_READ_LINES` ; `FILE_EXISTS`, `FILE_SIZE`, `FILE_TIME` ;
- **écrire** : `FILE_WRITE` et `FILE_WRITE_LINES` écrivent le fichier d'un bloc ; `FILE_APPEND` et `FILE_APPEND_LINE` ajoutent à la fin (un journal, un CSV) ; `FILE_COPY`, `FILE_MOVE`, `FILE_DELETE` ;
- **les dossiers** : `DIR_CREATE`, `DIR_EXISTS`, `DIR_LIST` (avec un filtre, `'*.csv'`), `DIR_DELETE`, `DIR_DATA` ;
- **les chemins** : `PATH_JOIN`, `PATH_NAME`, `PATH_STEM`, `PATH_EXTENSION`, `PATH_PARENT` ;
- **les ressources du projet** (un .txt, un .csv importé dans Ressources ; elles voyagent avec le projet) : `RESOURCE_READ`, `RESOURCE_LINES`, `RESOURCE_EXISTS`, `RESOURCE_SIZE`, `RESOURCE_LIST`, `RESOURCE_EXTRACT`.

Un chemin relatif part du dossier `donnees/` du projet. Un chemin complet (`C:/Donnees/recette.csv`) va où il dit. Les fichiers du projet (`ihm/`, `project.xpgproj`) ne s'écrivent pas par un script. Un échec ne coupe pas le script : la fonction rend FALSE, '' ou -1, et `FILE_ERROR()` dit pourquoi ; la Console le dit aussi, une fois. Dans une expression (fx), les natives de fichiers lisent seulement : `{RESOURCE_READ('consignes.txt')}` dans un texte.

Chaque native a sa fiche dans Natives, avec un exemple vérifié par les essais, et l'équivalent C et C++ (`std::filesystem`, `<fstream>`).

### Moins de trames vers les équipements

- Deux variables séparées de moins de **100 mots** se lisent d'une seule requête ; c'était 8 mots avant. C'est le réglage « Regrouper à moins de » d'un équipement (Équipements). Un nouvel équipement part à 100 ; **un projet existant garde son réglage enregistré (8)** : mets 100 dans chaque équipement pour en profiter (le projet de l'affaire est déjà à 100).
- Si un équipement refuse une lecture groupée (une adresse qu'il n'a pas dans le trou, trop de mots d'un coup), l'IHM lit chaque variable une à une, apprend où sont les trous ou la taille qu'il accepte, et ne les relit plus. Le journal de la communication le dit.
- L'esclave simulé répond à la milliseconde. Windows arrondissait ses attentes à 15,6 ms : une vue de 15 requêtes mettait environ 235 ms à se lire.
- La lenteur d'un équipement se juge par requête, et sur la période quand la lecture entière la dépasse.

### La pastille de l'IHM, en haut à droite

Elle disait « IHM arrêtée » alors que l'IHM tournait : sans automate simulé, elle n'était plus rafraîchie. Elle suit maintenant l'IHM à chaque image.

## 2. L'affaire 2025_06_118 : le projet IHM

Le projet de l'affaire (`2025_06_118_IHM`) et sa démonstration (`2025_06_118_IHM_DEMO`) sont régénérés. Une seule chose change : l'équipement `M580_HSBY` regroupe les variables à moins de **100 mots** (8 avant). Par cycle de lecture, le nombre de requêtes baisse de moitié ou plus : vue Régulation 21 → 7, Synthèse 15 → 8, Réglages PID 12 → 6, Réseau AMF 14 → 6, Performances de la régulation 15 → 6 ; les autres vues 7 ou 8 → 4 ou 5. Si l'automate refuse une lecture groupée, la 1.12.26 l'apprend et lit autrement. Générer : 0 erreur, 0 avertissement ; Compiler : 0 erreur (le projet de livraison et la démonstration) ; aucun objet n'en chevauche un autre.

Le projet s'ouvre aussi avec la 1.12.25. La 1.12.26 est conseillée, pour le regroupement et l'attente précise de l'esclave simulé.

## 3. Les essais

80 suites sur 80. Ce qui change dans les essais :
- `hminatives` (2767 contrôles) : les 29 fonctions de fichiers, chaque exemple de leur fiche exécuté ; le dossier donnees/ ; les fichiers du projet refusés (ihm/, project.xpgproj, le dossier du projet) ; une expression qui lit sans écrire ; FILE_ERROR ; les ressources (en-tête UTF-8 ôté, lignes, dossier, copie) ; sans dossier de projet. WAIT et les scripts détachés : un script non détaché bloque, refusé au-delà de 10 s ; un script détaché rend la main et reprend à son heure ; une instance à la fois ; IHM_SCRIPT_ACTIF et IHM_SCRIPT_ARRETER ; une boucle de calcul rend la main ; l'arrêt de l'IHM l'arrête sans erreur ; chacun garde ses variables ; un Démarrage détaché ; Détaché enregistré et relu.
- `hmi` (5774 contrôles) : le regroupement à 100 mots et les trous appris (un esclave qui refuse un mot et les lectures de plus de 10 mots) ; l'attente précise du simulé ; le guide (chaque exemple en ST, C et C++).
- `hmieditor` (7725 contrôles) : le Suivi (Propriétés, l'Explorateur, l'infobulle, le point orange, les réglages du clic droit) ; les natives en 9 thèmes (Fichiers) ; les notes (396) et les versions (59).

**Le même essai des natives, compilé pour Windows** (MinGW) et lancé sous Wine : 2767 contrôles, 0 échec (les chemins Windows, les scripts détachés sur des fils Windows).

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.12.26.exe` : l'installateur, qui pose les deux applications ;
- `API-Studio-1.12.26-portable.zip` et `IHM-Studio-1.12.26-portable.zip` : chaque application, sans installation ;
- `2025_06_118_IHM_Studio.zip` et `2025_06_118_IHM_DEMO.zip` : le projet IHM de l'affaire et sa démonstration ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.12.26.exe`. Il met à jour la version installée (la 1.12.25 ou une plus ancienne) au même endroit, et garde tes données. Les projets s'ouvrent tels quels.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».
3. Le projet de l'affaire : décompresse `2025_06_118_IHM_Studio.zip` (ou la démonstration) **à la place** de l'ancien dossier. Si tu avais modifié le projet de ton côté, garde une copie avant.

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.12.26.exe` (34,8 Mo, 36 449 890 octets) :
`F611980BA2C4665511E3F8143BCC42589BFA286DE1BFC4A85C1BB7A70A31A87D`

Les zips portables (151 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.12.26-portable.zip` (25,8 Mo) :
  `03C3DC6E429BF6BDFCBC73E713D8ACDB9FF16E0C65B0F45F28FCF2606CCB3464`
- `IHM-Studio-1.12.26-portable.zip` (25,8 Mo) :
  `6C7AB6BD40C10A7643182C432F160C53CA8034FB2B661232ABF63506BF2C3257`

## 5. Les vérifications sous Windows

**Les deux exe Windows**, compilés sous Linux avec MinGW-w64 et lancés sous Wine : `--version` répond `API Studio 1.12.26` et `IHM Studio 1.12.26` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB). Une séance scriptée ouvre la démonstration de l'affaire, lance la simulation (la pastille dit « IHM en marche ») et affiche la vue Régulation, sans erreur.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux pour tous les comptes, par-dessus la 1.12.25 (installée avant : `IHM Studio 1.12.25`) : « Installation process succeeded » ; le dossier d'installation a `API-Studio.exe`, `IHM-Studio.exe` et le désinstalleur ; le menu Démarrer garde son dossier **API IHM Studio** ; les deux exe installés sont identiques à ceux qui ont été testés et répondent `1.12.26`.

**Pas fait :** pas de nouvelles captures d'écran (livraison rapide). Ni les exe ni l'installateur n'ont été lancés sur un vrai Windows, et le projet Visual Studio n'a pas été compilé (pas de MSVC ici).

## 6. La suite

- Les captures de la fenêtre Historique, du Suivi et d'un script détaché, à la prochaine livraison.
- Communiquer les adresses de MAESTRO et de CIMVIEW ; reporter les adresses dans l'AF § 6 à sa prochaine révision.
- En attente : les fonctions IHM génériques (TEMPLATE) partout ; les raccourcis clavier avancés des éditeurs.
