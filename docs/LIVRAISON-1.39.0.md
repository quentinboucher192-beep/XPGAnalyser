# API Studio et IHM Studio 1.39.0 : l'offre modifiée en tableau, « pas dans ta licence », la licence perdue en direct

Faite à partir de ton retour du 08/11 sur la 1.38.0 (une capture et ta liste) :
- « c'est horrible comme message, il faut que ce soit beaucoup plus interactif et précis, pas qu'un message, intègre un tableau » (la notification « Offre « ENTERPRISE » modifiée ») ;
- « si on essaie de faire quelque chose de bloqué à cause de la puissance de la licence : dans une liste déroulante ou une liste, barrer le texte ; mettre une fenêtre qui dit que la licence ne permet pas de faire cette action » ;
- « si je révoque / désactive une licence ou un PC, le client doit le savoir ; on doit perdre la licence en direct chez le client, ou au prochain redémarrage ».

Elle se met à jour par-dessus la 1.38.0 et garde tes données. **Le service en ligne (le Worker) change** : redéploie-le (§ 1).

## 0. En bref

- **L'offre modifiée, précisément** :
  - **le message** dit ce qui change en une ligne : « 1 fonctionnalité retirée : Éditeur · 1 ajoutée : Export des vues · Prix : 190 € → 220 € · PC au plus : 2 → 3 » ;
  - **un tableau** dans la notification : Fonctionnalité, Logiciel, Avant, Après (retirée en rouge, ajoutée en vert), plus le prix, les PC et la durée qui changent ;
  - **Voir le détail** ouvre la fenêtre « Offre « X » : avant / après » : seulement les changements, ou toute l'offre ; un clic sur une ligne montre sa capture (la molette zoome, un clic l'ouvre en grand) ; « Comparer toutes les offres… » ouvre l'onglet Éditions.
- **Pas dans ta licence** :
  - **barré** : dans les menus (la barre du haut, le clic droit dans l'explorateur), l'arbre du projet (et les boutons de la rangée des outils, ex. Outil Modbus) et Aller à / Faire (Ctrl+K), une action que la licence ne permet pas a son texte barré, un cadenas et « pas dans ta licence » ; un bouton de la barre du haut a un petit cadenas ;
  - **la fenêtre** : la choisir ouvre « Ta licence ne permet pas cette action » : l'action, la fonctionnalité qu'elle demande (et son logiciel), pourquoi (« ta licence PERSONAL ne la contient pas », la Découverte, pas de licence valable), sa capture, les offres qui la contiennent avec leur prix ; **Voir les offres…** ouvre la boutique ;
  - l'Assistant IA (l'ouvrir, envoyer, créer une mission) passe aussi par cette fenêtre.
- **La licence perdue en direct** :
  - tu **révoques**, **désactives** une licence, ou **retires un PC** : ton outil le dépose tout de suite sur ton service ;
  - un Studio ouvert et connecté le voit **dans les 2 minutes** : la licence est effacée du PC, une fenêtre « **Ta licence n'est plus active sur ce PC** » le dit (avec la raison), et une notification « Licence perdue sur ce PC » ;
  - ses fonctions se ferment tout de suite ; **rien ne se ferme** : son projet reste ouvert, il peut l'enregistrer et l'exporter ;
  - hors ligne : à son **prochain lancement connecté**, au plus tard à la fin de son bail (30 jours) ;
  - tes **offres** aussi : un registre déposé est vu par les Studios ouverts dans les 2 minutes.
- **Ton outil** dit maintenant, dans Révoquer, Désactiver et Retirer ce PC, quand le client le verra.

## 1. Ce qu'il te reste à faire

1. **Redéploie le service** : double-clique `deployer.bat` dans le dossier `service-licences` du zip de ton outil. Sans ça, les Studios ne voient une révocation, une désactivation ou un PC retiré qu'à leur lancement (ou 7 jours avant la fin de leur bail).
2. **Installe la 1.39.0** chez tes clients : un Studio 1.38 ne regarde pas toutes les 2 minutes (il le voit à son lancement).

