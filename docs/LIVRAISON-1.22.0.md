# API Studio et IHM Studio 1.22.0 : les missions IA persistantes

Livrée le 01/11/2026. Elle suit la 1.21.3 (`docs/LIVRAISON-1.21.3.md`). L'audit de l'existant fait avant toute modification est dans `docs/AUDIT-MISSIONS-1.22.0.md`.

Le principe que tu as donné : « l'IA ne doit plus seulement se souvenir de ce qu'elle a dit. Elle doit savoir ce qu'elle a réellement prévu, ce qu'elle a réellement fait, ce qui reste à faire et dans quel état se trouve le projet. »

## 1. Ce qui change pour toi

Tout se passe dans **IHM Studio**, dans le panneau de l'Assistant IA (aide F1 : « Les missions IA »).

### Lancer une mission
- Écris ta grande demande comme d'habitude. Si elle touche plusieurs familles du projet (équipements, variables, alarmes, vues…) ou dit « complète », la carte **Créer une mission IA ?** apparaît au-dessus de la saisie, avant tout envoi : **Créer la mission**, ou **Continuer en conversation** (l'envoi habituel). La croix ou Échap la ferme : rien ne part, ton texte reste dans la saisie.
- Ou à la main : l'onglet **Missions IA** (le troisième, après Conversation et Notifications), bouton **+**.
- Il faut une **IA connectée** (Anthropic, ou un service compatible OpenAI, en mode « Outil ») et un projet ouvert. L'IA intégrée et la démonstration ne font pas de mission (elles ne savent pas planifier).

### Ce que fait la mission
1. **Analyse** : l'IA regarde le projet réel (ce qui existe et est conforme, à modifier, à compléter, à créer, impossible). Rien n'est modifié.
2. **Plan** : des étapes S1, S2… rangées par leurs dépendances (calculées par le logiciel, plus celles des catégories : des variables attendent leurs types et leurs équipements, une alarme ses variables, une vue ses variables, symboles et popups…).
3. **Propositions successives** P1, P2, P3… : **Proposer la suite** regroupe les étapes prêtes, à la mesure d'une réponse. Chaque proposition se prévisualise, se vérifie et s'applique comme avant (une commande, un Ctrl+Z). Avant chacune, l'IA reçoit ce qui est **réellement** dans le projet : elle réutilise les noms exacts, ne recrée rien.
4. **Suivi réel** : une étape n'est terminée que si ce qu'elle a créé est dans le projet. Ctrl+Z après coup : la mission le voit (la proposition passe « annulée », ses étapes redeviennent prêtes) ; Ctrl+Y : « rétablie ».
5. **Erreurs** : une étape en erreur garde l'erreur exacte, celles qui en dépendent sont **bloquées** (et disent pourquoi), les autres continuent. **Corriger** demande une proposition de correction, présentée comme les autres.
6. **Fin** : l'**audit final** vérifie tout le projet ; le **rapport final** dit Créés, Modifiés, Réutilisés, Terminés, Non réalisés, Erreurs, À vérifier. La mission n'est **Terminée** que si l'audit est passé sans erreur bloquante.

### Où suivre
| Où | Ce que tu y vois |
|---|---|
| Onglet **Missions IA** du panneau | Toutes / En cours / Archivées, la recherche ; pour chaque mission : état, projet, dernière activité, progression, prochaine action ; **Tableau de bord ›**, **Reprendre ›** ou **Pause** ; clic droit : renommer (F2), archiver (Suppr), annuler…, historique ; la pastille de l'onglet compte les missions qui attendent une décision (rouge : une erreur). |
| Bande **Mission IA** au-dessus de la conversation | La mission de la conversation ouverte : étapes terminées sur le total, la barre, la prochaine étape, les propositions ; son bouton fait le geste du moment (Proposer la suite, Corriger, Actualiser le contexte, Reprendre). Le chevron la replie. |
| **Tableau de bord** (au centre, onglet « Mission IA · nom ») | L'en-tête (état, projet, cohérence, progression, dernière activité ; Pause/Reprendre, Annuler…, Conversation), les cinq progressions, l'objectif et l'analyse, la prochaine action, le plan en tableau, les propositions P1, P2… (Prévisualiser, Vérifier, Appliquer ; le compte rendu réel d'une proposition appliquée), la suite prévue, le rapport final, l'historique. |
| **Écran de reprise** | Le dernier état sauvegardé, ce qui est fait / en cours / reste, les dernières opérations appliquées, l'état du projet comparé (identique, modifié avec la liste, introuvable), la prochaine action ; **Reprendre la mission** ou **Actualiser le contexte**. |
| Notifications de l'Assistant | Mission créée, plan prêt, proposition disponible, appliquée, étape terminée, erreur, bloquée, interrompue, terminée ; le bouton ouvre le tableau de bord. |

