# API Studio et IHM Studio 1.22.7 : supprimer une vue citée ailleurs, Retour et Corriger rapidement dans la prévisualisation

Correctif et ajouts de la 1.22.6, faits à partir de ta capture du 02/11 : « Erreur — E1 Supprimer la vue Vue_Accueil (et ses références) : … Pied . Navigation . views - cette liste nomme Vue_Accueil ne se retire pas sans casser ». Elle se met à jour par-dessus la 1.22.6 et garde tes données.

## 1. Ce qui change pour toi

### Supprimer une vue citée ailleurs ne bloque plus

Ce qui était faux : en stratégie « références », l'Assistant ne savait retirer une référence que d'une propriété simple ou d'une action. Une **liste** qui nommait la vue l'arrêtait net. C'était le cas de la barre de navigation de ton pied de page, qui liste Vue_Accueil avec d'autres vues.

L'Assistant **modifie maintenant ce qui cite la vue**, et le dit dans l'aperçu :
- **La barre de navigation** perd l'entrée et son libellé, et les autres vues restent dans l'ordre : « Pied.Navigation : Vue_Accueil retirée de la liste des vues (3 → 2) ». Si la liste devient vide, un avertissement le dit (la barre montre alors toutes les vues). La barre n'est jamais supprimée.
- **Une zone de plan** garde ses points et ses alarmes : seule sa vue est vidée.
- **Un argument d'instance** qui la nomme est vidé : le paramètre reprend sa valeur par défaut, et c'est dit.
- **Les vues filles** remontent à la vue au-dessus, sinon à la racine.
- **L'écran modèle** passe à celui au-dessus. Un **en-tête** ou un **pied** supprimé est remplacé par le premier du même rôle qui reste, sinon la case est décochée.
- **La vue de démarrage** devient la première vue ordinaire qui reste, et c'est dit. **La vue d'accueil d'un groupe** revient à la vue de démarrage.
- **Le code n'est jamais réécrit.** Un script, une fonction, une condition d'alarme ou une recette qui nomme la vue reste un refus. Le refus dit maintenant où (« script Script_Nav, ligne 2 ») et propose **remplacer**, avec les vues existantes les plus proches.
- Tout se fait sur une copie, et un seul Ctrl+Z remet tout.
- Les consignes de l'IA disent que « références » est le bon choix par défaut pour une vue citée ailleurs (« remplacer » pour garder les liens vers une autre vue).

### La prévisualisation : Retour

- Un bouton **Retour**, en bas à gauche, ferme la fenêtre **sans rien décider** : rien n'est refusé ni appliqué, la saisie n'est pas touchée et la proposition reste à décider. Échap fait de même quand aucune recherche n'est en cours.
- **Refuser…** refuse, comme avant ; son infobulle le précise.

### La prévisualisation : Corriger rapidement

Le clic droit sur une **erreur** ou un **avertissement** (y compris Générer et Compiler, et les lignes des autres vues) propose en tête **Corriger rapidement**. C'est la meilleure correction **sûre**, appliquée à la proposition (jamais au projet), **sans fermer la fenêtre**. La vérification et l'aperçu se refont aussitôt, et la vue montrée reste la même.

- **La correction choisie** :
  - une variable introuvable devient la variable **nettement** la plus proche : le même nom à la casse et aux séparateurs près, sinon les mêmes mots (Cmd pour Commande), sinon une faute de frappe d'au plus 2 lettres. Les chiffres doivent être les mêmes (Pompe_2 n'est jamais Pompe_1), et une seule candidate au meilleur rang ;
  - un constat de Générer et Compiler qui a une correction du logiciel reçoit cette correction ;
  - une suppression refusée parce que l'élément est cité ailleurs passe en stratégie **références** (pour un type ou un dossier, l'opération est retirée) ;
  - des objets sous un en-tête ou un pied (1.22.6) sont décalés ;
  - une autre erreur d'une opération retire l'opération ;
  - quand rien n'est sûr (« 2 objets créés ne sont reliés à aucune variable », plusieurs variables aussi proches), l'entrée est **grisée avec la raison**, et « Choisir une variable… » reste juste dessous.
