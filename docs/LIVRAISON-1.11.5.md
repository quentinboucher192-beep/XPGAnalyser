# XPGAnalyser 1.11.5 — deuxième des quatre livraisons du 05/10

Livrée le 05/10/2026. Les demandes du 05/10 arrivent en quatre livraisons, point par point :

| Version | Contenu |
|---|---|
| 1.11.4 (livrée) | la position, la taille, la rotation et les miroirs calculés en simulation ; les repères dans les paramètres ; Équipements sans « Variables liées » ; les barres de défilement qu'on tire |
| **1.11.5** (celle-ci) | les esclaves simulés en arbre, les onglets Variables IHM et Variables API, la recherche, le forçage, les bornes au clavier |
| 1.11.6 | les actions : le script dans une petite fenêtre, le clavier virtuel, Maths, les actions rangées en arbre |
| 1.11.7 | les popups et les fonctions propres à un symbole |

Fichiers livrés : `XPGAnalyser-Setup-1.11.5.exe` (l'installateur), ce document et 7 captures.

## 1. Les esclaves simulés en arbre, à toute profondeur

**Votre demande.** « Dans Simulation IHM › Esclaves simulés, il faut faire du treeview profondeur infinie sur les structures et variables. »

**Maintenant.** Une variable structurée se range sous ses nœuds, à toute profondeur. Par exemple, `Four1.Vannes[1].Ouverte` se range sous Four1 › Vannes › [1] › Ouverte.

| Nœud | Ce qu'il montre |
|---|---|
| l'esclave (Balance B) | comme avant : son état, « esclave lié » ou « seulement simulé » |
| une structure (Four1) | combien de valeurs elle contient et son type : « 14 valeurs · T_Four » |
| un tableau (Vannes) | « 9 valeurs · ARRAY[1..3] OF T_Vanne » |
| une case ([1]) | « 3 valeurs · T_Vanne » |
| une valeur (Ouverte) | la ligne de toujours : Animer, le mouvement, la barre, Forcer, la valeur |

- Un clic sur un nœud le replie ou le rouvre.
- Une valeur qu'on montre (une recherche, un clic dans une autre liste) rouvre ses nœuds.
- **Le même arbre** est dans IHM › Équipements › Valeurs simulées.

Captures : `1115_01_valeurs_simulees_arbre.png` (IHM › Équipements) et `1115_04_simulation_esclaves_arbre.png` (la simulation).

## 2. Les onglets Variables IHM et Variables API

**Votre demande.** « Ajouter un onglet avec les variables IHM, variables API avec treeview dans les 2 onglets […] garder le principe de forçage qui est bien. »

Dans Simulation IHM, deux onglets en arbre :

| Onglet | Ce qu'il contient |
|---|---|
| **Variables IHM** | toutes les variables de l'IHM ; une structure ou un tableau se déplie jusqu'à ses valeurs (Four2 › Vannes › [1] › Ouverte) |
| **Variables API** | toutes les variables de l'automate simulé, par exemple Armoires › [0] › ana › PT1 › mes |

L'onglet dit combien de valeurs il contient, et chaque nœud combien il en a dessous.

**Le forçage, comme dans les esclaves simulés :**
- la case **Forcer** tient la variable à sa valeur du moment ; la décocher la rend libre ;
- un **double-clic sur la valeur** ouvre un champ : Entrée force la variable à ce qu'on a tapé (`TRUE`, `12`, `1,5`, `'texte'`, `T#5s`), Échap annule ;
- une variable forcée se voit : sa valeur en orange ;
- le compte du haut dit combien sont forcées ;
- la barre d'état dit ce qui a été fait : « Four2.Temperature forcée à 80 (décocher Forcer la rend libre) ».

**Nouveau : une variable IHM se force.** Tant qu'elle est forcée, elle garde sa valeur :
- les scripts et les actions de l'IHM en marche ne l'écrivent plus ;
- liée à un équipement, elle n'est plus remplacée par ce que l'équipement renvoie, et une écriture ne part plus vers lui.

Elle se force aussi depuis les tables d'animation.

Côté API, c'est le forçage de l'automate simulé, le même que la page Forçages.

Captures : `1115_05_variables_ihm_arbre_forcage.png` (Four2.Temperature forcée à 80, Four2.Vannes[1].Ouverte forcée), `1115_06_variables_ihm_champ_valeur.png` (le champ du double-clic), `1115_07_variables_api_arbre.png`.

## 3. La recherche dans les trois onglets

En haut de Esclaves simulés, Variables IHM et Variables API (et de IHM › Équipements › Valeurs simulées) :

- elle cherche dans le nom complet de la variable (`Four1.Vannes[1].Ouverte`), et pour les esclaves aussi dans l'adresse (`43006`) ;
- elle se tape comme les autres listes de l'application : des mots (tous doivent y être), une `"phrase exacte"`, un `-mot` à exclure ;
- elle garde les valeurs trouvées et leurs nœuds, ouverts ;
- dans Variables IHM et Variables API, elle dit combien de valeurs sont montrées : « 86 sur 228 variables ».

## 4. Les bornes au clavier

**Votre demande.** « Pouvoir éditer les bornes plus facilement (à éditer aussi dans IHM › Équipements › Valeurs simulées). »

Avant, une borne ne se changeait qu'en tirant une poignée. Maintenant :

- le **crayon ✎** au bout de la barre, ou un **double-clic** sur la zone, ouvre un champ ;
- on tape les valeurs, Entrée les applique, Échap annule ;
- Ctrl+Z revient à la zone d'avant.

| Le mouvement | Ce qu'on tape | Exemple |
|---|---|---|
| une zone (sinus, aléatoire, rampe…) | min ; max | `20 ; 80` ou `1,5 .. 7,25` |
| une constante | la valeur | `50` |
| clignote | le temps à 1, en secondes | `1,5` |

Le champ accepte la virgule ou le point pour les décimales. Une saisie illisible ne change rien, et la barre d'état dit pourquoi.

Dans la simulation comme dans IHM › Équipements › Valeurs simulées. Captures : `1115_02_bornes_au_clavier.png` (le champ, `20 ; 80` tapé sur Courant_L1), `1115_03_bornes_appliquees.png`.

## Deux défauts trouvés et corrigés en préparant cette livraison

- **Une variable IHM liée à un équipement ne restait pas forcée** : à la lecture suivante, la valeur de l'équipement remplaçait la valeur forcée. Corrigé, avec un test qui échouait avant la correction.
- **La case Forcer d'une longue liste ne répondait pas au clic** quand la barre de défilement était visible : le clic était attendu 8 px à côté de la case dessinée. Corrigé, avec un test qui échouait avant la correction.

## Installer

1. Lancer `XPGAnalyser-Setup-1.11.5.exe`. Il met à jour la 1.11.4 installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisir « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.5.exe` (15,2 Mo, 15 223 743 octets) :
`1B007993FC95EE44A804F70DCA5DD6A39866CE73937AC96577FB4D362761FE2F`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (55 sur 55). Les nouveaux contrôles :

| Test | Ce qu'il vérifie |
|---|---|
| `esclavesArbre1115` (hmi_editor_test) | Four1 › Vannes › [1] › Position : les profondeurs, les comptes, les types des nœuds ; replier, rouvrir en montrant une valeur ; la recherche ; `20 ; 80`, `1,5 .. 7,25`, `90 10` (remis dans l'ordre), la constante `-3,5` lus, une borne seule ou du texte refusés ; le crayon ouvre le champ, Entrée applique, Échap annule, Ctrl+Z revient |
| `simulationVariables1115` (hmi_editor_test) | les onglets Variables IHM et Variables API en arbre ; la recherche ; forcer par le champ et par la case ; une variable IHM forcée ignore les écritures ; une case de structure se force, un texte pour un INT est refusé ; une variable de l'automate simulé forcée à 123,5 puis libérée ; dans une longue liste (barre visible), un clic au milieu de la case Forcer force la variable |
| les variables liées (hmi_test, lot 15) | une variable liée à une centrale Modbus, forcée : la centrale ne l'écrase plus, une écriture ne part pas vers elle ; libérée, elle relit la centrale |

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.5` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session `session-1115-esclaves-arbre.txt` a été rejouée sur une copie d'Armoire_Gaz : ce sont les 7 captures.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé en silencieux, pour tous les comptes, par-dessus la 1.11.4 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.5`.
- Seuls les raccourcis du menu Démarrer dont le nom a un accent n'ont pas pu être créés. C'est la limite de Wine déjà vue aux livraisons précédentes, sans rapport avec Windows.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Merci de vérifier en ouvrant un projet. En cas de souci, renvoyez-moi le résultat de `collect_diagnostics.bat`.

Les sources sont dans le dépôt GitHub, branche `claude/hopeful-goodall-h0srq0`, avec la session de captures `tools/sessions/session-1115-esclaves-arbre.txt` et son projet (`tools/sessions/preparer-projet-1115.cpp`).
