# API Studio et IHM Studio 1.16.1 : une IA intégrée qui comprend mieux, le relais, et ce que tu as demandé en route

Livrée le 31/10/2026 dans l'après-midi. Elle suit la 1.16.0 (`docs/LIVRAISON-1.16.0.md`), livrée le matin même.

Tes demandes, dans l'ordre :
- « l'assistant classique n'est pas très puissant et il comprend mal, comment le faire progresser ? », puis « fait au mieux / si je veux mettre Ollama je dois le télécharger ? » ;
- « ajoute les déballages dans les types IHM et membres » ;
- « faire une option pour avertir des réponses des IA, cochable/décochable » ;
- « dans la prévisualisation IA, pouvoir zoomer et mettre des tooltips dans la vue, pouvoir déballer/réduire la partie de gauche avec la liste des choses à faire » ;
- « j'ai fait Échap sans faire exprès dans la visu, ça a refusé, attention c'est trop brutal comme choix et il faut des sécurités » ;
- « bug : elle dit $Armoires[0]$.Prete n'existe pas alors que la variable existe bien dans la prévisualisation, et faire une option pour quand même exécuter même s'il reste des erreurs, elles seront juste ajoutées dans le journal ».

En bref :
- **L'IA intégrée lit tes phrases** au lieu de les découper mot à mot : plus de type nommé « pour » ou « qui ». Elle connaît des **modèles métier** (Capteur, Pompe, Vanne, Moteur…) et, quand elle ne comprend pas, **elle le dit** au lieu de proposer n'importe quoi.
- **Le relais « IA intégrée d'abord »** : avec Ollama ou Claude, ce que l'IA intégrée comprend est répondu tout de suite, sans connexion ; le reste part à l'IA connectée.
- **Les déballages des Types IHM**, dans la liste et dans le tableau des membres, avec les décalages.
- **La Prévisualisation** se zoome, chaque objet a son infobulle, la liste de gauche se replie et se masque.
- **Plus de refus par mégarde** : Échap ne refuse jamais, Refuser demande confirmation, un refus se reprend.
- **« Appliquer malgré les erreurs »** (une option) et **« M'avertir des réponses »** (une option, cochée).
- Le repère `$Armoires[0]$.Prete` n'est plus dit introuvable.
- **Les essais sous Windows (Wine) ont trouvé 5 défauts, corrigés avant la livraison** : « Ajoute un voyant lié à Pompe_1_Marche » crée maintenant le voyant **et** l'associe ; un membre « Duree en INT » n'est plus perdu ; une question hors sujet part à l'IA connectée ; le bouton Refuser de la carte pose aussi la question ; la note « laissée de côté » va à la ligne.

**Ce qui reste à faire, et quand** :
- la **1.17.0** : l'onglet Apprentissage refait (la progression en graphiques, les thèmes, la correction à la main, la recherche - la maquette que tu as vue), l'apprentissage **exporté et importé** dans un fichier `.xia`, et l'installateur qui **relie l'IA** et un apprentissage d'un autre PC ;
- la **1.18.0** : les nouveaux outils de l'IA (types génériques `<T, K>`, symboles, vues modèles en-tête et pied de page, popups, équipements avec détection de ce qui répond sur chaque port, scripts, fonctions, variables système, ressources et fichiers joints, alarmes, dossiers, natives), **les adresses**, **les variables liées aux équipements** et **les formules de lien dans les structures**.

## 1. Ce qui change

### L'IA intégrée comprend mieux

Ta capture montrait deux défauts : « Crée un type pour gérer les capteurs » donnait un type nommé **pour** (membres Gerer, capteurs), et « Crée une structure qui s'appelle Capteur… » un type nommé **qui**. Elle découpait la phrase au premier mot venu.

Elle **lit maintenant la phrase** : le nom vient de « qui s'appelle X », « nommé X », « T_X », ou du sujet (« un type **moteur** » → T_Moteur) ; un mot-outil (pour, qui, avec, une, de…) n'est **jamais** un nom ; « par exemple mesure, etc, défauts » donne des membres, pas « etc ».

Elle connaît des **modèles métier** : « un type pour gérer les capteurs » propose **T_Capteur** avec Mesure (REAL), Unite, Min, Max, SeuilHaut, SeuilBas, AlarmeHaute, AlarmeBasse, Defaut ; de même pour un capteur tout-ou-rien, une pompe, une vanne, un moteur, un variateur, une cuve, un régulateur. Tu gardes la main : ce n'est qu'une proposition, avec son aperçu.