## 2. Les essais

- **Linux : 160 suites sur 160 réussies** (`ctest`, sur la version livrée). Celles de cette version :
  - le **service** (`npm test` dans `service/`, le vrai Worker 1.39.0 sous `workerd`) : **91 essais** (1 de plus) — `GET /v1/etat` dit `revocations` et `changements` ; le compteur monte à une licence déposée (ou désactivée), un PC retiré, une liste de révocation, l'état d'un poste ; il ne bouge pas quand un PC s'active ou renouvelle, ni sur un dépôt refusé ; il ne dit ni licence, ni PC, ni motif ;
  - `service_studios` (151 contrôles, 5 de plus) : le Studio contre le vrai service — le regard lit le compteur sans rien dire, le voit bouger quand l'éditeur retire le PC, la synchronisation efface la licence et dit « perdue » ; une seule fenêtre par licence ; la notification « Licence perdue sur ce PC » ;
  - `licenceui` (111, 9 de plus) : l'offre modifiée — le message court, son tableau (rouge, vert), sa fenêtre (toute l'offre, les changements d'abord), le prix, les PC et la durée, rien d'inventé quand ils n'étaient pas retenus ; les données de la notification écrites puis relues ; le bouton `detail:` ; les entrées barrées (les scripts et l'Assistant IA sans licence ; jamais Arrêter, Enregistrer, le poste) ;
  - `licensing` (321), `activation` (213), `adminservice` (173), `adminguilicences` (277), `adminguishell` (223), `adminworkspace` (77), `adminmail` (306) : sans changement, toujours réussies.
