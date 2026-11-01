# API Studio et IHM Studio 1.22.5 : l'avis du logiciel, la correction par l'IA, le processus de développement

Suite de la 1.22.4, faite à partir de ta demande du 01/11 : « l'IA doit utiliser Compiler / Générer du logiciel pour avoir son avis, choper les erreurs et avertissements, les montrer en prévisualisation ; si elle fait des modifs à cause de ça, le montrer dans un processus de développement, et modifier pour corriger ». Elle se met à jour par-dessus la 1.22.4 et garde tes données.

## 1. Ce qui change pour toi

### L'avis du logiciel : Générer et Compiler sur chaque proposition

- **Chaque proposition de l'IA connectée passe par Générer et Compiler**, les mêmes que tes clics sur Générer et Compiler. Cela se fait sur une **copie du projet** où la proposition est appliquée. Rien ne change dans ton projet.
- **Ça tourne en arrière-plan**, sans figer l'écran : 0,7 à 1,5 s sur un projet de 37 vues et 477 objets, dans une construction de mise au point. C'est plus rapide dans l'installateur. Le projet d'avant n'est généré qu'une fois : il est gardé en cache pour les vérifications suivantes.
- **Seuls les constats nouveaux comptent.** Une erreur ou un avertissement que ton projet avait déjà avant la proposition n'est pas repris.
- **Dans la prévisualisation**, la catégorie **GÉNÉRER ET COMPILER** les liste. Son titre dit « en cours… » pendant le calcul, puis par exemple « 2 erreurs, 1 avertissement nouveaux · 1,4 s ».
  - Leurs objets sont encadrés en rouge ou en ambre, comme les autres erreurs de la 1.22.4. Choisir une ligne encadre son objet.
  - Le clic droit propose : Copier le message, Retirer cette opération, Demander à l'IA de corriger. Quand le logiciel a une correction toute prête, il propose aussi **Appliquer la correction du logiciel**. Elle change la proposition, jamais le projet, et l'avis est alors redemandé.
- **Une erreur du logiciel bloque Appliquer**, comme une erreur de la vérification, sauf avec « Appliquer malgré les erreurs ». Générer la bloquerait de toute façon ensuite.

### L'IA corrige seule

- Quand Générer ou Compiler trouvent des **erreurs nouvelles**, l'Assistant les renvoie **aussitôt** à l'IA :
  - dans une conversation, par un message du logiciel (marqué « Logiciel ») ;
  - dans une mission, par la correction de l'étape.
- L'IA renvoie une proposition corrigée, qui repasse par la vérification puis par Générer et Compiler.
- L'ancienne passe **« remplacée »** : elle ne s'applique plus, et « Voir la correction » mène à la nouvelle.
- **Deux corrections au plus** par demande. Ensuite la carte dit « 2 corrections n'ont pas suffi : … » et c'est à toi de décider.
- **Des avertissements seuls ne relancent rien** : ils sont dits.
- La protection de la 1.22.4 s'applique : une correction qui revient au même point est retenue comme un cercle.
- **Le mode Direct** n'applique une proposition qu'une fois l'avis du logiciel arrivé et propre.
- Ses consignes ont une **règle 21** : le logiciel génère et compile chaque proposition ; corrige ce qu'il dit en renvoyant la proposition entière, et dis en une phrase ce que tu as changé.
- **Paramètres de l'Assistant IA** : « Faire vérifier par Générer et Compiler » et « Corriger seule (2 fois au plus) », cochées par défaut.

### Le processus de développement

- Sur la carte de la proposition, une section **Processus de développement**, repliable et dépliée pendant le travail, dit chaque étape avec l'heure. Par exemple :
  - « 14:02 Proposition reçue : 18 opérations » ;
  - « 14:02 Vérification : 0 erreur » ;
  - « 14:02 Générer et Compiler (copie du projet) : 2 erreurs, 1 avertissement — 1,4 s », avec chaque constat dessous en rouge ou en ambre (un clic ouvre la prévisualisation) ;
  - « 14:03 Correction demandée à l'IA (1 sur 2) » ;
  - « 14:03 Proposition corrigée : 19 opérations », avec la phrase de l'IA ;
  - « 14:03 Générer et Compiler : aucun constat nouveau ».
