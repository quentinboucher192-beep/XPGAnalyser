# API Studio et IHM Studio 1.20.0 : l'enregistrement intelligent et l'analyse IA des incidents

Livrée le 31/10/2026 @HEURE@. Elle suit la 1.19.0 (`docs/LIVRAISON-1.19.0.md`), livrée le même jour en fin d'après-midi.

Ta demande (le prompt « XPGAnalyser 1.20.0 — Enregistrement intelligent et analyse IA des incidents en arrière-plan ») : qu'une alarme qui apparaît ne soit plus seulement notée, mais qu'IHM Studio comprenne à quoi elle est reliée dans le projet, retrouve ce qui peut l'expliquer, enregistre l'écran et le contexte du moment dans un dossier immuable, le fasse analyser par l'IA en arrière-plan, et te laisse le consulter, le commenter et le valider.

En bref :
- **Un dossier d'incident à chaque défaut, sans geste** : en simulation IHM et au poste d'exploitation, le front d'une alarme (apparition, réapparition) ou une perte de communication crée aussitôt un dossier dans `ihm/ressources/enregistrements/en attente/` – jamais un incident à chaque cycle.
- **L'écran** : l'image de la vue juste après le cycle de l'alarme (`capture.jpg`, dite image) et une **vraie vidéo** Motion-JPEG des secondes d'avant et d'après (`video.avi`, un tampon circulaire), lue par l'objet Vidéo de l'IHM.
- **Le contexte copié** : l'alarme telle qu'elle était, la chaîne de dépendances (condition → variables → structure → équipement et adresse → scripts et fonctions → vues et objets → autres alarmes, puis un niveau plus loin), les valeurs du moment (une valeur absente est dite absente), les alarmes actives et liées, la chronologie, les incidents précédents de la même alarme.
- **La fiche de diagnostic** en dix sections et des **pistes classées** avec leur certitude (fait vérifié, relation indirecte, hypothèse) – « peut expliquer », jamais « a causé » ; l'ordre d'apparition n'est pas une preuve.
- **L'analyse par l'IA choisie, en arrière-plan** : un contexte ciblé (l'incident, pas tout le projet), la réponse ajoutée au dossier sans jamais réécrire l'analyse initiale ; IA indisponible : l'incident est gardé, l'analyse en attente.
- **Immuable, empreint, validé** : chaque fichier a son empreinte SHA-256 ; valider copie le dossier dans `valides/`, vérifie chaque empreinte, puis seulement retire l'original. Reprise après un arrêt brutal, disque plein dit et réessayé.
- **Le volet Incidents** et **l'assistant de diagnostic** dans l'Assistant IA.
- **Trois défauts que tu as vus, corrigés avant la livraison** : l'IA adresse les membres de structures à toute profondeur ; les fichiers texte joints ne sont plus coupés à 200 Ko ; une grosse proposition ne gèle plus l'appli (voir 1 bis).

## 1 bis. Corrigé après tes essais

### L'IA adresse les membres de structures (« ConfigArmoireModifications.ConfPoids : variable IHM introuvable »)

`adresser` acceptait seulement une variable entière : un chemin `Variable.Membre` était cherché comme un nom de variable, donc refusé (tes 16 erreurs laissées de côté). Maintenant, à toute profondeur (`Config.ConfPoids`, `Config.Sous.Valeur`, `Tab[2].Pos`) : la variable doit exister ; le membre est cherché dans son type déplié – jamais inventé : un membre inconnu est refusé en listant les membres du niveau où le chemin s'arrête (« les membres de Config.Sous : Valeur, Seuil ») ; son adresse va dans les adresses corrigées de la variable (comme l'écran des variables), remplace celle du même chemin ; tous les membres suivent l'équipement de la variable (un autre équipement est refusé) ; une variable pas encore liée l'est, et c'est dit. Les consignes de l'IA le disent.

### Les fichiers texte joints ne sont plus coupés à 200 Ko

