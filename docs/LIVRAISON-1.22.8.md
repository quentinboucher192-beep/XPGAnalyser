# API Studio et IHM Studio 1.22.8 : des modèles de vues à reproduire, des unités créées par l'IA

Suite de la 1.22.7, faite à partir de trois demandes du 02/11 :
- « ajoute un fichier d'approbation des vues : un modèle à répliquer, couleurs etc. ; l'IA doit comprendre qu'on peut lui donner des modèles de vues et qu'elle doit les reproduire avec les données de l'application » ;
- ta capture des 26 erreurs « unité inconnue » : « il faut lui donner le droit de créer des unités » ;
- la fenêtre « Appliquer malgré 26 erreurs » : « ajouter une scrollbar ici ».

Elle se met à jour par-dessus la 1.22.7 et garde tes données.

## 1. Ce qui change pour toi

### Un modèle de vues à reproduire

Joins à une conversation un **PV d'approbation de vues** (un PDF) ou des **captures d'écran** (PNG, JPEG). L'Assistant les lit comme un **modèle à reproduire**. Le modèle donne la **forme**, et les **données** viennent toujours de ton projet.

- **Ce qui est lu**, en arrière-plan :
  - la page de garde : le numéro du document, son titre, sa dernière révision ;
  - **chaque vue** (les pages « VUE : <nom> ») : son nom, l'approbation et les commentaires quand ils sont écrits en texte ;
  - **l'image de chaque vue**, recadrée sur la capture (sans le cartouche ni la marge de la page) ;
  - **la charte graphique**, calculée sur les images : les couleurs dominantes avec leur part, et l'en-tête et le bandeau du bas répétés d'une vue à l'autre (« en-tête commun à 12 vues sur 13 »).
- **Un PV est reconnu tout seul**, par son texte. Une **image** ne l'est pas : dans **Contexte de la discussion**, le geste **Modèle de vues** de sa ligne la fait lire comme un modèle. Le même geste écarte un PDF qui n'en est pas un. Le contexte dit « Modèle de vues : 13 vues ».
- **Demande ensuite** par exemple « Reproduis ce modèle avec les données du projet », « Crée la vue Station comme dans le PV » ou « Même charte que le PV ». L'IA reprend les vues, leur disposition, les couleurs, l'en-tête, le pied et la navigation. Elle **relie chaque objet aux vraies données** par son repère : variables IHM, automate lu d'un .XPG joint, reports, types. C'est la **règle 24** de ses consignes.
- **Rien n'est inventé.** Un objet sans donnée trouvée est créé **non relié**, et le logiciel le liste « à relier » sous la réponse. La vérification, l'aperçu, Générer et Compiler et Appliquer sont les mêmes que pour toute proposition.
- **Peu de jetons** :
  - l'IA reçoit un **résumé** court du modèle à la place du PDF : environ 1 000 jetons pour un PV de 13 vues ;
  - elle reçoit l'**image d'une vue** seulement quand elle la demande, avec un nouvel outil (`voir_vue_modele`) : environ 1 600 jetons par image chez Anthropic ;
  - une IA sans outils reçoit les images une fois, avec la demande qui reproduit.
