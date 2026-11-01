# API Studio et IHM Studio 1.22.1 : sans blocage, les étapes en direct, les corrections d'un clic droit, les dossiers

Version corrective et complémentaire de la 1.22.0, faite à partir de ton essai sur un vrai Windows du 01/11 (rapport de blocage de 12:11 et capture de la prévisualisation). Elle se met à jour par-dessus la 1.22.0 et garde tes données (projets, conversations, clés, missions).

## 1. Ce qui change pour toi

### Plus de blocage à l'arrivée d'une proposition qui adresse des variables

Ton rapport de blocage (« la boucle principale ne bat plus depuis 8,1 s », projet Semeru) a été décodé avec la table des symboles du même exécutable :

`Assistant::finishLive` → `validate` → `Run::address` → `addr::preview` → `leafVariables` → `patternIndices` → `malloc`

À l'arrivée d'une proposition contenant des opérations « adresser » avec une formule, sa vérification tournait **sur le fil de l'interface**, deux fois (à l'arrivée de la réponse, puis pour la mission). Chaque opération recalculait les cases de **toutes** les variables de l'équipement.

- Une proposition lourde (une opération « adresser », ou 40 opérations et plus) se vérifie maintenant **en arrière-plan**. Cela vaut pour la carte (« Vérification… »), Appliquer, le mode Direct, l'arrivée de la réponse et la vérification de la mission. La mission ne dit la proposition « validée » qu'à la fin de cette vérification.
- Les places des autres variables sont **gardées d'une vérification à l'autre**, et relues dès qu'une variable ou un type change. Les chemins des cases se comparent sans copie de texte.
- Mesure sur un projet d'essai de 300 000 cases adressées sur un même équipement (construction de mise au point, sous Linux) : un aperçu d'adresses passait de 3,9 s à 0,6 s, et le suivant à 0,2 s. Sous Windows (construction optimisée), c'est environ dix fois moins, et ce n'est de toute façon plus sur le fil de l'interface.

### Où en est chaque étape d'une mission

- Chaque étape montre son **pourcentage réel** : ses opérations appliquées sur ses opérations prévues. C'est l'estimation du plan tant que rien n'est proposé. Une étape n'atteint 100 % qu'une fois vérifiée dans le projet ; ignorée ou annulée, elle n'a pas de pourcentage.
- Elle montre aussi **ce qu'elle fait en ce moment**, par exemple :
  - « L'IA prépare la proposition P2… (depuis 0:42) » ;
  - « P2 : 12 opérations à valider — Prévisualiser puis Appliquer » ;
  - « Application de P2… 40 % » ;
  - « Vérification sur le projet réel… » ;
  - « Attend S1 (dépendance) » ;
  - « En erreur : … » ;
  - « Terminée — 8 opérations appliquées le 01/11 à 12:04 ».
