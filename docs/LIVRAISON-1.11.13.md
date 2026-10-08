# XPGAnalyser 1.11.13 — la génération incrémentale de la Simulation IHM (lot 1)

Livrée le 08/10/2026 au soir. C'est le **lot 1** de votre spécification du 08/10 (« IHM Simulation : génération incrémentale, compilation, diagnostics, journalisation et rémanence ») : le moteur de build, ses états dans l'arbre, ses commandes partout, sa progression et le démarrage conditionné. La maquette interactive livrée plus tôt (`docs/maquettes/maquette-generation-simulation-ihm.html`) montre l'ensemble ; ce lot en code la première moitié, réellement, dans l'application.

| Votre demande | Où dans la 1.11.13 |
|---|---|
| Démarrer = un build structuré (analyse, API, IHM, compilation, validation, démarrage) | **Démarrer l'IHM**, F8, « Les deux », l'ouverture de l'onglet Simulation · IHM |
| Ne rien refaire si rien n'a changé, « Projet à jour » | le build incrémental ; la barre d'état et les sorties le disent |
| Ne refaire que ce qui a changé et ce qui en dépend, dans l'ordre API puis IHM | les dépendances d'interface et de contenu (§ 3) |
| Les 13 états, leurs icônes dans l'arborescence | tout à droite de chaque élément de l'IHM (§ 2) |
| Générer, Régénérer, Compiler, Générer et compiler partout | le clic droit de l'arbre, la barre des éditeurs de scripts et de fonctions |
| Une fenêtre de progression moderne, sans faux état | la fenêtre du build (phases A à G, 4 + 16 étapes, Annuler) |
| Empêcher le démarrage si une erreur bloquante subsiste ; double-clic vers la source | les sorties (IHM · Sorties), l'onglet Simulation le dit |
| Cache persistant, artefacts, écritures atomiques, verrou | `.xpg/build/` (§ 4 et § 5) |

Fichiers livrés : `XPGAnalyser-Setup-1.11.13.exe` (l'installateur), ce document et 22 captures prises sous Wine avec l'exe livré (`11113_*`, `11113a_*`, `11113c_*` et `11113d_*`, détaillées au § 7).

Les lots suivants : **2** le panneau du bas (Sorties, Console, Diagnostics) et `IHM_LOG(niveau, message)` ; **3** le cycle de la simulation (arrêt sur modification, Redémarrer, Générer et redémarrer, rémanence de simulation) ; **4** la rémanence d'exploitation par variable, la robustesse restante, les tests restants et le rapport final en 9 parties.

## 1. Ce qui change pour vous

> **À savoir avant d'ouvrir Armoire_Gaz.** Votre projet porte 8 erreurs réelles, que Compiler signalait déjà : des noms qui n'existent pas (détail au § 6). Jusqu'à la 1.11.12, la simulation démarrait quand même. Avec la 1.11.13, une erreur bloquante **empêche le démarrage**, comme vous l'avez demandé. Le premier Démarrer sur Armoire_Gaz dira donc « Démarrage bloqué : 8 erreurs bloquantes ». Un double-clic sur chaque erreur mène à sa source.

- **Démarrer l'IHM** (le bouton, F8, « Les deux », ou l'onglet Simulation · IHM qui s'ouvre) lance d'abord le build. Rien n'a changé depuis le dernier build valide : rien n'est refait, la simulation démarre aussitôt et la barre d'état dit « Projet à jour ». Quelque chose a changé : seuls les éléments touchés et ce qui en dépend sont refaits, dans l'ordre API puis IHM. Si le build dure plus de 0,3 s, la fenêtre de progression s'ouvre.
- **Une erreur bloquante empêche le démarrage.** L'onglet Simulation · IHM dit « Démarrage bloqué : N erreurs bloquantes ». L'onglet **IHM · Sorties** s'ouvre sur ses **Diagnostics** ; un double-clic sur l'un d'eux ouvre sa source (l'éditeur, la ligne, la colonne). Corrigez, puis Démarrer : seul l'élément corrigé est refait.
- **L'arbre montre l'état de build** de chaque élément de l'IHM, tout à droite de sa ligne. L'état suit chaque modification aussitôt (300 ms après la dernière frappe), sans rien construire. Un dossier montre le pire de ce qu'il contient. L'infobulle donne la raison, par exemple « Obsolète : dépendance modifiée : Pression_Gaz : REAL → LREAL ».
- **Le clic droit** de chaque élément ou dossier de l'IHM a une section **Build** : Générer, Régénérer, Compiler, Générer et compiler, puis un sous-menu (voir les diagnostics, aller à la première erreur, copier les erreurs, ouvrir l'artefact généré, nettoyer les artefacts). Une entrée grisée dit pourquoi : un build tourne déjà, rien à compiler ici, pas encore généré…
- **Les éditeurs de scripts** (scripts généraux et scripts de vue) et **de fonctions** (fonctions IHM et fonctions d'un symbole) ont dans leur barre : Générer, Régénérer, Compiler (F7), Générer et compiler, et l'état de l'élément choisi (« ✓ À jour », « ● Modifié », « ✕ Compilation échouée (2 erreurs) »). Un clic sur l'état ouvre les sorties.
- **La fenêtre de progression** montre :
  - la barre, le pourcentage, les tâches faites sur le total ;
  - l'étape (« C. Génération de l'IHM › 8. Vues ») et l'élément en cours ;
  - le temps écoulé, les avertissements et les erreurs ;
  - les phases A à G avec leur état, et sous B et C les 4 étapes de l'API et les 16 de l'IHM (à jour, faites sur à faire, en échec) ;
  - à la fin, un état clair : réussi, échoué ou annulé.

  **Annuler** arrête le build entre deux tâches : aucun fichier n'est coupé en deux et les artefacts valides restent. **Continuer en arrière-plan** ferme la fenêtre ; les sorties diront la fin.

