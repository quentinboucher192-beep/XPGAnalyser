# API Studio et IHM Studio 1.22.2 : les paramètres des popups compris par l'IA, moins de saccades, l'onglet Performances, les missions plus maniables

Suite de la 1.22.1, faite à partir de ta capture de la mission Semeru (P1 et P2 : « La variable « Vanne.Cmd » n'existe pas dans le projet ») de ta remarque sur les saccades, et de tes deux demandes sur les missions (déplier / réduire les propositions, forcer la fin). Elle se met à jour par-dessus la 1.22.1 et garde tes données.

## 1. Ce qui change pour toi

### L'IA comprend les paramètres d'une popup ou d'un symbole

Dans Popup_Vanne, **Vanne** est un paramètre de type **T_Vanne** (une structure : Cmd, Autorisee, F0, F1, NbOuv, NbFerm, RazCpt). Ses objets lisent `Vanne.Cmd`, `Vanne.F0`… L'Assistant les prenait pour des variables du projet, inventées. Le moteur de l'IHM les lisait pourtant déjà correctement (`params::expressionType`). L'erreur venait de deux endroits.

- **La vérification de l'Assistant** ne connaissait les paramètres de la vue que dans les formules, et par leur nom seul. Elle accepte maintenant un paramètre et ses membres partout où un objet lit ou écrit une valeur : sa variable, le code brut, une action qui écrit. Les membres sont vérifiés d'après le type du paramètre. Un membre inconnu est refusé avec les vrais membres, par exemple : « « Vanne.Inconnu » : Vanne est un paramètre de Popup_Vanne (T_Vanne), et T_Vanne n'a pas ce membre (ses membres : Cmd, Autorisee, … ) ». La liaison est affichée « (paramètre de Popup_Vanne) ».
- **L'IA ne recevait pas les paramètres.** Elle les reçoit maintenant :
  - les paramètres de la popup ou du symbole ouvert, avec les membres de leurs structures ;
  - chaque autre popup et symbole avec ses paramètres, par exemple `Popup_Vanne(Vanne : T_Vanne)` ;
  - une **règle 19** dans ses consignes : dans ses objets, un paramètre se lit par son nom et ses membres, ce n'est pas une variable à chercher ni à créer, et c'est l'appelant qui le donne à l'ouverture (Vanne := Vannes[3]).

### Moins de saccades avec une mission en cours

Après chaque modification du projet, chaque mission vérifiait que ce qu'elle avait créé existait encore. Pour cela, elle reconstruisait tout le **graphe des relations du projet**, en relisant toutes les formules et tous les scripts, sur le fil de l'interface. Sur un gros projet, cela faisait un à-coup à chaque geste. La mission cherche maintenant directement par le nom. Le graphe ne sert plus que si un élément semble manquer, si bien qu'un élément n'est jamais déclaré absent à tort.

### L'onglet Performances (panneau du bas)

Il est à côté de Sorties, Console et Diagnostics. Il mesure IHM Studio lui-même, image par image, sur le fil de l'interface.

- **Ce qui consomme.** 82 parties sont mesurées : l'Assistant IA (sa mise à jour, ses cartes, leur empreinte et leur vérification, le plan en cours, la discussion), les missions (bande, tableau de bord, onglet), la peinture, les événements, la simulation, les tableaux, l'enregistrement automatique, etc. Elles sont triées par temps propre, sur 10 s, 60 s ou depuis le lancement, avec pour chacune sa part, sa moyenne par image, sa pire image, ses appels et son cumul.
- **La cadence.** Les images par seconde, le temps moyen et la pire image, avec une courbe des 60 dernières secondes. S'y ajoutent la mémoire (ensemble de travail et privée), le processeur et les travaux de fond en cours (appel à l'IA, vérification ou application d'une proposition, audit de mission, lecture de classeur, build, import).
- **Le journal des saccades.** Chaque image plus longue que le seuil (50 ms par défaut ; 33, 100 ou 250 ms au choix) est notée avec l'heure, sa durée et les trois parties qui l'ont prise. Une saccade de 250 ms ou plus est aussi écrite dans l'historique interne : un futur rapport de blocage la montrera.
- **Copier le rapport.** Le bouton donne un texte prêt à m'envoyer : la configuration, les 20 parties qui consomment le plus et les 20 dernières saccades. Ouvre l'onglet sur ton projet Semeru pendant que ça accroche, puis copie le rapport : on saura exactement quoi corriger.
- **Ce que coûte la mesure.** Environ 75 ns par partie mesurée, soit moins d'un millième d'une image. L'onglet ne se relit que quatre fois par seconde, et seulement quand il est à l'écran.

### Les missions : déplier, réduire les propositions

- Dans le tableau de bord, chaque proposition a un en-tête avec un chevron (▸ / ▾). Un clic la réduit à une ligne (« P1 Mission Semeru : S1, S2, S4, S3 — 302 opérations · 293 appliquées, 9 en échec » et son état) ou la déplie.
- « Tout déplier » / « Tout replier » sont dans l'intertitre « Propositions », et au clic droit sur un en-tête (avec Déplier / Réduire).
- Au départ, celles qui attendent ta décision (à valider, validée, en échec) sont dépliées et les autres réduites. Ton choix est gardé jusqu'à la fermeture du logiciel ; rien n'est écrit dans la mission.

### Les missions : forcer la fin

- **Forcer la fin de la mission…** (tableau de bord, à côté d'Annuler ; clic droit d'une mission dans l'onglet Missions IA) termine la mission là où elle en est, après une question qui dit ce qui va se passer.
- Ce qui se passe :
  - l'appel à l'IA en cours s'arrête ;
  - une proposition en attente passe « refusée » et n'est jamais appliquée ;
  - chaque étape non terminée devient **ignorée (fin forcée)**.
- **Rien n'est défait dans le projet.**
- La mission est **Terminée (fin forcée)**. L'audit final suit, et son rapport le dit, avec les étapes ignorées. Le fichier de la mission garde la date, la raison, les étapes et les propositions concernées.
- C'est impossible pendant qu'une proposition s'applique en arrière-plan (la raison est dite), et sur une mission déjà terminée ou annulée.

## 2. Les essais

Sous Linux : **122 suites d'essais sur 122** réussies (CTest), dont une nouvelle.

- **Nouvel essai :** `perf_test`. Il couvre les mesures (temps propre, temps avec ce qu'une partie appelle, fenêtres, courbe, remise à zéro), les saccades (seuil, borne, historique), le rapport et l'onglet dessiné hors écran. Résultat : 74 vérifications.
- **Essais complétés :**
  - `ai_options_test` refait ton cas : Popup_Vanne et son paramètre Vanne de type T_Vanne. On y vérifie Vanne.Cmd et $Vanne$.Autorisee valides, un membre inconnu refusé avec les vrais membres, l'application, le refus hors de la popup et le contexte envoyé à l'IA. Résultat : 244 vérifications (8 nouvelles).
  - `hmi_editor_test` vérifie l'onglet Performances, son « Copier » et son clic droit. Résultat : 9 559 contrôles.
  - `ai_missions_test` : forcer la fin (étapes ignorées avec la raison, proposition en attente non appliquée, historique, rapport, refus, relecture). Résultat : 363 vérifications (291 en 1.22.1).
  - `ai_missions_e2e_test` : forcer la fin pendant un appel à l'IA (l'appel est arrêté, rien n'est appliqué, audit et rapport), avec une proposition en attente, et pendant une application de fond (refusé). Résultat : 237 vérifications (199 en 1.22.1).
  - `ai_missions_ui_test` : les cartes dépliées / réduites par défaut, Tout déplier / Tout replier, le clic et le clic droit, la question puis l'état après la fin forcée. Résultat : 295 vérifications (238 en 1.22.1).

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.22.2.exe`, l'installateur (47 280 835 octets). Il met à jour la 1.22.1 au même endroit ;
- `API-Studio-1.22.2-portable.zip` et `IHM-Studio-1.22.2-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.22.2.exe` | `C6F0C197DBC5424DC1AC662F9A593A64497CEE584B0AB34A152518F05B03ED30` |
| `API-Studio-1.22.2-portable.zip` | `BE9B871917289F5EEAFC12BCF82552B788A7B5D717106F1CBC3349C0CC1AC65E` |
| `IHM-Studio-1.22.2-portable.zip` | `D1BAD3A29CC1F877415C5A26C3356CF9F6B4443F63F1B8B2FB5BE15E1984F0CC` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.22.1 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.22.2 » et « IHM Studio 1.22.2 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.22.1 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **Ajouter un paramètre à une popup existante** (le « Sortie » de P2) : aucune opération de l'Assistant ne le fait. La règle 19 lui demande de te le dire plutôt que d'inventer une variable ; tu l'ajoutes dans l'onglet Paramètres de la popup. Une popup créée par la proposition elle-même porte ses paramètres, et ses objets les lisent tout de suite.
- **Le remplacement d'un paramètre par clic droit** n'est pas proposé : un paramètre n'est plus une erreur, il n'y a rien à corriger.
- **L'onglet Performances sous Windows** : la mémoire est lue par `K32GetProcessMemoryInfo` (kernel32, sinon psapi.dll) et le processeur par `GetProcessTimes`. Rien n'a été vérifié sur un vrai Windows ; sous Wine, seule l'installation l'a été.
  - Pendant qu'on déplace ou redimensionne la fenêtre, Windows bloque l'appli : la saccade est attribuée à « evenements » (l'infobulle le dit).
  - Au repos, « peinture.presenter » (l'attente de l'écran) domine : c'est normal.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows**, et jamais avec une vraie IA. Les nouveaux écrans n'ont été vus que dessinés hors écran par les essais.
- **Pas de captures d'écran**, selon ta consigne permanente.