- **La bande du haut** du panneau dit la dernière étape, par exemple « Correction 1/2 : l'IA corrige 2 erreurs de Compiler… ».
- **Dans une mission**, l'activité de l'étape le dit aussi (« Générer et Compiler vérifient P2… », « L'IA corrige (Compiler : 2 erreurs)… »), et son journal garde ces étapes.

## 2. Les essais

Sous Linux : **123 suites d'essais sur 123** réussies (CTest), dont une nouvelle.

- **Nouvel essai : `ai_build_test`.** Il vérifie l'avis du logiciel sur un projet construit en mémoire :
  - une formule qui lit un membre inconnu : l'erreur est nouvelle, sur le bon objet, avec son opération ;
  - une proposition propre ne reprend pas les deux constats que le projet avait déjà ;
  - un avertissement nouveau (une courbe sans plume) ;
  - le cache : le projet d'avant n'est généré qu'une fois ;
  - le versement dans la vérification, le texte pour l'IA, le JSON ;
  - la prévisualisation : « en cours… » puis le résumé, les couleurs, les cadres, la sélection, le menu, Appliquer bloqué, « Appliquer la correction du logiciel », une proposition d'avant (« pas lancés »).

  Résultat : 97 vérifications.
- **`ai_missions_e2e_test`** : la correction automatique.
  - L'erreur part à l'IA, l'ancienne proposition est remplacée, et le processus a ses 6 étapes dans l'ordre.
  - Deux corrections qui ne suffisent pas : la correction s'arrête et le dit.
  - Des avertissements seuls ne relancent rien ; les options décochées non plus.
  - Une mission passe par sa correction ; le mode Direct attend un avis propre.
  - Le retour envoyé à l'IA contient les constats ; une conversation de la 1.22.4 se relit.

  Résultat : 388 vérifications (303 en 1.22.4).
- **`ai_missions_ui_test`** : la section Processus de développement dessinée hors écran, dépliée puis repliée, le clic sur un constat, la bande du haut. Résultat : 315 vérifications (295 en 1.22.4).

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.22.5.exe` : l'installateur (47 565 452 octets). Il met à jour la 1.22.4 au même endroit ;
- `API-Studio-1.22.5-portable.zip` et `IHM-Studio-1.22.5-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.22.5.exe` | `83F7FAC8E77F0005E140F51D0857D14F36E67936BD1320B19F6BAC0AEB2221E3` |
| `API-Studio-1.22.5-portable.zip` | `9CCC34D108FD0E5B313A9AE71FA01E41FE809EB5A4FCE5CF703797228D0A2F14` |
| `IHM-Studio-1.22.5-portable.zip` | `229623488E697ED0E65ED82BAEB9CD6D4E831B8EF17F7142DDF1D5F400757BFC` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.22.4 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.22.5 » et « IHM Studio 1.22.5 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.22.4 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **Ce que la vérification refuse déjà ne passe pas par Générer et Compiler.** Une opération refusée par la vérification de l'Assistant (une variable inconnue, par exemple) ne s'applique pas à la copie : c'est la vérification qui la dit. Générer et Compiler attrapent ce qu'elle laisse passer : un membre inconnu, une division par zéro, un texte comparé à un nombre, le mauvais nombre d'arguments, une courbe sans plume…
- **Les sévérités sont celles de Générer**, comme dans sa fenêtre : une erreur de Générer bloque la proposition même si la simulation, elle, la tolérerait.
- **Les avis de l'IA intégrée** (sans connexion) ne passent pas par Générer et Compiler, et une proposition d'avant la 1.22.5 n'a pas d'avis (« pas lancés ») : redemande-la à l'IA, ou modifie-la, pour l'avoir.
- **Pendant la correction d'une mission**, ses étapes restent en erreur jusqu'à la proposition corrigée : une étape qui en dépend peut paraître un instant « bloquée ».
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows**, et jamais avec une vraie IA : les essais rejouent des réponses écrites à la main. Les nouveaux écrans n'ont été vus que dessinés hors écran par les essais.
- **Pas de captures d'écran**, selon ta consigne permanente.