## 2. Les 13 états et leurs 9 icônes

Les icônes sont dessinées par l'application elle-même (comme ✓ ✕ ⊘ de la 1.11), donc identiques sous Windows et sous Linux.

| Icône | État(s) | Ce que cela veut dire |
|---|---|---|
| ✓ (vert) | À jour, Généré, Compilé | généré (et compilé s'il a du code), rien n'a changé depuis |
| ⚠ (jaune) | À jour, avec des avertissements | à jour ; l'infobulle compte les avertissements |
| ● (orange) | Modifié, Génération requise, Compilation requise | changé depuis le dernier build : build requis (l'infobulle dit lequel) |
| ◌ (gris) | Non généré | jamais généré (projet neuf, élément ajouté) |
| ⚙ (bleu) | Génération en cours | pendant le build |
| ◔ (accent) | Compilation en cours | pendant le build |
| ✕ (rouge) | Génération échouée, Compilation échouée | le dernier build a échoué sur lui (le nombre d'erreurs dans l'infobulle) |
| ⛓ (orange) | Dépendance invalide | il cite un élément supprimé, introuvable ou en échec |
| ⏱ (bleu) | Obsolète | une dépendance a changé d'interface, ou le générateur a changé de version |

## 3. Les règles d'invalidation

Chaque élément a trois **empreintes** (FNV-1a 64 bits) : son **contenu** (ce qui s'exécute ou s'affiche), sa **configuration** (taille, fond, réglages de popup…) et son **interface** (ce que les autres en voient : un nom et un type, une signature).

- **La documentation n'entre dans aucune empreinte** (description, commentaire de variable, dossier de rangement, grille, guides) : la changer ne refait rien. Un commentaire *dans le code* d'un script fait partie du code : le script est refait.
- Un élément est refait si son contenu ou sa configuration a changé (**Modifié**), s'il a été renommé ou déplacé (**Renommé**, affiché Modifié), s'il n'a jamais été généré, si son artefact manque ou a été altéré (**Génération requise**), si son dernier build a échoué, ou si le générateur a changé de version (**Obsolète**).
- **Dépendance d'interface** : un script cite une variable, une fonction, un type, une variable de l'API ; une animation cite une variable ; une action vise une vue. Le dépendant n'est refait que si l'**interface** change :
  - le corps d'une fonction change : la fonction seule est refaite ;
  - sa signature change (un paramètre ajouté) : ses appelants aussi ;
  - une variable de l'API change d'adresse : elle seule ;
  - elle change de type : les animations, scripts et alarmes qui la citent, et rien d'autre (une vue qui ne la cite pas n'est pas touchée).
- **Dépendance de contenu** : une vue et son modèle (écran modèle, en-tête, pied de page), une vue et les symboles qu'elle pose (ils y sont dépliés), un type et ses membres de type IHM. Tout changement du dépendu refait le dépendant.
- **Un élément cité mais introuvable** (un symbole supprimé, une ressource absente) : le dépendant passe en **Dépendance invalide**. S'il n'a pas de code (une vue), sa génération échoue avec un code clair (E120 symbole introuvable, E121 modèle introuvable, E130 ressource introuvable, E132 référence introuvable). Un nom de code inconnu est dit par la compilation.
- **Un élément en échec** rend ses dépendants « Dépendance invalide » jusqu'à sa correction. Ils restent compilés à part si leur code le permet : la compilation dit l'appel.
- **Un élément supprimé** quitte le cache, et son artefact est retiré.
- **Cache absent, coupé (pas de ligne « fin »), illisible, ou d'un autre format** : tout est régénéré, et le journal dit pourquoi (« Régénération complète : cache absent… »).
- **Ce qui bloque le démarrage** :
  - une erreur de génération ou de compilation ;
  - une erreur de validation de structure (vues, objets, calques, identifiants, références circulaires, vue de démarrage) ;
  - une ressource manquante ;
  - la cohérence API / IHM (une variable inexistante).

  Les autres constats de l'ancien **Générer** sont dits en **avertissements** (« non bloquant pour la simulation ») : les bornes d'une recette, les paramètres d'une action, deux écritures sur un même registre, la qualité, les langues. La simulation tournait avec eux avant ; elle tourne encore.

## 4. Le cache de build

`<projet>/.xpg/build/build-cache.txt`, au format texte des fichiers IHM (une ligne par enregistrement, `mot clé=valeur`, guillemets pour les textes) :

```
# XPGAnalyser - cache de build de l'IHM (ne pas modifier a la main)
cache_build format=1 generateur="1" ecrit="2026-10-08 18:16:20"
element cle="script:69" genre="script" chemin="IHM/Programmation générale/Scripts/Horloge" contenu=c77b6a7d4350759c config=14650fb0739d0383 dependances=8a882f59d897c78a interface=db1da8d70a082e61 echec="" valide="2026-10-08 18:16:20" genere="2026-10-08 18:16:20" compile="2026-10-08 18:16:20" generateur="1" generation=genere compilation=compile artefact="ihm/05-scripts_generaux/script_69-Horloge.txt" empreinte_artefact="24de58c024ecfbbb"
dependance de="script:69" vers="variable:67" empreinte=a3407b90546b41fb signature="Secondes_IHM : DINT"
diagnostic de="vue:107" gravite="Avertissement" code="Lisibilité" categorie="Lisibilité" message="le texte déborde : ~183 px pour 176 px de place (« Clé de maintenance »)" ligne=0 colonne=0 longueur=0 etape="Validation" date="2026-10-08 18:16:20" suggestion="" vue=107 objet=117 script=0 item=0 propriete="text" fichier="ihm/vues/0107-Vue_Supervision.vue"
…
fin
```

(Un extrait réel : le cache d'une copie d'Armoire_Gaz après la session B, 788 lignes, 203 Ko.)

- `cache_build` : le format (1) et la version du générateur. Une autre version du générateur rend tout **Obsolète**.
- `element` : un élément, ses empreintes, ses dates (dernier build valide, génération, compilation), son état et son artefact. `echec` garde l'empreinte du code qui a échoué : un élément n'est jamais marqué généré ou compilé si l'opération a échoué.
- `dependance` : ce qu'il cite, avec l'empreinte et la signature lisible vues au dernier build.
- `diagnostic` : les remarques du dernier build, avec ce qu'il faut pour aller à la source.

Il est écrit d'un bloc :
1. tout est écrit dans `build-cache.txt.tmp` ;
2. le fichier temporaire est relu et comparé ;
3. l'ancien cache est copié en `.bak` ;
4. le temporaire est renommé.

Un cache sans dernière ligne `fin` est refusé. Le fichier `build.lock` (PID et heure) empêche deux instances de construire le même projet en même temps. Un verrou de plus de 10 minutes est repris.

## 5. Les artefacts

`<projet>/.xpg/build/api/NN-etape/` et `ihm/NN-etape/<clé>-<nom>.txt`, par exemple `ihm/08-vues/vue_1-Vue_Accueil.txt` ou `api/02-variables_globales/api-variable_Armoires-Armoires.txt`. Chaque artefact contient la forme générée de l'élément (une vue : ses instances de symboles dépliées), au format texte des fichiers IHM, avec son en-tête (clé, empreintes, dépendances). Il finit par `fin` et est écrit d'un bloc comme le cache. Les noms de dossiers et de fichiers sont en ASCII (`05-scripts_generaux`, `14-utilisateurs_et_securite`). Le dossier `.xpg/` n'entre pas dans les **versions** du projet. Pour Armoire_Gaz : 15 artefacts de l'API et 293 de l'IHM, 2,1 Mo en tout.

## 6. Votre projet Armoire_Gaz : les 8 erreurs qui bloquent le démarrage

Session A (`tools/sessions/session-11113a-demarrage-bloque.txt`), sur une copie d'Armoire_Gaz tel quel, sans cache. Le premier build : 304 éléments générés, 216 compilés, **8 erreurs**, 54 avertissements, en 0,4 s sous Wine. Les 8 erreurs sont de la même famille, un nom qui n'existe pas :

| Élément | Erreur |
|---|---|
| IHM/Vues/Vue_Armoire_A/Animations | `{Debit_Entree}` : Debit_Entree n'existe pas |
| IHM/Vues/Vue_Armoire_B/Animations | `{Debit_Entree}` : Debit_Entree n'existe pas |
| IHM/Popups/Popup_Armoire/Animations (3 erreurs) | `Equipement.Marche`, `{Equipement.Marche}`, `Equipement.Mesure` : Equipement n'existe pas |
| IHM/Popups/Popup_Armoire/Actions (2 erreurs) | cible `Equipement.Marche` : Equipement n'existe pas |
| IHM/Alarmes/PT9_Hors_Service | condition `Capteur_PT9 > 3` : Capteur_PT9 n'existe pas |

Chaque message finit par « ni variable IHM, ni variable système, ni variable de l'automate : corrige le nom ou déclare la variable ».

Pour la session B, une **copie** a été corrigée ainsi (votre projet n'est pas touché) :
- un type IHM `T_Equipement`, avec Marche : BOOL et Mesure : REAL ;
- trois variables IHM : `Equip_Armoire : T_Equipement`, `Debit_Entree : REAL` et `Capteur_PT9 : REAL` ;
- dans Popup_Armoire, un paramètre `Equipement : T_Equipement`, qui vaut `Equip_Armoire` par défaut.

C'est une correction possible parmi d'autres. Si `Debit_Entree` devait être une variable de l'automate, il vaut mieux corriger le nom.

## 7. Le parcours vérifié sous Wine

Session B (`tools/sessions/session-11113-build-incremental.txt`), sur la copie corrigée, sans cache :

| Étape | Ce qui s'est passé | Captures |
|---|---|---|
| 1. Avant tout build | les 308 éléments sont « ◌ Non généré » | `11113_01` |
| 2. Régénérer et compiler | 533 tâches : 308 générés, 225 compilés, 0 erreur, 51 avertissements, en 0,4 s | `11113_02` (en cours), `11113_03` (fin) |
| 3. L'arbre après le build | tout est ✓, ou ⚠ là où il y a des avertissements | `11113_04` |
| 4. Démarrer sans rien changer | « Projet à jour » : 0 généré, 0 compilé, la simulation démarre aussitôt | `11113_05` |
| 5. Une ligne ajoutée au script Horloge | Horloge et ses dossiers passent à « ● Modifié », sans build ; son clic droit montre la section Build | `11113_06`, `11113_07` |
| 6. Générer et compiler Horloge | 2 tâches : 1 généré, 1 compilé, 307 réutilisés | `11113_08` |
| 7. Une faute dans Horloge, puis Démarrer | 1 erreur (« une valeur est attendue (trouvé : ';') ») : démarrage bloqué, IHM · Sorties s'ouvre sur ses diagnostics | `11113_09`, `11113_10` |
| 8. Ctrl+Z, puis Démarrer | seul Horloge est refait (1 généré, 1 compilé, 307 réutilisés), la simulation démarre | `11113_11` |
| 9. Le clic droit d'un dossier (IHM/Vues) | la section Build du dossier | `11113_12` |

Les autres sessions :

| Session | Ce qu'elle montre | Captures |
|---|---|---|
| A, Armoire_Gaz tel quel | l'arbre, le build qui échoue sur les 8 erreurs, les diagnostics, un double-clic sur Capteur_PT9 qui ouvre l'alarme PT9_Hors_Service, l'onglet Simulation « Démarrage bloqué : 8 erreurs bloquantes » | `11113a_01` à `11113a_06` |
| C, la fenêtre quand le démarrage échoue | F et G disent « ⊘ bloqué : 8 erreurs » (jamais « à venir » une fois le build fini), puis Voir les sorties | `11113c_01`, `11113c_02` |
| D, Démarrer sur la copie corrigée | « ✓ la simulation démarre », « ✓ rien à restaurer », puis l'IHM en marche | `11113d_01`, `11113d_02` |

## 8. Le rôle de chaque service

Les services de votre § 17, et où ils vivent :

| Service demandé | Dans l'application | Rôle |
|---|---|---|
| BuildManager | `HmiBuildManager` (`src/app/hmi/HmiBuild`) | un build à la fois, sur un fil à côté de l'interface ; l'annulation ; la progression ; l'analyse 300 ms après chaque modification ; l'historique des builds |
| IncrementalBuildService | `pipeline::plan` et `pipeline::run` (`src/hmi/HmiPipeline`) | ce qui doit être généré ou compilé, puis les phases A à G dans l'ordre |
| DependencyGraphService | `pipeline::collect` et `pipeline::analyse` | les éléments, leurs dépendances d'interface et de contenu, les 13 états et leur propagation |
| ApiGenerationService | la phase B de `run` | les 4 étapes de l'API : configuration, variables globales, types et structures, tables d'échange |
| HmiGenerationService | la phase C de `run` | les 16 étapes de l'IHM, dans votre ordre |
| ScriptCompilationService | la phase D : le compilateur existant (`hmi::check`, la vérification des scripts et des expressions), ciblé sur les éléments à compiler | les erreurs de compilation, avec leur ligne et leur colonne |
| ValidationService | la phase E | la validation de structure, les ressources, la cohérence API / IHM ; ce qui bloque et ce qui avertit |
| BuildCacheService | `parseCache`, `serialize`, `saveCache` | le cache du § 4 : écrit d'un bloc, refusé s'il est coupé |
| ArtifactService | les artefacts de `run` | écrire les artefacts, vérifier leur empreinte, les retirer (§ 5) |
| DiagnosticService | `pipeline::Diagnostic` et l'onglet Diagnostics | le modèle de votre § 18 : identifiant, gravité, code, message, catégorie, élément, chemin, fichier, ligne, colonne, date, étape, suggestion ; le double-clic vers la source |
| LogService | le journal de chaque build, dans IHM · Sorties | la Console et `IHM_LOG` arrivent au lot 2 |
| SimulationLifecycleService | le démarrage conditionné par le build (`HmiSimulationPane`) | Arrêter, Redémarrer, Générer et redémarrer et l'arrêt sur modification arrivent au lot 3 |
| SimulationPersistenceService | — | lot 3 |
| RuntimePersistenceService | — | lot 4 |

Les écritures atomiques sont dans `core::writeFileAtomic` (`src/core/AtomicFile`) : écrire un fichier temporaire, le relire, garder l'ancien en `.bak`, renommer. Le cache, les artefacts et le verrou les utilisent.

## 9. Fichiers créés et modifiés

**Créés :**
- `src/hmi/HmiPipeline.hpp` et `.cpp` : le moteur (collecte, empreintes, dépendances, états, plan, exécution, cache, artefacts, verrou) ;
- `src/core/AtomicFile.hpp` et `.cpp` : les écritures atomiques ;
- `src/app/hmi/HmiBuild.hpp` et `.cpp` : le gestionnaire (fil de travail, annulation, analyse, historique) ;
- `src/app/hmi/HmiBuildPanes.hpp` et `.cpp` : la fenêtre de progression et l'onglet IHM · Sorties ;
- `src/app/screens/HmiBuildWorkspace.cpp` : le branchement dans l'écran (arbre, menus, barres, démarrage, commandes de session) ;
- `tests/hmi_build_test.cpp` : 190 contrôles ;
- `tools/sessions/session-11113-build-incremental.txt`, `session-11113a-demarrage-bloque.txt`, `session-11113c-fenetre-echec.txt` et `session-11113d-demarrer-reussi.txt` ;
- ce document.

**Modifiés :**
- `src/hmi/HmiCheck`, `HmiScriptCheck` et `HmiExprCheck` : la compilation ciblée ; les paramètres d'une popup de symbole et les instances sont reconnus (de fausses alertes trouvées sur Armoire_Gaz) ;
- `src/hmi/HmiVersions.cpp` : `.xpg/` reste hors des versions ;
- `src/app/ViewModels` : les états dans l'arbre ;
- `src/app/screens/ExplorerMenus.cpp` : la section Build du clic droit ;
- `src/app/screens/HmiWorkspace.cpp`, `HistoryWorkspace.cpp` et `Screens.hpp` : l'analyse après chaque modification, sur l'horloge de l'écran ;
- `src/app/hmi/HmiScriptPanes` et `HmiFunctionPanes` : la barre de build des éditeurs ;
- `src/app/hmi/HmiSimulation` : le démarrage passe par le build ;
- `src/app/ScriptRunner` : les commandes de session `ihm-build`, `ihm-build-attendre`, `ihm-build-etat`, `ihm-sorties` et `ihm-script-modifier` ;
- `src/platform/AtlasSymbols.hpp` et `FontAtlas.cpp` : les 5 symboles dessinés ◌ ⚙ ◔ ⛓ ⏱ ;
- `src/platform/SdlRenderer.cpp` : un caractère dessiné pour la première fois est visible dès cette image (la fenêtre du build s'ouvrait sur « % » au lieu de « 8 % ») ;
- la version et ses fichiers : `src/core/Version.hpp`, `src/hmi/HmiPackage.hpp`, `src/help/ReleaseNotes.cpp`, `src/core/CodeStats.hpp`, `CMakeLists.txt`, `installateur/XPGAnalyser.iss`, `outils/config.ini`, `resources/windows/xpg_analyzer.rc`, `tests/hmi_editor_test.cpp` et `dist/SHA256SUMS.txt`.

## 10. Les tests ajoutés

`tests/hmi_build_test.cpp` (test CTest `hmibuild`) : **190 contrôles, 0 échec**. Ils couvrent les cas de votre § 19 qui relèvent de ce lot :

| Votre cas | Le test |
|---|---|
| l'ordre API puis IHM, les éléments et leurs dépendances | `collecte` |
| première génération complète ; démarrage sans modification | `premiereGenerationEtDemarrageSansModification` |
| modification d'un seul script ; d'une vue sans impact sur les autres | `unSeulScriptEtUneVue` |
| modification d'une variable API utilisée par plusieurs vues ; changement de type | `variableApi` |
| renommage d'une fonction IHM (et sa signature) | `fonctionIhm` |
| suppression d'un symbole utilisé | `suppressionSymbole` |
| échec de compilation ; correction après échec | `echecEtCorrection` (et F et G « bloqué », puis « la simulation démarre ») |
| régénération d'un élément, d'une branche, complète | `commandes` |
| cache absent, cache corrompu, artefact supprimé à la main | `cacheEtArtefacts` |
| annulation d'un build ; coupure pendant une sauvegarde ; dossier en lecture seule | `annulation`, `coupureSauvegarde` |
| le format du cache | `formatDuCache` |
| les fausses alertes trouvées sur Armoire_Gaz | `faussesAlertes` |
| le gestionnaire : le build sur un fil à part, l'analyse après une modification, le refus d'un second build, l'annulation | `gestionnaire` |

La suite complète passe : **56 tests sur 56** (Linux, GCC 13). Un test d'horloge plus ancien (« Heure du poste, Appliquer ») a échoué une fois, quand la machine compilait en même temps : une seconde a passé entre deux gestes. Relancé seul, il passe, et la suite entière aussi.

Les cas de rémanence, d'arrêt sur modification et d'`IHM_LOG` viennent avec les lots 2 à 4. La navigation vers la source est vérifiée sous Wine (session A, double-clic sur Capteur_PT9).

## 11. Les limites de ce lot

- **Le panneau du bas** (Sorties, Console, Diagnostics) et **`IHM_LOG`** : lot 2. En attendant, les sorties et les diagnostics sont dans l'onglet IHM · Sorties.
- **Une modification pendant que la simulation tourne** passe l'élément à « Modifié » dans l'arbre, mais n'arrête pas la simulation. Arrêter, Redémarrer et Générer et redémarrer : lot 3.
- **La rémanence** (de simulation, puis d'exploitation) : lots 3 et 4. La phase G dit « rien à restaurer ».
- **Le volet des opérateurs** n'a pas encore sa barre de build.
- Rien n'a encore été lancé sur un **vrai Windows** : tout a été vérifié sous Wine.

## 12. Tester vous-même

1. Installez la 1.11.13, puis ouvrez une **copie** d'Armoire_Gaz.
2. Dans l'arbre, IHM : chaque élément a son état à droite, « ◌ » partout (rien n'est encore construit).
3. Ouvrez Simulation › IHM : le build part, puis « Démarrage bloqué : 8 erreurs bloquantes ». L'onglet IHM · Sorties s'ouvre sur ses Diagnostics.
4. Double-cliquez sur l'erreur de Capteur_PT9 : l'alarme PT9_Hors_Service s'ouvre. Corrigez les 8 erreurs (le § 6 donne une façon de faire).
5. Démarrer l'IHM : seuls les éléments corrigés et ce qui en dépend sont refaits ; la simulation démarre.
6. Arrêtez, puis Démarrer l'IHM sans rien changer : « Projet à jour », la simulation démarre aussitôt.
7. Ajoutez une ligne à un script général : il passe à « ● Modifié » dans l'arbre, ses dossiers aussi. Clic droit, Générer et compiler : seul ce script est refait.
8. Mettez une faute dans ce script (par exemple `X := X + ;`), puis Démarrer l'IHM : démarrage bloqué, l'erreur avec sa ligne. Un double-clic ouvre le script sur la ligne fautive. Ctrl+Z, puis Démarrer : la simulation repart.
9. Fermez l'application, rouvrez le projet, puis Démarrer : « Projet à jour » (le cache est relu ; vérifié sous Wine sur la copie de la session D).
10. Supprimez à la main un fichier de `.xpg/build/ihm/08-vues/`, puis Démarrer : seule cette vue est refaite.
11. Pour tout reconstruire : clic droit sur IHM, Régénérer et compiler. La fenêtre de progression montre les phases A à G et les 4 + 16 étapes ; Annuler l'arrête entre deux tâches.

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.13.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.13.exe` (14,9 Mo, 15 617 768 octets) :
`E5F4BC1DB72671C0022732D10CC70766784E564B1BF3EAB7FB9860F4DA6DB78E`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (56 sur 56), dont les 190 contrôles du moteur de build.

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.13` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- les sessions A, B, C et D (§ 7) ont été rejouées avec l'exe livré, sur des copies d'Armoire_Gaz sans cache. Ce sont les 22 captures `11113*.png`.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la version déjà installée : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.13`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Le § 12 donne le parcours à refaire chez vous.

## La suite

- **Lot 2** : le panneau du bas (Sorties, Console, Diagnostics) et `IHM_LOG(niveau, message)` dans tous les scripts.
- **Lot 3** : le cycle de la simulation (arrêt sur modification, Arrêter, Redémarrer, Générer et redémarrer) et la rémanence de simulation, avec son option et sa confirmation.
- **Lot 4** : la rémanence d'exploitation par variable, la robustesse restante, les tests restants de votre § 19 et le rapport final en 9 parties.
