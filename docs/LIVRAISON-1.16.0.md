# API Studio et IHM Studio 1.16.0 : une IA intégrée, les IA gratuites, la limite en direct

Livrée le 31/10/2026 au matin. Elle suit la 1.15.0 (`docs/LIVRAISON-1.15.0.md`), qui branchait l'Assistant IA sur Anthropic (Claude).

Ta demande : « ajoute les options dont on parlait, mais aussi ajoute une IA codée dans le code source pas forcément liée, et les IA liées, je dois voir la limite en live, doit pouvoir être liée à une IA gratuite ». Cette version fait les quatre :
- **une IA intégrée**, écrite dans le logiciel : gratuite, sans compte, sans réseau, sans modèle à télécharger. C'est le fournisseur d'une installation neuve ;
- **les IA gratuites** : le fournisseur « Compatible OpenAI » parle la langue d'Ollama et de LM Studio (un modèle sur ton PC, gratuit et sans clé) et de services en ligne qui ont une offre gratuite (OpenRouter, Groq, Google Gemini, Mistral) ;
- **la limite en direct** : sous l'en-tête du panneau, une bande dit ce que le service te laisse et ce que tu as consommé, avec un budget si tu veux ;
- **les options dont on parlait** : l'Assistant crée une vue, des variables et des types IHM, **lit le .XPG de ton automate** (joint à la demande) pour proposer ses variables et ses DDT en variables et types IHM, duplique avec les repères `$…$` comme Ctrl+Maj+D, et pose des formules (fx) pour animer.

En bref :
- **Toutes les propositions passent par le même contrôle**, quelle que soit l'IA : la même validation, la même carte, le même aperçu, **un seul Ctrl+Z**. Rien n'est appliqué sans toi.
- **Rien n'est écrit en dur** : ni clé, ni modèle. Les adresses des services sont une liste pour remplir la case ; le modèle reste une case libre (« Lister les modèles »).
- **Une installation existante garde son fournisseur.** Si tu étais en Démonstration ou sur Anthropic, rien ne change : l'IA intégrée se choisit dans les paramètres de l'Assistant.

**Ce qui n'a pas pu être fait ici : un essai contre un vrai Ollama ou un vrai service en ligne.** Je n'ai ni clé, ni Ollama, ni accès à ces services dans cet environnement. Tout a été essayé avec un **faux fournisseur local**, qui parle les deux protocoles (Anthropic et compatible OpenAI) avec les mêmes flux, les mêmes en-têtes de limite et les mêmes erreurs. Les essais ont aussi tourné sur l'exe Windows, sous Wine. L'IA intégrée, elle, n'a besoin de rien : elle est essayée pour de vrai. Le § 2 dit comment faire le premier essai avec Ollama.

## 1. Ce qui change

### L'IA intégrée, sans connexion

