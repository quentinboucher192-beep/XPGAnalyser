# XPGAnalyser 1.11.16 : la rémanence d'exploitation (lot 4)

Livrée le 09/10/2026 dans la nuit. C'est le **lot 4**, le dernier de votre spécification « IHM Simulation » du 08/10. Il couvre :
- la rémanence des variables IHM en exploitation (§ 14) ;
- la séparation des deux rémanences, côté poste (§ 15 B) ;
- la robustesse restante (§ 16) ;
- les tests restants de votre § 19 ;
- le **rapport final** en 9 parties demandé au § 20 : `docs/RAPPORT-FINAL-IHM-SIMULATION.md`.

Les lots 1 à 3 (1.11.13 à 1.11.15) restent tels quels.

Fichiers livrés :
- `XPGAnalyser-Setup-1.11.16.exe` (l'installateur) ;
- ce document et le rapport final ;
- les captures `11116_*` et `11116b_*`, prises sous Wine avec l'exe livré.

## 1. Ce qui change pour vous

- **Variables IHM : la case « Rémanente »**, une colonne de la table (oui / non) et une ligne de la fiche. Une variable cochée garde sa valeur **sur le poste d'exploitation** :
  - elle est écrite à chaque changement, sans attendre l'arrêt ;
  - elle est rendue au lancement suivant.

  La simulation de l'éditeur n'y écrit jamais.
- **La fiche « Rémanence (exploitation) »** de chaque variable : Rémanente, valeur initiale, valeur actuelle, dernière valeur sauvegardée, sa date, l'état de sauvegarde et le fichier.
- **Six commandes au clic droit** :
  - Réinitialiser cette variable rémanente ;
  - Réinitialiser toutes les variables rémanentes ;
  - Exporter les valeurs rémanentes… ;
  - Importer les valeurs rémanentes… ;
  - Afficher l'emplacement du stockage ;
  - Vérifier l'intégrité des données rémanentes.
- **L'échange avec Excel** : l'export et l'import en CSV (BOM, `;`, virgule décimale) ; le collage depuis Excel remplit aussi la colonne Rémanente.
- **Un seul poste écrit** les variables rémanentes d'un projet. Un second poste les lit, le dit, et prend la relève quand le premier s'arrête.
- **Deux corrections** :
  - un poste relancé après un arrêt brutal ne fait plus reculer une variable rémanente ;
  - un double-clic sur un diagnostic (ou une ligne de la Console) qui vise un script général montre l'onglet **Scripts généraux**, le script ouvert à sa ligne. Avant, la Programmation générale restait sur Variables IHM ou Types IHM si l'un d'eux était affiché : le script était choisi, mais pas montré. La vérification sous Wine de ce lot l'a trouvé.

## 2. La case Rémanente

| Où | Ce qu'on y fait |
|---|---|
| la colonne **Rémanente**, juste après Initiale (visible sans faire défiler, même avec la fiche ouverte) | oui / non dans une liste ; elle se filtre comme les autres colonnes ; « — » pour une variable liée à un équipement |
| la fiche, ligne **Rémanente** | la case à cocher |
| un collage depuis Excel (Ctrl+V) | une colonne « Rémanente » (ou Remanente, Retain, Persistante…) : oui/non, VRAI/FAUX, x, 1/0 |

- **Une variable liée à un équipement** n'en a pas besoin : l'équipement garde sa valeur. La case est refusée, et le refus le dit.
- **Une structure ou un tableau** est gardé case par case.
- **Cocher ou décocher** est une modification du projet : Ctrl+Z l'annule.

**La fiche « Rémanence (exploitation) »**. Les libellés sont courts pour se lire en entier dans la fiche ; celui de votre spécification est dans l'aide de chaque ligne.

| Ligne (votre propriété) | Ce qu'elle dit |
|---|---|
| Rémanente | la case |
| Initiale (valeur initiale) | celle qui revient si rien n'est gardé, après une réinitialisation, ou si le type a changé sans conversion possible |
| Actuelle (valeur actuelle) | celle de la simulation de l'éditeur si elle tourne, avec « (simulation de l'éditeur : jamais gardée) » ; sinon « — (simulation arrêtée) » |
| Gardée (dernière valeur sauvegardée) | la valeur gardée par le poste (« 3.75 », ou « 4 cases ») ; « aucune » |
| Gardée le (date de dernière sauvegarde) | la date de sa dernière écriture |
| État (état de sauvegarde) | voir ci-dessous |
| Fichier | le stockage du poste |
| (réinitialiser la valeur rémanente) | au clic droit : Réinitialiser cette variable rémanente |