- **Ce qui a changé est montré** :
  - la ligne quitte les erreurs ;
  - la catégorie **Corrections** gagne une ligne « … (corrigé rapidement) », éclairée en vert un instant ;
  - les objets touchés sont encadrés par le cadre de sélection, qui pulse ;
  - une bande en haut de la liste dit « Corrigé : … · **Revenir** » : un clic annule la correction.
- **Les autres corrections du menu ne ferment plus la fenêtre non plus** : Utiliser « … », Créer la variable, Retirer l'opération, la correction du logiciel. Seule « Demander à l'IA de corriger » la ferme, pour que tu relises le texte avant de l'envoyer.
- **Tout corriger rapidement**, sur les titres ERREURS, AVERTISSEMENTS et GÉNÉRER ET COMPILER (le lien ou leur clic droit), corrige tout ce qui est sûr et dit ce qui reste (« 3 erreurs corrigées, 2 restent… »).
- Le même « Corriger rapidement » est aussi sur la carte de la conversation.
- **Une grosse proposition** se revérifie en arrière-plan : la liste et l'aperçu d'avant restent affichés, avec « Revérification… », et Appliquer attend la fin.

## 2. Les essais

Sous Linux : **128 suites d'essais sur 128** réussies (CTest), dont une nouvelle. Au premier passage, le banc des jetons a dépassé son plafond de 23 jetons : la description de la suppression en ajoute 64 aux consignes. Le plafond est ajusté ; le coût de ce cas reste à −50 %.

- **Nouvel essai : `ai_rapide_test`** (104 vérifications). Il couvre :
  - Retour, avec la saisie intacte ;
  - Corriger rapidement sur une variable, avec l'objet encadré, la bande et Revenir à la souris ;
  - un constat du logiciel, puis Revenir ;
  - une suppression qui passe à « références » ;
  - les objets décalés sous un en-tête ;
  - les cas grisés et leur raison ;
  - Tout corriger rapidement ;
  - une grosse proposition en fond.
- **Essai complété : `ai_delete_test`** (160 vérifications, 115 en 1.22.6). Il refait ta capture : un pied de page dont la barre de navigation liste Vue_Accueil et deux autres vues. La suppression est acceptée, les deux autres vues restent dans l'ordre, l'aperçu le dit, et un seul Ctrl+Z remet tout. Il couvre aussi :
  - les plans, les arguments, les vues filles ;
  - la vue de démarrage, les groupes, le dernier écran ;
  - l'en-tête, le pied, l'écran modèle ;
  - le code qui nomme la vue : refusé avec sa ligne et la stratégie conseillée.

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.22.7.exe` : l'installateur (47 982 572 octets). Il met à jour la 1.22.6 au même endroit ;
- `API-Studio-1.22.7-portable.zip` et `IHM-Studio-1.22.7-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.22.7.exe` | `98AA80203E0F50CB3CA633FF0E582755A94EFE17AAA13C83AD19258846E1434F` |
| `API-Studio-1.22.7-portable.zip` | `7F07ADAB63BE5D82435AC9A99CD3955EF4B418EA81075FE0E8E735172ABF7792` |
| `IHM-Studio-1.22.7-portable.zip` | `B343EB483F22D23F41B97E64BC4A2BE07A55ECF20FD6ED32FDB6535910D9155E` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.22.6 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.22.7 » et « IHM Studio 1.22.7 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.22.6 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **Passer une suppression en « références » est plus fort que retirer l'opération** : ce qui cite l'élément est modifié ailleurs dans le projet. L'aperçu montre chaque modification avant Appliquer, et Revenir reprend le choix.
- **Toujours retirées en bloc, comme avant** : une variable dans la valeur d'une action (« Equipement := Pompe_2 ») retire l'action entière, et une liste de formules (les plumes d'une courbe) est vidée en entier.
- **Un changement de comportement** : supprimer la dernière vue ordinaire quand c'est la vue de démarrage est maintenant refusé, même en stratégie « seul ». Avant, le projet restait sans vue de démarrage.
- **Le marquage « (corrigé rapidement) »** ne dure que tant que la fenêtre est ouverte. Les corrections, elles, restent avec la proposition et la catégorie Corrections les liste.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows**, et jamais avec une vraie IA. Les nouveaux écrans n'ont été vus que dessinés hors écran par les essais.
- **Pas de captures d'écran**, selon ta consigne permanente.