Ce n'est pas un grand modèle de langage : c'est un **moteur de règles en français**, écrit dans le logiciel. Il lit ta phrase (les verbes, les genres d'objets et leurs synonymes, les nombres, les couleurs, les positions) et la rapproche de ce que le logiciel sait déjà :
- les 116 genres d'objets du logiciel ;
- les objets de la vue ouverte et la sélection ;
- les variables du projet, et celles de l'automate quand tu joins son .XPG.

L'en-tête dit « IA intégrée – sans connexion » ; la bande dit « Sans connexion : gratuit, sans limite ». Aucun réglage.

Ce qu'elle comprend, par exemple :

| Demande | Exemples |
|---|---|
| Créer des objets | « Ajoute 3 pompes », « Mets deux boutons Marche et Arrêt sous la pompe P1 », « Ajoute un voyant rouge à droite de la cuve » |
| Un synoptique | « Crée un synoptique de station de pompage avec 3 cuves et 2 pompes par cuve » |
| Modifier | « Mets P1 en rouge », « Change le texte de Bouton_1 en Démarrer », « Agrandis la cuve », « Déplace V1 à droite de P2 », « Cache V1 » |
| Associer | « Associe P1 à Pompe_1_Marche », « Relie la cuve au niveau » |
| Créer des variables, un type, une vue | « Crée les variables Niveau_Cuve_1 en REAL et Defaut_P1 en BOOL », « Crée un type T_Pompe avec Marche en BOOL et Vitesse en REAL », « Crée une vue Vue_Pompes avec 3 pompes et un voyant » |
| L'automate (un .XPG joint) | « Propose les types IHM des DDT », « Crée les variables de l'automate qui commencent par PT » |
| Aligner, répartir, dupliquer, supprimer | « Aligne les boutons en haut », « Duplique P1 3 fois vers la droite », « Supprime les vannes » |
| Animer | « Fais clignoter V1 quand Defaut_V1 », « P1 verte si Pompe_1_Marche sinon rouge » |
| Questions | « Que sais-tu faire ? », « Combien de pompes ? », « Quelles variables ? » |
| Une suite | « Et une autre », « La même en bleu », « Mets-la en rouge » |

Ce qu'elle ne fait jamais :
- **inventer une variable** : un nom introuvable est dit, avec les plus proches du projet et la phrase pour la créer, à cliquer ;
- **remplacer un objet introuvable** : « Mets Bouton_9 en rouge » sans Bouton_9 donne « Je ne trouve pas « Bouton_9 » », avec les plus proches, et aucune proposition ;
- **deviner** : « la pompe » quand il y en a deux, « B1 » quand deux objets portent ce libellé, elle demande lequel ;
- **proposer ce qui ne change rien** : des boutons déjà alignés, une pompe déjà rouge, elle le dit ;
- **faire semblant** : une demande qu'elle ne comprend pas reçoit une réponse honnête et des formulations qu'elle comprend, à cliquer.

Elle comprend des phrases simples, pas tout le français. Pour le langage courant, branche une IA (sur ton PC ou en ligne) : l'aide le dit.

### Une IA gratuite : le fournisseur « Compatible OpenAI »

Beaucoup d'IA parlent la même langue que l'API d'OpenAI (« chat completions »). Le nouveau fournisseur la parle :
- **sur ton PC** : Ollama et LM Studio. Gratuit, sans clé, sans connexion : rien ne quitte le PC ;
- **en ligne** : OpenRouter (certains modèles gratuits), Groq, Google Gemini, Mistral. Une clé, et leurs conditions (souvent une offre gratuite limitée en demandes par minute ou par jour).

Dans les paramètres :
- la liste **Service** remplit l'adresse (« Autre » : l'adresse à la main) ;
- la **clé** est facultative pour un modèle sur ce PC ;
- **Lister les modèles** et **Tester la connexion** interrogent le service ;
- un serveur Ollama de l'usine (`http://` vers une adresse privée, par exemple 192.168.x) est accepté **sans clé seulement** : une clé ne passe jamais en clair sur le réseau.

### Les petits modèles gratuits