Les états possibles :
- **à jour** : rendue au prochain lancement du poste ;
- **jamais sauvegardée** ;
- **type changé** : la valeur sera convertie, ou la valeur initiale sera reprise (incompatible) ;
- **non rémanente** : la valeur gardée sera ignorée ;
- **liée** : sa valeur vient de l'équipement.

## 3. Le poste d'exploitation garde et rend

- **Quand il écrit** :
  - les variables cochées sont capturées toutes les 0,25 s ;
  - une valeur qui a changé est écrite **au plus une fois par seconde** ;
  - tout est écrit à l'arrêt et en quittant le poste.

  Il n'attend donc pas l'arrêt propre : une fermeture brutale perd au plus la dernière seconde.
- **Comment** : d'un bloc (fichier temporaire, relu, ancienne version en `.bak`, remplacement). La dernière ligne compte les valeurs : un fichier coupé est refusé et sa copie reprise.
- **Une écriture qui échoue** (disque plein, accès refusé) : le dernier fichier valide reste. L'état le dit, le journal du poste aussi (« Variables rémanentes non écrites (…) — nouvel essai dans 5 s »), et un nouvel essai a lieu 5 s plus tard.
- **Un dossier inaccessible** : le poste démarre quand même et dit qu'il ne garde rien.
- **Au lancement**, les valeurs sont rendues **après les valeurs initiales et avant les scripts de Démarrage**. Le journal du poste le dit, sous « Rémanence » : « Variables rémanentes restaurées : N valeurs rendues ». Les règles sont celles de la rémanence de simulation (§ 6 du rapport final) :
  - l'identifiant stable compte, pas le nom ;
  - un type compatible est converti ;
  - un type incompatible reprend la valeur initiale, avec un avertissement ;
  - une variable supprimée ou décochée est ignorée.
- **Le script de Démarrage a le dernier mot** : dans Armoire_Gaz, `Init` remet `Compteur_Clics` à 0. C'est pourquoi la vérification sous Wine utilise Vitesse_Pompe et Consigne_Pression.
- **La reprise d'un arrêt brutal** (lot 15) recharge l'état d'avant, pris toutes les 10 s. Les variables rémanentes, plus récentes, sont ensuite rendues à nouveau : elles l'emportent.
- **Deux postes sur le même projet** : un verrou (`remanence_exploitation.lock`, PID et heure) désigne celui qui écrit.
  - Le second lit les valeurs, ne les écrit pas, et le dit dans son journal.
  - Il réessaie toutes les 30 s et prend la relève quand le premier s'arrête.
  - Un verrou que personne n'a rafraîchi depuis 3 minutes est repris.
  - L'éditeur refuse Réinitialiser et Importer pendant qu'un poste écrit.

## 4. Le stockage, à part de la simulation

| | Rémanence de simulation (lot 3) | Rémanence d'exploitation (ce lot) |
|---|---|---|
| Qui écrit | l'onglet Simulation de l'éditeur, si l'option est cochée | le poste d'exploitation seul |
| Quoi | toutes les variables IHM non liées, et les esclaves simulés | les variables cochées « Rémanente » |
| Où | `<projet>/.xpg/simulation/remanence.txt` | `<projet>/ihm/historique/remanence_exploitation.txt` |
| En-tête | `xpg-simulation-remanence 1` | `xpg-remanence-exploitation 1` |
| Effacée par | Redémarrer (après la question) | Réinitialiser (clic droit, après la question), jamais par un build |

Le format exact, avec un extrait réel, est au § 5 du rapport final.

