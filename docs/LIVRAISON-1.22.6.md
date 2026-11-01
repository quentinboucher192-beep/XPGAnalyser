# API Studio et IHM Studio 1.22.6 : toutes les vues de l'aperçu, l'IA en direct, la mission depuis la conversation, moins de jetons, l'IA qui règle variables, sécurité et vues

Suite de la 1.22.5, faite à partir de tes demandes du 01/11 au soir, avec tes captures :
- la conversation à 991 k jetons et la limite à 3 999 / 4 000 requêtes ;
- « Ce qui bloque la suite » ;
- les en-têtes et les pieds de page.

Elle se met à jour par-dessus la 1.22.5 et garde tes données.

## 1. Ce qui change pour toi

### La prévisualisation montre toutes les vues

- Une proposition qui crée ou modifie plusieurs vues les montre toutes. Au-dessus de l'aperçu, une barre affiche par exemple « < Vue 2/4 : Popup_Vanne (neuve, popup) · 1 erreur > ».
  - Les flèches changent de vue, tout comme PageUp/PageDown et Ctrl+gauche/droite.
  - Un clic sur le nom ouvre la liste des vues, avec ce que chacune crée, modifie ou supprime, et ses erreurs.
- **À gauche, chaque ligne dit sa vue.** Celles de la vue montrée sont marquées d'une pastille, les autres sont grisées.
  - Choisir un objet d'une autre vue fait passer l'aperçu sur cette vue et encadre l'objet.
  - La puce « Cette vue seulement » ne garde que les lignes de la vue montrée.
  - Les titres disent « (12, 3 sur cette vue) ».
- Les cadres (nouveau, modifié, supprimé, erreurs, avertissements) sont ceux de la vue montrée.

### Ce que fait l'IA, en direct

Sous les « … » de la réponse en cours, une ligne dit ce qui se passe à l'instant, et un trait fin avance dessous. Les lignes réellement produites sont par exemple :
- « Envoi de la demande (env. 48 k jetons de contexte)… » ;
- « L'IA réfléchit… env. 1 240 jetons de réflexion (0:12) » ;
- « Demande envoyée : 48 k jetons de contexte (dont 41 k en cache) · 1,2 s » ;
- « L'IA écrit la proposition : opération 12 (créer Voyant_P3 sur Vue_Pompes)… » ;
- « L'IA lit le classeur Liste_ES.xlsx (outil lignes_classeur, lignes 200 à 399)… » ;
- « Analyse du projet (analyser_projet : orphelins)… » ;
- « Attente du service (limite : nouvelle tentative dans 8 s)… » ;
- « Réponse reçue : vérification de 45 opérations… » puis « Générer et Compiler sur une copie du projet… ».

Au-dessus de la ligne : la mission et l'étape servie. Dessous, en gris : les dernières étapes et leur durée (un clic sur l'horloge les replie).

### Les coûts en direct, et le détail par action

- Le bas du panneau compte la réponse **pendant** qu'elle s'écrit : « Cette conversation », « aujourd'hui », la limite. « env. » reste affiché jusqu'au compte exact du service, et la bande se met à jour quatre fois par seconde au plus.
- Chaque réponse garde son **bilan** : « 48 k jetons · 0:42 ». Le **+** déplie une ligne par appel au service. Par exemple : « 1. Proposition — entrée 48 k (dont 41 k lus du cache) · sortie 5,2 k (dont env. 4,8 k de réflexion) · 19 s », puis le total.
- **Deux comptes corrigés.** Un tour avec des outils (une analyse, une lecture de classeur) additionne maintenant tous ses appels ; avant, le dernier écrasait les précédents et les jetons étaient sous-comptés. Une réponse arrêtée garde aussi ce qu'elle a déjà consommé.
- Le logiciel ne connaît pas les prix : il montre les jetons seulement.

### Une grosse demande devient une mission

- **Clic droit sur un message envoyé › Créer une mission avec ce message.**
  - La mission reprend la demande, ses pièces jointes (le .XPG lu, les classeurs) et un résumé de la conversation : ce qui a été appliqué, la suite annoncée et combien de fois.
  - Elle part dans une **conversation neuve** : l'historique et ses jetons ne la suivent pas. Elle planifie ses étapes comme une mission ordinaire.
  - La conversation d'origine dit « Mission « X » créée — Ouvrir ».