Quand elle **ne comprend pas** (« Quelle est la capitale de l'Australie ? »), elle le dit, propose des formulations à cliquer, et **n'ouvre aucune carte d'opérations**.

Son corpus d'essai compte 98 phrases réelles (toutes comprises comme attendu) ; sa suite, 604 vérifications.

**Comment la faire progresser** : dis-moi les phrases qu'elle comprend mal (copie-les telles quelles) : chacune devient un essai et une règle. Pour comprendre tout ce qu'on lui dit, il faut un vrai modèle de langage : Ollama (gratuit, sur ton PC) ou Claude - et le relais ci-dessous les fait travailler ensemble.

### Le relais : « IA intégrée d'abord »

Dans les paramètres de l'Assistant, sous Fournisseur, la case **IA intégrée d'abord** (active avec Anthropic ou Compatible OpenAI). Cochée, chaque demande passe d'abord par l'IA intégrée :
- ce qu'elle comprend est répondu **tout de suite**, sans connexion ni jeton ; la réponse le dit en bas (« Répondu par l'IA intégrée, sans connexion… ») ;
- ce qu'elle ne comprend pas, ou une demande avec une pièce jointe, part à l'IA connectée, comme avant.

Avec un Ollama un peu lent (sans carte graphique), les demandes simples restent instantanées.

### Ollama : oui, il faut le télécharger (une fois)

Ollama est un logiciel à part, gratuit, qui fait tourner un modèle sur ton PC (rien ne quitte le PC, pas de clé) :
1. ollama.com › Download › Windows (OllamaSetup.exe) ; il tourne ensuite en arrière-plan.
2. Un modèle, dans une invite de commandes : `ollama pull qwen2.5:7b` (environ 4,7 Go ; 8 Go de mémoire au moins, 16 conseillés), ou `ollama pull qwen2.5:3b` pour un petit PC (environ 2 Go).
3. Paramètres de l'Assistant : Fournisseur **Compatible OpenAI**, Service **Ollama** (l'adresse se remplit), **Lister les modèles**, choisis le tien, **Enregistrer**.
4. Coche **IA intégrée d'abord**.

L'aide F1 (« Une IA gratuite sur ce PC : Ollama ») donne aussi le cas d'un PC sans Internet et la fenêtre de contexte à agrandir.

### Les déballages des Types IHM

Comme les Variables IHM, les types se déplient sans quitter la page :
- **la liste des types** : la flèche d'une structure montre ses membres (leur type, leurs mots, leur description) ; une énumération, ses valeurs ; un membre structure ou tableau de structures se déplie à son tour (`Purge : ARRAY[0..2] OF T_CfgPurge` montre une fois les membres de sa case). Un clic sur un membre choisit le type et ce membre à droite ; un double-clic choisit son type à lui ;
- **le tableau des membres** : un membre structure ou tableau se déplie sur place - ses cases (`Purge[0]`, `Purge[1]`…, les 200 premières), puis leurs membres, en retrait, avec leur **Décalage depuis le début du type**. Ces lignes se lisent (elles se changent dans leur propre type) ;
- Droite, Gauche, le clic droit **Tout déplier / Tout replier** ; ce qui est déplié se garde d'un type à l'autre.

### La Prévisualisation

- **Le zoom** : la molette sur l'aperçu (autour de la souris), les boutons − / Ajuster / + en haut à droite, Ctrl+plus / moins / 0 ; zoomé, l'aperçu se déplace au glisser ; un double-clic l'ajuste.
- **Une infobulle sur chaque objet** : son nom, son genre, nouveau / modifié / supprimé, ce qui change (avant → après), sa place.
- **La liste de gauche** : un clic sur le titre d'un groupe (Erreurs, Variables associées, Objets concernés…) le replie ; le petit bouton entre la liste et l'aperçu la masque toute.

### Plus de refus par mégarde