- La ligne d'activité le dit : « L'IA regarde la vue Station du modèle (voir_vue_modele)… ».
- **Un calque de référence**, si tu le demandes (« mets l'image du PV en calque ») : l'image de la vue devient une ressource du projet, posée dans la vue comme un objet Image **verrouillé**, à demi transparent, **masqué en marche**. Tu vois le modèle sous ta vue dans l'éditeur ; en marche, il n'apparaît pas.

### L'IA crée les unités qui manquent

Sur ta capture, 26 variables en mmCE étaient refusées : « unité inconnue ». L'IA n'avait pas le droit de créer une unité. Elle l'a maintenant.

- **Une nouvelle opération, `creer_unite`** : le symbole, la famille, le facteur vers l'unité de référence, le décalage et le libellé. L'IA la met dans sa proposition, puis s'en sert pour les variables (`creer_variables` accepte une unité par variable).
- **Les mêmes contrôles que Configuration › Unités de mesure** : l'éditeur et l'Assistant partagent maintenant le même code de vérification.
  - Un symbole déjà pris est refusé avec la raison : « existe déjà (Pression, unité fournie) ».
  - Un facteur nul ou absent est refusé : jamais de 1 par défaut.
  - Une famille inconnue est refusée, sauf si l'IA la crée avec son unité de référence.
  - Une unité identique à une existante est réutilisée.
- **Le facteur est vérifié.** Le logiciel connaît les unités courantes : mmCE, cmCE, mCE, inH2O, mmHg, atm, degF, L/min, Nm3/min, t/h, kg/s, %HR, kW, kWh, tr/min… Un facteur qui s'écarte de plus de 1 % est un avertissement. L'aperçu montre la conversion : « Unité mmCE (pression) : 1 mmCE = 9,80665 Pa ». Le facteur est gardé exact (9.80665, pas 9.807).
- **L'unité s'applique avant les variables qui l'emploient**, même si l'IA l'écrit après, et tout se reprend d'un seul Ctrl+Z.
- **L'IA peut aussi modifier une unité du projet** (`modifier_unite` : facteur, décalage, libellé). Une unité fournie reste verrouillée.
- Ses consignes ont une **règle 25** : les familles fournies et leur unité de référence. Le contexte de la demande liste les unités du projet avec leur conversion, seulement quand le projet en a.
- **Dans la prévisualisation**, sur une erreur « unité inconnue » :
  - **Corriger rapidement** crée l'unité quand le logiciel la connaît ;
  - **Tout corriger rapidement** n'en crée qu'**une par symbole** : les 26 lignes de ta capture donnent une seule unité mmCE, et les 26 erreurs disparaissent ;
  - **Créer l'unité « … »…** ouvre une petite fenêtre, remplie d'office pour une unité connue, à compléter sinon (la famille, le facteur) ;
  - une unité inconnue du logiciel (« xyz ») est grisée avec la raison, et « Créer l'unité… » reste dessous.
- L'unité va dans la **proposition**, jamais directement dans le projet. **Revenir** la retire, et la fenêtre reste ouverte.

### « Appliquer malgré les erreurs » : la liste défile

- La fenêtre de confirmation **défile** quand la liste des erreurs est longue. Ses boutons restent toujours visibles en bas.
- **Les erreurs qui ne diffèrent que par le nom tiennent sur une ligne** : « Variable « A », « B », « C » et 23 autres (26 erreurs) : … ».

## 2. Les essais

Sous Linux : **130 suites d'essais sur 130** réussies (CTest), dont deux nouvelles.

Au premier passage, `ai_registre_test` a échoué. Il fige la liste des opérations de l'IA (27) et la taille d'une demande (moins de 53 Ko). Les unités ajoutent 2 opérations, et la demande de la station d'essai fait maintenant 53,5 Ko. L'essai est mis à jour : 29 opérations, moins de 54 Ko. Les exécutables livrés n'ont pas changé.

- **Nouvel essai : `ai_modele_vues_test`** (134 vérifications). Il fabrique en mémoire un PV d'approbation de 3 vues et des captures, puis vérifie :
  - le recadrage de l'écran dans une page, la réduction, les couleurs dominantes et l'en-tête commun ;
  - la reconnaissance du PV par son texte, la lecture des vues, des approbations, des commentaires et des commentaires annotés à la main ;
  - le PV rendu par PDFium, recadré, et ce que l'IA reçoit (le résumé, l'outil, l'image) ;
  - une conversation de bout en bout : la pièce lue avant l'envoi, le résumé, l'image demandée par l'outil, la proposition. Le ventilateur est relié à la variable existante trouvée par son repère, l'afficheur sans donnée reste non relié et listé « à relier », et le calque de référence est verrouillé, masqué en marche ;
  - les captures PNG : le geste Modèle de vues, le geste inverse, une capture nommée dans la demande.
- **Banc des jetons, `ai_jetons_test`** : un nouveau cas, un PV de 13 vues joint. Il coûte 20 502 jetons au total, comme une conversation ordinaire (le résumé remplace le PDF). Une vue montrée par l'outil ajoute environ 1 640 jetons d'image.
- **Nouvel essai : `ai_unites_test`** (101 vérifications). Il couvre :
  - l'unité mmCE puis une variable qui l'emploie, dans la même proposition et un seul Ctrl+Z ;
  - les refus : un symbole fourni ou du projet, un facteur nul ou absent, une famille inconnue ;
  - l'avertissement sur le facteur et la conversion dans l'aperçu ;
  - le JSON exact, `modifier_unite`, le contexte et la règle 25 ;
  - ta capture, avec 3 variables en mmCE : une seule unité, les erreurs disparaissent, la fenêtre reste ouverte, Revenir ;
  - Tout corriger rapidement, qui ne crée qu'une unité ;
  - « xyz » grisé avec sa raison ; la petite fenêtre remplie pour mmCE et kW, vide pour xyz.
- **`hmi_editor_test`** : l'éditeur des unités passe maintenant par les contrôles partagés, avec les mêmes messages. Résultat : 9 580 contrôles.
- **`ai_corrections_test`** : le regroupement des erreurs répétées. Résultat : 160 vérifications (157 en 1.22.4).
- **`ai_registre_test`** : les 29 opérations et la nouvelle borne. Résultat : 251 vérifications.

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.22.8.exe` : l'installateur (48 227 849 octets). Il met à jour la 1.22.7 au même endroit ;
- `API-Studio-1.22.8-portable.zip` et `IHM-Studio-1.22.8-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.22.8.exe` | `6F42D025B1B445384B393CF307B357E5EEED18B836E5E541F9B74B77F23B34F3` |
| `API-Studio-1.22.8-portable.zip` | `2A3B2FDCCDE721766F95EF0AE03DC0A24429ADA2A9DFE18ADAC2DEE45646FF02` |
| `IHM-Studio-1.22.8-portable.zip` | `F36BDA2775D1ABA46739CB1584559603293878A8D44AE35875988A379FA836B9` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.22.7 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.22.8 » et « IHM Studio 1.22.8 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.22.7 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **Le rôle des couleurs** (défaut, marche, valeur…) est dit « probable » : le calcul seul ne le devine pas, l'IA le confirme en regardant l'image.
- **Ce qui est écrit à la main ne se lit pas** : une approbation cochée à la main n'est pas relevée. Des commentaires manuscrits non plus, mais l'IA est prévenue et peut regarder leur page.
- **Le logiciel n'a pas de palette de projet** : la charte est appliquée objet par objet.
- **Un PDF de reports n'est pas lu comme une table de reports** : seuls les classeurs (.xlsx, .xlsm) le sont, comme avant.
- **L'analyse d'un gros PDF** (le rendu des pages) se fait en arrière-plan, mais la demande attend qu'elle soit finie pour partir.
- **Un symbole se compare sans la casse**, comme dans l'éditeur : « MW » et « mW » sont le même symbole. La table ne connaît donc que MW.
- **Les membres d'un type** créé par l'IA ne prennent pas d'unité ; les variables, oui.
- **Le banc des jetons est proche de son plafond** : les consignes ont grandi d'environ 225 jetons (la règle 25), mis en cache. Le premier cas est à 19 052 jetons pour un plafond de 19 100 ; le prochain ajout de consignes le relèvera.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows**, et jamais avec une vraie IA : les essais rejouent des réponses écrites à la main. Les nouveaux écrans n'ont été vus que dessinés hors écran par les essais.
- **Pas de captures d'écran**, selon ta consigne permanente.