- Le bouton **Envoyer en mission**, à côté d'Envoyer, fait de la demande tapée une mission.
- **Le logiciel te le conseille avant l'envoi** : la demande énumère au moins 3 étapes (ton message « S3b, S5, S6, S7, S8, S9 » : « elle énumère 6 étapes »), la même suite a déjà été annoncée, une réponse a été coupée, ou la conversation dépasse 500 k jetons.
- **L'IA peut le conseiller elle aussi** (règle 22 de ses consignes).
- Dans les deux cas, une carte propose **Créer la mission** ou **Continuer ici**. « Continuer ici » est retenu pour la conversation, et rien n'est créé sans ton clic.

### Moins de jetons par demande, mesuré avant / après

Un banc d'essai compte ce qui part réellement au service, sans réseau, sur cinq cas types. Le coût compte une lecture en cache pour 0,1 et une écriture en cache pour 1,25.

| Cas | Total avant → après | Coût avant → après |
|---|---|---|
| 1. Première demande (projet moyen, 300 variables) | 18 274 → 18 651 (+2 %) | 21 679 → 22 368 (+3 %) |
| 2. 10e tour, 5 propositions de 40 opérations | 28 920 → 25 659 (−11 %) | 16 663 → 8 393 (**−50 %**) |
| 3. Proposition de mission | 19 514 → 20 303 (+4 %) | 7 257 → 6 706 (−8 %) |
| 4. Correction automatique | 18 981 → 19 722 (+4 %) | 6 724 → 6 365 (−5 %) |
| 5. Conversation avec un .XPG lu | 19 992 → 20 348 (+2 %) | 7 735 → 6 992 (−10 %) |
| **Ensemble** | 105 681 → 104 683 (−1 %) | 60 058 → 50 824 (**−15 %**) |

Ce qui a donné ces gains :
- **Le cache** : le contexte du projet, qui change à chaque demande, part maintenant à la fin. Les consignes et l'historique se relisent donc en cache, dix fois moins cher.
- **L'historique résumé** : une ancienne proposition réglée part en résumé (libellé, comptes, noms, sort) au lieu de ses opérations. Au 10e tour, l'historique passe de 9 187 à 4 652 jetons.
- **Le contexte** : les fonctions natives ne partent que pour une demande de code.

Les nouvelles opérations de cette version (variables, sécurité, vues) ajoutent environ 1 000 jetons aux consignes, dans la partie mise en cache. C'est pourquoi la première demande coûte un peu plus.

### L'IA règle elle-même les variables, la sécurité et les vues

Sur ta capture, l'IA te demandait de décocher « Lecture seule » sur Armoires et d'activer la sécurité toi-même. Elle le propose maintenant, comme toute proposition : vérifiée, avec un aperçu, et annulable d'un seul Ctrl+Z.

- **Les variables IHM.** L'IA peut cocher ou décocher Lecture seule, et changer :
  - la description, la valeur initiale, l'unité, le dossier, la rémanence ;
  - l'équipement, l'adresse (un chevauchement est refusé), la mise à l'échelle ;
  - le nom : elle peut renommer la variable avec tous ses emplois.

  Elle ne touche ni une variable inconnue (refusée, avec les plus proches) ni une variable de l'automate. L'aperçu dit par exemple « Armoires : lecture seule décochée ».
- **La sécurité.** L'IA peut l'activer ou la désactiver, créer et modifier des groupes (niveau, rôles, vue de départ) et des utilisateurs (identifiant, nom, groupe).
  - **Elle ne choisit jamais de mot de passe et n'en voit aucun.** Un compte qu'elle crée reste **désactivé**, « mot de passe à définir dans Configuration > Utilisateurs ». Le contexte ne contient aucune empreinte de mot de passe.
  - Activer la sécurité sans compte capable d'administrer est refusé : on se fermerait dehors.
  - Désactiver la sécurité, changer le groupe ou la protection d'un compte, ou retirer le dernier administrateur est signalé en tête de la proposition. **Le mode Direct ne l'applique jamais sans ton clic.**