- Un clic sur une étape du tableau de bord ouvre son **journal** : les propositions et leur sort, les opérations faites (et celles en échec, avec l'erreur exacte), les décisions, avec l'heure.
- L'étape du moment est aussi dans la **bande** de l'Assistant et dans l'onglet **Missions IA** (« En ce moment »), à jour en direct : une fois par seconde pendant que l'IA répond, jamais à chaque image.

### Corriger une erreur d'un clic droit

Dans la prévisualisation et sur la carte de la conversation, un **clic droit sur une erreur** propose les corrections possibles :

- **Variable introuvable** (« Vanne.Cmd » de ta capture) :
  - « Utiliser « … » » pour chacune des 5 variables les plus proches ;
  - « Choisir une variable… » ;
  - « Créer la variable « … » » (BOOL, INT, DINT, REAL, WORD, STRING, TIME ; le type conseillé vient du nom) ;
  - « Retirer les opérations qui l'utilisent ».
- **Erreur d'une opération** (le code brut illisible de ta capture) : « Retirer cette opération ».
- **Toujours** : « Copier le message » et « Demander à l'IA de corriger ». Ce dernier met le texte dans la saisie et n'envoie rien tout seul.

La correction change la **proposition, jamais le projet**. L'erreur disparaît, la catégorie **Corrections** dit ce qui a été fait (« 4 variables remplacées, 2 variables à créer, 2 opérations retirées ») et un clic droit sur sa ligne propose **Revenir**. **Appliquer** prend la proposition corrigée, en un seul Ctrl+Z. Les erreurs identiques (Vanne.Cmd deux fois) tiennent sur une ligne « (2 opérations) ».

### Les variables créées par l'IA rangées dans des dossiers

- L'IA reçoit l'**arborescence réelle** des dossiers de variables, avec leur nombre de variables, et une règle de rangement : zone, puis équipement, puis nature (Commandes, États, Mesures, Défauts, Paramètres, Compteurs).
  - Un équipement va dans un dossier, et deux équipements ne sont jamais mélangés.
  - Les variables communes vont dans « Commun ».
  - Quatre niveaux au plus.
- Chaque variable de `creer_variables` peut porter son **dossier**, ou celui de l'opération.
  - Les dossiers existants sont repris sans tenir compte de la casse (« pompes » → « Pompes »), et jamais dupliqués.
  - Les dossiers manquants sont créés, en un seul Ctrl+Z.
  - L'aperçu montre le dossier de chaque variable, et « dossier créé : … » signale un dossier neuf.
- **Filet de sécurité** quand l'IA ne donne pas de dossier : le logiciel en déduit un du préfixe des noms, et le dit dans un avertissement.
  - D'abord le dossier où sont déjà les variables du même équipement.
  - Sinon un dossier au nom de l'équipement.
  - Plusieurs équipements d'une même famille créés ensemble vont dans un dossier de famille, avec un sous-dossier chacun (PompeG/PompeG_1).
  - Un dossier qui mélangerait deux équipements est signalé, avec la séparation proposée.
- L'IA intégrée (sans connexion) range de la même façon les variables qu'elle crée.

## 2. Les essais

Sous Linux : ****121 suites d'essais sur 121** réussies (CTest), dont deux nouvelles**.

- **Nouveaux essais :**
  - `ai_corrections_test` : la proposition de ta capture refaite (7 variables manquantes dont Vanne.Cmd deux fois, un code brut illisible, 9 erreurs), puis remplacer, créer, retirer, appliquer, le JSON, la prévisualisation et la carte. Résultat : 113 vérifications.
  - `ai_dossiers_test` : chemins, reprise de l'existant, déduction, mélange, JSON, consignes et contexte, IA intégrée. Résultat : 76 vérifications.
- **Essais complétés :**
  - `ai_missions_test` : le % par étape, chaque ligne d'activité, les journaux, la relecture d'un fichier 1.22.0. Résultat : 291 vérifications (208 en 1.22.0).
  - `ai_missions_ui_test` : le % et l'activité de S1 à S5 partout, le journal au clic, le direct pendant une vraie correction. Résultat : 238 vérifications (196 en 1.22.0).
  - `ai_missions_e2e_test` : la vérification de fond d'une proposition avec une opération « adresser », hors mission et dans une mission. Résultat : 199 vérifications (183 en 1.22.0).
  - `hmi_test` : les motifs de cases (blancs, casse, signe, 9 chiffres) et le cache des places, relues quand une variable ou un type change. Résultat : 6 636 contrôles.

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.22.1.exe` : l'installateur (47 098 866 octets). Il met à jour la 1.22.0 au même endroit.
- `API-Studio-1.22.1-portable.zip` et `IHM-Studio-1.22.1-portable.zip` (167 fichiers chacun).
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.22.1.exe` | `A07B2BDBCFFEA23238F709016794E46AAC71F538DB1AF5D09031E8722BDF0661` |
| `API-Studio-1.22.1-portable.zip` | `50D3FD37FD24D0A888808B1777B9432F5FC755BDC5780A3150E54F68E2DCEDC2` |
| `IHM-Studio-1.22.1-portable.zip` | `0FAB461D893532215D2D347A404DA4434069EE88BD57161D3E1EE513F44796EC` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.22.0 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.22.1 » et « IHM Studio 1.22.1 » ;
- `API-Studio.exe --cli` relit le projet d'essai : 891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution (un cas réussi, un cas faux exprès) ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.22.0 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **Ton projet Semeru n'a pas été rejoué** : il n'est pas ici. Le blocage a été reproduit sur un projet fabriqué pour l'occasion, qui ressemble à ton cas (tableaux de structures adressés par formule sur un même équipement), et la pile de ton rapport désigne exactement ce chemin.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows.** Les nouveaux écrans (pourcentages, journal, menu des corrections) n'ont été vus que dessinés hors écran par les essais.
- **Jamais essayé avec une vraie IA** : les essais rejouent des réponses écrites à la main. La qualité du rangement en dossiers proposé par l'IA dépend du modèle ; le logiciel garantit la reprise de l'existant, la déduction et l'avertissement de mélange.
- **Ce que le remplacement d'une variable réécrit** : les liaisons (associer, propriété `variable`), comme le choix de variable existant. Les formules, le code brut et les scripts ne sont pas réécrits : pour ces erreurs, le menu propose Retirer, Copier et Demander à l'IA.
- **Le rangement par préfixe est une heuristique** : P1_Marche et Marche_P1 donnent P1, Pompe_1_Marche donne Pompe_1. Un nom sans chiffre n'est pas pris pour un équipement. Une liste de mesures (une variable par capteur) n'est pas signalée comme un mélange.
- **Un changement visible** : un clic sur une ligne du plan d'une mission ouvre maintenant le journal de l'étape. La carte de la proposition se rejoint par le lien « Voir la carte de Pn › ».
- **Pas de captures d'écran**, selon ta consigne permanente.
