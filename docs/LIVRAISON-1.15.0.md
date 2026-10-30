# API Studio et IHM Studio 1.15.0 : l'Assistant IA branché sur une vraie IA

Livrée dans la nuit du 30 au 31/10/2026. Elle suit la 1.14.2 (`docs/LIVRAISON-1.14.2.md`), qui livrait la maquette de l'Assistant et sa mémoire.

Tu as validé la maquette (« continue ») : cette version fait les **phases 3 à 9** de la demande. L'Assistant se branche sur une vraie IA, le fournisseur **Anthropic (Claude)**. Le logiciel est prêt pour d'autres fournisseurs : ils passeront par la même interface.

En bref :
- **Rien n'est écrit en dur** : ni la clé, ni le modèle. La case du modèle est vide au départ ; « Lister les modèles » donne ceux que ta clé peut utiliser.
- **L'IA ne touche jamais au projet elle-même.** Elle ne peut qu'appeler un seul outil, « proposer des opérations ». Le logiciel vérifie chaque opération (le genre de l'objet, ses propriétés, les variables) avant de te la montrer. Tu appliques, et un seul Ctrl+Z reprend tout.
- **L'IA connaît le logiciel tel qu'il est.** Avec chaque demande, elle reçoit les 116 genres d'objets tirés des tables du code, le contexte de la vue (selon tes réglages de confidentialité) et les connaissances actives de la mémoire.
- **Le réseau tourne à part** : l'éditeur ne gèle jamais. Arrêter coupe la connexion tout de suite.
- **La démonstration reste le fournisseur par défaut.** Tant que tu ne choisis pas Anthropic, rien ne quitte le PC.

**Ce qui n'a pas pu être fait ici : un essai contre la vraie API.** Je n'ai pas de clé dans cet environnement. Tout a été essayé avec un **faux fournisseur local** qui parle exactement le même protocole (mêmes flux, mêmes erreurs), y compris l'exe Windows sous Wine. Le premier essai avec ta clé est donc le vrai premier essai : le § 2 dit comment le faire en deux minutes.

## 1. Ce qui change

### Les paramètres de l'Assistant

| Réglage | Ce qu'il fait |
|---|---|
| Fournisseur | **Démonstration** (sans IA, rien ne quitte le PC) ou **Anthropic (Claude)**. Les autres de la liste restent « à venir ». |
| Modèle | Une case libre, **vide au départ**, avec une indication grise (« ex. claude-opus-5-5 »). **Lister les modèles** demande au fournisseur ceux que ta clé peut utiliser, sans consommer de jetons ; un clic en choisit un. |
| Effort | Auto (celui du modèle), Bas, Moyen, Haut : combien l'IA réfléchit avant de répondre. |
| Adresse | Vide : celle d'Anthropic. Sinon, le **relais de ton entreprise** (https://…). `http://` n'est accepté que vers ce PC (127.0.0.1). |
| Délai | Les secondes sans rien recevoir avant de dire « Délai dépassé » (60 par défaut). |
| Clé API | Masquée ; Afficher, Effacer. **Mémoriser la clé (protégée par Windows)** la garde d'une séance à l'autre. |
| Tester la connexion | Demande la liste des modèles avec ces réglages : « Connexion réussie : 12 modèles », ou l'erreur en clair. |

Viennent ensuite, comme avant : le mode par défaut, la conservation de l'historique, la confidentialité (la vue active, la sélection, les variables, les pièces jointes : chacune se décoche) et **Mémoire et apprentissage…**.

### L'en-tête du panneau dit l'état réel

- « Démonstration – aucune IA connectée » ;
- « À configurer : choisis un modèle et une clé » (un clic ouvre les paramètres) ;
- « Anthropic – *le modèle* » (« (relais) » avec une adresse d'entreprise).

### Une demande, de bout en bout

1. **Avant le premier envoi** d'une conversation, une ligne sous la saisie dit **ce qui part** : ta demande, la vue et son nombre d'objets, la sélection, le nombre de variables, les pièces jointes, selon tes réglages.
2. La demande part sur un **fil de travail** : l'éditeur reste libre.
3. La réponse arrive par étapes : **En attente…**, **Réflexion…**, **Écriture…** (le texte au fil de l'eau), puis **Préparation des opérations…** quand l'IA écrit sa proposition.
4. La proposition est lue **strictement** : une opération inconnue, un champ en trop, un JSON coupé, et elle est refusée à la lecture. Sinon, elle passe par la **même validation, la même carte « Opérations proposées », le même aperçu et le même Ctrl+Z** que la démonstration.
5. **L'IA sait ce que sont devenues ses propositions** : appliquée, refusée, en attente, annulée par Ctrl+Z, ou refusée par la validation (avec les raisons, et les vraies propriétés des genres en cause). Sa réponse suivante en tient compte : elle corrige d'elle-même.
6. La conversation se garde avec ses appels d'outil : rouverte après un redémarrage, elle continue.

### Ce que l'IA sait

- **Les consignes**, en français et au tutoiement : n'inventer ni genre, ni propriété, ni variable ; demander quand la demande est ambiguë ; placer les objets dans la vue, alignés et espacés.
- **Le registre des genres** : les 116 genres d'objets (clé, libellé, famille de la bibliothèque), tirés des tables d'IHM Studio. Pour les genres déjà dans la vue et ceux que ta demande cite (« pompe », « cuve », « vanne »…), le détail de leurs propriétés : la valeur par défaut, le rôle (un nombre, une couleur, une formule…), les mots d'une liste à choix.
- **Le contexte** : le projet, la vue ouverte (taille, fond, calques), ses objets, la sélection de gauche à droite, les variables (nom, type, description), celles de l'automate relié, les noms des autres vues. **La valeur d'une variable ne part jamais.** Chaque case de confidentialité décochée retire sa partie, et l'IA le sait : elle demande au lieu de deviner.
- **Les connaissances actives** du centre de mémoire. Une connaissance désactivée, ou d'un autre projet, ne part pas.
- Une grande vue se résume : la sélection et ses voisins sont détaillés, les autres seulement nommés. Une demande ordinaire pèse de 25 à 35 Ko.
- Les consignes et le registre, qui changent peu, partent en premier et **sont mis en cache chez le fournisseur** : les demandes suivantes coûtent moins cher.

### Ce qu'elle peut proposer

Créer, Modifier (dont déplacer et redimensionner : un groupe garde ses enfants), Associer une variable qui existe, Aligner, Répartir, **Dupliquer** (avec les enfants, décalés, les copies nommées), Supprimer (plusieurs : une confirmation), et **Code brut** (un objet riche, un bouton et ses actions, au format de l'onglet Code brut de l'inspecteur). Toute la proposition s'applique en une seule étape d'annulation.

### Ce que le logiciel vérifie avant de te montrer quoi que ce soit

- **Le genre** existe et la bibliothèque le propose ; un genre inconnu est refusé, avec les genres proches.
- **Les propriétés** sont celles du genre ; chaque valeur convient à son rôle (un nombre, TRUE ou FALSE, une couleur #RRGGBB, un mot de la liste de l'inspecteur, une durée).
- **Les variables** existent : celle qu'on associe, celles qu'une formule lit, celles d'un texte à trous (`{Niveau}`). Une variable introuvable n'est jamais créée en silence : l'aperçu propose les plus proches, ou « Créer… ».
- **Les objets cités** existent, ou sont créés plus haut dans la même proposition. Un nom déjà pris : l'objet neuf est renommé, et l'aperçu le dit.
- Un objet qui déborde de la vue est signalé.
- **Le code brut** ne porte ni script, ni appel de fonction, ni calcul Maths, ni fonction redéfinie.

### Les erreurs, dites en clair

Clé refusée (401), accès refusé (403), modèle introuvable (404), demande trop lourde (413), pas de réseau, délai dépassé : la réponse le dit en rouge, en français, et **Réessayer** renvoie la même demande. Un service surchargé (529) ou trop sollicité (429) est **réessayé seul deux fois**, avec une attente croissante (et l'attente que le service demande) ; l'indicateur le dit : « Nouvelle tentative (1/2) dans 2 s… ». Une réponse coupée (trop longue) ou déclinée par l'IA ne propose rien, et le dit.

### Les pièces jointes

| Pièce | Ce que l'IA en reçoit |
|---|---|
| PNG, JPG | L'image (5 Mo au plus). |
| PDF | Le document (20 Mo au plus). |
| TXT, CSV, JSON, XML | Le texte, avec son nom (les 200 premiers Ko). |
| XLSX, .xihm, .xapi | Seulement son nom : ces fichiers ne sont pas lus par l'IA dans cette version, et le panneau le dit. |

### La clé API

- Elle n'est **jamais écrite en clair** : ni dans les paramètres, ni dans un journal, ni dans la conversation.
- Sous Windows, « Mémoriser la clé » la **chiffre par Windows (DPAPI)**, liée à ta session, dans `IA\cle.bin`. Un autre compte, ou le fichier copié sur un autre PC, ne la relit pas.
- Sous Linux, elle est gardée le temps de la séance seulement (et c'est dit).
- **Pour une application distribuée**, la bonne architecture est le **relais d'entreprise** : un serveur interne détient la clé, IHM Studio ne parle qu'à lui (Paramètres › Adresse), et la case de la clé reste vide. Aucune clé n'est jamais intégrée à l'exe.

### La connexion

Sous Windows, par **WinHTTP** : les proxys et les certificats de l'entreprise sont ceux du système. Sous Linux, par **libcurl**, chargée à la demande ; si elle manque, l'Assistant le dit. Aucune bibliothèque n'est ajoutée à l'installateur : WinHTTP fait partie de Windows.

### L'aide et les notes

- L'aide F1 « L'Assistant IA » est réécrite (le titre perd « (démonstration) »), et un nouveau sujet explique **ce que l'Assistant sait et ce qu'il peut proposer**.
- Huit notes de version 1.15.0, dans le domaine « Assistant IA ».

## 2. Pour l'essayer avec ta clé (deux minutes)

1. Dans IHM Studio, ouvre une vue, puis l'Assistant (**Ctrl+Maj+I**).
2. Clique sur l'état de l'en-tête (ou **Paramètres**) et choisis le fournisseur **Anthropic (Claude)**.
3. Colle ta clé API dans **Clé API**. Coche **Mémoriser la clé** si tu veux la garder.
4. Clique **Lister les modèles**, puis choisis-en un dans la liste.
5. Clique **Tester la connexion** : « Connexion réussie : … modèles ».
6. Ferme les paramètres. L'en-tête dit « Anthropic – *le modèle* ». Demande, par exemple : « Crée-moi un synoptique de station de pompage avec trois cuves, deux pompes par cuve, des vannes et des indicateurs de niveau ».

Si quelque chose ne va pas, l'erreur est dite en clair dans la conversation. Envoie-moi la capture : je corrigerai.

## 3. Les exigences de la demande, et comment elles sont tenues

| La demande, mot pour mot | Ce qui la tient |
|---|---|
| « Ne jamais exécuter du code C++ arbitraire généré par l'IA » | L'IA n'a qu'un outil, « proposer des opérations ». Ses opérations forment une liste fermée, lue strictement. Le code brut n'accepte ni script, ni appel, ni calcul. Rien de ce qu'elle écrit n'est exécuté. |
| « Ne jamais inventer une variable automate ou un identifiant » | Les consignes l'interdisent, et surtout **le logiciel le vérifie** : variable associée, lue par une formule ou par un texte à trous, objet cité, genre, propriété. Une proposition fautive est refusée et revient à l'IA avec ses raisons. |
| « Ne pas coder en dur une clé API ni un modèle » | La clé vient des paramètres ; le modèle est une case vide (« Lister les modèles »). Le seul identifiant de modèle du logiciel est l'exemple gris de la case. |
| « Pour une application distribuée, protéger la clé API via une architecture adaptée plutôt que de l'intégrer en clair dans l'exécutable » | Aucune clé dans l'exe. Le relais d'entreprise (l'adresse) garde la clé sur un serveur interne ; sinon la clé de l'utilisateur est chiffrée par Windows (DPAPI). |
| « Le réseau ne doit jamais bloquer le thread principal » | La demande part sur un fil de travail ; l'interface lit ce qui est arrivé à chaque image, sans jamais attendre. Les essais le vérifient : Arrêter rend la main tout de suite, même au milieu d'un flux lent. |
| « Ne jamais prétendre que le modèle distant est réentraîné si ce n'est pas réellement le cas » | Rien ne le dit : la mémoire, ce sont les connaissances envoyées avec chaque demande, et l'aide le dit ainsi. Les captures ont été relues pour cela. |

## 4. Les essais

**97 suites sur 97**, sur le tout fusionné, puis de nouveau après les corrections des captures. Deux suites sont nouvelles :
- `aireseau` (190 vérifications) :
  - les adresses (Anthropic, un relais, `http://` refusé hors de ce PC) ;
  - le lecteur du flux, avec des morceaux coupés n'importe où ;
  - le corps de la requête : les champs, les pièces jointes, la réponse à l'outil en tête du tour, l'effort, les consignes en deux blocs (le premier mis en cache), aucun champ refusé par l'API ;
  - le fournisseur sur un faux transport : le texte, l'appel d'outil reconstitué morceau par morceau, une réponse déclinée, une réponse coupée, les erreurs 401, 404, 429 et 529, les deux reprises, une coupure, Arrêter ;
  - **de bout en bout avec libcurl** contre un serveur local : le flux en morceaux, une erreur 401 lue, une coupure au milieu du flux, Arrêter qui ferme la connexion tout de suite ;
  - la clé : jamais écrite sous Linux ;
  - l'Assistant branché sur un faux fournisseur : envoyer, le texte, une proposition validée, Appliquer (un Ctrl+Z), puis le tour suivant qui porte « appliquée » ; une proposition fautive, et le tour suivant qui porte l'erreur ; Arrêter ; l'appel d'outil gardé dans la conversation ; le mode Direct ; la démonstration, qui ne change pas ;
  - les paramètres (le fournisseur, le modèle vide au départ, l'effort, l'adresse, le délai).
- `airegistre` (251 vérifications) :
  - le registre contre les tables du logiciel (les 116 genres, aucun oublié, aucun inventé) ;
  - le texte compact envoyé, et la taille de la demande (une vue ordinaire, une vue de 300 objets) ;
  - le contexte et chaque case de confidentialité (ce qui part, ce qui ne part pas : jamais la valeur d'une variable) ;
  - les connaissances actives (une désactivée ou d'un autre projet ne part pas) ;
  - le schéma de l'outil ;
  - la lecture stricte de propositions réelles (la station de pompage, des propositions fautives) ;
  - les nouvelles opérations (Dupliquer, un groupe avec ses enfants compris ; Code brut), chacune en un Ctrl+Z ;
  - une variable de l'automate relié.

491 notes, 70 versions, 266 sujets d'aide ; le guide et son C++ disent la même chose. Le projet Visual Studio compte les mêmes fichiers que CMake.

**Le chemin jusqu'ici** :
1. Un contrat commun d'abord (`AiProvider.hpp`, `AiPrompt.hpp` : ce qu'un fournisseur reçoit et rend, ce qu'on envoie au modèle et ce qu'on lit), puis deux chantiers en parallèle : **le réseau** (HTTPS, le flux, le fournisseur Anthropic, la clé, le branchement de l'Assistant, le faux fournisseur) et **le registre** (les 116 genres tirés du code, le contexte, les consignes, l'outil, la validation par rôle, les nouvelles opérations).
2. Le réseau a repris le registre avant de finir : les consignes en deux blocs et le retour d'erreur au modèle passent de l'un à l'autre.
3. La fusion, sans conflit ; la version, les notes, l'aide ; les 97 suites ; les deux exe Windows compilés.
4. Une première passe de captures sous Wine, avec l'exe Windows branché sur le faux fournisseur, a trouvé **un plantage** et quatre défauts, tous corrigés avant la livraison :
   - **l'application se fermait** quand une réponse arrivait lentement : un vrai fournisseur annonce l'écriture avant d'envoyer le premier mot, et le panneau dessinait alors le curseur d'écriture au bout d'une ligne qui n'existait pas encore (6 séances sur 9 : une réponse lente, ou Réessayer après Arrêter). Les essais Linux ne le voyaient pas, parce que leur faux fournisseur envoyait tout d'un coup. Un nouvel essai le reproduit (il plantait sans la correction) et passe maintenant ;
   - l'indication grise de la case Modèle était coupée : la case prend maintenant toute la largeur, et « Lister les modèles » passe dessous ;
   - une proposition refusée par le logiciel (une propriété inventée) portait la pastille « À compléter », alors qu'il n'y a rien à compléter : elle dit « Non valide » ;
   - la Vue générale de la mémoire disait toujours « aucune IA connectée (démonstration) » : elle dit le vrai fournisseur ;
   - une séance de script (celles des captures) recopiait la clé API dans son journal : elle est masquée.

## 5. Les captures

Dans `captures-1.15.0.zip` (86 images et le GIF). Ce sont de **vraies captures** : l'exe Windows, lancé sous Wine et piloté par des séances de script, sur un projet neuf et neutre, `Station_Pompage`.

**L'IA de ces captures est le faux fournisseur local** (`tests/fixtures/ia/faux_fournisseur.py`) : l'exe lui parle par WinHTTP comme à un relais d'entreprise (l'adresse `http://127.0.0.1:8765`), avec le même protocole et les mêmes erreurs que l'API d'Anthropic. Ses réponses sont enregistrées d'avance : ce n'est pas Claude qui répond. Pour voir les états de la génération, une petite enveloppe de test espace ses morceaux comme le ferait un vrai réseau (mêmes flux, même protocole).

Un dossier par sujet :
- `1-parametres/` (14) : Démonstration, puis Anthropic (Claude) ; la case Modèle vide et son indication grise, en entier ; l'adresse du relais et la clé masquée ; **Lister les modèles** et la liste rendue ; le modèle choisi ; l'effort ; la clé affichée ; Mémoriser la clé ; **Tester la connexion** réussi (« 3 modèles »), avec une clé refusée (401), avec un modèle absent de la liste.
- `2-entete/` (8) : les trois états de l'en-tête (Démonstration, À configurer, « Anthropic – modele-essai (relais) »), en 1920 × 1200 et en 1280 × 800.
- `3-conversation/` (13) : « ce qui part » sous la saisie et son infobulle ; la station de pompage : En attente, Réflexion, le texte puis Préparation des opérations ; la carte (Créer 31) ; la prévisualisation ; Appliquer ; Ctrl+Z (Annulée, Réappliquer) ; Ctrl+Y ; le tour suivant, où l'IA dit avoir reçu « appliquée ».
- `4-validation/` (5) : une propriété inventée, refusée par le logiciel (pastille « Non valide », Appliquer grisé), et le tour suivant qui renvoie l'erreur à l'IA ; une opération inconnue (« executer ») : rien n'est proposé ; une petite proposition valide et son aperçu.
- `5-erreurs/` (15) : clé refusée (401) ; modèle introuvable (404) ; service surchargé (529) : « Nouvelle tentative (1/2) dans 2 s… », puis (2/2), puis la réponse ; surcharge totale : l'erreur, Réessayer ; une coupure ; une erreur dans le flux ; une demande déclinée ; une réponse trop longue (rien n'est proposé) ; le fournisseur arrêté (« Connexion impossible… »), à l'envoi et dans Tester.
- `6-arreter/` (9) : une réponse lente au fil de l'eau ; Arrêter, et ce qui reste ; **Réessayer** : l'écriture recommence et la réponse arrive entière ; une première demande lente : « Écriture… » avant le premier mot (l'état qui plantait), puis la réponse complète.
- `7-memoire-et-aide/` (6) : la Vue générale de la mémoire (« IA : Anthropic (Claude) », 116 genres connus) ; l'aide F1 des deux sujets de l'Assistant.
- `8-demonstration/` : le parcours principal en 16 images numérotées (paramètres → lister → tester → demander → réflexion → proposition → prévisualiser → appliquer → Ctrl+Z → Ctrl+Y → le tour suivant), et le même parcours en **GIF animé** (`demonstration_ia_connectee.gif`, 1280 de large).

**La vérification de la clé, sous Wine** : avec « Mémoriser la clé », `IA\cle.bin` fait 209 octets, chiffré par Windows ; la clé n'y est pas en clair, ni dans `parametres.json`, ni dans les conversations, ni dans l'activité. Sans la case cochée, aucun fichier n'est créé.

**Après la correction du plantage** : 6 séances sur 6 sans plantage, dont Réessayer après Arrêter (deux fois), une première réponse lente (deux fois), et tout le parcours au rythme d'un vrai réseau. Avant, 6 sur 9 plantaient.

Le pointeur Windows n'apparaît pas : SDL ne le dessine pas dans une capture.

## 6. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.15.0.exe` : l'installateur ;
- `API-Studio-1.15.0-portable.zip` et `IHM-Studio-1.15.0-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.15.0.zip`) et le GIF de la démonstration ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.15.0.exe`. Il met à jour la version installée au même endroit, garde tes données (le dossier `IA\` et tes conversations compris) et reprend ton choix d'applications.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Le format des projets ne change pas (33). L'Assistant n'écrit rien dans le projet.

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.15.0.exe` (40,3 Mo, 42 243 189 octets) :
`3B0A3FDB111E8E379DF19664FD2DDC4AC338823D703C17D6F3BA837322CC0D00`

Les zips portables (167 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.15.0-portable.zip` (31,3 Mo) :
  `8B68F4B37A23678976391C9E34A474CEC681F3808F3FCFF34F1EA097EE267B96`
- `IHM-Studio-1.15.0-portable.zip` (31,3 Mo) :
  `61EB21732503DAA71CBA3F30F0F874CD5EE1A05BC921EFEA54FC13AECF1CDA49`

### Les vérifications sous Windows

**Les deux exe Windows** sont compilés sous Linux avec MinGW-w64 et lancés sous Wine.
- `--version` répond `API Studio 1.15.0` et `IHM Studio 1.15.0`.
- `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant : 891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB.
- Les exe appellent maintenant `WINHTTP.dll` et `CRYPT32.dll`, deux bibliothèques de Windows : rien de plus à livrer (la seule DLL livrée reste `SDL3.dll`, avec `pdfium.dll`).

**L'installateur** est compilé par Inno Setup 6.4.1 sous Wine. Il a été lancé en silencieux, pour tous les comptes, par-dessus la 1.14.2 livrée : « Installation process succeeded ».
- Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été testés ; le manifeste compte 167 fichiers.
- Les associations `.xihm` et `.xapi` sont là.
- La copie de secours est faite (168 fichiers, 126,4 Mo) ; deux copies au plus : la plus ancienne (1.14.1) est partie.

## 7. Les fichiers créés et modifiés

Depuis la 1.14.2 : 17 fichiers créés et 22 modifiés (un `.cpp` et son `.hpp` comptent pour deux).

**Créés** (`src/app/ai/`, l'espace de noms `app::ai`) :

| Fichier | Rôle | Module de la demande (section 4) |
|---|---|---|
| `AiProvider` | le contrat d'un fournisseur (start, poll, cancel) et la fabrique | CAIProvider |
| `AiHttp` | HTTPS : WinHTTP sous Windows, libcurl sous Linux ; annulation depuis un autre fil | CAIHttpClient |
| `AiSse` | le lecteur du flux (morceaux coupés n'importe où) | CAIStreamParser |
| `AiAnthropic` | le fournisseur Anthropic : le corps, le flux, les erreurs, les reprises, la liste des modèles | CAIProvider (Anthropic) |
| `AiSecret` | la clé : DPAPI sous Windows, la séance sous Linux | CAISecretStore |
| `AiPrompt` | les consignes, l'outil des opérations, la lecture stricte de la proposition, le retour d'erreur | CAIPromptBuilder, CAICommandParser |
| `AiRegistry` | le registre des 116 genres, tiré du code | CAIObjectRegistry |
| `AiContext` | le contexte de la vue, selon la confidentialité, résumé pour une grande vue | CAIContextBuilder |

Les essais : `tests/ai_reseau_test.cpp`, `tests/ai_registre_test.cpp`, et le faux fournisseur `tests/fixtures/ia/faux_fournisseur.py`.

**Modifiés** :
- l'Assistant : `AiAssistant`, `AiPanel`, `AiSettingsDialog`, `AiStore`, `AiWorkspace.cpp`, `AiOperations` (Dupliquer, Code brut, la validation par rôle), `AiKnowledge`, `AiDemo.cpp` ;
- les séances de script : `src/app/ScriptRunner.cpp/.hpp` (les commandes `ia fournisseur`, `ia modele`, `ia cle`, `ia tester`…) ;
- la compilation : `CMakeLists.txt` (winhttp et crypt32 sous Windows), `XpgAnalyzer.vcxproj`, `XpgAnalyzer.vcxproj.filters`, `outils/mingw/package_mingw.py` ;
- la version, l'aide et les notes : `src/core/Version.hpp`, `outils/config.ini`, `src/help/ReleaseNotes.cpp`, `tools/guide-ihm/guide-ihm.txt`, `src/hmi/HmiGuideText.cpp`, `src/hmi/HmiTutorialDeduce.cpp` ;
- les comptes des essais : `tests/hmi_editor_test.cpp`.

## 8. Les limites et la suite

**Les limites connues** :
- **Pas essayé contre la vraie API**, faute de clé ici (voir le § 2). Le protocole suit la documentation de l'API à la lettre, et le faux fournisseur le rejoue, mais le premier essai réel reste à faire.
- Sous Linux, la clé n'est pas gardée d'une séance à l'autre (pas de trousseau du système).
- Un proxy d'entreprise qui demande un mot de passe (erreur 407) n'est pas géré : la connexion échoue, et l'erreur le dit. Le relais d'entreprise contourne le problème.
- Les classeurs Excel (XLSX) et les fichiers du logiciel joints ne sont pas lus par l'IA, seulement nommés.
- Une coupure du réseau au milieu d'une réponse n'est pas reprise seule : Réessayer la relance.
- La validation ne fait pas tourner « Générer » sur la copie de la vue : une formule proposée est vérifiée sur les variables qu'elle lit, pas compilée (Générer le fera ensuite, comme pour une formule tapée à la main).
- L'IA ne pose pas d'instance d'un symbole de l'utilisateur : elle le dit, une instance se pose depuis la section Symboles de la bibliothèque.
- À 1280 × 800, avec l'Assistant ouvert, l'inspecteur reste étroit (comme en 1.14.2).
- Trois petites choses vues aux captures, pas encore reprises : juste après Enregistrer dans les paramètres, la barre d'état du bas dit encore « Démonstration » quelques secondes ; en 1280 × 800, l'en-tête « À configurer : choisis un modèle et une clé » perd son dernier mot (il se lit au survol) ; dans l'aide F1, le sommaire « Sur cette page » ne suit pas le défilement.

**À regarder sur un vrai Windows** :
- le premier essai avec ta clé (§ 2) ;
- « Mémoriser la clé », puis un redémarrage : la clé est relue ;
- derrière le proxy de l'entreprise ;
- une image et un PDF joints.

**À part** :
- la bibliothèque de sons (arrêtée en cours, mise de côté, à reprendre à ta demande) ;
- la bibliothèque « Standard » ;
- les raccourcis clavier avancés des éditeurs ; les requêtes HTTP, MQTT ou SQL depuis un script ; les grafcets côté IHM.