- **Les droits par niveau sur un bouton** passent par sa propriété Accès, que l'IA sait déjà modifier.
- **Les vues.** L'IA peut changer :
  - la vue de démarrage ;
  - la vue parente (une boucle est refusée) ;
  - l'écran modèle, l'en-tête et le pied de page.

  Elle peut aussi les poser dès la création d'une vue.
  - Quand elle coche un en-tête ou un pied, **les objets recouverts sont signalés** (par exemple « l'en-tête (80 px) recouvre 3 objets : Titre_A, Btn_Auto, Voyant_1 ») et **déplacés** sous l'en-tête ou au-dessus du pied, en gardant leurs écarts. L'aperçu les montre modifiés.
  - S'il n'y a plus la place, la proposition le dit avec la hauteur nécessaire, et l'IA peut agrandir la vue.

## 2. Les essais

Sous Linux : **127 suites d'essais sur 127** réussies (CTest), dont quatre nouvelles. Un essai ancien, `crashtrail`, a échoué une fois sous la charge (la construction Windows tournait en même temps). En cause : une supposition de l'essai sur le temps de processeur, pas le logiciel (son module n'a pas changé). L'essai est corrigé, puis les 127 suites sont toutes passées.

- **Quatre essais nouveaux :**
  - `ai_vues_test` (toutes les vues de l'aperçu) : 92 vérifications ;
  - `ai_activite_test` (la ligne d'activité, les coûts en direct, le bilan par action) : 115 vérifications ;
  - `ai_jetons_test` (le banc des jetons : le tableau ci-dessus ; il échoue si une demande dépasse son plafond) : 43 vérifications ;
  - `ai_securite_test` (ta capture : Lecture seule décochée puis les boutons posés ; les groupes et les comptes sans mot de passe en clair ; la désactivation que le mode Direct n'applique pas ; la vue de démarrage, la vue parente et sa boucle refusée ; l'en-tête qui recouvre 3 objets, déplacés en un seul Ctrl+Z) : 131 vérifications.
- **Essais complétés :**
  - `ai_missions_e2e_test` refait ton message « S3b… S9 » : la carte de conseil, Créer la mission, Continuer ici, Envoyer en mission, l'IA qui conseille. Résultat : 452 vérifications (388 en 1.22.5) ;
  - `ai_reseau_test` vérifie la place du contexte, les points de cache et les jetons des tours d'outils, chaque appel compté une fois. Résultat : 292 vérifications.

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.22.6.exe` : l'installateur (47 910 822 octets). Il met à jour la 1.22.5 au même endroit ;
- `API-Studio-1.22.6-portable.zip` et `IHM-Studio-1.22.6-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.22.6.exe` | `EE08051430B978D544AF17C44150DBC738165A92AA60AB79E0549FCE19FF9B80` |
| `API-Studio-1.22.6-portable.zip` | `1F6206BB31A681C834FD5023593E170C667C8BBD16BBAAE9A78CD0F6D9DC7246` |
| `IHM-Studio-1.22.6-portable.zip` | `1580DF9A0872E94109C5FC41D88BD8E4020E31487719D146C99E2359C5E28EC9` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.22.5 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.22.6 » et « IHM Studio 1.22.6 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.22.5 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **La réflexion du modèle Opus** : par défaut, le service n'envoie pas le texte de sa réflexion. Le compteur de la ligne d'activité ne monte donc pas pendant qu'elle réfléchit : la ligne montre le temps, et le bilan donne la réflexion à la fin (« env. »). Il monte avec un service qui envoie ce texte.
- **L'entrée de l'outil en direct** (« opération 12 ») demande au service Anthropic d'envoyer la proposition en morceaux. S'il refuse ce réglage, la demande repart aussitôt sans lui, et la ligne ne dit plus l'opération en cours. **Cela n'a pas été vérifié avec le vrai service.**
- **Les jetons du banc sont estimés** (3,5 octets par jeton) et le coût suit les tarifs relatifs du cache d'Anthropic. Les jetons réels dépendent du modèle.
- **Un changement visible** : le contexte du projet part maintenant à la fin de la demande, et non plus dans les consignes.
- **Une adresse en chevauchement** est refusée quand l'IA modifie une variable, alors que l'éditeur et l'opération « adresser » se contentent d'avertir.
- **La prévisualisation de plusieurs vues** n'a été vue que dessinée hors écran. À regarder sur une vraie capture, surtout quand la liste de gauche est étroite.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows**, et jamais avec une vraie IA : les essais rejouent des réponses écrites à la main.
- **Pas de captures d'écran**, selon ta consigne permanente.
