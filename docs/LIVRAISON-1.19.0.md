# API Studio et IHM Studio 1.19.0 : l'IA qui conçoit et construit des projets complets

Livrée le 31/10/2026 en fin d'après-midi. Elle suit la 1.18.0 (`docs/LIVRAISON-1.18.0.md`), livrée le même jour.

Ta demande (le prompt « XPGAnalyser 1.19.0 : l'IA qui conçoit et construit des projets complets ») : qu'une demande comme « Crée une station de pompage complète avec deux pompes, une cuve, les équipements Modbus, les variables, les alarmes, les scripts, les symboles, les popups et la vue de supervision » devienne un plan d'opérations ordonné et vérifié, appliqué d'un bloc (un seul Ctrl+Z), expliqué par un plan d'action et un compte rendu fidèles ; que l'IA connaisse les relations du projet, sache le vérifier en entier, ranger ses adresses, réutiliser l'existant et le modifier.

En bref :
- **Le plan d'action** : chaque opération devient une étape (E1, E2…), rangée par ses **dépendances réelles** – jamais par l'ordre où l'IA l'a écrite –, vérifiée sur une **copie** du projet, puis appliquée en **une seule transaction** (un échec laisse le projet intact ; un Ctrl+Z reprend tout, Ctrl+Y le remet).
- **La carte de la conversation** montre les phases réelles, le plan dépliable avec l'état de chaque étape, « retirer » une étape facultative, puis le **compte rendu lu dans le projet** (créé, modifié, réutilisé, à faire, erreurs). Un clic sur une étape appliquée ouvre ce qu'elle a créé. Tout reste avec la conversation.
- **Réutiliser plutôt que recréer** : une variable, un type ou un équipement identique est réutilisé ; le même nom avec un autre type est refusé, jamais remplacé en silence.
- **Les relations du projet** : « Quels objets utilisent Niveau_Cuve_1 ? », « Si je supprime le type T_Pompe ? », « Quelles alarmes concernent la pompe 2 ? »… – répondues sans connexion à partir d'un graphe des relations, jamais inventées.
- **Vérifier tout le projet** (erreurs bloquantes, avertissements, informations, corrections proposées) et **optimiser les adresses** (plan avant / après) : rien ne change sans ta validation.
- **L'IA intégrée conçoit** une station de pompage, une ligne de production, un parc de cuves ou une régulation complets, et **modifie l'existant** (une troisième pompe sur le même modèle, dupliquer une cuve, une alarme sur toute une série, une mesure, remplacer un symbole, ranger une famille de vues).

## 1. Ce qui change

### Le plan d'action

Toute proposition (Claude, un service compatible, l'IA intégrée) passe par le même moteur :

| Étape du moteur | Ce qu'il fait |
| --- | --- |
| Les unités | Une opération du projet ; une vue neuve et ses objets ; les objets d'une vue existante. |
| Les dépendances | Lues dans les noms réellement écrits : le type avant ses variables, l'équipement avant les adresses, la variable avant l'alarme et le script qui la lisent, la vue avant ses objets. Deux fonctions qui s'appellent sont permises (le code est compilé une fois tout créé) ; deux types qui se contiennent sont une erreur bloquante. |
| La vérification | Unité par unité, sur une copie du projet : rien ne change pendant la planification. Chaque étape reçoit son état : validée, échouée (avec sa raison), ignorée (« dépend de E3 »), retirée. |
| L'application | Une seule commande ; une copie de secours restaure le projet si quelque chose échoue au dernier moment. |
| Le compte rendu | La différence entre le projet d'avant et celui d'après : une étape n'est « appliquée » que si son résultat est vraiment dans le projet. |

Une proposition peut créer **plusieurs vues** (la vue, son symbole, sa popup) : chaque objet va dans la vue qu'il nomme (le champ `vue`), sinon dans la dernière vue créée au-dessus, sinon dans la vue ouverte.

### Dans la conversation

- **Les phases** : Analyse › Planification (N étapes) › Vérification › Proposition (en attente de ta validation), puis Application › Compte rendu – seulement celles qui ont eu lieu.
- **Plan d'action — 24 étapes, 24 validées** : un clic le déplie ; une pastille par étape (validée, appliquée, échouée, ignorée, annulée par Ctrl+Z) ; « retirer » / « remettre » sur une étape facultative.
- **Le compte rendu**, après Appliquer : Créé, Modifié, Réutilisé (jamais compté comme créé), À faire (l'adresse IP définitive d'un équipement simulé, une détection à lancer), Erreurs, puis le résultat chiffré. Le journal d'activité garde le résultat et les étapes non appliquées.
- **Prévisualiser…** commence par l'aperçu global : l'architecture, puis une section par catégorie (types et variables, équipements, adresses et liaisons, scripts et fonctions, alarmes, vues et symboles, popups, ressources, dossiers) – chaque étape avec son action, son état, ses dépendances, ce qu'elle réutilise, ses avertissements.
- Une simple question, ou une seule opération, n'a pas de plan.

### Les questions sur le projet (IA intégrée, sans connexion)

| Question | Réponse |
| --- | --- |
| « Quels objets utilisent Niveau_Cuve_1 ? » | Les objets qui la lisent ou l'écrivent, et où. « Cette variable » : celle de l'objet choisi. |
| « Montre-moi les scripts qui utilisent Niveau_Cuve_1 » | Scripts et fonctions, avec la ligne. |
| « Quelles alarmes concernent la pompe 2 ? » | « La pompe 2 » se lit Pompe_2 quand elle existe. |
| « Quels équipements sont utilisés dans cette vue ? » | Par les variables que ses objets, actions et scripts lisent ou écrivent. |
| « Si je supprime le type T_Pompe ? » | Supprimé avec lui, touché, déplacé – une simulation. |
| « Quels objets sont orphelins ? » | Variables jamais lues ni écrites, popups jamais ouvertes, fonctions jamais appelées…, avec la raison. |
| « Vérifie tout mon projet et propose les corrections » | Générer, Compiler, les adresses qui se chevauchent, les orphelins, les dossiers ; les corrections d'adresse forment une proposition. |
| « Optimise les adresses des pompes » | L'emprise avant / après, les chevauchements, chaque déplacement et sa raison ; une proposition. |
| « Quelles sont les conventions du projet ? » | Préfixes, séries, taille des vues, plages d'adresses, priorités, dossiers – à garder si tu veux dans la mémoire de l'IA (une connaissance propre au projet). |

L'IA connectée reçoit aussi, quand ta demande nomme une variable, un type, une vue ou un équipement, ce qui l'utilise (lu dans le même graphe, borné à quelques lignes).

### Concevoir une installation, modifier l'existant (IA intégrée)

- « Crée une station de pompage complète avec deux pompes, une cuve, les équipements Modbus, les variables, les alarmes, les scripts, les symboles, les popups et la vue de supervision » : 54 opérations – `T_Pompe` (Marche, Defaut, Disponible, Mode_Auto, Consigne, Intensite, Heures_Fonctionnement), `T_Cuve`, Pompe_1, Pompe_2, Cuve_1, l'équipement `Station_Modbus` (simulé) et des adresses libres sans chevauchement, 4 alarmes, un script de démarrage et un script cyclique (compilés, jamais exécutés par l'Assistant), `Sym_Pompe`, `Pop_Pompe`, `Vue_Station`, le dossier `Station`.
- « Crée une ligne de production avec 3 convoyeurs », « Crée un parc de 4 cuves », « Crée une régulation avec 2 boucles ».
- « Ajoute une troisième pompe sur le même modèle », « Duplique la cuve 1 en cuve 4 », « Ajoute une alarme de niveau haut au-dessus de 90 sur toutes les cuves » (sans seuil, elle le demande), « Ajoute une mesure de pression sur la pompe 2 », « Remplace le symbole de toutes les pompes par Sym_Pompe_2 », « Déplace toutes les vues de production dans le dossier Production » : le nombre d'éléments concernés est dit d'abord.
- Ce qui existe est réutilisé (un `T_Pompe` identique, une variable du même type, l'équipement, les alarmes, le symbole, la popup) ; une vue de l'utilisateur n'est jamais réécrite (une vue neuve `Vue_Ligne_2` si `Vue_Ligne` existe déjà) ; ce que les opérations ne savent pas faire est dit (« À savoir »).

## 2. Les essais

**105 suites sur 105.** Les nouvelles et celles qui changent :
- `aiplan` (109 vérifications) – les scénarios du prompt, sur un vrai projet et sa pile Ctrl+Z :
  - **A, B, C, D** par les phrases elles-mêmes : la station du prompt (54 opérations, une commande, aucune adresse en conflit), une troisième pompe, la cuve 4, des alarmes sur la série, une mesure ; la ligne de production, la régulation ; tout repris par Ctrl+Z ;
  - **l'ordre** : une proposition écrite à l'envers (les objets avant leur vue, la variable avant son type, l'alarme avant sa variable) est rangée, validée et appliquée ; à l'endroit, le même résultat ;
  - **E** (erreurs volontaires : un type inconnu, un nom pris avec un autre type, un équipement absent, un cycle de types) : chaque étape dit son erreur, ce qui en dépend est ignoré, **rien ne s'applique** (le projet identique) ; « appliquer malgré les erreurs » n'applique que le valide ;
  - **F** : après Appliquer, Ctrl+Z rend **exactement** le projet d'avant (types, variables, équipement, adresses, alarme, script, vue, dossier) et Ctrl+Y celui d'après – seul le compteur d'identifiants ne recule pas, par conception (un identifiant n'est jamais réutilisé) ;
  - **H** : le compte rendu dit un type et une variable créés quand deux autres sont réutilisés, l'étape retirée « non exécutée » ; le plan et le compte rendu relus du fichier ;
  - **G** : 40 pompes, 400 variables, 40 alarmes, 120 objets – 204 opérations validées, appliquées et confirmées en 0,4 s ;
  - les questions au projet, la vérification (un chevauchement volontaire, sa correction proposée puis appliquée et reprise), les conventions, le contexte de l'IA connectée.
- `aidesign` (258) : les quatre installations sur un projet vide et sur l'exemple `bac/Armoire_Gaz` du dépôt (copié, jamais écrit) – 0 erreur, une commande, Ctrl+Z et Ctrl+Y exacts ; la réutilisation (la station deux fois, une troisième pompe, un T_Pompe différent, un Pompe_1 en BOOL) ; les six modifications ; 243 opérations (20 pompes, 10 cuves) validées en 0,1 s et appliquées en 0,2 s.
- `hmirelations` (149) : le graphe et chaque question sur un projet de station ; la vérification (3 erreurs, 3 avertissements, 6 informations attendus) ; l'optimisation des adresses ; un projet de 60 équipements, 600 variables, 30 vues vérifié en moins d'une seconde ; comparé aux usages que le logiciel trouve lui-même sur 135 projets du logiciel : aucun écart.
- `aioptions` (226) : les règles de la 1.18 suivent celles de la 1.19 (l'ordre libre, la réutilisation).
- `airegistre` (251) : la demande à l'IA connectée reste sous 41 Ko.
- `hmieditor` : 75 versions, 531 notes, 268 sujets d'aide (une nouvelle page : « Le plan d'action, le compte rendu et les questions sur le projet »).

## 3. Les captures

À ta demande, pas de passe de captures. L'installateur est vérifié sous Wine en silencieux.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.19.0.exe` : l'installateur ;
- `API-Studio-1.19.0-portable.zip` et `IHM-Studio-1.19.0-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.19.0.exe` : il met à jour la version installée au même endroit et garde tes données (le dossier `IA\`, tes conversations, ton apprentissage, tes clés API).
2. Windows SmartScreen peut avertir (installateur non signé) : « Informations complémentaires », puis « Exécuter quand même ».

Le format des projets ne change pas (33). Une conversation d'avant s'ouvre telle quelle (ses cartes n'ont pas de plan).

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.19.0.exe` (41,9 Mo, 43 952 772 octets) :
`7DD67097D254F69DB2DE1F591433560C9B08B10C4F01EB6223E31235682E2B29`

Les zips portables (167 fichiers chacun) :
- `API-Studio-1.19.0-portable.zip` (32,5 Mo) :
  `39D8C68E4BA475F1C48100620FD52817D0D0CAE79A6193B5ECB0E96563A71A59`
- `IHM-Studio-1.19.0-portable.zip` (32,5 Mo) :
  `34FE2BF716A1DF3BB5FB2A136D9F428F786EB1FD78FBEE6F24515678EE303394`

### Les vérifications sous Windows

**Les deux exe Windows** sont compilés sous Linux avec MinGW-w64 et lancés sous Wine : `--version` répond `API Studio 1.19.0` et `IHM Studio 1.19.0` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB).

**L'installateur** (Inno Setup 6.4.1 sous Wine) a été lancé en silencieux, pour tous les comptes, par-dessus la 1.18.0 livrée, avec **une clé API d'essai** déposée dans `%APPDATA%\XpgAnalyzer\IA\cles` : « Installation process succeeded ».
- La clé est **intacte** après l'installation (même empreinte avant et après) ; le journal dit « Clés API : 1 gardée (protégée par Windows ; la réinstallation n'y touche pas) ».
- Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été essayés ; le manifeste compte 167 fichiers ; les associations `.xihm` et `.xapi` sont là ; la copie de secours de la 1.18.0 est faite.

## 5. Les fichiers modifiés

Depuis la 1.18.0, 37 fichiers :
- `AiPlan` (nouveau) : le plan, l'ordonnancement, les états, le compte rendu ;
- `AiOperations` : le moteur par unités (copie, transaction, réutilisation, plusieurs vues, la portée d'une proposition) ;
- `AiDesign` (nouveau) : les installations et les modifications de l'existant ;
- `AiProjectAnswers` (nouveau) : les relations, vérifier, optimiser, les conventions ;
- `HmiRelations` et `HmiAudit` (nouveaux) : le graphe des relations, la vérification globale, les adresses ;
- `AiLocal` : les phrases ; `AiAssistant` : le compte rendu à l'application, le plan d'une carte, retirer une étape ; `AiPanel`, `AiPreviewDialog`, `AiWorkspace` : la carte, l'aperçu par catégories, ouvrir un élément ; `AiPrompt`, `AiContext` : les consignes et le contexte de l'IA connectée ;
- la version, les notes, l'aide ; le projet Visual Studio ;
- les essais : `ai_plan_test`, `ai_design_test`, `hmi_relations_test` (nouveaux), `ai_options_test`, `ai_registre_test`, `hmi_editor_test`.

## 6. Les limites

- **Pas d'instance de symbole posée par l'Assistant** : le moteur refuse ce genre d'objet (il ne vient pas de la bibliothèque). `Sym_Pompe` est créé, mais `Vue_Station` utilise des objets du synoptique liés aux variables ; la réponse le dit.
- **Les commandes d'une popup ne se lient pas à ses paramètres** (seules les formules les lisent) : `Pop_Pompe` affiche la pompe et se ferme, sans commandes Marche / Mode / Consigne.
- **L'existant ne se modifie pas en profondeur** : pas de membre ajouté à un type qui existe (une mesure devient une variable à part), un script ou une alarme existants ne sont pas réécrits (une troisième pompe n'entre pas dans `Station_Cycle` : c'est dit), une vue n'est pas agrandie.
- Les heures de fonctionnement ne sont pas comptées par le script (laissées à l'appareil).
- Le graphe suit les noms écrits ; un nom calculé en marche (une cible `=expression`) n'y est pas suivi – la réponse le rappelle.
- L'optimisation ne déplace pas une variable à places réparties, un bit d'un mot, une variable sur deux tables, une adresse par formule (chaque cas est dit).
- Les conventions du projet sont lues et montrées, pas écrites seules dans la mémoire (c'est toi qui les ajoutes).
- La carte et l'aperçu n'ont pas été capturés à l'écran (à ta demande) ; rien n'a été essayé sur un vrai Windows.

## 7. Ce qui a été optimisé pour livrer plus tôt

- **Trois agents en parallèle** de mon travail sur le moteur : le graphe des relations et la vérification globale, le concepteur d'installations, et l'analyse de l'architecture pour la 1.20.0 ; chacun dans ses propres fichiers, relu et compilé avant d'être commité.
- **Le moteur de la 1.18 réutilisé**, pas réécrit : le plan découpe une proposition en unités et passe chacune au même moteur de vérification et d'application.
- **Pas de captures** ; l'installateur vérifié en silencieux sous Wine.
- **Une compilation Windows incrémentale** (53 étapes au lieu de toutes) et les scripts de fabrication préparés pendant que les agents travaillaient.
- **Des commits par tranche**, chacun poussé dès que ses essais passent.

## 8. La suite : la 1.20.0

Ton prompt 1.20.0 (surveillance des alarmes, dossiers d'incident immuables, analyse IA en arrière-plan) est en file. L'analyse de l'architecture est faite : les points d'accroche existent (l'avis d'alarme du moteur, l'historique, la capture d'écran, les écritures atomiques, l'empreinte SHA-256, les fournisseurs IA en arrière-plan) et le graphe des relations de la 1.19 sert à remonter la chaîne Alarme → Variable → Équipement → Adresse → Script → Vue.