- **Échap ferme la Prévisualisation sans décider** : la proposition reste sur sa carte.
- Le bouton « Annuler » (qu'on prenait pour « fermer ») devient **Refuser…**, qui pose une question : Refuser ou Garder.
- **Modifier la demande** ne refuse plus la proposition.
- Une carte refusée propose **Revenir sur le refus**.

### Appliquer malgré les erreurs

Une case des paramètres, décochée au départ. Cochée, une proposition qui garde des erreurs peut s'appliquer : le bouton dit « Appliquer malgré N erreurs… », une confirmation les liste, puis **ce qui est valide s'applique** et ce qui est en erreur est **laissé de côté** - noté sous la réponse et dans le journal d'activité (Mémoire et apprentissage › Activité). Un seul Ctrl+Z reprend tout. Une vue qui a changé depuis la proposition bloque toujours.

### M'avertir des réponses

Une case des paramètres, cochée au départ. Une réponse qui arrive quand tu ne regardes pas le panneau (une autre fenêtre devant, le panneau replié, une autre conversation) : un avis dans la **cloche** (Voir ouvre la conversation) ; si la fenêtre est en arrière-plan, son bouton **clignote** dans la barre des tâches, avec le son de notification de Windows.

### Vu aux essais sous Windows, et corrigé

La passe de captures sous Wine (8 séances, aucun plantage) a trouvé 5 défauts et 4 petits ; tous sont corrigés et ont leur essai :
- **Créer et associer dans la même phrase** : « Ajoute un voyant lié à Pompe_1_Marche » (ou « associé à ») donnait un voyant *nommé* Pompe_1_Marche, sans variable, ou cherchait le voyant dans la vue. Il crée maintenant le voyant **et** l'associe à la variable. Si la variable n'existe pas (« lié à Variable_Qui_N_Existe_Pas »), le voyant est proposé seul, la réponse dit pourquoi et donne la phrase pour créer la variable : **jamais une variable inventée**.
- **« Duree en INT »** : le mot « durée » était pris pour le type TIME, et le membre disparaissait sans rien dire. Un mot qui dit aussi un type, suivi de **son** type, est un nom : `T_CfgPurge` a bien Debit (REAL), Duree (INT), Active (BOOL).
- **Une demande hors sujet** (« Écris-moi un poème sur les pompes centrifuges ») recevait « Quel texte ? » et comptait comme comprise : avec « IA intégrée d'abord », elle ne partait jamais à l'IA connectée. Elle n'est plus comprise : elle part à l'IA connectée (sans IA connectée, la réponse le dit et propose des formulations).
- **Refuser… de la carte** refusait sans question, alors que l'aide disait « une question d'abord ». La carte pose maintenant la même question que l'aperçu (Refuser ou Garder).
- **Après « Appliquer malgré »** : la note sous la réponse montrait un caractère inconnu à la place du saut de ligne ; elle va maintenant à la ligne. La confirmation coupait la ligne d'erreur à droite : les erreurs sont dans son paragraphe, qui va à la ligne.
- Les petits : « … et 1 autre » (et non « 1 autres ») ; le titre « ERREURS » dit « laissées de côté par Appliquer malgré » quand l'option est cochée ; le tableau des réglages de l'aide est d'un seul tenant ; la note de version nomme la case « IA intégrée d'abord ».

### Le repère `$Armoires[0]$.Prete`

Une proposition qui associait un objet à `$Armoires[0]$.Prete` (un repère que Dupliquer avec repères fera avancer) disait « n'existe pas » alors que la variable Armoires existe : la validation lisait le nom avec ses `$`. Corrigé, avec un essai.

## 2. Les essais

**102 suites sur 102.** Les vérifications ajoutées :
- `aiintegree` : 612 vérifications (257 à la 1.16.0), dont le corpus de 98 phrases réelles, les phrases de ta capture, les modèles métier, les demandes non comprises, et les défauts vus sous Windows (un voyant lié à une variable, connue ou non ; « Duree en INT » ; le poème) ;
- `aicompatible` : 189 (le relais : ce que l'IA intégrée répond ne part pas au fournisseur ; une pièce jointe part ; l'avis de réponse ; les cases des paramètres) ;
- `aioptions` : 182 (le repère `$Armoires[0]$.Prete`, appliquer malgré les erreurs, une vue changée qui bloque) ;
- `aiassistant` : 264 (Échap ne refuse jamais, Refuser… et sa question - dans l'aperçu et sur la carte -, Modifier la demande, Revenir sur le refus ; le zoom, les infobulles, la liste repliable) ;
- `aiapprentissage` (45) et `aiapprentissagefichier` (43) : les deux nouvelles suites du moteur de la 1.17.0 (voir plus bas, « Déjà dedans ») ;
- `hmieditor` : les déballages des types IHM (23 vérifications : la liste, le tableau des membres, les décalages, une ligne dépliée en lecture seule, un renommage sous des lignes dépliées qui vise le bon membre, Tout déplier / replier).

512 notes, 72 versions, 267 sujets d'aide ; le guide et son C++ disent la même chose. Le projet Visual Studio compte les mêmes fichiers que CMake.

## 3. Les captures

(à compléter après la passe sous Wine)

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.16.1.exe` : l'installateur ;
- `API-Studio-1.16.1-portable.zip` et `IHM-Studio-1.16.1-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.16.1.zip`) ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.16.1.exe`. Il met à jour la version installée au même endroit, garde tes données (le dossier `IA\` et tes conversations compris) et reprend ton choix d'applications.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Le format des projets ne change pas (33). Les trois nouvelles options de l'Assistant prennent leur valeur par défaut (M'avertir des réponses cochée, IA intégrée d'abord et Appliquer malgré les erreurs décochées) : rien ne change tant que tu ne les coches pas.

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.16.1.exe` (41,0 Mo, 43 018 983 octets) :
`C7641C8D352FBDE7D11901140AAEF2D4D8D23C59180317D4A84D58A8B854ABC6`

Les zips portables (167 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.16.1-portable.zip` (31,8 Mo) :
  `7B78CE84DB77A3F00B350DA24FF2185A1C05371AEA81CDC0D9611853A051786C`
- `IHM-Studio-1.16.1-portable.zip` (31,8 Mo) :
  `6268291A581D198CD5CDBB0197FD79A9C0EA7CBBBA3F910F7AE8E0BAD890E36A`

### Les vérifications sous Windows

**Les deux exe Windows** sont compilés sous Linux avec MinGW-w64 et lancés sous Wine.
- `--version` répond `API Studio 1.16.1` et `IHM Studio 1.16.1`.
- `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant : 891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB.

**L'installateur** est compilé par Inno Setup 6.4.1 sous Wine. Il a été lancé en silencieux, pour tous les comptes, par-dessus la 1.16.0 livrée : « Installation process succeeded ».
- Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été testés ; le manifeste compte 167 fichiers.
- Les associations `.xihm` et `.xapi` sont là.
- La copie de secours de la 1.16.0 est faite (deux copies au plus).

## 5. Les fichiers modifiés

Depuis la 1.16.0, dans le code et les essais : 35 fichiers modifiés et 1 créé.
- l'Assistant : `AiLocal` (la lecture des phrases, les modèles métier, « pas compris »), `AiAssistant` (le relais, appliquer malgré les erreurs, revenir sur le refus), `AiOperations` (les repères, l'application tolérante), `AiPanel` (les sécurités du refus, les boutons des cartes, l'avis de réponse), `AiPreviewDialog` (le zoom, les infobulles, la liste repliable, Refuser…), `AiSettingsDialog`, `AiStore` (les trois options), `AiWorkspace.cpp` (l'avis dans la cloche) ;
- le moteur de l'apprentissage de la 1.17.0 (voir « Déjà dedans ») : `AiLearning` et `AiLearningFile` (créés), `AiKnowledge.hpp` (la catégorie Modèles métier) ;
- `HmiAskDialog.cpp` : une ligne de liste trop longue finit par « … » ;
- les Types IHM : `HmiFolderTable` (les lignes dépliées d'une liste en dossiers), `HmiVariablePanes` (la liste et le tableau des membres) ;
- l'application : `App` (la fenêtre a-t-elle le focus, la faire clignoter), `SingleInstance` (le clignotement Windows), `screens/ApiWorkspace.cpp` et `Screens.hpp` (Voir, depuis la cloche) ;
- les séances de script : `ScriptRunner.cpp` (`ia dabord`, `ia avertir`) ;
- la version, l'aide et les notes ; les essais.

### Déjà dedans : le moteur de la 1.17.0, sans son écran

Cette version contient déjà le moteur de l'apprentissage de la 1.17.0, essayé (88 vérifications) mais **sans son onglet** : rien ne change à l'écran.
- Les modèles métier et le vocabulaire de l'IA intégrée sont rangés comme des connaissances (catégorie « Modèles métier » de la fenêtre Mémoire et apprentissage) ; tant que tu ne les corriges pas, ils sont ceux du logiciel.
- Chaque réponse de l'IA intégrée s'inscrit dans `IA\apprentissage.jsonl` (la date, comprise ou non, la demande) : ce sera la courbe de progression de la 1.17.0.
- Les commandes de séance `ia apprentissage exporter "chemin.xia"`, `voir` et `importer` existent déjà ; l'onglet et l'installateur viendront avec la 1.17.0.

## 6. Les limites

- **Pas essayé contre un vrai Ollama** ni sur un vrai Windows : le faux fournisseur local tient lieu d'Ollama (voir la 1.16.0).
- L'IA intégrée reste un moteur de règles : elle comprend les demandes du logiciel, pas les questions générales. Le relais est fait pour ça.
- Les déballages du tableau des membres montrent les 200 premières cases d'un tableau, et 2 000 lignes dépliées au plus (au-delà, une ligne le dit) ; une structure générique (`T_Mesure<T>`) ne se déplie pas dans le tableau des membres (sa place dépend de T).
- Le clignotement et le son de l'avis sont ceux de Windows ; ailleurs (les essais sous Linux), seul l'avis de la cloche.