Avant d'envoyer, la limite est vérifiée : la **fenêtre de contexte du modèle** (lue dans la liste des modèles, ou donnée dans les Paramètres ; inconnue : 200 000 jetons chez Anthropic, 128 000 en compatible OpenAI, 32 768 pour un modèle sur ce PC) moins la réponse attendue, les consignes et les messages, avec une marge. Le fichier part **entier** tant qu'il tient, découpé en **parties** aux fins de ligne (« partie 2/5, lignes 1201 à 2400 »). Au-delà, les parties qui ne tiennent pas sont **lues à part** : une demande par partie (sans outil, avec ta question), qui recopie fidèlement ce qui sert ; l'extrait part à la place de la partie (« lue à part, un extrait, pas le texte entier »). Le panneau dit « Lecture de mesures.csv : partie 3/9… » pendant ce temps – sur le fil du fournisseur, jamais sur celui de l'interface. Rien n'est coupé en silence : ce qui n'a pas pu être lu (au-delà de 40 lectures, une lecture en échec) est dit à l'IA (qui ne doit pas le deviner) et à toi. Les jetons des lectures s'ajoutent à la consommation ; les extraits sont gardés le temps de la séance (le tour suivant ne relit pas). Anthropic et compatibles OpenAI ; un CSV d'Excel est relu correctement dans les deux.

### Une grosse proposition ne gèle plus l'appli

