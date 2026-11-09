# API Studio et IHM Studio 1.42.0 : les options, Mes achats, les offres temporaires ; dans l'Admin, les Clients en arbre

Faite à partir de tes retours du 09/11 sur la maquette de la 1.42.0 : « je valide tout, c'est parti ». Les sessions (deux PC sur le même projet) viennent ensuite, comme convenu : la 1.43.0 commence par le réseau local.

Elle se met à jour par-dessus la 1.41.0 et garde tes données. **Le service en ligne ne change pas** : rien à redéployer.

## 0. En bref

- **Les options** : une fonction en plus sur une licence, **pour un temps** (1 semaine, 1 mois, jusqu'à la fin de la licence, ou des dates), même si son offre ne l'a pas.
  - **Dans l'Admin**, une page **Options** (le catalogue : ce qu'elle ouvre, ses durées et leurs prix, les offres à qui elle est proposée, la boutique des Studios, un essai offert, retirée, supprimer) ; dans **Licences**, le bouton **Ajouter une option…** (l'option, la durée, le début, le prix ou offerte, ce que le client aura) ; la fiche montre **Options (1 en cours)** et **Achats du client**.
  - **Dans les Studios**, l'onglet **Options** de la fenêtre Licence : le catalogue pour ton offre, l'option en cours (encore 6 j, « Prolonger… »), **la demande** (la durée, « dès qu'elle est acceptée » ou une date, des dates, un mot) — aucun paiement : l'éditeur reçoit la demande, l'accepte, l'option arrive toute seule sur les PC du client, puis **s'arrête seule à sa date** (le reste ne bouge pas).
  - **Dans l'Admin, Demandes en ligne** : une demande d'option dit « Option : Assistant IA avancé · 1 mois · 29 € HT » ; **Ajouter cette option…** ouvre la fenêtre réglée sur la demande. Signée, elle part : licence liée à ses PC, le PC du client lit « Acceptée » et sa licence avec l'option ; licence en ligne, la nouvelle émission part sur le service et le Studio renouvelle tout de suite.
  - **Une action fermée par la licence propose l'option qui l'ouvre** (« Option Assistant IA avancé · dès 29 € HT le mois », bouton **Voir l'option**).
  - **Les offres par défaut** (le registre d'exemple) portent trois options : Exports avancés (PERSONAL), Assistant IA avancé (PERSONAL, PROFESSIONAL), Travail à plusieurs (PROFESSIONAL).
- **Mes achats** (onglet de la fenêtre Licence) : tout ce que le client a pris depuis sa première licence — les offres, les prolongations, les options, fini ou en cours : son offre, l'option en cours et ce qu'il lui reste, depuis quand et combien en tout, **une ligne de temps**, le tableau (Tout, Offres, Options) et **son export en CSV**. L'Admin reconstruit cet historique à chaque émission ; une licence d'avant la 1.42 montre ce que dit la licence installée.
- **Les offres temporaires** (Admin › Offres, l'ancienne page Éditions) : « En vente : toujours / pendant une période » **du … au …** avec **le mini calendrier** (fini la date tapée AAAA-MM-JJ : le bug des réductions) ; la boutique des Studios ne montre l'offre que pendant sa période. **Supprimer une offre** : permis si plus aucune licence en cours ne l'utilise ; sinon **« Retirer de la vente »** à la place, et l'Admin la supprime d'elle-même après la dernière licence.
- **Les Clients de l'Admin en arbre** : un client se déplie (ses licences, leurs PC, leurs options, ses achats, ses demandes en attente), **Tout déplier / Tout replier**, **Actualiser** (F5) ; **Supprimer le client…** (grisé, et il dit pourquoi, tant qu'une licence court) ; la corbeille et **Restaurer**.
- **La fenêtre Licence** : le formulaire de la demande défile (on en voit le bas), le dernier bouton n'est plus rogné.

## 1. Ce qu'il te reste à faire

1. **Installe l'Admin** avec `XPG-License-Admin-Setup-1.42.0-EDITEUR.exe` (le zip **à part, pour toi seul**), par-dessus la 1.41.0.
2. **Installe la 1.42.0 chez tes clients avant** d'ajouter une option ou de signer un registre avec des options ou une offre temporaire : **un Studio 1.41 refuse** un registre ou une licence qui en porte (il garde l'ancien : rien ne casse, mais il ne voit pas le nouveau). L'Admin te le rappelle la première fois.
3. Rien à redéployer : le service en ligne est le même que celui de la 1.41.0.

## 2. Les essais

- **Linux : 162 suites sur 162 réussies** (`ctest`, sur la version livrée). Celles que cette version touche :
  - `adminguioptions` (63 contrôles, nouvelle) : la page **Options** (les trois options par défaut, en créer une, ses durées et ses prix, les offres à qui elle est proposée), **Ajouter une option…** dans Licences, la fiche (Options, Achats du client), **l'offre temporaire** (le mini calendrier) et **supprimer / retirer de la vente** une offre ;
  - `adminguidemandes` (142 contrôles) : une **demande d'option** (licence liée à ses PC) : la ligne « Option : … », **Ajouter cette option…** réglée sur la demande, la réponse « Acceptée » au PC ;
  - `adminguidonnees` (231 contrôles) : **les Clients en arbre** (déplier, replier, Tout déplier, Actualiser, chercher un PC), **Supprimer le client…** grisé tant qu'une licence court (et il dit pourquoi), la corbeille ;
  - `adminservice` (176 contrôles) : la demande d'option relevée et répondue ; un identifiant de PC fait de chiffres seuls retrouvé ;
  - `service_studios` (155 contrôles) : **la demande d'option envoyée par le Studio** (la vraie, celle du bouton) et l'option qui arrive ;
  - `gateway` (48 contrôles) : la demande d'option scellée (l'option, la durée, les dates) et ses contrôles ;
  - `offresoptions` (44 contrôles) : le registre par défaut porte les trois options ; un registre sans option n'écrit aucun champ de la 1.42 (un Studio 1.41 le lit toujours) ;
  - `hmieditor` : la note de la 1.42.0 dans Nouveautés (115 versions) ; l'historique des objets signé par le PC (pour les sessions de la 1.43).
- **Sous Wine, de bout en bout**, avec les programmes Windows et le vrai programme du service lancé sur ce poste (`workerd`, le même que celui de la 1.41.0) (journal : `essai-1.42.0-wine.txt`, le code masqué ; captures : `captures-1.42.0.zip`). **L'Admin est celui installé par son installateur** ; le Studio est une copie avec la clé d'essai, jamais livrée :
  - le PC A active une licence PERSONAL, ouvre **Licence › Options** (le catalogue de son offre, `1420_01`) et **demande l'option** Assistant IA avancé pour 1 mois (`1420_02`) ; une action fermée **propose l'option** (« Voir l'option », `1420_03`) ;
  - l'Admin relève la demande (« Option : Assistant IA avancé · 1 mois · 29 € HT », `1420_04`), **Ajouter cette option…** s'ouvre réglée sur la demande (`1420_05`), elle part (`1420_06`) ;
  - **le Studio la reçoit seul** (`1420_07` : « Option « Assistant IA avancé » ajoutée à ta licence »), l'onglet Options la montre **active, encore 30 j** (`1420_08`), **Mes achats** : l'offre et l'option, la ligne de temps, le tableau (`1420_09`) ;
  - l'Admin : la fiche de la licence avec son option et ses achats (`1420_10`), **l'offre temporaire et le mini calendrier** (`1420_11`), la page **Options** (`1420_12`), **les Clients en arbre** (`1420_16`), le tableau de bord (`1420_17`) ;
  - le PC B demande sa licence, l'éditeur l'accepte : « Ta licence en ligne est activée » (`1420_13` à `1420_15`, le code d'essai masqué sur `1420_14`).
- **Les Studios** : l'installation en silencieux par-dessus la 1.41.0 passe, ta clé d'API est gardée, les programmes installés sont ceux construits (1.42.0), le code laissé par `/CODEFICHIER=` n'est pas dans le journal. Le Studio livré (ton service compilé) s'ouvre (`1420_00a`), sa fenêtre Licence aussi (`1420_00b`, et l'onglet Options sans licence : `1420_00d`) ; API Studio livré (`1420_00c`). Ils ont été lancés avec une adresse de service locale où rien ne répond : **ton service n'a pas été appelé**.
- **L'installateur de l'Admin** (sous Wine, pour toi seul, en silencieux) : l'installation passe ; les programmes installés sont ceux construits ; le menu Démarrer, le Bureau, les quatre types de fichiers, la console dans le PATH ; une mise à jour par-dessus reprend les choix ; la désinstallation retire le programme, le service, les associations et le PATH ; **le dossier de ta clé reste intact** (avant et après). Ses pages : captures `1420_91` à `1420_97`.

## 3. Installer

**L'outil de l'éditeur** : double-clique `XPG-License-Admin-Setup-1.42.0-EDITEUR.exe` (dans le zip **à part, pour toi seul**).

**Les Studios** : lance `API-IHM-Studio-Setup-1.42.0.exe` (dans le zip) par-dessus la 1.41.0. Tes projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.42.0.exe` | `93EBFCAA28F2960A9AF89E90C903512AB16184F0295C17142674454BF345223A` | 57 286 903 octets |
| `API-Studio-1.42.0-portable.zip` | `46B67A5AE254E05F00D474012A81B03097AB7B6739EC3F3855875530CD04465F` | 217 fichiers |
| `IHM-Studio-1.42.0-portable.zip` | `E7D4429C5DA089F925EF4B68D93168B4A1681E09D0F947D080D368A4F697F18B` | 218 fichiers |
| `XPG-License-Admin-Setup-1.42.0-EDITEUR.exe` (**à part, pour toi seul**) | `4755808A0DE3062BD4DCB146B167A832FCE68970239401ED24D84F2396938629` | 6 595 615 octets |

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine, contre le vrai programme du service lancé sur ce poste (`workerd`). Ton service en ligne n'a pas été appelé.
- **Aucun paiement** dans le logiciel : une demande d'option arrive chez toi, tu l'acceptes (ou la refuses avec une raison), comme une demande de licence. Les prix sont ceux que tu écris.
- **L'essai offert** d'une option (« Essai : N jours ») est noté dans le catalogue et montré, mais pas encore accordé tout seul : tu l'ajoutes avec « Offerte ».
- **Une demande à la fois par PC** : une demande d'option attend comme une demande de licence (l'annuler : Demander ma licence › Annuler la demande).
- **La fenêtre qui annonce l'option** arrivée sur le PC reprend celle d'une licence acceptée (« Ta licence est activée », avec le mot « Option « … » ajoutée à ta licence ») : son titre propre (« Ton option est arrivée ») vient avec la 1.43.0.
- **Sous Wine**, après la désinstallation de l'Admin, les deux raccourcis du menu Démarrer dont le nom a un accent restent (comme en 1.41.0) ; le reste part.
- **Les sessions** (deux PC sur le même projet, la maquette validée) : à partir de la 1.43.0.