Deux pièges des petits modèles sont gérés :
- **un modèle sans appel d'outil** (le service répond « does not support tools ») : l'Assistant redemande aussitôt, et le modèle écrit sa proposition en JSON dans sa réponse. Elle passe par la **même lecture stricte** et la même validation. L'Assistant s'en souvient pour la suite. Le réglage **Outil** (Auto, Appel d'outil, JSON dans le texte) le force si besoin ;
- **une petite fenêtre de contexte** (Ollama coupe en silence ce qui dépasse, souvent 4 096 jetons) : sous 16 000 jetons, les consignes sont resserrées (le registre réduit aux genres utiles, la vue résumée). Le réglage **Fenêtre de contexte** donne la taille, ou Auto (celle que le service annonce).

### La limite et la consommation, en direct

Sous l'en-tête du panneau, une bande fine, toujours à jour :

| Ce qu'elle dit | Quand |
|---|---|
| « Limite : 47/50 requêtes · 38,4 k/40 k jetons · remise dans 12 s » | un service en ligne (Anthropic, OpenRouter, Groq…) : lu dans les en-têtes de sa dernière réponse |
| « Cette conversation : 12,3 k jetons · aujourd'hui 45 k » | ce que la conversation et la journée ont consommé |
| « Contexte de la dernière demande : 9,8 k / 32 k » | quand la fenêtre du modèle est connue |
| « Sur ce PC : pas de limite » | Ollama, LM Studio, un serveur de l'usine |
| « Sans connexion : gratuit, sans limite » | l'IA intégrée et la démonstration |
| « Limite atteinte : le service demande d'attendre 20 s » | une réponse 429 : le temps d'attente, qui décompte |

- Le compte à rebours avance chaque seconde, **sans rien envoyer**.
- Au survol : les chiffres exacts, l'heure de la mise à jour, leur source, les jetons de la dernière réponse (entrée, sortie, cache).
- Les cumuls sont gardés dans `IA\usage.json` : des nombres seulement, jamais ta demande, jamais ta clé.

### Le budget

Dans les paramètres, **Budget** : les jetons que tu t'accordes, par jour ou par mois (0 : aucun).
- La bande montre une jauge : verte, orange à 80 %, rouge au-delà.
- Le budget atteint, l'envoi demande d'abord confirmation : « Ton budget du jour est atteint (210 k / 200 k jetons). Envoyer quand même ? ». Rien n'est bloqué sans te le dire.

### Les nouvelles opérations

Pour toutes les IA (l'intégrée, Anthropic, les compatibles), avec le même contrôle :
- **créer une vue** : la vue neuve (nom, taille, fond, description), puis les objets dedans. Après Appliquer, elle s'ouvre dans l'éditeur ; un seul Ctrl+Z retire la vue et ses objets ;
- **créer des variables IHM** (nom, type, valeur initiale, description) et **des types IHM** (une structure et ses membres), à ta demande seulement. La carte le dit (« Crée aussi : 12 variables IHM, 2 types IHM ») et l'aperçu donne la liste avant que tu appliques. Un type inconnu est refusé avec les plus proches (REEL → REAL). Une opération plus bas peut citer ce qui est créé plus haut (associer un voyant à la variable neuve) ;
- **l'automate vers l'IHM, par un .XPG joint** : IHM Studio n'ouvre pas les exports de Control Expert ; tu joins donc à ta demande le fichier .XPG du programme (bouton Joindre). L'Assistant le lit sans l'ouvrir comme projet et le dit (« J'ai lu l'automate de MAST.XPG : … variables globales et 28 types (DDT) »). Il propose ensuite ses globales qui n'ont pas encore de variable IHM et ses DDT qui n'ont pas encore de type IHM, avec la conversion de la migration 1.12.0 (EBOOL → BOOL, un DDT devient le type IHM du même nom, dans l'ordre des dépendances ; la description reprend le commentaire ; le nom IHM est celui de la globale, sans « API. »). Une instance de bloc (TON, un DFB) est écartée. Ça marche avec l'IA intégrée comme avec une IA connectée ;
- **dupliquer avec les repères** : « duplique Cuve_1 trois fois avec les repères, 180 px à droite ». Les copies sont faites **par le moteur de Dupliquer… (Ctrl+Maj+D)** : noms en série, `$Cuves[0]$` devient `$Cuves[1]$`, `$Cuves[2]$`, `$Cuves[3]$`. Le résultat est exactement celui de l'éditeur ;
- **les formules (fx)** : une valeur qui commence par `=` pilote la propriété, comme dans l'inspecteur. Une couleur selon un état, visible si, clignotant si. Le logiciel la vérifie comme l'inspecteur : lisible, chaque nom lu existe, les repères `$…$` permis.

### Les paramètres

La fenêtre des paramètres suit le fournisseur choisi :
- IA intégrée, Démonstration : rien à régler (les cases de la connexion, du modèle, de la clé et du budget sont grisées) ;
- Anthropic : comme en 1.15.0, plus le Budget ;
- Compatible OpenAI : Service, Adresse, Clé (facultative sur ce PC), Modèle (Lister les modèles), Fenêtre de contexte, Outil, Budget, Tester la connexion.

Elle reste lisible en 1280 × 800.

### L'aide et les notes

- Un nouveau sujet d'aide F1, **« L'IA intégrée (sans connexion) »** : ce qu'elle comprend, comment elle lit, ce qu'elle ne fait jamais.
- Dans **« L'Assistant IA »** : le fournisseur compatible et ses services, « La limite et la consommation, en direct », « Le budget », « Une IA gratuite sur ce PC : Ollama » (pas à pas), « Un service en ligne gratuit ».
- Dans **« Ce que l'Assistant IA sait et ce qu'il peut proposer »** : les nouvelles opérations, l'automate, les repères, les formules.
- 11 notes de version 1.16.0 (Nouveautés).

## 2. Pour l'essayer

**L'IA intégrée, tout de suite** : ouvre une vue, puis l'Assistant (**Ctrl+Maj+I**). Si l'en-tête dit autre chose que « IA intégrée », clique dessus et choisis le fournisseur **IA intégrée (sans connexion)**. Demande « Que sais-tu faire ? », puis « Ajoute 3 pompes ».

**Une IA gratuite sur ton PC, avec Ollama (une dizaine de minutes)** :
1. Installe Ollama (ollama.com) ; il tourne en arrière-plan.
2. Dans une invite de commandes : `ollama pull llama3.2` (environ 2 Go ; un modèle plus gros comprend mieux, mais demande plus de mémoire et répond plus lentement).
3. Dans les paramètres de l'Assistant : Fournisseur **Compatible OpenAI**, Service **Ollama** (l'adresse se remplit : `http://localhost:11434/v1`), **Lister les modèles**, choisis le tien, **Enregistrer**. Pas de clé.
4. La bande dit « Sur ce PC : pas de limite ». Demande, par exemple : « Ajoute une cuve et deux pompes en dessous ».
5. Ollama coupe en silence au-delà de sa fenêtre de contexte (souvent 4 096 jetons). Pour de meilleures réponses, agrandis-la : la variable d'environnement `OLLAMA_CONTEXT_LENGTH=16384`, relance Ollama, et donne 16384 dans **Fenêtre de contexte**.