## 5. La robustesse (§ 16)

| Votre cas | Ce qui se passe |
|---|---|
| fermeture brutale | le build : son cache et ses artefacts sont écrits d'un bloc. Le poste : ses rémanentes sont écrites au plus 1 s après un changement. La simulation de l'éditeur : à l'arrêt, et en quittant |
| coupure pendant la sauvegarde | le `.tmp` laissé est ignoré ; la cible reste entière ou la copie `.bak` est reprise (testé : fichier coupé, temporaire laissé) |
| fichier de rémanence corrompu | refusé (en-tête, ligne `fin` comptée) ; la copie `.bak` est reprise, sinon les valeurs initiales ; c'est dit |
| build interrompu, artefact partiel | Annuler ne remplace jamais un build valide ; un artefact altéré ou supprimé est refait (lot 1) |
| script invalide, dépendance supprimée | une erreur bloquante, avec sa source ; « Dépendance invalide » dans l'arbre (lot 1) |
| changement de version du générateur | tout passe à Obsolète et est refait (lot 1) |
| changement de structure d'une variable | les règles du retour : converti, sinon la valeur initiale, avec un avertissement |
| espace disque insuffisant, accès refusé | l'écriture échoue sans rien abîmer ; c'est dit ; nouvel essai 5 s plus tard (testé) |
| plusieurs instances du logiciel | `build.lock` pour le build (lot 1), `remanence_exploitation.lock` pour le poste (ce lot) |
| cache verrouillé par un autre processus | le build le dit (« PID 4120, depuis 10:38 ») ; un verrou de plus de 10 minutes est repris |

## 6. Le parcours vérifié sous Wine

Deux sessions jouées avec l'exe livré, sur une copie d'Armoire_Gaz sans cache ni stockage du poste. La session A se termine en **fermant l'application pendant que le poste tourne** ; la session B la **relance**. Le poste d'Armoire_Gaz a un mot de passe de sortie : les sessions ne le quittent pas par sa fenêtre de sortie. Chaque contrôle d'une session échoue si la valeur attendue n'y est pas ; il n'y a eu aucun échec.

