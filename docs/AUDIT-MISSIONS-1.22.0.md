# 1.22.0 — Audit de l'existant avant les missions IA persistantes

État des lieux technique fait sur le code livré en **1.21.3** (commit `4e8e63f`), avant toute modification. Les chemins sont relatifs à la racine du dépôt.

## 0. Où vit l'Assistant IA

- Le panneau de l'Assistant (`src/app/ai/AiPanel.*`) est le quatrième volet du séparateur `analysis.upper` de l'écran du projet (`src/app/screens/MainAnalysisScreen.cpp`), **ajouté seulement si `core::hasIhm()`** : il existe dans **IHM Studio**, pas dans API Studio (l'action `ia.assistant` d'API Studio répond « L'Assistant IA s'ouvre dans l'écran du projet (IHM Studio) »).
- Ses hôtes (document IHM, vue ouverte, sélection, `App::apply`, fenêtres, barre d'état, cloche) sont dans `src/app/ai/AiWorkspace.cpp`. Le volet ne touche jamais l'application directement.
- Les opérations proposées par l'IA ne modifient que le **projet IHM** (`hmi::Project`).

**Conséquence pour la 1.22.0** : les missions vivent là où vit l'Assistant, dans IHM Studio. API Studio n'a pas d'Assistant ; l'y amener (et des opérations sur le projet automate) est hors du périmètre de cette version : c'est dit dans les limites.

## 1. Conversations et messages

| Quoi | Où | Comment |
|---|---|---|
| Racine des données IA | `ai::root()` (`AiPaths.hpp`), dossier `IA/` de l'utilisateur | posée par `App.cpp` au démarrage |
| Conversations | `IA/conversations/<id>.json` | `saveConversation` / `loadConversations` (`AiStore.cpp`) ; un fichier par conversation |
| Écriture | `writeJsonFile` → `core::writeFileAtomic` (`src/core/AtomicFile.hpp`) | fichier `.tmp` relu puis renommé ; sans `.bak` pour les conversations |
| Message | `ai::Message` (`AiStore.hpp`) | `id` (`m1`…), rôle, texte, état (`fini` / `arrete` / `erreur`), pièces jointes, `optional<Proposal>`, appel d'outil, note du logiciel, `extra` |
| Messages du logiciel | `extra.logiciel` (1.21.2) | jamais renvoyés au modèle (`requestFor` les saute) |
| Paramètres | `IA/parametres.json` | `Settings` ; la clé API n'y est jamais |

Une réponse en cours d'écriture n'est enregistrée qu'à sa fin (`finishLive` / `failLive`) : après un arrêt brutal, le message en cours n'existe pas sur le disque.

## 2. Une proposition et sa suite

- `ai::Proposal` (`AiOperations.hpp`) : `id` (`op…`, `newId`), état `proposee` / `appliquee` / `refusee`, vue visée, empreintes `fingerprint` (à la proposition) et `fingerprintAfter` (juste après Appliquer), demande, résumé, opérations, opérations retirées (`excluded`), **plan** (`plan::toJson`) et **compte rendu** réel (`plan::reportToJson`), `extra` (champs inconnus gardés).
- **1.21.3** : `Proposal::next` (« suite » de l'outil `proposer_operations`), gardé avec la conversation ; `Assistant::nextRequest(message)` rend « Propose la suite : … » une fois la proposition appliquée ; la carte montre « Suite prévue » et le bouton **Proposer la suite**.
- La suite est **un texte**, sans lien structuré vers les étapes ni vers la proposition suivante. C'est la limite que la 1.22.0 lève.

## 3. Les opérations appliquées

- `plan::build(project, proposal, check)` (`AiPlan.hpp`) : une étape par opération (`E1`, `E2`…), avec ce qu'elle **crée / modifie / réutilise** (`plan::Ref` : genre + nom + vue, `exists`), ses dépendances calculées sur les **noms réellement écrits**, triées topologiquement (jamais l'ordre du modèle) ; un cycle est bloquant (sauf entre du code).
- États d'une étape d'opérations : à faire, validée, appliquée, échouée, ignorée, annulée.
- Après Appliquer, `plan::report(before, after, plan, proposal)` compare **le projet d'avant et celui d'après** : créés, modifiés, réutilisés, supprimés, échecs, ce qui reste à faire. Une étape est « appliquée » seulement si ce qu'elle crée est dans le projet d'après. Le plan et le compte rendu sont gardés dans la proposition (`Proposal::plan`, `Proposal::report`) et dans la conversation.
- `Assistant::appliedState(message)` dit **après coup** si une proposition appliquée l'est toujours, a été annulée (Ctrl+Z, ou projet rouvert sans enregistrer) ou si la vue a changé depuis — par les empreintes, pas par la mémoire.

## 4. Les références vers les objets du projet

- Le projet IHM identifie ses éléments par `hmi::Id` (stables dans un projet ouvert) et par **nom** (vues, variables, types, alarmes, scripts, fonctions, équipements…).
- Le plan et le compte rendu de l'Assistant désignent les éléments **par genre + nom (+ vue pour un objet)** — `plan::Ref` — car l'identifiant d'un élément qui n'existe pas encore n'est pas connu à la proposition, et un projet rouvert garde ses noms.
- Le graphe des relations sait retrouver un élément par genre + nom (`hmi::rel::Graph::find`) ou par identifiant (`byId`).

## 5. Le graphe des dépendances

- `hmi::rel::build(project)` (`src/hmi/HmiRelations.hpp`, 1.19.0) : un nœud par élément, une arête « dépend de » tirée des données réelles (lectures dans le code par `hmi::forEachCode`, types, instances de symboles, actions, adresses…) ; `users`, `impact`, `orphans`, `readers`…
- `hmi::audit::run(project)` (`src/hmi/HmiAudit.hpp`) : la **vérification globale** — Générer et Compiler comme le logiciel, adresses en conflit, orphelins, dossiers incohérents — sans modifier le projet. C'est la base de l'audit final d'une mission.
- Côté proposition, `plan::schedule(proposal)` ordonne les opérations par les noms qu'elles écrivent et lisent.

## 6. Les tâches de fond et l'interface

- **Fournisseurs** (`AiAnthropic.cpp`, `AiOpenAI.cpp`) : un `std::thread` par demande ; `poll()` appelé à chaque image sur le fil de l'interface rend les événements (`Status`, `Text`, `ToolUse`, `Usage`, `Done`, `Error`) ; `cancel()` coupe la connexion. Les outils locaux (`LocalTool` : bac à sable, mémoire, recherche des conversations, lignes des classeurs…) sont **répondus sur le fil du fournisseur**, sur une copie du projet ou des fichiers relus — jamais sur le projet vivant.
- **Application de fond** (1.20.1, `Assistant::ApplyJob`) : la proposition est appliquée à une **copie** du projet sur un fil ; `pollApply()` (au tick) pose le résultat par **une** commande si le projet n'a pas changé entre-temps (`Document::revision`), sinon rien n'est appliqué et c'est dit.
- **Pièces jointes** : `attach::Analyzer` (un fil d'analyse), classeurs relus sur un fil (`WorkbookLoad`) ; le journal des notifications est écrit sur son propre fil.
- Règle commune : le fil de l'interface est le seul à toucher le projet et les widgets ; les fils de fond travaillent sur des copies et rendent des résultats sondés au tick.

## 7. La détection des modifications du projet

- En séance : `hmi::Document::revision` (incrémenté par `touched`, à chaque commande), le signal `changed`, `dirty`.
- Par proposition : `fingerprint(view)`, `proposalPrint(project, proposal)` (vue visée + éléments créés), `fingerprintAfter` après Appliquer.
- **Rien ne compare le projet d'une séance à l'autre** : `revision` repart de zéro. `hmi::store::serializeProject(project)` (`src/hmi/HmiStore.hpp`) donne le projet en fichiers (`ihm.txt`, `vues/…`, `scripts/…`, `ressources/…`) sans disque : c'est la base d'une **empreinte persistante** du projet, et d'une empreinte par fichier pour dire *ce qui* a changé.
- Identité d'un projet : `memory::identifyProject(nom, chemin, date de création)` (1.21.0, `AiMemory`), qui distingue deux projets de même nom.

## 8. Les pièces jointes

- `ai::Attachment` : identifiant unique (`p-AAAAMMJJ-…`), nom, extension, MIME, fichier copié relatif à `IA/` (`pieces-jointes/<id>/<nom>`), conversation et message d'origine, analyse (texte extrait en cache, feuilles, pages).
- Le contexte de la discussion (`attach::buildContext`) les retrouve par identifiant ; `AiTextParts` lit les gros textes par parties et l'outil `lire_piece_jointe` en rend le texte exact ; les classeurs sont relus depuis leur copie.
- Une mission peut donc **référencer une pièce jointe par son identifiant** sans la copier.

## 9. Les versions des données et leurs migrations

- Projets IHM : `hmi::store::kFormatVersion = 33`, migrations à la lecture (`load` / `parseProject`, `LoadReport`).
- Données IA : JSON avec un champ `format` quand il existe (paramètres `format: 1`, fichier d'apprentissage `kFormatRevision = 2`) ; à la lecture, **les champs inconnus sont gardés** (`json::unknownMembers` → `extra`) et réécrits tels quels : un fichier d'une version plus récente n'est pas abîmé par une plus ancienne ; les champs absents prennent leur valeur par défaut.
- Écriture atomique (`core::writeFileAtomic`, option `.bak`).

## 10. Ce que la 1.22.0 réutilise, et ce qu'elle ajoute

| Besoin de la mission | Réutilisé | Ajouté |
|---|---|---|
| Propositions, validation, aperçu, transaction, Undo | `Proposal`, `validate`, `Preview`, `apply` / `applyInBackground`, une commande par proposition | le lien proposition → mission et étapes (`Proposal::extra`) |
| Ce qui a réellement été fait | `plan::Report` (projet avant / après), `appliedState` | l'agrégation par étape et par mission |
| Dépendances du projet, audit final | `hmi::rel`, `hmi::audit::run` | les dépendances **entre étapes de la mission** (catégories + étapes citées) |
| Reprise | `serializeProject`, `identifyProject` | l'empreinte persistante du projet et par fichier |
| Persistance | `writeFileAtomic`, `unknownMembers`, dossier `IA/` | `IA/missions/<id>.json`, format versionné, `.bak` |
| Fonds et interface | `poll` au tick, copies du projet | — |
| Notifications | `notifs::post` (1.21.2) | les événements de mission |
| Recherche depuis une autre conversation | outils locaux (`LocalTool`) comme `chercher_conversations` | `chercher_missions`, `etat_mission` sur les fichiers de missions |
| Longueur des réponses, suite | 1.21.3 (`outputTokens`, `suite`, reprise en parties) | la suite **structurée** (étapes prévues) |
