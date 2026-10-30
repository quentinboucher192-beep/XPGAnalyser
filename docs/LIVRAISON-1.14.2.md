# API Studio et IHM Studio 1.14.2 : la maquette de l'Assistant IA et sa mémoire

Livrée le 30/10/2026. Elle suit la 1.14.1 (`docs/LIVRAISON-1.14.1.md`).

Elle répond à :

> « PROJET : INTÉGRATION D'UN ASSISTANT IA DANS UN LOGICIEL DE CONCEPTION IHM INDUSTRIELLE […]
> IMPORTANT : commencer par réaliser une MAQUETTE INTERACTIVE intégrée au logiciel avant de développer toute la logique IA.
> Cette maquette doit utiliser les véritables composants graphiques et les véritables vues de l'application. […]
> Commencer exclusivement par l'audit puis la maquette interactive. Ne pas développer immédiatement toute l'IA. »
>
> « ajoute ça » : « 12. CENTRE DE MÉMOIRE, APPRENTISSAGE ET STOCKAGE DE L'IA […] »

Ce qui est fait : **la phase 1 (l'audit)** et **la phase 2 (la maquette interactive dans le vrai logiciel)**, avec le **centre « Mémoire et apprentissage de l'IA »** de la section 12, qui fonctionne vraiment sur les fichiers du PC.

Aucune IA n'est branchée : c'est voulu à cette étape. Les réponses viennent d'un module de démonstration, et l'en-tête de l'Assistant l'affiche (« Démonstration – aucune IA connectée »). Rien ne quitte le PC.

L'audit est dans `docs/ASSISTANT-IA-AUDIT.md`. Il décrit l'existant, chaque fichier et chaque fonction cités, puis ce qu'il faudra pour les phases 3 à 9 et leur coût.

## 1. Ce qui change

### L'Assistant IA (IHM Studio)

Un panneau à droite de l'éditeur de vues, quatrième volet de la fenêtre. Il pousse l'éditeur sans le cacher, se tire et se replie comme les autres panneaux. Au départ, il prend 22 % de la fenêtre (entre 280 et 460 px) ; tiré, il garde sa largeur.
- **L'ouvrir** : **Ctrl+Maj+I**, **Affichage › Assistant IA**, **Projet › Disposition** (la ligne « Assistant IA »), ou **Ctrl+K** « Assistant IA ». Seulement dans IHM Studio.
- **L'en-tête** : l'icône de l'Assistant, l'état « Démonstration – aucune IA connectée », **Nouvelle conversation**, **Paramètres**, et le mode **Prévisualisation | Direct**. En dessous : la vue concernée.
- **Les conversations** : la liste des récentes, avec leur date et la marque « démo » pour les deux conversations de démonstration créées à la première ouverture. Un clic en choisit une. Double-clic ou F2 : renommer sur place. Clic droit : Renommer, Supprimer… (avec confirmation).
- **La discussion** :
  - tes messages (sous « Moi ») et ceux de l'Assistant, avec leur heure ;
  - une mise en forme simple : gras, listes, `code`, titres courts ;
  - pendant la génération : « en attente », « réflexion », puis le texte qui s'écrit mot à mot, et **Arrêter** ;
  - la saisie est un champ de message : le texte passe à la ligne, le champ grandit de 1 à 5 lignes, puis défile. Il reste visible en bas quand la discussion défile ;
  - **Entrée** envoie, **Maj+Entrée** passe à la ligne ;
  - des **suggestions** cliquables quand la conversation est vide.
- **La carte « Opérations proposées »**, dans la réponse :
  - les comptes : objets à créer, à modifier, à supprimer ;
  - une miniature de la vue modifiée, dessinée comme dans l'éditeur ;
  - les avertissements ;
  - les boutons **Prévisualiser…**, **Appliquer**, **Refuser** ; ensuite, la carte affiche « appliquée » ou « refusée » ;
  - **elle suit la vue** : après un Ctrl+Z (ou un projet rouvert sans avoir été enregistré), elle affiche « Annulée » et propose **Réappliquer** ; si la vue a changé depuis, elle le dit.
- **Les demandes reconnues par la démonstration** (six) :
  - « Crée-moi un synoptique de station de pompage avec trois cuves, deux pompes par cuve, des vannes, des indicateurs de niveau et les boutons de commande associés » : **31 vrais objets** (3 cuves, 3 bargraphes de niveau, 3 vannes, 3 tuyauteries, 6 pompes, 12 boutons à impulsion, un titre) ;
  - « Ajoute une cuve avec un indicateur de niveau » ;
  - « Mets celle de droite en rouge » (sur la sélection) ;
  - « Aligne les quatre boutons » ;
  - « Associe cette pompe à la variable PUMP_01_RUN » : cette variable n'existe pas. L'Assistant ne l'invente pas : il cite les variables du projet, et l'aperçu propose de choisir l'une d'elles ou de la créer si tu le demandes. Une fois le choix fait, l'aperçu dit quelle variable remplace PUMP_01_RUN ;
  - « Supprime les vannes » : une suppression de plusieurs objets se confirme toujours.

  Pour toute autre demande, la réponse dit franchement qu'en démonstration, seules les demandes proposées sont reconnues, puis affiche les suggestions.
- **Les vrais objets du logiciel** : Cuve, Pompe, Vanne, Bargraphe, Bouton à impulsion, Tuyauterie, Texte, créés comme dans la bibliothèque, avec leurs vraies propriétés. Une propriété inconnue est refusée.
- **Appliquer** modifie vraiment la vue, par les mécanismes de l'éditeur : **un seul Ctrl+Z** retire tout. Le **mode Direct** applique dès la réponse ; Ctrl+Z défait aussi.
- **Une proposition périmée** : une proposition d'une ancienne conversation garde l'empreinte de la vue d'alors. Si la vue a changé depuis, elle ne s'applique jamais ; l'Assistant le dit et propose de redemander.

### La prévisualisation des modifications

Une fenêtre du logiciel, déplaçable et redimensionnable. Rien ne change avant **Appliquer**.
- Les comptes (à créer, à modifier, à supprimer), les erreurs et les avertissements, dont la variable introuvable.
- Les variables associées.
- Chaque objet, avec ses propriétés avant → après.
- **L'aperçu de la vue** telle qu'elle serait, dessiné par les vrais painters de l'éditeur, sur une copie temporaire de la vue. Les nouveaux objets sont encadrés en vert, ceux qui seront modifiés en bleu, ceux qui seront supprimés barrés de rouge.
- **Appliquer**, **Annuler**, **Modifier la demande** (remet le texte dans la saisie).

### Les paramètres de l'Assistant

Une fenêtre du logiciel, réglage par réglage :
- l'Assistant activé ou non ;
- le fournisseur (Démonstration, et ceux à venir) et le modèle : une case libre, rien n'est écrit en dur ;
- l'adresse de connexion et le délai ;
- la clé API : masquée, avec Afficher et Effacer. Elle reste en mémoire pendant la séance et **ne s'écrit jamais sur le disque**. La fenêtre indique qu'elle sera protégée par Windows (DPAPI) quand une IA sera branchée ;
- le mode par défaut (Prévisualisation ou Direct) ;
- la conservation de l'historique (durée, nombre) ;
- la confidentialité : ce qui serait envoyé (la vue active, la sélection, les variables, les pièces jointes), chaque élément pouvant être désactivé ;
- le bouton **Mémoire et apprentissage…**.

Les réglages s'enregistrent dans `IA/parametres.json`, sans la clé.

### Les pièces jointes

**Joindre** ouvre le sélecteur de fichiers du logiciel. Chaque fichier joint apparaît dans une liste : icône de son type, nom, taille, progression du « traitement », retirer.
- Les formats acceptés : PNG, JPG, PDF, TXT, CSV, XLSX, XML, JSON, et les fichiers du logiciel (`.xihm`, `.xapi`, le projet).
- Un autre format est refusé, et l'Assistant le dit.
- Le fichier est **copié à l'identique** dans `IA/pieces-jointes/`. L'original ne bouge pas.
- Le traitement est simulé, mais sa progression est visible.

### Les conversations gardées

**Un fichier par conversation**, dans le dossier `IA/` des réglages de l'utilisateur, à côté de Mes images (`%APPDATA%\XpgAnalyzer\IA` ; jamais dans le projet). Il contient les messages, les pièces jointes et l'état des propositions. Fermer puis rouvrir l'application retrouve tout. Un champ inconnu est gardé à la relecture.

### Mémoire et apprentissage de l'IA (la section 12)

Une fenêtre du logiciel, ouverte par le bouton **Mémoire et apprentissage…** des paramètres ou par **Ctrl+K**. Elle n'affiche **rien de simulé** :
- les tailles sont mesurées sur les fichiers du dossier `IA/`, sur le disque et dans la mémoire du processus ;
- elles sont remesurées après chaque opération ;
- à la première ouverture, `IA/` contient très peu de choses, et l'écran affiche donc de petites tailles.

Six onglets :
- **Vue générale** :
  - l'état de la mémoire ;
  - les comptes : conversations, messages, connaissances, **genres d'objets connus 116/116** (tirés du registre du logiciel) ;
  - les tailles (totale, cache, pièces jointes, index et données techniques) et l'espace libre du disque, avec des jauges et un graphique de répartition ;
  - deux blocs séparés : **la mémoire vive (RAM) de l'application** (actuelle, pic, RAM libre du PC) et **le stockage permanent (disque)**. L'écran précise que le modèle distant n'occupe rien sur le PC et qu'aucun traitement IA local ne tourne aujourd'hui.
- **Apprentissage** :
  - la liste des connaissances (nom, description, dates, source, taille estimée, active ou non), filtrée par catégorie et par projet ;
  - Consulter, Modifier, Activer, Désactiver, Supprimer, Ajouter ;
  - l'activité récente ;
  - un encart franc : « Apprendre ici = enrichir la mémoire locale que l'Assistant joint à ses demandes. Le modèle distant n'est pas réentraîné. »
  - **Six connaissances intégrées** sont tirées des vraies tables du logiciel : les 116 genres d'objets ; les propriétés de chacun, telles que la création par défaut les pose, avec leur rôle ; les actions ; le vocabulaire et les synonymes de la bibliothèque ; l'objet posé pour une variable ; les modèles de vues. Elles se consultent, s'activent et se désactivent, mais ne se suppriment pas.
  - Les autres connaissances (une règle, une préférence, le contexte d'un projet, une connaissance métier) s'ajoutent, se modifient et se suppriment. Une connaissance liée à un projet n'est pas jointe aux demandes faites dans un autre projet.
- **Mémoire** : les informations gardées, par catégorie (conversations, messages, informations par projet, préférences, connaissances, références aux pièces jointes). On peut les **rechercher**, et supprimer une conversation, une mémoire ou une catégorie entière (avec confirmation).
- **Stockage PC** :
  - la répartition précise, en tableau et en graphique ;
  - la taille totale, l'espace libre, le nombre de fichiers ;
  - l'emplacement (copiable) et **Ouvrir le dossier** ;
  - la date du dernier nettoyage.
- **Nettoyage** :
  - **Vider le cache** : d'abord le détail des six catégories (taille, nombre de fichiers, ce que les vider change) et le total à libérer, puis une confirmation. Seul le dossier `cache/` est vidé : **jamais** les conversations, les connaissances, les pièces jointes originales, les projets ni les paramètres ;
  - **Supprimer les données persistantes** est à part, dans une zone rouge : cocher, puis « Je comprends », puis confirmer.
- **Activité** : le journal (date, type, texte), filtré par type. Chaque opération de l'écran y ajoute sa ligne.

### L'aide et les notes

Un nouveau chapitre F1, **Concevoir les vues › L'Assistant IA**, avec deux sujets : `assistant-ia` et `memoire-ia`. L'aide passe de 263 à 265 sujets. 6 notes de version 1.14.2.

**Une erreur de la 1.14.1 est corrigée** : ses notes de version s'affichaient comme « à venir ». La 1.14.1 est maintenant notée comme livrée.

## 2. Les livrables demandés (section 3.8)

1. **La liste des fichiers créés et modifiés** : voir § 7.
2. **Une capture réelle de l'application avec l'Assistant ouvert** : `captures-1.14.2.zip`, dossier `1-assistant/`.
3. **Une capture de la conversation** : dossier `2-conversation/`.
4. **Une capture de l'écran de prévisualisation** : dossier `3-previsualisation/`.
5. **Une capture des paramètres IA** : dossier `4-parametres/`.
6. **Une démonstration des interactions** : dossier `7-demonstration/`, une suite numérotée du parcours principal et son GIF animé.
7. **Les vues utilisent le framework graphique existant** : oui. Tout est fait avec `ui::Widget`, `WidgetHost`, les widgets de `src/ui/widgets`, les fenêtres modales du logiciel, `ui::Theme`, les icônes, l'atlas de police et `platform/Renderer.hpp` (SDL_Renderer). Il n'y a aucun appel OpenGL ou DirectX, aucun second système graphique, aucune fenêtre à part, aucune page HTML. L'aperçu de la vue est dessiné par les painters de l'éditeur. Les objets proposés sont de vrais `hmi::Object` de vrais genres (`Kind`), avec les clés de propriétés du logiciel.
8. **Une compilation fonctionnelle** : les deux exe Windows (MinGW) et l'exe Linux compilent sans erreur. Les 95 suites d'essais passent (§ 4). L'installateur est vérifié sous Wine (§ 6).

## 3. Les critères de réussite (section 11), dans la maquette

| # | Le critère | Dans la 1.14.2 |
|---|---|---|
| 1 | Ouvrir le logiciel | oui |
| 2 | Accéder à l'Assistant | oui (Ctrl+Maj+I, Affichage, Disposition, Ctrl+K) |
| 3 | Créer une conversation | oui |
| 4 | Joindre une capture ou un document | oui (copié dans `IA/`, traitement simulé) |
| 5 | Demander un synoptique en langage naturel | oui, pour les six demandes reconnues ; la démonstration reconnaît des phrases, elle ne comprend pas la langue |
| 6 | Voir l'IA comprendre les objets disponibles | les objets proposés sont les vrais genres et propriétés ; le registre des 116 genres est réel (Mémoire et apprentissage) ; la compréhension viendra avec l'IA branchée |
| 7 | Voir le résultat dans l'éditeur réel | oui (aperçu par les vrais painters, puis la vue elle-même) |
| 8 | Valider les modifications | oui (Appliquer, ou le mode Direct) |
| 9 | Annuler les modifications | oui (un seul Ctrl+Z ; Refuser avant) |
| 10–12 | Fermer, rouvrir, retrouver la conversation et continuer | oui (conversations, pièces jointes et état des propositions gardés ; une proposition périmée ne s'applique pas) |

## 4. Les essais

**95 suites sur 95**, sur le tout fusionné. Deux suites sont nouvelles :
- `aiassistant` (242 contrôles) couvre chaque interaction de la section 3.6 :
  - créer, choisir, renommer, supprimer une conversation ; envoyer ; les états de génération et Arrêter ; le défilement, la saisie toujours visible ;
  - joindre (copie dans `IA/`, refus d'un format) ;
  - les paramètres relus, jamais la clé sur le disque ;
  - la proposition : ses comptes, des objets de vrais genres aux vraies clés, l'avertissement de la variable inventée ;
  - Appliquer (le projet change, un Ctrl+Z le défait), Refuser, le mode Direct, la confirmation d'une suppression multiple ;
  - la persistance (écrire, relire, un champ inconnu gardé) ; une proposition périmée refusée ;
  - l'état réel de la carte : appliquer, Ctrl+Z (Annulée), Ctrl+Y (Appliquée), Réappliquer, la relecture, un autre geste sur la vue ;
  - les trois boutons de la carte qui restent dans la carte à la largeur la plus petite du volet, et pendant qu'une réponse s'écrit au-dessous ;
  - une conversation ouverte sur sa dernière demande, même quand le volet rétrécit après l'ouverture ;
  - l'aperçu dessiné par les vrais painters.
- `aimemoire` (205 contrôles) couvre toute la section 12.10 :
  - ajouter, modifier, supprimer une connaissance ; la sauvegarde et la relecture ;
  - **le calcul réel de l'espace** : des fichiers de tailles connues dans chaque catégorie, et les sommes exactes retrouvées ;
  - vider le cache : les conversations et les pièces jointes originales restent identiques, octet pour octet ;
  - les jauges et les comptes qui suivent chaque opération ;
  - les 116 genres tirés des vraies tables ;
  - les données persistantes supprimées seulement après confirmation ;
  - la RAM lue (non nulle), l'activité écrite et relue, la recherche ;
  - aucun nom technique du système dans ce que l'utilisateur lit, sur les six onglets ;
  - une catégorie vide qui ne se coche pas dans la zone rouge, et le bouton qui reste grisé ;
  - le fond assombri une seule fois quand la mémoire s'ouvre depuis les paramètres ;
  - le détail de l'Apprentissage qui suit la liste filtrée ; l'extrait de la recherche sans marques de mise en forme.

483 notes, 69 versions, 265 sujets d'aide ; le guide et son C++ disent la même chose.

**Le chemin jusqu'ici** :
1. L'audit d'abord (`docs/ASSISTANT-IA-AUDIT.md`), avant toute ligne de code.
2. Un contrat commun, `src/app/ai/AiPaths.hpp` (le dossier `IA/` et ses sous-dossiers), puis deux chantiers en parallèle : l'Assistant (panneau, conversations, aperçu, paramètres, pièces jointes) et la mémoire (le moteur de mesure, les connaissances, le nettoyage, l'activité, la fenêtre). Chacun a rendu des aperçus dessinés avec la vraie police, en thème clair et sombre, à 1920 × 1200 et à 1280 × 800, regardés avant la fusion.
3. Des défauts vus sur ces aperçus et par les essais, corrigés avant la fusion :
   - le menu du clic droit des conversations ne se dessinait pas ;
   - dans Stockage PC, le tableau était trop étroit et coupait des noms de catégories ;
   - **un lien symbolique dans le cache** : Vider le cache le suivait et pouvait effacer ce qu'il visait, hors de `IA/`. Il retire maintenant le lien sans le suivre ; un essai le vérifie avec un fichier et un dossier extérieurs, qui restent intacts ;
   - quand on répondait Non à la confirmation, rien ne disait que le cache n'avait pas été vidé : c'est dit maintenant.
4. La fusion, puis les 95 suites ; l'aide F1, les notes et les comptes des essais mis à jour.
5. Une première passe de captures, avec l'exe Windows sous Wine (108 images), n'a montré aucun plantage, mais des défauts, tous corrigés avant la livraison :
   - **le champ de saisie se comportait comme un éditeur de code** : une marge à gauche, une bande sur la ligne courante, pas de retour à la ligne (une longue demande défilait de côté, son début caché), et une mention « modifié » qui, le champ vidé, s'écrivait par-dessus l'indication. C'est maintenant un champ de message, propre à l'Assistant (aucun autre champ du logiciel ne change) ;
   - **après Ctrl+Z, la carte disait toujours « Appliquée »** alors que les objets avaient disparu (de même après un redémarrage sans enregistrer). Elle dit maintenant l'état réel : Annulée, avec Réappliquer, ou « la vue a changé depuis » ;
   - **l'Assistant vouvoyait**, alors que le reste du logiciel tutoie : tout tutoie maintenant (« Décris ta demande… », « Moi · 17:41 ») ;
   - dans l'aperçu, une fois une variable choisie, le libellé perdait le nom de celle qui n'existe pas (« «  » n'existe pas ») ;
   - à 1280 × 800, le bouton Refuser débordait de la carte ; des avertissements des cartes étaient coupés par « … » : ils passent à la ligne ;
   - la conversation de démonstration périmée annonçait 31 objets mais sa carte disait « Créer 0 » ; c'était elle qui s'ouvrait en premier, avec une carte d'erreur : c'est maintenant « Découvrir l'Assistant » ;
   - à 1280 × 800, le volet prenait trop de place : il suit maintenant la largeur de la fenêtre ;
   - la réponse à PUMP_01_RUN disait ne voir aucune variable approchante alors que l'aperçu en proposait : elle cite les variables du projet ;
   - dans la mémoire : des noms de fonctions Windows s'affichaient (ils ne sont plus que dans une infobulle) ; une catégorie vide pouvait être cochée pour la supprimer ; des lignes de l'Activité étaient coupées avant l'essentiel ; ouverte depuis les paramètres, la fenêtre assombrissait le fond une seconde fois.
6. Une seconde passe complète, sur l'exe corrigé (108 images), a confirmé ces corrections et trouvé quatre petits défauts de plus, corrigés aussi :
   - pendant qu'une réponse s'écrivait, le bouton Réappliquer d'une carte au-dessus sortait de sa carte ;
   - en 1280 × 800, une conversation s'ouvrait défilée tout en bas (« Découvrir l'Assistant » sans sa question) : elle s'ouvre maintenant sur sa dernière demande ;
   - des libellés coupés du panneau (un titre de conversation, la vue visée, un nom de pièce jointe) ne se lisaient nulle part : ils se lisent au survol ; l'aide sous le champ dit partout « Maj+Entrée : à la ligne » ;
   - dans la mémoire, le détail d'une connaissance cachée par le filtre restait affiché (il suit maintenant la liste), et la recherche montrait le texte avec ses marques de mise en forme (`**`).
7. Une troisième passe (112 images) a confirmé ces corrections et trouvé un dernier défaut : en 1280 × 800, à la toute première ouverture, la conversation restait collée en bas, la question cachée (elle tenait à la première largeur du volet, qui rétrécit ensuite). Corrigé, avec son essai.
8. La passe finale, avec les exe livrés : **aucun défaut**, aucun plantage (en dehors de la limite connue de l'inspecteur à 1280, § 8).

## 5. Les captures

Dans `captures-1.14.2.zip` (111 images et le GIF), prises avec les exe livrés. Ce sont de **vraies captures** : l'exe Windows livré, lancé sous Wine et piloté par des séances de script. Elles ont été prises sur un projet neuf et neutre, `Station_Pompage` (une vue vide `Vue_Station` de 1920 × 1080, deux variables BOOL `Pompe_1_Marche` et `Pompe_2_Marche`), avec un dossier `IA/` absent au départ, comme à la première ouverture chez toi. En 1920 × 1200, et en 1280 × 800 et en thème sombre pour les écrans principaux. Un dossier par sujet :
- `1-assistant/` (17) : la première ouverture (« Découvrir l'Assistant », en 1920 et en 1280), deux textes coupés lus au survol, l'en-tête (Prévisualisation, Direct), les conversations (renommer en place, clic droit, supprimer avec sa confirmation), l'Assistant replié puis rouvert par Ctrl+Maj+I, le thème sombre ; après un redémarrage, la conversation retrouvée.
- `2-conversation/` (16) : les suggestions et le champ vide ; la demande de station de pompage tapée sur trois lignes ; « En attente… », « Réflexion… », l'écriture mot à mot et Arrêter (une carte « Annulée » au-dessus garde son bouton dans sa carte) ; la réponse et sa carte « Opérations proposées » ; une demande non reconnue et la réponse honnête ; la discussion qui défile, la saisie toujours visible ; en 1280 × 800.
- `3-previsualisation/` (25) :
  - l'aperçu des 31 objets dessiné par l'éditeur ; Appliquer (la vue dans l'éditeur) ; Ctrl+Z (la carte « Annulée » et Réappliquer) ; Ctrl+Y ;
  - « celle de droite en rouge » (avant → après, Modifier la demande) ;
  - PUMP_01_RUN introuvable : rien n'est inventé, les variables du projet proposées, Pompe_1_Marche choisie, le libellé qui le dit ;
  - le mode Direct et la suppression des vannes avec sa confirmation ;
  - la proposition périmée (sa carte, son aperçu) ;
  - en 1280 × 800 et en sombre.
- `4-parametres/` (7) : la fenêtre des paramètres, la clé masquée, les fournisseurs (ceux à venir grisés), le bouton Mémoire et apprentissage… et la mémoire ouverte par-dessus (un seul voile) ; en 1280 et en sombre.
- `5-pieces-jointes/` (9) : joindre un PDF, un CSV et un PNG (glissé-déposé), la progression, un `.exe` refusé, retirer une pièce, le message envoyé avec ses pièces.
- `6-memoire/` (24) :
  - les six onglets, avec les tailles réelles du dossier `IA/` ;
  - ajouter une connaissance, la consulter, la désactiver, le filtre par projet (le détail suit la liste), la recherche « pompe » ;
  - Nettoyage : le détail avant, la confirmation, le cache vidé (703,0 Ko, 14 fichiers), puis la Vue générale (2 conversations et 18 messages intacts) ;
  - la zone rouge : une catégorie vide grisée, une catégorie cochée, « Je comprends », la confirmation refusée (rien n'est supprimé) ;
  - l'Activité et son filtre, en 1280 et en sombre.
- `7-demonstration/` : le parcours principal en 13 images numérotées (ouvrir → demander → générer → proposer → prévisualiser → appliquer → Ctrl+Z → Ctrl+Y → paramètres → mémoire), et le même parcours en **GIF animé** (`demonstration_assistant_ia.gif`, 1280 × 800).

Le cache de démonstration (14 fichiers, 703 Ko) a été ajouté à la main pour que Nettoyage ait quelque chose à vider : la maquette n'écrit encore rien dans le cache. Le pointeur Windows n'apparaît pas : SDL ne le dessine pas dans une capture. Le sélecteur de fichiers de Windows n'est pas montré : les séances passent par l'explorateur de fichiers du logiciel, qui se pilote par script.

## 6. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.14.2.exe` : l'installateur ;
- `API-Studio-1.14.2-portable.zip` et `IHM-Studio-1.14.2-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.14.2.zip`) ;
- l'audit (`ASSISTANT-IA-AUDIT.md`) ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.14.2.exe`. Il met à jour la version installée au même endroit, garde tes données et reprend ton choix d'applications.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Le format des projets ne change pas (33) : les projets de la 1.14.1 s'ouvrent tels quels. L'Assistant n'écrit rien dans le projet. Ses données vont dans le dossier `IA/` des réglages de l'utilisateur.

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.14.2.exe` (40,0 Mo, 41 948 954 octets) :
`1989353C77F54C6FAE4B32D73899DB57C1B1BDFE7204AC6FBB76FC2F1807954C`

Les zips portables (167 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.14.2-portable.zip` (31,1 Mo) :
  `21E4F79BF4B04AE3EF3A93CBBDEB61F6BC023969CD489031AD3BC1C662A1EEDA`
- `IHM-Studio-1.14.2-portable.zip` (31,1 Mo) :
  `A30BD514262CC9CCABE8BA6FC8A1E31FF26BC78DD57516F8A4CB66BA0065025B`

### Les vérifications sous Windows

**Les deux exe Windows** sont compilés sous Linux avec MinGW-w64 et lancés sous Wine.
- `--version` répond `API Studio 1.14.2` et `IHM Studio 1.14.2`.
- `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant : 891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB.

**L'installateur** est compilé par Inno Setup 6.4.1 sous Wine. Il a été lancé en silencieux, pour tous les comptes, par-dessus la 1.14.1 livrée : « Installation process succeeded ».
- Le profil repris est « les deux » (« Profil : deux (avant : deux, version 1.14.1) »).
- Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été testés ; le manifeste compte 167 fichiers.
- Les associations `.xihm` et `.xapi` sont là.
- La copie de secours de la 1.14.1 est faite (168 fichiers, 125,5 Mo) et gardée : deux copies au plus, la plus ancienne (1.14.0) est partie.

## 7. Les fichiers créés et modifiés (livrable 1)

Depuis la 1.14.1, dans le code et les essais : 28 fichiers créés et 27 modifiés.

**Créés** (`src/app/ai/`, l'espace de noms `app::ai`) :

| Fichier | Rôle | Module de la demande (section 4) |
|---|---|---|
| `AiPaths.hpp` | le dossier `IA/` et ses sous-dossiers (le contrat commun) | CAIPersistenceManager |
| `AiJson` | le petit JSON (l'ordre et les champs inconnus gardés) | — |
| `AiOperations` | les opérations, leur validation, la commande unique (un Ctrl+Z), l'aperçu sur copie, l'empreinte de la vue | CAICommandParser, CAICommandValidator, CAICommandExecutor, CAIPreviewManager |
| `AiStore` | conversations, paramètres, pièces jointes, activité | CAIConversation, CAIAttachmentManager, CAIPersistenceManager |
| `AiDemo` | les réponses de démonstration (le futur fournisseur réseau prendra sa place) | — (remplacé par CAIHttpClient) |
| `AiAssistant` | la séance : envoi, états de génération, Arrêter, mode, appliquer, refuser | CAIManager |
| `AiPanel` | le panneau (en-tête, conversations, discussion, cartes, pièces, saisie) | — |
| `AiPreviewDialog`, `AiSettingsDialog` | les fenêtres de prévisualisation et des paramètres | — |
| `AiWorkspace.cpp` | le branchement dans l'écran du projet (le volet, Ctrl+Maj+I, la Disposition, Ctrl+K) | — |
| `AiMemory`, `AiMemoryJson` | la mesure de `IA/`, la RAM, le nettoyage, l'activité, la recherche | (section 12) |
| `AiKnowledge` | le registre des connaissances (intégrées et ajoutées) | CAIObjectRegistry (amorce) |
| `AiMemoryDialog` | la fenêtre Mémoire et apprentissage de l'IA (six onglets) | (section 12) |

Les essais : `tests/ai_assistant_test.cpp`, `tests/ai_memoire_test.cpp`. Les fichiers `.cpp` et `.hpp` comptent pour un fichier chacun dans les 28.

**Modifiés** :
- la compilation : `CMakeLists.txt`, `XpgAnalyzer.vcxproj`, `XpgAnalyzer.vcxproj.filters` ;
- la version : `src/core/Version.hpp`, `outils/config.ini` ;
- le branchement :
  - `src/app/App.cpp/.hpp` : la racine `IA/` et la commande `ia.memoire` ;
  - `src/app/Disposition.cpp`, `src/app/screens/DispositionWorkspace.cpp` : la ligne « Assistant IA » ;
  - `src/app/screens/MainAnalysisScreen.cpp`, `Screens.hpp` : le quatrième volet ;
  - `src/app/TopBar.cpp`, `TopBarWorkspace.cpp` : Affichage › Assistant IA ;
  - `GoToWorkspace.cpp` : Ctrl+K ;
  - `HistoryWorkspace.cpp`, `src/help/Shortcuts.cpp` : la touche ;
  - `ApiWorkspace.cpp` ;
- l'icône : `src/ui/Icons.cpp/.hpp` (`Icon::Assistant`) ;
- les séances de script : `src/app/ScriptRunner.cpp/.hpp` (les commandes `ia …` et `memoire-…`, pour les captures) ;
- l'aide et les notes : `tools/guide-ihm/guide-ihm.txt`, `src/hmi/HmiGuideText.cpp`, `src/hmi/HmiTutorialDeduce.cpp`, `src/help/ReleaseNotes.cpp` ;
- les comptes des essais : `tests/hmi_editor_test.cpp`, `tests/api2_test.cpp`.

## 8. Les limites et la suite

**Ce que la maquette ne fait pas, volontairement** (la demande : « Ne pas développer immédiatement toute l'IA ») :
- **aucune IA n'est branchée**, aucun réseau. La démonstration reconnaît six phrases par leurs mots-clés : elle ne comprend pas la langue, et elle le dit ;
- la clé API n'est gardée qu'en mémoire, le temps de la séance. Sa protection par DPAPI viendra avec la phase 5 ;
- le traitement des pièces jointes est simulé (la copie, elle, est réelle) ;
- Ctrl+Maj+I et l'Assistant n'existent que dans IHM Studio.

**Les limites connues** :
- à 1280 × 800, avec l'Assistant ouvert, l'inspecteur de l'éditeur reste étroit (des noms de propriétés coupés). Il suffit de tirer un séparateur, ou de replier l'Assistant (Ctrl+Maj+I). La correction de fond touche le séparateur commun à tous les panneaux : je ne l'ai pas changé à la veille d'une livraison ;
- deux fenêtres ouvertes l'une sur l'autre assombrissent encore deux fois le fond ailleurs dans le logiciel. C'est corrigé pour la mémoire ouverte depuis les paramètres de l'Assistant, pas encore en général ;
- après un redémarrage, une proposition appliquée puis non enregistrée apparaît « Annulée » : c'est l'état réel du projet rouvert.

**La suite (phases 3 à 9)**, décrite et chiffrée dans l'audit (§ 2.12) : environ **6 à 8 semaines** pour un copilote utilisable.
- Le client HTTPS : WinHTTP sous Windows, libcurl sous Linux. Le logiciel n'en a aucun aujourd'hui.
- Le fournisseur et son format d'opérations en JSON. L'IA ne fait jamais exécuter de C++.
- Le contexte de la vue, résumé.
- Le registre complet des 116 genres, tiré du code.
- Toutes les opérations, validées par rôle.
- La clé protégée par DPAPI.
- Les essais avec un fournisseur simulé.

**Pour lancer la phase 3, il me faut** :
- ton avis sur la maquette ;
- le fournisseur visé ;
- ce qu'un client accepte d'envoyer hors du site. Le réglage de confidentialité et un relais d'entreprise y répondent en partie.

**À regarder sur un vrai Windows** (ce que Wine et les essais ne montrent pas) :
- Joindre avec le sélecteur de fichiers de Windows, et un fichier glissé depuis l'Explorateur Windows sur le panneau (les captures passent par l'explorateur du logiciel) ;
- le dossier `IA` (`%APPDATA%\XpgAnalyzer\IA`) et « Ouvrir le dossier » de Stockage PC ;
- la mémoire vive de la Vue générale, mesurée par Windows (sous Wine, ce sont les chiffres de Wine) ;
- le champ de message au clavier AZERTY : les accents, Maj+Entrée, coller un long texte ;
- la molette dans la discussion et dans la mémoire.

**À part** :
- la **bibliothèque de sons** (écouter, importer, Mes sons) a été arrêtée en cours. Le travail partiel est mis de côté, hors de cette version, et peut reprendre à ta demande ;
- la **bibliothèque « Standard »** (objets, types et fonctions fournis d'office) n'est pas commencée.

**En attente**, comme avant :
- les raccourcis clavier avancés des éditeurs ;
- les requêtes HTTP, MQTT ou SQL depuis un script ;
- les grafcets côté IHM.