Mesuré sur 600 opérations (dont 400 adresses, dans un projet de 1 500 variables) : chaque opération était validée dans sa propre unité, qui recopiait tout le projet et relisait toutes les variables pour chaque adresse. Maintenant la prochaine adresse libre de chaque équipement est lue une fois puis avancée (les mêmes adresses qu'avant, vérifié une à une), les adresses prises sont indexées une fois : **valider 408 → 80 ms, aperçu 393 → 62 ms, appliquer 787 → 150 ms** (version de mise au point, plus lente que la tienne). Et dans le panneau :
- à partir de **40 opérations**, la carte se vérifie **sur un fil de fond** (une copie du projet, les noms de l'automate et les pièces jointes figés) : elle dit « Vérification… » avec une **barre d'avancement**, Appliquer revient à la fin, tu continues à travailler ;
- l'empreinte de la carte n'est plus recalculée à chaque affichage (13 ms pour 600 opérations, plusieurs fois par seconde) : seulement quand le projet change, ou chaque seconde ;
- une carte appliquée ou refusée ne revalide plus rien.

## 1. Ce qui change

### La surveillance (ce qui déclenche un incident)

Le détecteur suit les **fronts** que le moteur de l'IHM annonce (le même avis que les notifications) : une apparition fait un incident ; une réapparition après disparition aussi (dans le délai anti-rebond, 5 s par défaut : le même) ; le **maintien** est noté une fois dans la chronologie (après 30 s) ; la disparition et l'acquittement aussi, avec la durée. Une perte de communication d'un équipement fait un incident (réglable), son retour est noté. Plusieurs alarmes au même instant : un incident chacune, et chacun voit les autres. Les alarmes surveillées et ignorées se règlent (sans casse, `*` permis). Le détecteur ne lit jamais le projet : rien de lourd à chaque cycle.

### Le contexte du défaut, copié

Au cycle suivant le front, sur le fil de l'interface et en quelques millisecondes (8 ms au pire sur un projet de 3 000 variables et 1 500 alarmes) :
- **l'alarme telle qu'elle était** : identifiant, nom, description, condition, message, groupe, catégorie, consigne, priorité, délai, acquittement ;
- **la chaîne de dépendances**, lue dans le graphe des relations de la 1.19 (gardé tant que le projet en marche ne change pas) : les variables lues par la condition (un membre de structure compris), leur structure, leur équipement et leur adresse, les scripts et fonctions qui les écrivent ou les lisent, les objets et vues qui les montrent ; puis un niveau plus loin, ce que lisent les traitements qui écrivent la condition. Chaque maillon est un **fait vérifié** ou une **relation indirecte** ; bornée à 120 maillons ;
- **les valeurs** de la condition au moment du défaut et leur qualité ; une valeur que l'IHM n'avait pas est dite « non disponible » ;
- **les alarmes liées** : dépendance technique commune (même variable ; même équipement parmi les alarmes présentes), apparue au même moment (±1 s), potentiellement secondaire (après une perte de communication de l'équipement), indépendante ;
- **les pistes**, classées : un défaut de communication de l'équipement (relation indirecte), la mesure ou le capteur, un traitement qui écrit la condition, une cause commune avec une alarme apparue avant – chacune avec ce qui l'appuie et ce qu'il faut vérifier ; « peut expliquer », jamais « a causé » ;
- la vue affichée et ses objets concernés, l'état des équipements, la chronologie (120 s), les incidents précédents de la même alarme.

### Le dossier d'incident

`ihm/ressources/enregistrements/en attente/<date>-<heure>-<alarme>-<id>/` :
- `capture.jpg` : la vue dessinée juste après le cycle de l'alarme – **une image**, dite comme telle ;
- `video.avi` + `video.json` : une **vidéo Motion-JPEG** des secondes d'avant et d'après (10 s et 5 s, 2 images par seconde par défaut) prise dans un **tampon circulaire** : les pixels sont copiés sur le fil de l'interface, réduits et codés en JPEG sur le fil d'écriture, bornés en mémoire (48 Mo). La vidéo se lit avec le décodeur des objets Vidéo de l'IHM ;
- `incident.json` : le contexte copié (format versionné) ;
- `analyses/000-initiale.json` : la fiche de diagnostic, faite sur ce PC ; puis une analyse par fichier ;
- `commentaires/NNN.json` ;
- `empreintes.sha256` : l'empreinte de chaque fichier, au format de `sha256sum` (vérifiable hors du logiciel).

La création passe par un dossier `.partiel` renommé à la fin ; tout est écrit sur un fil à part. Disque plein ou erreur d'écriture : c'est dit dans le journal de l'IHM, l'incident est gardé en mémoire et réessayé toutes les 30 s (4 essais).

### La fiche de diagnostic

Dix sections : Résumé, Alarme déclenchée, Origine des données, Chaîne de dépendances, Autres alarmes, Chronologie (« L'ordre d'apparition n'est pas une preuve de causalité »), Analyse technique, Causes potentielles, Éléments à vérifier, Niveau de certitude (faits, relations indirectes, hypothèses, ce qui manquait).

### L'analyse par l'IA, en arrière-plan

Si l'Assistant utilise une IA connectée (Claude ou un service compatible), elle reçoit, sur son propre fil, un **contexte ciblé** (la fiche de l'incident, bornée à 12 Ko, et les analyses déjà faites – pas le projet) avec une consigne : ne rien inventer, distinguer faits / relations indirectes / hypothèses, ne jamais conclure à une cause, dire ce qui manque, ne jamais donner d'ordre de commande. Sa réponse devient `analyses/001-....json` avec sa source, sa date et sa certitude ; l'analyse initiale n'est jamais réécrite. La même demande au même modèle n'est pas renvoyée (cache sur l'empreinte du contexte). IA indisponible (pas de clé, pas de modèle, réseau, erreur du service) : l'échec est noté dans le dossier, l'incident est gardé, l'analyse « en attente » se relance plus tard. La surveillance n'attend jamais le modèle.

### Valider, immuable

**Valider** vérifie d'abord chaque empreinte (un incident altéré n'est pas validé), copie le dossier dans `valides/` (un nom jamais écrasé), revérifie chaque empreinte de la copie, ajoute `validation.json` (date, auteur, note, empreinte de la liste), puis seulement retire l'original. Au démarrage : un `.partiel` complet est terminé, un incomplet mis à part (`.<nom>.incomplet`), une copie de validation interrompue retirée, un original resté après une validation finie retiré. Rien n'est jamais recalculé depuis le projet du moment : une alarme renommée ou supprimée, un équipement retiré, une vue modifiée ne changent pas un incident.

### Le volet Incidents

IHM › Ressources › **Incidents [N]** (N : en attente), Affichage › Incidents, Ctrl+K. Deux onglets (En attente, Validés) ; chaque ligne : miniature, nom, date, alarme, équipement, vue, synthèse, certitude, état de l'analyse, validation. À droite, la fiche en groupes dépliables : la capture, la vidéo (lue sur place, ou avec le lecteur du système), les dix sections, les **dépendances cliquables** (un élément qui n'existe plus le dit, la fiche garde son nom d'origine), les analyses et « Comparer l'initiale et la dernière », les commentaires, la validation, l'intégrité (« intact : N fichiers vérifiés »). Gestes : Consulter, Analyse complète, Dépendances, Commenter, Nouvelle analyse, Valider, Supprimer (en attente seulement), Ouvrir le dossier, Réglages de la surveillance (gardés dans `reglages-incidents.json` à côté des incidents). Les miniatures sont décodées sur un fil à part, une fois.

### L'assistant de diagnostic

L'IA intégrée répond, sans connexion, à partir des dossiers : « Pourquoi cette alarme est-elle apparue ? », « Fais-moi un diagnostic complet de cet incident », « Retrouve toute la chaîne de dépendances de cette alarme », « Quelles variables peuvent être à l'origine de ce défaut ? », « Pourquoi cinq alarmes sont-elles apparues en même temps ? », « Est-ce que cette alarme est déjà apparue dans le passé ? », « Compare les trois derniers incidents de la pompe 2 », « Quel équipement est commun à ces différents défauts ? », « Montre-moi les scripts qui peuvent influencer cette variable », « Est-ce que les défauts de communication expliquent les alarmes de mesure ? ». Une demande de création n'en est jamais une. L'IA connectée reçoit un résumé des trois incidents concernés (6 Ko au plus) seulement quand la demande parle d'incident.

XPGAnalyser aide à comprendre un événement : ce n'est pas un système de sécurité ni une surveillance déterministe de la logique de l'automate.

## 2. Les essais

**107 suites sur 107.** Les nouvelles et celles qui changent :
- les trois correctifs : `aioptions` (236) – un membre imbriqué adressé (`Config.Sous.Valeur`), réadressé sans doublon, un membre inconnu refusé avec la liste du bon niveau, un autre équipement refusé, une racine inconnue refusée ; 600 opérations validées, prévisualisées et appliquées, les 100 adresses automatiques identiques à l'ancien calcul ; `aireseau` (217) – le découpage aux fins de ligne, la fenêtre (1 000 jetons : rien ; 200 000 : plus de 300 Ko), 420 Ko envoyés entiers en 5 parties à un modèle d'un million de jetons (Anthropic et compatible OpenAI), à une petite fenêtre les parties qui tiennent et le reste dit, les lectures préalables sur le faux réseau (3 demandes puis la réponse, l'état « Lecture », les jetons, la note), les extraits repris au tour suivant sans relecture, des lectures en échec dites ; une proposition de 80 opérations vérifiée sur un fil de fond (« Vérification… » dessiné, puis Appliquer disponible), une petite vérifiée tout de suite ;
- `hmiincident` (152 vérifications) – les essais A à G de ta demande :
  - **A** : apparition, maintien (une fois après 30 s), disparition et sa durée, réapparition, anti-rebond, cinq alarmes simultanées, filtres surveillées / ignorées, perte et retour de communication, surveillance arrêtée ;
  - **B** : variable directe, membre d'une structure, structure, équipement, adresse, script qui écrit, fonction qui lit, objet et vue, deux alarmes sur une variable, dépendance indirecte ;
  - **C** : chaîne jusqu'à l'équipement, alarme potentiellement secondaire après une perte de communication, équipement commun, simultanée sans lien, piste la mieux appuyée d'abord, les dix sections, « n'est pas une preuve de causalité », « peut expliquer » et jamais « a causé », valeurs indisponibles signalées, aucun maillon inventé (chacun existe dans le projet), une alarme hors projet sans chaîne inventée, contexte IA borné ;
  - **D** : création dans « en attente », capture dite image, vraie vidéo AVI Motion-JPEG relue par le lecteur de l'application, horodatage, version du projet, vue et objets visibles ; l'hôte complet (tampon circulaire, capture du défaut demandée aussitôt, vidéo des secondes d'avant et d'après, valeurs du moment) ;
  - **E** : projet modifié, alarme supprimée, équipement renommé, vue refaite – `incident.json` identique octet pour octet ; vidéo et analyses jamais réécrites ; une capture altérée détectée ;
  - **F** : validation, déplacement vers `valides/`, conservation de tout, empreintes vérifiées, jamais d'écrasement, reprise des `.partiel` (complet, incomplet, copie interrompue, doublon après validation) ;
  - **G** : IA distante indisponible (incident gardé, analyse en attente), puis revenue (analyse ajoutée, certitude lue), cache, aucune IA prête, disque plein (dit, gardé, réécrit quand il revient), dossier impossible, quatre incidents simultanés, projet de 3 000 variables / 1 500 alarmes (graphe 90 ms, contexte 8 ms au pire), 150 000 avis d'alarme en 0,6 s.
- `aiincidents` (408) : chaque question de diagnostic sur des incidents construits à la main ; rien d'inventé (chaque nom cité vient des données), « non disponible » jamais remplacé, simultanées, équipement commun, communication (« peut s'expliquer », jamais « à cause »), aucun incident, bloc distant ≤ 6 Ko avec 200 gros incidents.
- `aiplan` (118) : les questions d'incident passent par la vraie compréhension de l'IA intégrée et par le contexte de l'IA connectée.
- `hmieditor` : 76 versions, 539 notes, 244 sujets de l'IHM (la rubrique « Les incidents : enregistrement et analyse »), Ressources a deux enfants.
- `api2` : le menu Affichage › Incidents.

## 3. Les captures

À ta demande, pas de passe de captures. L'installateur est vérifié sous Wine en silencieux.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.20.0.exe` : l'installateur ;
- `API-Studio-1.20.0-portable.zip` et `IHM-Studio-1.20.0-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.20.0.exe` : il met à jour la version installée au même endroit et garde tes données (le dossier `IA\`, tes conversations, ton apprentissage, tes clés API).
2. Windows SmartScreen peut avertir (installateur non signé) : « Informations complémentaires », puis « Exécuter quand même ».

Le format des projets ne change pas (33). Les incidents vivent à côté du projet, dans ses ressources (`ihm/ressources/enregistrements/en attente/` et `valides/`) ; les réglages de la surveillance dans `ihm/ressources/enregistrements/reglages-incidents.json`. Un projet sans ces dossiers s'ouvre comme avant ; la surveillance est active par défaut (elle se règle dans le volet).

@EMPREINTES@

## 5. Les fichiers modifiés

Depuis la 1.19.0, 45 fichiers (8 830 lignes ajoutées) :
- `HmiIncident` (nouveau) : le détecteur, le contexte du défaut, les pistes, la fiche ;
- `AiIncident` (nouveau) : le dossier d'incident, son JSON, les empreintes, la validation, la reprise, l'analyse (consigne, contexte, cache) ;
- `HmiIncidentHost` (nouveau) : la surveillance dans l'application (le tampon d'images, le fil d'écriture, l'analyse en arrière-plan, les réglages) ;
- `HmiIncidentsPane` et `IncidentsWorkspace` (nouveaux) : le volet et ses dialogues ;
- `AiIncidentAnswers` (nouveau) : l'assistant de diagnostic ;
- `App` (le branchement : la vue en marche, l'image, l'IA choisie), `HmiWorkspace` et `StationScreen` (les avis d'alarme, le journal), `ViewModels`, `ExplorerMenus`, `GoToWorkspace`, `TopBar`, `ApiWorkspace`, `Screens` (le nœud, le menu, Ctrl+K) ;
- `AiLocal`, `AiContext`, `AiPrompt`, `AiAssistant`, `AiWorkspace` (l'Assistant lit les incidents) ;
- la version, les notes, l'aide, F1 ; le projet Visual Studio ;
- les essais : `hmi_incident_test`, `ai_incident_answers_test` (nouveaux), `ai_plan_test`, `api2_test`, `hmi_editor_test`.

Les trois correctifs : `AiOperations` (adresser un membre, le curseur d'adresse libre et l'index partagés par la proposition, l'empreinte indexée, l'environnement figé, l'avancement), `AiPrompt` (la consigne d'adresser), `AiPlan` (un changement d'adresse de membre dans le compte rendu), `HmiEquipment` (le curseur d'adresse libre), `HmiCommands` (la révision du document), `AiTextParts` (nouveau : le découpage, le budget, les lectures préalables), `AiAnthropic`, `AiOpenAI`, `AiProvider`, `AiAssistant` (les lectures, la note, les jetons), `AiPanel` (la carte vérifiée en fond), `AiWorkspace` (la liste des noms de l'automate) ; les essais `ai_options_test`, `ai_reseau_test`.

## 6. Les limites

- **La vidéo** a la cadence réglée (2 images par seconde par défaut, 10 au plus) : c'est une vidéo Motion-JPEG des secondes autour du défaut, pas un film fluide. Elle n'existe que si la vue en marche était affichée ; sinon, le dossier le dit.
- **Les valeurs** sont celles de la condition, lues au cycle suivant le front (aucun cycle du moteur entre les deux) ; les variables plus loin dans la chaîne ne sont pas relevées.
- **La chaîne** suit les noms écrits (comme le graphe de la 1.19) ; un nom calculé en marche n'y est pas suivi. Les alarmes d'objet (hors du projet) ont leur nom et leur message, pas de condition copiée.
- **Les pistes** sont des règles, pas un raisonnement : l'analyse d'une IA connectée les complète.
- **Les liens de l'Assistant** : les éléments sont nommés en `code`, mais le panneau ne les rend pas encore cliquables (les dépendances le sont dans le volet).
- **Un commentaire** se saisit sur une ligne.
- **Les fichiers joints découpés** : une partie lue à part arrive en EXTRAIT (ce qui sert à la question posée), pas en texte entier ; au-delà de 40 parties en trop, le reste n'est pas lu (c'est dit). Chaque lecture est une demande de plus (des jetons) ; la fenêtre est estimée prudemment (2,5 octets par jeton) – un texte très dense peut encore dépasser : le service le dit, et Paramètres › Contexte permet de la réduire.
- **Appliquer** une grosse proposition se fait encore sur le fil de l'interface (une seule commande annulable par Ctrl+Z) : cinq fois plus rapide qu'avant, mais sans barre. **Prévisualiser** aussi. La vérification de fond de la carte ne démarre qu'à partir de 40 opérations, et pas quand l'automate relié ne donne pas sa liste de noms (elle se fait alors comme avant).
- Le volet et l'Assistant n'ont pas été capturés à l'écran (à ta demande) ; rien n'a été essayé sur un vrai Windows (seulement sous Wine, en silencieux).

## 7. Ce qui a été optimisé pour livrer plus tôt

- **Deux agents en parallèle** de mon travail : le volet Incidents et l'assistant de diagnostic, chacun dans ses fichiers, contre des en-têtes que j'avais écrits d'abord (le stockage, l'hôte) ; relus, construits et essayés avant d'être commités.
- **Rien de recréé** : l'avis d'alarme du moteur, le graphe des relations et l'audit de la 1.19, l'écriture atomique, SHA-256, l'encodeur JPEG, l'écrivain et le lecteur d'AVI Motion-JPEG, les fournisseurs IA et leur fil, le décodeur des vignettes.
- **La 1.19.0 livrée pendant que la 1.20 démarrait** (le noyau des incidents écrit pendant la compilation Windows de la 1.19).
- **Pas de captures** ; l'installateur vérifié en silencieux sous Wine ; une compilation Windows incrémentale.
- **Des commits par tranche**, poussés dès que leurs essais passent.