- **Sous Wine, de bout en bout**, avec les programmes Windows et le vrai programme du service 1.39.0 lancé sur ce poste (`workerd`) (journal : `essai-1.39.0-wine.txt`, le code masqué ; captures : `captures-1.39.0.zip`). Ton outil (console et application) tourne avec une clé d'essai ; le Studio est une copie avec la clé d'essai, jamais livrée. Une licence en ligne **PERSONAL** sur le PC A, un projet d'exemple ouvert :
  - **barré** : l'arbre (Rapports, et le bouton Outil Modbus de la rangée des outils), le clic droit sur Vues (Importer des vues…, Exporter les vues… : « pas dans ta licence »), Ctrl+K « rapport » (IHM › Rapports) ;
  - **la fenêtre** : un clic sur Rapports ouvre « Ta licence ne permet pas cette action » : « IHM › Rapports (PDF, Excel) », la fonctionnalité Exports avancés, « ta licence PERSONAL (XPG-2026-00391) ne la contient pas », sa capture, « Elle est dans : PROFESSIONAL (490 € HT / an) · ENTERPRISE (1 290 € HT / an) » ;
  - **Studio ouvert, l'éditeur change l'offre PERSONAL** (registre 319 : − Scripts, + Exports, 190 € → 220 €, 2 → 3 PC) : le Studio le voit **seul** (le regard des 2 minutes) ; la notification : « 1 fonctionnalité retirée : Scripts · 1 ajoutée : Exports · Prix : 190 € HT / an → 220 € HT / an · PC au plus : 2 → 3 » et son tableau ; Voir le détail : la fenêtre avant / après et la capture des Scripts ;
  - **Studio toujours ouvert, l'éditeur désactive la licence** (`service-desactiver`) : le Studio la **perd seul** — la fenêtre « Ta licence n'est plus active sur ce PC » (« l'éditeur a désactivé cette licence »), « Sans licence » en haut, Programmation générale barrée à son tour, la notification « Licence perdue sur ce PC : XPG-2026-00391 » ;
  - **ton application** : la question de Désactiver dit ce que le client verra.
- **Le Studio livré** (ton service compilé) : l'accueil et la fenêtre Licence s'ouvrent (captures `1390_00a`, `1390_00b`). Il a été lancé avec une adresse de service locale où rien ne répond : **ton service n'a pas été appelé**.
- **L'installation en silencieux par-dessus la 1.38.0** : elle passe, ta clé d'API est gardée, les programmes installés sont ceux construits (1.39.0), le bac à sable tourne, le code laissé par `/CODEFICHIER=` n'est pas dans le journal.
- **Une suite fragile sous charge** : `service_studios` a échoué une fois quand toute la suite tournait en parallèle (déjà vu en 1.38), puis a réussi seule et deux fois en parallèle ; la suite complète, refaite sur la version livrée, est passée en entier.
- **Corrigé en chemin**, grâce aux captures : dans Récents, le cadenas passait sur « IHM » ; les colonnes Avant / Après de la fenêtre étaient coupées ; la console disait encore « s'arrête à la fin de son bail » pour Désactiver et Retirer.

## 3. Installer

**L'outil de l'éditeur** (le zip **à part, pour toi seul**) : remplace l'ancien dossier par le nouveau. Ta clé, ton archive, ton service et tes réglages ne bougent pas. Puis **redéploie le service** (§ 1).

**Les Studios** : lance `API-IHM-Studio-Setup-1.39.0.exe` (dans le zip) par-dessus la 1.38.0. Tes projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.39.0.exe` | `0FA70176284FE38F345D10B3B9ACA4765C43E7041FF34E5824C5DD2ECD6C5B0E` | 56 838 564 octets |
| `API-Studio-1.39.0-portable.zip` | `0F1528F0C6B553DF79FB6A01A8DCB5C0BBEB351F1E983BA8E2EEBFF460358347` | 214 fichiers |
| `IHM-Studio-1.39.0-portable.zip` | `CBD92AC5A93E932431205100B4A17EC9943C9A9C95692F28FEEB742E41487B2C` | 215 fichiers |
| `XPG-License-Admin-1.39.0-EDITEUR.zip` (**à part, pour toi seul**) | `9E077EE72B9AE7A67F909F66CB54E122CCF0C5039C88502B988F349BA5D6FAD5` | 7 302 119 octets |

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine, contre le vrai programme du service lancé sur ce poste (`workerd`). Ton service en ligne n'a pas été appelé.
- **« En direct », c'est dans les 2 minutes**, pour un Studio ouvert et connecté. Un PC qui ne se connecte jamais garde sa licence jusqu'à la fin de son bail (30 jours) : c'est le prix du travail hors ligne. Une licence par fichier (liée au PC, sans service) n'est révoquée que par un fichier qu'il importe.
- **Rien ne se ferme quand la licence est perdue** : le projet ouvert reste ouvert, un volet déjà ouvert aussi ; ce sont les **entrées** qui se ferment (la règle « jamais de perte de travail »). Le poste d'exploitation (`--ihm`) n'est jamais arrêté par la licence.
- **Le regard des 2 minutes** est un appel public léger à ton service (`GET /v1/etat`, rien d'écrit) par Studio ouvert qui a une licence : environ 30 appels par heure et par PC (le plan gratuit de Cloudflare en permet 100 000 par jour : environ 140 PC ouverts 24 h sur 24, ou plus de 400 PC ouverts 8 h par jour).
- **Barré** : dans les menus, l'arbre du projet, Ctrl+K et les boutons de la barre du haut. Les boutons des barres d'outils des onglets ne sont pas barrés ; leur clic ouvre la même fenêtre.
- **Le prix, les PC et la durée** d'une offre : ils ne sont dits qu'à partir du deuxième changement vu par un Studio 1.39 (la 1.38 ne les retenait pas : rien n'est inventé).
- **Sous Wine, les Studios d'un même compte partagent leurs réglages** (le carnet des notifications) : sur de vrais PC, chacun a le sien.