**Un service gratuit en ligne** (OpenRouter, Groq, Gemini, Mistral) : crée un compte chez eux, copie leur clé dans **Clé API**, choisis le Service, puis **Lister les modèles**. La bande montre en direct ce qu'il te reste. Lis leurs conditions : certaines offres gratuites se réservent le droit d'utiliser les demandes.

**Les variables de ton automate** : dans l'Assistant, bouton **Joindre**, choisis le .XPG exporté de Control Expert, et demande « Propose les types IHM des DDT », puis « Crée les variables de l'automate qui commencent par … ». Regarde l'aperçu, puis Appliquer.

Si quelque chose ne va pas, l'erreur est dite en clair dans la conversation. Envoie-moi la capture : je corrigerai.

## 3. Les exigences de la demande, et comment elles sont tenues

| La demande, mot pour mot | Ce qui la tient |
|---|---|
| « Ne jamais exécuter du code C++ arbitraire généré par l'IA » | Rien ne change : une liste fermée d'opérations, lue strictement, quelle que soit l'IA. Le JSON écrit dans le texte par un petit modèle passe par la **même** lecture stricte. Une formule est vérifiée comme dans l'inspecteur, en lecture seule. L'IA intégrée ne produit que ces mêmes opérations. |
| « Ne jamais inventer une variable automate ou un identifiant » | Le logiciel vérifie chaque nom (variables, objets, genres, propriétés, noms lus par une formule). Une variable ou un type n'est créé que sur demande, et la carte le dit. L'IA intégrée cite les plus proches et ne prend jamais une variable au hasard. |
| « Ne pas coder en dur une clé API ni un modèle » | Aucune clé ; le modèle est une case libre. Les adresses des services sont une liste pour remplir la case, modifiable (« Autre »). Un nom de modèle n'apparaît qu'en exemple : dans l'aide (`ollama pull llama3.2`) et dans l'indication grise de la case Modèle, qui suit le fournisseur. |
| « Pour une application distribuée, protéger la clé API via une architecture adaptée plutôt que de l'intégrer en clair dans l'exécutable » | Comme en 1.15.0 (aucune clé dans l'exe ; DPAPI ou un relais). En plus : une clé n'est jamais envoyée en `http://` en clair vers un serveur du réseau. Les gratuits sur ce PC n'en demandent pas. |
| « Le réseau ne doit jamais bloquer le thread principal » | Le fournisseur compatible tourne sur le même fil de travail que celui d'Anthropic. La bande lit l'état sans attendre ; le compte à rebours est calculé, pas demandé. L'IA intégrée répond sur place, sans réseau. |
| « Ne jamais prétendre que le modèle distant est réentraîné si ce n'est pas réellement le cas » | Rien ne le dit. L'aide dit que l'IA intégrée est un moteur de règles et qu'elle n'apprend pas ; un petit modèle « s'en souvient » seulement au sens d'un réglage gardé par le logiciel (Outil). Les captures ont été relues pour cela. |

## 4. Les essais

**100 suites sur 100**, sur le tout fusionné, puis de nouveau après chaque série de corrections des captures (la dernière fois sur la version livrée).

Trois suites sont nouvelles :
- `aiintegree` (257 vérifications) : l'IA intégrée sur des dizaines de phrases réelles (chaque phrase de son aide F1 a son essai) (créer, un synoptique avec les nombres de la phrase, modifier, associer, aligner, dupliquer, supprimer, animer, créer des variables et une vue, les questions, les suites « et une autre », « mets-la en rouge ») ; les genres par synonyme et une faute près ; les nombres en lettres ; les noms d'objets sans casse ; une variable introuvable jamais inventée (les plus proches) ; un objet introuvable jamais remplacé par un autre ; un nom ambigu qui fait demander ; ce qui ne change rien n'est pas proposé ; une demande non comprise ; **le .XPG d'essai lu** (MAST.XPG : les types IHM de ses 28 DDT, des variables IHM converties) ; chaque proposition validée par le même contrôle qu'une IA en ligne ;
- `aicompatible` (168 vérifications) : le corps compatible OpenAI (messages, outil, images, appels d'outil et réponses) ; le flux (texte, appels d'outil coupés partout et recollés, l'usage final, `[DONE]`, une coupure) ; les erreurs (401, 404, 429 et ses reprises, 5xx, le message du service) ; le modèle sans outil (la reprise, le bloc JSON lu strictement, un bloc illisible refusé) ; les en-têtes de limite (Anthropic et OpenAI ; les durées « 6m0s », les dates RFC 3339) ; l'usage cumulé (jour, mois, le fichier relu, ni clé ni demande dedans) ; la bande du panneau (les textes, le compte à rebours qui avance sans envoi, le budget à 80 % et dépassé, la confirmation) ; les paramètres ; un serveur local qui annonce sa limite (la bande et l'infobulle disent la même chose) ; un autre service qui efface la limite de l'ancien ; **de bout en bout avec libcurl** contre un petit serveur local compatible ;
- `aioptions` (174 vérifications) : chaque nouvelle opération validée, refusée quand il faut (un nom pris, un type inconnu, une formule qui lit une variable absente, une vue au nom pris, des types dans le mauvais ordre), appliquée en **un** Ctrl+Z, relue d'une conversation ; une vue puis ses objets (la vue créée s'ouvre, Ctrl+Z rend la carte « Annulée », Réappliquer la remet - par une IA en ligne et par l'IA intégrée) ; des variables puis une association ; l'automate (des globales et des DDT, sans ce qui existe déjà) ; dupliquer 3 fois avec repères = le résultat de l'éditeur ; une formule de couleur et de visibilité ; la lecture stricte des nouveaux champs.

Les suites de la 1.15.0 passent toujours, dont `aireseau` (190), `airegistre` (251), `aiassistant` (242), `aimemoire` (205).

502 notes, 71 versions, 267 sujets d'aide ; le guide et son C++ disent la même chose. Le projet Visual Studio compte les mêmes fichiers que CMake.

**Le chemin jusqu'ici** :
1. Un contrat commun d'abord (le fournisseur, l'usage et la limite, les nouvelles opérations, les réglages), puis trois chantiers en parallèle : **l'IA intégrée**, **les fournisseurs** (compatible OpenAI, la limite, le budget, le faux fournisseur) et **les options** (vue, variables, types, automate, repères, formules).
2. La fusion des trois, sans conflit ; la version, les notes, l'aide ; toutes les suites ; les deux exe Windows compilés.
3. **Une première passe de captures sous Wine** (l'exe Windows, 11 séances, aucun plantage) a trouvé une vingtaine de défauts, tous corrigés :
   - **l'IA intégrée devinait** : un objet cité qui n'existe pas (« Mets Bouton_9 en rouge ») était remplacé en silence par le dernier objet cité ; « V1 » visait à la fois la vanne et le voyant ; « le texte de Bouton_1 » était lu comme le genre Texte ; « X en REAL et Y en BOOL » créait les deux en BOOL ; un déplacement pouvait poser un objet sur un autre ou hors de la vue. Elle dit maintenant ce qu'elle ne trouve pas, demande quand un nom est ambigu, évite les autres objets et reste dans la vue. Elle sait aussi créer un type IHM. Sa suite d'essais est passée de 170 à 257 vérifications ; l'ancienne version en échoue 46 ;
   - **créer une vue par l'IA intégrée** : la vue neuve ne s'ouvrait pas, la carte restait « Appliquée » après Ctrl+Z, l'aperçu disait le nom de la vue ouverte. Corrigé, avec un essai ;
   - **la bande** : pour un serveur local qui annonce une limite, l'infobulle la donnait et la bande disait « pas de limite ». Les deux disent maintenant la limite annoncée ; un autre service efface celle de l'ancien ;
   - **l'affichage** : les formulations longues dépassaient le bord droit du panneau ; dans les paramètres, les cases inutiles restaient actives et deux libellés étaient coupés ; la conversation d'accueil d'une installation neuve décrivait la démonstration ; le « ? » de la fenêtre du budget partait seul à la ligne ;
   - **les textes** de l'IA intégrée : les pluriels (« 9 pompe »), « En couleur » au lieu de « En rouge », une phrase sur les formules dite sans formule, 112 genres au lieu de 116.
4. **Un manque, trouvé à cette occasion** : IHM Studio n'ouvre pas les exports de Control Expert, donc « crée les variables de l'automate » ne pouvait rien donner. D'où **le .XPG joint à la demande** (§ 1).
5. Les 100 suites de nouveau, la version Windows recompilée, puis **une deuxième passe de captures** sur l'exe corrigé (9 séances, aucun plantage) : sept points sur huit étaient corrigés. Elle a trouvé que **le .XPG joint était lu puis ignoré** dans IHM Studio : le modèle de l'application, vide d'automate, passait avant lui. Elle a aussi vu des titres de conversation faux (« Ajoute une vanne » titrée « Supprimer les vannes », héritage de la démonstration), des suggestions de la démonstration montrées avec une IA connectée et deux boutons de la clé restés actifs. Tout est corrigé.
6. **Une troisième et une quatrième passe**, courtes : le .XPG avec l'IA intégrée de bout en bout (28 types IHM, des variables converties, Ctrl+Z), les titres, les suggestions ; puis le bouton Afficher de la clé, le message pour des instances de bloc, et « l'automate de MAST.XPG » au lieu de « l'automate relié ». Aucun plantage.

## 5. Les captures

Dans `captures-1.16.0.zip` (223 images et le GIF). Ce sont de **vraies captures** : l'exe Windows, lancé sous Wine et piloté par des séances de script, sur un projet neuf et neutre, `Station_Pompage`.

**L'IA intégrée répond pour de vrai.** **Les IA connectées sont le faux fournisseur local** (`tests/fixtures/ia/faux_fournisseur.py`) : il tient lieu d'Anthropic et d'un Ollama sur ce PC (127.0.0.1:11434), avec les mêmes protocoles, les mêmes en-têtes de limite et les mêmes erreurs ; ses réponses sont enregistrées d'avance, ce n'est pas un vrai modèle qui répond. Le .XPG joint est `MAST.XPG`, le projet d'essai du dépôt.

Un dossier par sujet :
- `1-integree/` : l'en-tête « IA intégrée – sans connexion » et la bande « Sans connexion : gratuit, sans limite » ; la conversation d'accueil d'une installation neuve ; « Que sais-tu faire ? » ; créer, le synoptique, modifier, associer, une variable inconnue jamais inventée, dupliquer, un seul Ctrl+Z ; et chaque défaut de la première passe refait, corrigé (un objet introuvable, « le texte de », un nom ambigu, les placements, les pluriels, déjà aligné…).
- `2-compatible/` : les paramètres « Compatible OpenAI » (la liste Service, l'adresse remplie par Ollama, la clé facultative, Lister les modèles, l'Outil, la Fenêtre de contexte, le Budget, Tester) ; l'en-tête « Ollama – modele-essai » ; une demande et sa proposition ; le modèle « sans outil » (la proposition reprise en JSON dans le texte, lue et appliquée) ; le petit modèle (contexte 2,1 k / 4,1 k).
- `3-limite/` : la bande en direct, avec Anthropic puis avec le compatible : 19, 18, 17 sur 20 d'une demande à l'autre, le compte à rebours qui avance, l'infobulle du détail, la consommation, le budget à 80 % (orange) puis dépassé (rouge) et la confirmation « Envoyer quand même ? », une 429 (« Limite atteinte : le service demande d'attendre 1 min ») ; « Sur ce PC : pas de limite ».
- `4-operations/` : créer une vue (l'aperçu « Vue neuve « Vue_Pompes » », Appliquer : la vue s'ouvre, Ctrl+Z : « Annulée », Réappliquer) ; créer des variables (chacune son type) et un type IHM ; dupliquer avec repères (`$Cuves[1]$` → `$Cuves[3]$`) ; les formules de couleur, de visibilité et de clignotement ; un seul Ctrl+Z.
- `5-parametres-et-aide/` : la fenêtre des paramètres pour chaque fournisseur, en 1920 × 1200 et en 1280 × 800 (avec l'IA intégrée et la Démonstration, la connexion grisée) ; l'aide F1 « L'IA intégrée », « La limite et la consommation, en direct », « Une IA gratuite sur ce PC : Ollama », « Ce que l'Assistant IA sait ».
- `6-demonstration/` : le parcours en 22 images numérotées (l'IA intégrée : demander, la carte, l'aperçu, Appliquer, la vue créée qui s'ouvre, le .XPG lu et ses 28 types ; puis le compatible : paramètres, Lister, demander, la limite qui baisse, la 429), et le même parcours en **GIF animé** (`demonstration_ia_160.gif`, 1280 de large).
- `7-automate-xpg/` : sans .XPG, l'IA dit comment le joindre ; MAST.XPG joint : « J'ai lu l'automate de MAST.XPG : 251 variables globales et 28 types (DDT) », la carte « Crée aussi : 28 types IHM », l'aperçu (les membres et les commentaires de l'automate), appliqués, les types dans l'arbre ; les variables (noms sans « API. », EBOOL → BOOL), appliquées, Ctrl+Z ; des instances de bloc, dites ; le même fichier avec le faux compatible.

Quatre passes en tout (une vingtaine de séances Wine) : **aucun plantage**. Le pointeur Windows n'apparaît pas : SDL ne le dessine pas dans une capture.

## 6. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.16.0.exe` : l'installateur ;
- `API-Studio-1.16.0-portable.zip` et `IHM-Studio-1.16.0-portable.zip` : chaque application, sans installation ;
- les captures (`captures-1.16.0.zip`) et le GIF de la démonstration ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.16.0.exe`. Il met à jour la version installée au même endroit, garde tes données (le dossier `IA\` et tes conversations compris) et reprend ton choix d'applications.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisis « Informations complémentaires », puis « Exécuter quand même ».

Le format des projets ne change pas (33). L'Assistant n'écrit rien dans le projet tant que tu n'appliques pas.

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.16.0.exe` (40,8 Mo, 42 786 081 octets) :
`5C295194BCD46140BB4F8C53091C624C97421E38991E0E06C76F414C61672FB9`

Les zips portables (167 fichiers chacun ; décompresse, puis lance l'exe) :
- `API-Studio-1.16.0-portable.zip` (31,7 Mo) :
  `4D0302F155FF77D347969AE9873589245B46315F1C7BCBC4DC084411DA1F4EA6`
- `IHM-Studio-1.16.0-portable.zip` (31,7 Mo) :
  `B8EA259731D011D70893B7E45014934E6E50652AB3F6017A9728580D5DCC7A1B`

### Les vérifications sous Windows

**Les deux exe Windows** sont compilés sous Linux avec MinGW-w64 et lancés sous Wine.
- `--version` répond `API Studio 1.16.0` et `IHM Studio 1.16.0`.
- `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant : 891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB.

**L'installateur** est compilé par Inno Setup 6.4.1 sous Wine. Il a été lancé en silencieux, pour tous les comptes, par-dessus la 1.15.0 livrée : « Installation process succeeded ».
- Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été testés ; le manifeste compte 167 fichiers.
- Les associations `.xihm` et `.xapi` sont là.
- La copie de secours est faite ; deux copies au plus : la 1.15.0 est gardée.

## 7. Les fichiers créés et modifiés

Depuis la 1.15.0, dans le code et les essais : 11 fichiers créés et 39 modifiés (un `.cpp` et son `.hpp` comptent pour deux).

**Créés** (`src/app/ai/`) :

| Fichier | Rôle |
|---|---|
| `AiLocal` | l'IA intégrée : le moteur de règles en français, les suggestions |
| `AiOpenAI` | le fournisseur compatible OpenAI : le corps, le flux, les erreurs, le modèle sans outil, la liste des services, les en-têtes de limite |
| `AiUsage` | la consommation (jour, mois, conversation, `IA\usage.json`), le budget, le texte de la bande |
| `AiAutomate` | l'automate vers l'IHM : la lecture d'un .XPG joint, ses globales et ses DDT en variables et types IHM |

Les essais : `tests/ai_integree_test.cpp`, `tests/ai_compatible_test.cpp`, `tests/ai_options_test.cpp` ; le faux fournisseur `tests/fixtures/ia/faux_fournisseur.py` parle maintenant aussi le compatible OpenAI.

**Modifiés** :
- l'Assistant : `AiAssistant` (le .XPG joint, l'empreinte d'une vue neuve), `AiPanel` (la bande, les codes qui se coupent), `AiSettingsDialog`, `AiStore` (les réglages, les pièces jointes .XPG), `AiDemo.cpp` (l'accueil, les titres), `AiWorkspace.cpp` (l'automate d'un .XPG), `AiProvider` (l'usage et la limite), `AiAnthropic` (ses en-têtes de limite et son usage), `AiHttp`, `AiPrompt` (les consignes des nouvelles opérations, le JSON dans le texte, les consignes resserrées), `AiOperations` (vue, variables, types, repères, formules), `AiContext` (les DDT), `AiPreviewDialog`, `AiWorkspace.cpp` ;
- l'interface : `src/ui/widgets/Controls.cpp` (un bouton bascule grisé se dessine grisé) ;
- les séances de script : `src/app/ScriptRunner.cpp` (`ia service`, `ia budget`, `ia limite`, `ia contexte`, `ia outil`) ;
- la compilation : `CMakeLists.txt`, `XpgAnalyzer.vcxproj`, `XpgAnalyzer.vcxproj.filters` ;
- la version, l'aide et les notes : `src/core/Version.hpp`, `outils/config.ini`, `src/help/ReleaseNotes.cpp`, `tools/guide-ihm/guide-ihm.txt`, `src/hmi/HmiGuideText.cpp`, `src/hmi/HmiTutorialDeduce.cpp` ;
- les comptes des essais : `tests/hmi_editor_test.cpp`, `tests/ai_assistant_test.cpp`, `tests/ai_registre_test.cpp`.

## 8. Les limites et la suite

**Les limites connues** :
- **Pas essayé contre un vrai Ollama, LM Studio ou service en ligne**, ni sur un vrai Windows (voir le § 2). Le protocole suit leur documentation, et le faux fournisseur le rejoue, mais le premier essai réel reste à faire.
- **L'IA intégrée** comprend des phrases simples :
  - une phrase composée (« ajoute une cuve et mets-la en bleu ») n'est pas comprise d'un coup : deux demandes ;
  - « 50 % » n'est pas lu comme une taille ;
  - « renommer » passe par une copie exacte puis le retrait de l'original (l'aperçu le montre) : une formule ailleurs qui citait l'ancien nom est à revoir.
- **La limite** :
  - la bande dit ce qui reste, pas la période de la limite (par minute ou par jour) ; l'infobulle donne l'heure de la remise ;
  - les jetons d'une réponse arrêtée en cours (Arrêter) ne sont pas comptés ;
  - Fenêtre de contexte « Auto » n'est connue qu'après « Lister les modèles », et seulement si le service la donne.
- **Les nouvelles opérations** :
  - une vue créée par l'Assistant est toujours une vue ordinaire (pas une popup) ;
  - les DFB de l'automate ne sont pas convertis (seulement les DDT) ;
  - un repère sans indice (`$Nom$`) reste tel quel dans les copies, comme dans l'éditeur.
- **Le .XPG joint** :
  - il est gardé le temps de la séance : après un redémarrage d'IHM Studio, joins-le de nouveau ;
  - il se lit sur place (une fraction de seconde pour le projet d'essai de 891 variables) : un très gros programme fera attendre l'éditeur un instant ;
  - ses variables ne s'associent pas directement à un objet : on crée d'abord la variable IHM, puis on l'associe (l'Assistant le fait dans la même proposition si tu le demandes).
- Un PDF joint ne part pas vers un service compatible (seulement vers Anthropic) ; une image, si le modèle les accepte.

- Vu aux captures, sans gravité, pas repris :
  - une réponse 429 cite le texte du service tel quel, en anglais (« Rate limit reached for requests »), dans une phrase en français ;
  - « Déplace Voyant_1 à droite de P2 », quand toute la place proche est prise, pose l'objet à la première place libre de la rangée, qui peut être loin (elle le dit) ;
  - l'avertissement de l'aperçu d'une vue neuve compte mal les objets non reliés (« 3 objets » pour 4) ;
  - dans la conversation d'accueil, les exemples sont du texte, pas des boutons (les suggestions sous la saisie, elles, se cliquent).

**À regarder sur un vrai Windows** :
- l'IA intégrée sur tes propres vues (dis-moi les phrases qu'elle ne comprend pas : je les ajouterai) ;
- Ollama avec un modèle de ton choix (§ 2) ;
- un service gratuit en ligne et sa bande de limite.

**À part** :
- la bibliothèque de sons (mise de côté, à reprendre à ta demande) ;
- la bibliothèque « Standard » ;
- les raccourcis clavier avancés des éditeurs ; les requêtes HTTP, MQTT ou SQL depuis un script ; les grafcets côté IHM.