### Depuis une autre conversation
« Où en est la station de pompage ? », « Reprends la mission d'hier » : l'IA cherche dans les missions **enregistrées** (outils `chercher_missions`, `etat_mission`) et répond avec leur état réel, jamais de mémoire ; une mission introuvable, elle le dit.


## 2. L'architecture

### Les fichiers

| Fichier | Rôle |
|---|---|
| `src/app/ai/AiMission.hpp/.cpp` | Le modèle et ses règles, sans disque ni réseau ni écran : les états d'une étape et leurs transitions, le plan (lu de l'outil `planifier_mission`), les dépendances implicites, l'ordre, le prochain lot, le suivi des propositions, la vérification dans le projet, la progression, le rapport final, les consignes données à l'IA, la recherche. |
| `src/app/ai/AiMissionStore.hpp/.cpp` | Le disque (`IA/missions/<id>.json`, écrit d'un bloc avec une copie `.bak`), l'empreinte du projet, la reprise après un redémarrage. |
| `src/app/ai/AiAssistant.hpp/.cpp` | Les missions dans la séance : création, appels à l'IA (plan, proposition, correction, actualisation) écrits **avant** l'appel, branchements dans Appliquer (direct et de fond), Refuser, Ctrl+Z/Ctrl+Y constatés au tick, audit final sur un fil (une copie du projet), reprise au démarrage, notifications, résumé des missions joint à toute demande, outils `chercher_missions` et `etat_mission` (relus du disque, sur le fil du fournisseur). |
| `src/app/ai/AiPrompt.cpp`, `AiOperations.cpp` | La propriété `etape` d'une opération (l'étape de mission qu'elle sert). |
| `src/app/ai/AiMissionsView.hpp/.cpp` | L'onglet Missions IA du panneau. |
| `src/app/ai/AiMissionStrip.hpp/.cpp` | La bande Mission IA et la carte « Créer une mission IA ? ». |
| `src/app/ai/AiMissionBoard.hpp/.cpp`, `src/app/screens/MissionsWorkspace.cpp` | Le tableau de bord au centre et l'écran de reprise. |
| `src/app/ai/AiPanel.*`, `AiWorkspace.cpp`, `AiNotifications.*` | Le troisième onglet, la bande, la carte, la notification `mission:<id>`. |
| `tests/ai_missions_test.cpp` | Le noyau (ctest `aimissions`). |
| `tests/ai_missions_e2e_test.cpp` | De bout en bout avec l'Assistant et un faux fournisseur : scénarios A à D (ctest `aimissionse2e`). |
| `tests/ai_missions_ui_test.cpp` | L'interface, dessinée hors écran (ctest `aimissionsui`). |

### Le format du fichier

`IA/missions/<id>.json`, un fichier par mission, **format 1** (le champ `format`) :

- l'identité : `id` (`mission-AAAAMMJJHHMMSS-xxxx`), `nom`, `description`, `demande` (la demande initiale **exacte**), `cree`, `modifie` ;
- le projet : `projet.id` (l'identité fiable du projet, 1.21.0), `nom`, `chemin`, `empreinte` (de tout le projet), `fichiers` (une empreinte par fichier du projet et par famille de l'index : variables, types, alarmes, équipements…), `empreinte_prise` ;
- les conversations : `conversation` (celle qui l'a lancée), `conversations` (toutes celles qui y ont travaillé), `fournisseur`, `modele` ;
- l'état : `etat` (`planification`, `en_cours`, `en_pause`, `bloquee`, `terminee`, `annulee`), `archivee`, `revision_plan` ;
- le plan : `etapes` (`id` S1…, `titre`, `detail`, `categorie`, `depend_de`, `implicites`, `etat`, `obligatoire`, `operations` estimées, `produit`, `propositions`, `note`, `erreur`, `maj`) **dans l'ordre d'exécution** ; `analyse` (`conforme`, `a_modifier`, `a_completer`, `a_creer`, `impossible`, `resume`) ;
- les propositions : `propositions` (`id`, `ordre` P1…, `conversation`, `message`, `etapes`, `titre`, `etat`, les comptes, `cree`, `appliquee_le`, `erreurs`, `lignes` = chaque opération avec son étape, son état réel et ce qu'elle a créé, `compte_rendu` = le compte rendu lu dans le projet, `suite`) ;
- la mémoire : `historique` (400 événements au plus, le premier gardé), `decisions`, `appel_en_cours` (écrit **avant** un appel à l'IA, effacé après), `coherence`, `point_de_reprise`, `resume`, `pieces_jointes` (les identifiants, jamais copiées), `rapport_final`.

### Les migrations

- **Format 0** (un fichier sans `format`) : lu ; les états écrits en clair (« Prête », « Terminée »…) sont compris, une liste de propositions en identifiants devient P1, P2…, l'ordre des étapes est recalculé ; il est réécrit au format 1 à la sauvegarde suivante.
- **Un format plus récent** (d'une version future) : lu en **lecture seule** (les champs inconnus sont gardés), **jamais réécrit** par la 1.22.0.
- Les champs inconnus d'un fichier sont toujours gardés et réécrits tels quels.

### L'écriture et la reprise

- Chaque événement (plan, proposition générée, vérifiée, appliquée, refusée, étape changée, pause…) est **enregistré aussitôt** : écriture dans un `.tmp` relu, puis remplacement ; la version précédente reste en `.bak`.
- Un arrêt brutal pendant l'écriture laisse toujours une version complète : au démarrage, un fichier illisible est **repris de sa copie** (et c'est dit) ; le fichier abîmé est gardé à côté (`.abime`), jamais effacé. Un fichier illisible sans copie est signalé et laissé tel quel.
- Au démarrage, chaque mission est comparée au projet ouvert :
  - **cas A**, le même projet, identique : on reprend ;
  - **cas B**, le même projet, modifié depuis : la mission dit **quoi** (« les variables (modifiées) », « la vue Vue_Accueil (ajoutée) ») et la prochaine action est d'actualiser le contexte avant toute nouvelle proposition ;
  - **cas C**, le projet introuvable (un autre projet ouvert, ou aucun) : rien ne se propose, la mission demande d'ouvrir son projet.
- Un appel à l'IA en cours à la fermeture est marqué **interrompu** : sa réponse n'existe pas, **rien n'est rejoué**, ses étapes redeviennent prêtes. Une application n'est **jamais** rejouée : une proposition appliquée l'est par le projet réel, vérifié à la reprise (`verifyAgainst`).

### Les règles du suivi

- Le projet réel est toujours la source de vérité : une étape n'est **terminée** que si ce que ses opérations ont créé est dans le projet d'après ; une proposition n'est **appliquée** que si le compte rendu lu dans le projet le dit, jamais parce que l'IA l'a annoncé.
- Générée ≠ vérifiée ≠ appliquée ≠ vérifiée dans le projet : quatre états distincts, quatre mesures de progression distinctes.
- Ctrl+Z d'une proposition : la mission le voit (la proposition passe « annulée », ses étapes redeviennent prêtes) ; Ctrl+Y : « rétablie ».
- Un plan revu (par l'IA ou par toi) ne réinitialise jamais : les étapes terminées et les opérations appliquées sont gardées ; l'historique dit ce qui a été ajouté, retiré, gardé.
- Les dépendances : celles que l'IA écrit, plus celles des **catégories** (des variables dépendent des types, des équipements et de l'analyse ; une alarme des variables ; une vue des variables, symboles, popups et types ; la vérification de tout…), ajoutées seulement si elles ne créent pas de cycle, et **dites** (« implicites ») ; l'ordre est calculé (tri topologique stable), jamais celui dans lequel l'IA les a écrites ; un cycle est refusé et dit (« S3 -> S5 -> S3 »).
- La progression (la règle affichée) : chaque étape pèse son estimation d'opérations (au moins 1) ; les étapes ignorées ou annulées sortent du calcul ; planification 100 % dès que le plan existe ; génération = les étapes qui ont une proposition ; validation = vérifiée ou appliquée ; application = appliquée ; **globale = terminée**, c'est-à-dire appliquée et vérifiée dans le projet.
- La mission n'est **terminée** que si toutes ses étapes obligatoires le sont **et** que l'audit global du projet (`hmi::audit::run`, le même que « Vérifie tout mon projet ») a été fait sans erreur bloquante. Sans audit : « non vérifiable », jamais « vérifié ».

## 3. Les essais

**119 suites d'essais sous Linux** (116 en 1.21.3, plus les trois des missions), lancées par CTest sur le code livré : voir le résultat final ci-dessous. Pour cette livraison :

| Suite | Ce qu'elle vérifie | Résultat |
|---|---|---|
| `ai_missions_test` (nouvelle, ctest `aimissions`) | le noyau : les 12 états et leurs transitions, les clés du fichier ; le plan (ordre calculé, dépendances implicites, cycle et dépendance inconnue refusés, la mission intacte après un refus, plan revu fusionné) ; les modifications de plan ; le prochain lot ; **le suivi réel sur un vrai projet** (générée ≠ appliquée, une commande, le compte rendu lu dans le projet, Ctrl+Z / Ctrl+Y constatés, une validation en erreur, un refus, une erreur d'application et ses dépendantes bloquées, la correction) ; la progression et sa règle ; le rapport final avec et sans audit ; pause, reprise, annulation, archivage ; le fichier (aller-retour exact, format 0 migré, format plus récent jamais réécrit) ; le disque (copie `.bak`, fichier incomplet repris, `.bak` seul, fichier illisible laissé) ; l'empreinte (identique après relecture, la date d'enregistrement ignorée, « les variables (modifiées) », « la vue X (ajoutée) ») ; la reprise A / B / C, un projet jamais enregistré ; le scénario B ; la recherche et les outils ; l'historique borné | 208 vérifications, 0 échec |
| `ai_missions_e2e_test` (nouvelle, ctest `aimissionse2e`) | l'Assistant de bout en bout avec un faux fournisseur : scénarios A, B, C, D (§ 4), erreur et correction, autre projet, pause / reprise / annulation, plan revu | 183 vérifications, 0 échec |
| `ai_missions_ui_test` (nouvelle, ctest `aimissionsui`) | l'interface dessinée hors écran : le geste du moment, l'onglet Missions IA (liste, recherche, filtres, gestes, clavier), la bande, la carte « Créer une mission IA ? », le tableau de bord, l'écran de reprise (identique, modifié, introuvable), la notification, aucun dessin qui bloque | 196 vérifications, 0 échec |
| `ai_assistant_test`, `ai_registre_test`, `ai_reseau_test`, `ai_compatible_test` | l'Assistant inchangé pour une demande ordinaire ; la taille de la demande avec la propriété `etape` | 298, 251, 287, 190 vérifications, 0 échec |
| `hmi_editor_test` | les notes de version (573, 83 versions), l'aide F1 (270 sujets, « Les missions IA » rattaché à sa ligne de l'arbre), les glyphes de l'atlas Windows | 9 547 contrôles, 0 échec |
| `ai_sandbox_test` | (corrigé) l'annulation d'un processus enfant demandée avant son lancement | 67 vérifications, 0 échec (5 fois de suite) |

Deux défauts trouvés par les essais pendant la livraison, corrigés : un caractère « ≈ » du tableau de bord absent de la police de l'application sous Windows (remplacé par « environ ») ; la nouvelle page d'aide sans sa ligne de l'arbre (pour les tutoriels). Et un défaut plus ancien (1.21.0), vu une fois sous forte charge : une annulation demandée avant le lancement d'un processus enfant (le bac à sable) pouvait être perdue si l'enfant finissait avant que le fil appelant ne reprenne la main ; corrigé dans `core::process::run` (Linux et Windows).

**Résultat final : 119 suites sur 119 réussies** (CTest, Linux, sur le code exact de cet installateur).

## 4. Les scénarios demandés

| Scénario | Où il est joué | Ce qui est vérifié |
|---|---|---|
| **A** — station de pompage complète | `ai_missions_e2e_test` [A] (et `ai_missions_test`, le suivi réel) | la carte « Créer une mission IA ? » ; l'appel du plan écrit avant de partir ; le plan écrit dans le désordre (S4, S3, S5, S2, S1) rangé par ses dépendances, implicites comprises ; P1 (équipement, variables) appliquée en **une** commande, S1 et S2 terminées parce que leurs éléments sont dans le projet ; P2 reçoit les noms réels et **réutilise** `Cuve_Niveau` sans doublon ; Ctrl+Z puis Ctrl+Y constatés ; la suite relue du disque ; l'audit final et le rapport |
| **B** — interruption pendant la 3e proposition | `ai_missions_e2e_test` [B], `ai_missions_test` (scénario B) | l'appel en cours enregistré avant de partir ; l'arrêt brutal (la mission relue du disque) ; à la reprise : « interrompu », rien de rejoué, P1 et P2 restent appliquées, les étapes de P3 de nouveau prêtes, le projet identique, aucune duplication |
| **C** — modification externe pendant la préparation | `ai_missions_e2e_test` [C], `ai_missions_test` (empreinte) | le projet modifié pendant que l'IA écrit : la proposition arrive **obsolète** et n'écrase rien ; à la reprise, la liste de ce qui a changé (« les variables (modifiées) », « la vue X (ajoutée) ») et « Actualiser le contexte » d'abord |
| **D** — mission reprise depuis une autre conversation | `ai_missions_e2e_test` [D], `ai_missions_test` (recherche) | une nouvelle conversation : les outils `chercher_missions` et `etat_mission` exécutés depuis la vraie requête, la mission exacte citée avec son état réel ; une mission inconnue : « aucune mission » ; la reprise de la mission depuis cette conversation |

Aussi : une étape en erreur (sa dépendante bloquée, l'indépendante terminée, la mission jamais « terminée », puis Corriger) ; un autre projet ouvert puis le bon rouvert ; pause (la réponse vraiment arrêtée, l'application refusée), reprise, annulation ; le plan revu qui garde le travail fait ; un fichier incomplet repris de sa copie ; un format 0 migré ; un format plus récent jamais réécrit ; l'historique borné.

## 5. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.22.0.exe` : l'installateur (46 888 964 octets). Il met à jour la 1.21.3 au même endroit et garde tes données (projets, conversations, clés, missions) ;
- `API-Studio-1.22.0-portable.zip` et `IHM-Studio-1.22.0-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.22.0.exe` | `749596194C8145E836F80771F0B0634D62A36332AF1A009DE24FF9D332A11181` |
| `API-Studio-1.22.0-portable.zip` | `CCAC7CEB7FD59FDB9A7214BFC715B2C55E2F5A368B3CA47BB861D15D2D8D0313` |
| `IHM-Studio-1.22.0-portable.zip` | `4CBEBDAEC622D0CE705CF26B61782F34BCD68CE5BB5A3158DFCECABB1EB67257` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.21.3 livrée : installation réussie ; une clé API d'essai gardée à l'identique ; les deux exécutables installés identiques à ceux construits ; `--version` répond « API Studio 1.22.0 » et « IHM Studio 1.22.0 » ; `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ; le bac à sable de l'exécutable Windows (analyse, compilation, exécution, un cas réussi et un cas faux exprès) ; les associations `.xihm` et `.xapi` ; la copie de secours de la 1.21.3 gardée. L'interface (dont celle des missions) n'a pas été ouverte dans une fenêtre.

## 6. Limites (la liste précise)

- **IHM Studio seulement** : les missions vivent là où vit l'Assistant ; API Studio n'en a pas, et une mission ne travaille que sur le projet IHM (pas le programme de l'automate).
- **Jamais essayé avec une vraie IA** : tous les essais utilisent un faux fournisseur qui rejoue des réponses écrites à la main (aucune clé dans la session). La qualité de l'analyse et du plan dépend du modèle ; le logiciel ne fait que garantir l'ordre, les états, la validation et la fidélité au projet.
- **Une IA connectée en mode « Outil »** est nécessaire (Anthropic ou compatible OpenAI) ; ni l'IA intégrée, ni la démonstration, ni les modèles qui reçoivent les propositions en texte.
- **Windows** : les exécutables ont été vérifiés **sous Wine, pas sur un vrai Windows**. L'interface des missions n'a été vue que dessinée hors écran par les essais sous Linux, jamais dans une fenêtre.
- **Pas de captures d'écran** des nouvelles interfaces : ta consigne permanente les a supprimées pour livrer plus tôt (voir § 7).
- **Deux fenêtres d'IHM Studio** sur la même mission ne se synchronisent pas : chacune la relit au démarrage, la dernière écriture l'emporte.
- **Le constat de reprise** (l'empreinte du projet) est calculé sur le fil de l'interface, seulement à l'ouverture de l'écran de reprise, sur « Vérifier de nouveau », et au plus toutes les 2 s si le projet change pendant que cet écran est affiché : sur un très gros projet, une courte attente est possible à ce moment-là.
- **L'empreinte** compare le projet en mémoire (pas les fichiers du disque) ; la date d'enregistrement n'y compte pas.
- **Les dépendances implicites** suivent des règles de catégories (dites dans le plan, jamais cachées) : un plan atypique peut attendre plus que nécessaire ; on corrige en modifiant les dépendances de l'étape.
- **Les étapes d'une mission ne se retirent pas une fois appliquées** (ignorer la suite seulement) : ce qui est appliqué reste dans le projet, Ctrl+Z pour le défaire.
- `editMissionPlan` (ajouter, ignorer, renommer une étape à la main) et `attachMission` (rattacher une conversation) existent dans l'Assistant mais **n'ont pas encore de bouton** dans l'interface : on passe par l'IA (« ajoute une étape… ») ou par l'actualisation du plan.
- Une **compilation Visual Studio (MSVC)** n'a pas été faite : seule la chaîne CMake (Linux, MinGW) est construite et essayée.

## 7. Choix faits, et écarts à la demande

- **Captures d'écran** : la demande les liste dans les livrables ; ta consigne permanente (« supprime les futures captures d'écrans ») les a retirées pour livrer plus tôt. Je l'ai suivie : aucune séance de captures sous Wine. Les écrans sont vérifiés par `ai_missions_ui_test`, qui les dessine hors écran et contrôle leur contenu. Dis-le si tu veux une passe de captures pour cette version.
- **Branches** : rien n'est fusionné dans une autre branche ; seule la branche de travail de la session est poussée, comme d'habitude.
- **Fichiers d'aide** : une section ajoutée à l'aide F1 (« Les missions IA ») et six notes de version ; aucune autre aide n'est modifiée.
- **Version Visual Studio** : le fichier `resources/windows/xpg_analyzer.rc` était resté à 1.12.0 (trouvé par la mission de documentation) ; il est aligné sur 1.22.0, sans quoi `build.bat` refusait de construire.

## 8. La documentation de référence (mission README)

Faite en parallèle, documentation seulement (aucun code, aucun format, aucune configuration de compilation ; la seule exception, le `.rc`, est dans la 1.22.0 et dite plus haut).

- **Créés** : `README.md` (le point d'entrée : présentation d'API Studio et d'IHM Studio, fonctionnalités, architecture avec deux schémas Mermaid, organisation du dépôt, installation et utilisation, compilation Windows/Linux, formats, Assistant IA, essais, limites, contribution, licences) ; `docs/API-STUDIO.md`, `docs/IHM-STUDIO.md`, `docs/ASSISTANT-IA.md`, `docs/FORMATS.md`, `docs/TESTS.md` ; `docs/images/README.md` (les 18 captures à ajouter, aucune image inventée).
- **Mis à jour (périmés)** : `docs/ARCHITECTURE.md` (réécrit : il décrivait le premier jalon, un seul exécutable), `docs/GUIDE-COMPILATION.md` (il datait de la 1.11.2 ; Linux, MinGW, le paquet), `docs/GUIDE-INSTALLATION.md`, `docs/CONFIG-INI.md`, `docs/DEPANNAGE.md`, une note « historique » en tête de `docs/ASSISTANT-IA-AUDIT.md`. Puis, pour la 1.22.0 : la version, les missions IA, le nombre d'essais.
- **Architecture constatée** : deux exécutables d'un seul code (seul `main.cpp` change) ; les bibliothèques `xpg_core`, `xpg_xls`, `xpg_import`, `xpg_ui`, `xpg_help`, `xpg_hmi` puis `xpg_app` ; une interface maison sur SDL3 (`SdlRenderer`, aucun rendu GDI ou Direct3D écrit à la main) ; un seul fil pour l'interface et les modèles, des fils de fond sondés à chaque image ; l'Assistant dans IHM Studio seulement ; le format IHM 33.
- **Vérifié** : 256 liens relatifs et ancres, 619 chemins cités, les 7 schémas Mermaid passés à l'analyseur de mermaid 10.9.1 ; aucune donnée de client ni clé.
- **Non vérifiable (dit comme tel dans les documents)** : un vrai Windows (seulement Wine) ; une compilation MSVC ; l'interface graphique sous Linux pour les versions actuelles ; l'Assistant avec une vraie IA ; les versions d'outils sont « constatées » dans l'environnement de construction.
- **Constats laissés tels quels** (hors périmètre) : `.XEF`/`.ZEF` reconnus mais non lus, `.XDB` non implémenté ; `SDL3.dll`/`.lib` et PDFium non versionnés ; 22 suites d'essais déclarées sans leur source, 3 sources d'essai non référencées ; `build.sh` et `runtests.sh` historiques ; l'aide F1 annonce six onglets pour la fenêtre Mémoire, il y en a sept.
- **Reste à faire** : les 18 captures listées ; un essai réel sous Windows et avec une vraie IA.