| Étape | Ce qui s'est passé | Capture |
|---|---|---|
| **A1.** Variables IHM : Rémanente à « oui » pour Consigne_Pression, Vitesse_Pompe et Secondes_IHM | La colonne dit « oui » en vert et « non » en gris, juste après Initiale. La fiche « Rémanence (exploitation) » : Rémanente cochée, Initiale 2.5, Actuelle « — (simulation arrêtée) », Gardée « aucune », État « jamais sauvegardée (pas encore de fichier sur ce poste) » | `11116_01` |
| **A2.** Le clic droit sur Consigne_Pression | Les six commandes ; celles qui ne peuvent rien faire sont grisées avec leur raison (« aucune valeur gardée pour elle ») | `11116_02` |
| **A3.** `IHM_LOG(BLABLA, 'x');` ajouté au script Horloge, Compiler | Le build échoue : 1 erreur, 308 générés, 224 compilés. Double-clic sur l'erreur dans Diagnostics : l'onglet Scripts généraux s'affiche, Horloge est ouvert, le curseur à la ligne 2, et l'erreur est dite sous l'éditeur. Ctrl+Z retire la ligne | `11116_03` |
| **A4.** Le projet enregistré (`remanente=1` pour les trois), puis le poste d'exploitation ; une saisie : Consigne_Pression = 3.75, Vitesse_Pompe = 72.5 | Écrites deux secondes plus tard, sans arrêter le poste : « 3 valeur(s) gardée(s), écrites le 2026-10-09 00:30:37 » | `11116_04` |
| **A5.** L'application fermée pendant que le poste tourne | Le stockage est entier (extrait au § 5 du rapport final), sa copie `.bak` est à côté, le verrou est rendu | — |
| **B1.** Relance : la fiche de Consigne_Pression | Gardée 3.75, Gardée le 2026-10-09 00:30:25, État « à jour : rendue au prochain lancement du poste » | `11116b_01` |
| **B2.** Exporter les valeurs rémanentes (`.csv`) | 3 valeurs exportées, avec la virgule décimale (extrait au § 5 du rapport final) | — |
| **B3.** Vérifier l'intégrité | « 3 variable(s) rémanente(s). Fichier lisible et complet : 3 valeurs de 3 variables, écrit le 2026-10-09 00:30:37. » | `11116b_02` |
| **B4.** Réinitialiser cette variable rémanente (Consigne_Pression) | La question, puis « 1 valeur(s) oubliée(s) pour Consigne_Pression : le poste reprendra la valeur initiale au prochain lancement. » ; la fiche dit Gardée « aucune » | `11116b_03`, `11116b_04` |
| **B5.** Le poste relancé | Vitesse_Pompe = **72.5** (rendue après la relance de l'application) ; Secondes_IHM = 6 (elle repart de 5) ; Consigne_Pression = 2.5 (réinitialisée : sa valeur initiale) ; Mode_Pompe = 2 (non rémanente : sa valeur initiale) | `11116b_05` |

## 7. Fichiers créés et modifiés dans ce lot

**Créés :**
- `src/hmi/HmiRetain.hpp` et `.cpp` : la rémanence d'exploitation (le format, le fichier et sa copie, le verrou, la prise et la fusion, l'état d'une variable, la réinitialisation, l'intégrité, le CSV, l'import) ;
- `tests/hmi_retain_test.cpp` : 94 contrôles ;
- `tools/sessions/session-11116-remanence.txt` et `session-11116b-relance.txt` : les sessions Wine du § 6 ;
- ce document et `docs/RAPPORT-FINAL-IHM-SIMULATION.md`.

**Modifiés :**
- `src/hmi/HmiModel.hpp` et `HmiStore.cpp` : la case Rémanente, dans le projet ;
- `src/hmi/HmiSimData` : la prise filtrée et la valeur en texte, partagées avec la rémanence d'exploitation ;
- `src/hmi/HmiRuntime` : `captureData(keep)`, `setStartData(cellules, libellé)` et `applyData` (des valeurs rendues en marche) ;
- `src/hmi/HmiPipeline` : la règle de l'arrêt sur modification sort de l'écran pour être essayée (`runPrints`, `runChange`) ;
- `src/app/hmi/HmiSimulation` : le poste garde et rend (capture, écriture regroupée, nouvel essai, verrou, reprise) ;
- `src/app/hmi/HmiVariablePanes` : la colonne, la fiche, le clic droit, l'export et l'import ;
- `src/app/hmi/HmiScriptPanes.cpp` : aller à un script montre l'onglet Scripts généraux ;
- `src/app/screens/HmiWorkspace.cpp`, `StationScreen.cpp` et `HmiBuildWorkspace.cpp` : les hôtes de l'éditeur, le stockage du poste, la reprise, la règle du moteur ;
- `src/app/ScriptRunner.cpp` : les commandes de session ;
- `tests/hmi_editor_test.cpp` et `tests/hmi_build_test.cpp` ;
- la version et ses fichiers :
  - `src/core/Version.hpp`, `src/hmi/HmiPackage.hpp`, `src/help/ReleaseNotes.cpp`, `src/core/CodeStats.hpp` ;
  - `CMakeLists.txt`, `installateur/XPGAnalyser.iss`, `outils/config.ini`, `resources/windows/xpg_analyzer.rc` ;
  - `dist/SHA256SUMS.txt`.

## 8. Les tests ajoutés

- **`hmi_retain_test`** (CTest `hmiretain`) : **94 contrôles, 0 échec**.
  - le format : coupé, abîmé ou étranger, il est refusé ;
  - le fichier et sa copie `.bak` ;
  - le verrou : un autre poste, un verrou périmé, rafraîchi, un dossier inaccessible ;
  - la prise : les seules cochées, jamais une liée ;
  - la fusion datée ;
  - l'état pour l'éditeur ;
  - l'échange avec Excel : `sep=`, colonnes mélangées, VRAI/FAUX, virgule, tabulation ;
  - l'import par identifiant puis par nom ;
  - la case dans le projet ;
  - le moteur : rendu avant Démarrage ; un type converti ou incompatible.
- **`hmi_editor_test`**, *remanence1116* :
  - la simulation de l'éditeur n'écrit jamais ;
  - le poste écrit en marche, au plus une fois par seconde ;
  - quitter le poste écrit et rend le verrou ;
  - le lancement suivant rend les valeurs, et `restart()` aussi ;
  - la reprise d'un arrêt brutal ne fait pas reculer ;
  - un autre poste : lecture seule, puis la relève ;
  - une écriture qui échoue : le fichier valide reste, nouvel essai à 5 s ;
  - un dossier inaccessible ;
  - l'éditeur : la colonne, la fiche, Ctrl+Z, une variable liée refusée, le type changé ; Exporter, Importer, Réinitialiser, l'intégrité ; les six commandes du menu ; le refus pendant qu'un poste écrit.
- **`hmi_editor_test`**, *sources1116* : le double-clic sur une ligne des Diagnostics, des Sorties et de la Console donne sa source (le script, la ligne) ; aller à la source montre l'onglet Scripts généraux.
- **`hmi_build_test`** :
  - *arretSurModification* : la règle de l'arrêt ;
  - *ihmLogInvalide* : `IHM_LOG(BLABLA, …)` fait échouer le build sur sa ligne ; corrigé, le build passe ;
  - *fonctionIhm* : la vraie commande Renommer, puis le build.

Les 29 cas de votre § 19 et leurs tests sont au § 7 du rapport final.

## 9. Les limites

- Rien n'a été lancé sur un vrai Windows : tout a été vérifié sous Wine.
- Le poste écrit au plus une fois par seconde. Une coupure dans la seconde qui suit un changement perd ce changement ; le fichier garde la valeur précédente, entière.
- La « valeur actuelle » de la fiche est celle de la simulation de l'éditeur. Le poste ne tourne pas sous l'éditeur.
- Le détail case par case d'une structure rémanente n'est pas montré dans la fiche (« 4 cases »). L'export CSV le donne.

## 10. Tester vous-même

1. Prenez une copie d'Armoire_Gaz. Ouvrez IHM > Programmation générale > Variables IHM.
2. Mettez Rémanente à « oui » pour Vitesse_Pompe et Consigne_Pression. La fiche de droite montre « Rémanence (exploitation) ».
3. Faites Essayer le poste et changez la vitesse de la pompe.
4. Attendez deux secondes, puis Quitter le poste > Passer en conception (avec le mot de passe de sortie s'il y en a un). La fiche montre la valeur gardée (« Gardée »), sa date et « à jour ».
5. Relancez le poste : la vitesse est rendue. Quittez l'application **pendant que le poste tourne** et relancez-la : elle est encore rendue.
6. Clic droit, Exporter les valeurs rémanentes… : un `.csv`, que vous ouvrez dans Excel. Modifiez la valeur, enregistrez, puis Importer : le compte rendu s'affiche.
7. Clic droit, Réinitialiser cette variable rémanente : la question, puis « aucune ». Au lancement suivant du poste, la valeur initiale revient.

## Installer

1. Lancez `XPGAnalyser-Setup-1.11.16.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisissez « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.16.exe` (15,0 Mo, 15 746 529 octets) :
`72B9B92EAA92834865AB44FB9805A65C1F415314979240A3AAC0705FF88EAA76`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (59 sur 59). Elle comprend les 94 contrôles de `hmiretain`, les 212 de `hmibuild` et les 5 933 de `hmieditor`.

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.16` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- les sessions A et B du § 6 ont été rejouées avec l'exe livré : 9 captures `11116_*` et `11116b_*`, aucun contrôle en échec.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.15 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.16`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Le § 10 donne le parcours à refaire chez vous.

## La suite

La refonte des scripts et des fonctions (votre demande du 08/10) commence maintenant. Elle suit votre § 26 : d'abord l'analyse complète du dépôt (11 points, avec la charge relative de chaque module), puis des lots cohérents, chacun compilé et testé avant le suivant. La maquette interactive est déjà livrée.
