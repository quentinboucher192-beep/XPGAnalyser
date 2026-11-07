# API Studio et IHM Studio 1.33.0 : l'outil de l'éditeur devient une application

Faite à partir de ta demande du 07/11 : « le .exe admin je veux avoir une interface, une application là c'est galère de tout configurer », puis « continue avec la 1.33 ».

## 0. En bref

- **`XPG-License-Manager-Admin.exe`** : ton outil de licences a maintenant des fenêtres, avec le même aspect que les Studios. Plus besoin de la ligne de commande ni de fichiers de paramètres pour le travail de tous les jours.
- **Le menu de la maquette, avec ses 13 entrées.** Dix marchent. Les trois autres (Licences flottantes, Boutique, Commandes) disent qu'il leur faut le service en ligne, qui n'existe pas encore. Elles ne simulent rien.
- **Une licence en six étapes**, avec l'assistant « Nouvelle licence » :
  1. les demandes des PC ;
  2. le client et l'identifiant (le suivant est proposé) ;
  3. l'édition et la durée ;
  4. les fonctionnalités ;
  5. le récapitulatif ;
  6. signer, puis publier sur GitHub si tu veux.
- **Nouveau : retirer un PC d'une licence.** Un PC remplacé ne bloque plus une place. Dans l'application (Licences › Retirer un PC…) et dans la console (`--retirer-pc`).
- **Glisser des fichiers sur la fenêtre** :
  - des demandes (`.xpgreq`), sur le Tableau de bord ou la page Licences, ouvrent l'assistant ; sur Activations, elles sont notées ;
  - des licences, un registre ou une liste de révocations sont rangés dans l'archive.
- **La console `XPG-License-Admin.exe` reste**, pour tes scripts. Les deux partagent le même dossier : ta clé, ton archive et ta synchro ne bougent pas.
- **Pour tes clients, rien ne change** dans les Studios, sauf le numéro de version.
- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux, et sous Wine pour les programmes Windows (§ 4).

## 1. Démarrer

1. Dézippe `XPG-License-Admin-1.33.0-EDITEUR.zip` (**pour toi seul**) dans ton dossier de l'outil, à la place de l'ancien. Garde `SDL3.dll` à côté de l'application.
2. Lance **`XPG-License-Manager-Admin.exe`** (double-clic). Il ouvre ton poste habituel, `%LOCALAPPDATA%\XPG\LicenseAdmin`. Ta clé, ton archive, tes activations et ta synchro GitHub y sont déjà.
3. Le **Tableau de bord** résume l'archive : clients, licences actives, expirées et révoquées, PC activés, demandes en attente, licences qui finissent dans 30 jours. Un clic sur un chiffre ouvre sa page.
4. **Pour un nouveau client** :
   - glisse sa demande (`.xpgreq`) sur la fenêtre, ou clique **Nouvelle licence…** ;
   - suis les six étapes ;
   - **Signer et enregistrer…** écrit le `.xpglic` à lui envoyer, et le range dans l'archive.
5. **Pour un PC de plus** : dans Licences, choisis la licence, puis **Ajouter un PC…**. L'assistant reprend la licence avec son édition, ses fonctionnalités et sa fin. Si tu ne changes pas la durée, la fin reste exactement la même.
6. **Pour un PC remplacé** :
   - dans Licences, choisis la licence, puis le PC dans la liste du bas, et clique **Retirer un PC…** ;
   - une nouvelle licence est signée sans ce PC ; envoie-la au client ;
   - ajoute ensuite le nouveau PC (étape 5).
7. **Sans clé sur ce poste** (première fois) : Paramètres de sécurité te le dit, avec le bouton pour la créer. Sauvegarde-la juste après, avec une phrase de passe.

Ton travail reste dans l'archive. Le bandeau du bas montre le dernier résultat, et **Journal** garde ceux de la séance.

## 2. Les pages

Captures prises sous Wine, sur un poste de démonstration : une clé d'essai, des clients et des PC fictifs (« Client démo … »). Elles sont jointes à la livraison (`captures-1.33.0.zip`).

| Page | Ce qu'elle fait |
|---|---|
| **Tableau de bord** | Les chiffres de l'archive, les dernières opérations, la répartition par édition, l'état du poste (clé, protection, registre, synchro). Boutons : Synchroniser, Noter des demandes…, Nouvelle licence…. |
| **Clients** | Les clients tirés des licences signées et des demandes reçues : leurs licences, leurs PC, leurs demandes en attente. **Nouvelle licence pour ce client…** préremplit l'assistant. |
| **Licences** | Toutes les licences, avec recherche et filtres (actives, expirées, révoquées). En bas : le détail de la licence choisie et ses PC. Boutons : Nouvelle licence…, Ajouter un PC…, Exporter le fichier…, Retirer un PC…, Révoquer…, Ouvrir le dossier de l'archive. |
| **Éditions** | Les éditions du registre : type, PC, durée, grâce, fonctionnalités accordées. Tu modifies un brouillon, puis **Signer le registre n° …**. Annuler les modifications revient au registre signé. |
| **Fonctionnalités** | Les fonctionnalités de chaque Studio, et leurs limites (par exemple un nombre de vues). Même brouillon que les Éditions. |
| **Activations** | Les PC connus de ce poste (leurs demandes et les licences signées) : empreinte, client, licence, édition, logiciel, date. Recherche et filtre par état. Noter des demandes…, Exporter pour Excel…. |
| **Licences flottantes, Boutique, Commandes** | Grisées dans le menu. La page dit ce qu'il faudrait (le service en ligne) et qu'il n'existe pas encore. Rien n'est simulé. |
| **Révocations** | La liste signée : Révoquer… (avec la raison), Retirer la révocation, Exporter la liste… pour la donner aux Studios. |
| **Synchronisation GitHub** | L'état (dépôt, jeton, clé de synchro, dernier résultat) et un assistant en trois étapes : le dépôt, le jeton (champ masqué, gardé protégé sur ce PC), vérifier puis synchroniser. Le réseau tourne à part : la fenêtre ne se fige pas. Le code de secours ne s'affiche que sur ta demande, et ne se copie que sur un clic. |
| **Historique** | Tout ce qui s'est passé : demandes reçues, licences signées, extensions (PC ajoutés), révocations. Exporter… en CSV. |
| **Paramètres de sécurité** | Ta clé : son identifiant, son empreinte, sa protection (DPAPI), ses fichiers. Sauvegarder la clé… (phrase de passe, Argon2id), Vérifier une sauvegarde…, Restaurer une sauvegarde…, Exporter la clé publique…. Les phrases sont masquées et effacées juste après usage. |

Les captures jointes :

- `01-tableau.png` à `11-flottantes.png` : chaque page ;
- `12-assistant-1-demandes.png` à `16-assistant-5-recapitulatif.png` : l'assistant, étape par étape, pour un « Client démo Nouveau » (édition PROFESSIONAL).

## 3. Ce qui ne change pas

- **La console `XPG-License-Admin.exe`** : mêmes commandes, plus `signer-licence --retirer-pc CLE` (répétable). Un PC révoqué te propose maintenant de le retirer de la licence.
- **Les fichiers** : `.xpgreq`, `.xpglic`, `.xpgreg`, la liste des révocations. Un client n'a rien à refaire.
- **Le dossier du poste**, ta clé, ta sauvegarde, ton jeton GitHub et ta clé de synchro.
- **Les Studios** : l'activation par fichier, vérifiée hors ligne. La 1.33.0 n'apporte rien de neuf à tes clients ; ils peuvent rester en 1.32.0.
- **L'application n'est jamais livrée à un client.** Le contrôle du paquet arrête la construction s'il trouve son nom ou sa marque dans un installateur ou un portable. Elle est dans le zip de l'éditeur seulement.

## 4. Les essais

- **Linux : 151 suites sur 151 réussies** (`ctest`, sur la version livrée). Cinq sont nouvelles, pour l'application :
  - `adminworkspace` (57 contrôles) : le modèle de données, sans fenêtre. La clé, l'archive, l'assistant, signer, révoquer, la synchro, la sauvegarde de la clé, remplacer un PC ;
  - `adminguishell` (212 contrôles) : la coquille, le menu, le tableau de bord et les pages du service en ligne ;
  - `adminguilicences` (216) : Licences, l'assistant, Révocations ;
  - `adminguisecurite` (158) : Paramètres de sécurité, Synchronisation GitHub. Le travail lent tourne sur un vrai fil, et aucun secret n'apparaît dans les messages ;
  - `adminguidonnees` (226) : Éditions, Fonctionnalités, Activations, Clients, Historique.

  Les pages sont dessinées et cliquées sans fenêtre : les boutons font l'opération, les refus sont vérifiés (pas de clé, travail en cours, champ vide).
- **Sous Wine, avec les programmes Windows livrés :**
  - **l'installation par-dessus la 1.32.0** :
    - elle passe, et ta clé d'API est gardée ;
    - les programmes installés sont ceux construits : versions 1.33.0, agent 1.2.0 ;
    - `--cli`, le bac à sable, les associations de fichiers et les copies de secours marchent ;
    - rien de l'outil de l'éditeur n'est installé ;
  - **la console de l'éditeur** (WinHTTP et DPAPI de Wine, contre le faux GitHub local) : 17 commandes réussies et les 5 refus attendus. Journal joint : `essai-outil-wine.txt`, codes de secours masqués ;
  - **l'application** : ouverte sur un poste de démonstration préparé avec la console, une capture par page et par étape de l'assistant (§ 2). Elle a été lancée sous Wine et dessinée, mais personne n'a cliqué dedans à la souris : les clics des captures sont faits par le programme (`--cliquer`, `--taper`, `--choisir`).

## 5. Installer

**L'outil de l'éditeur** (le zip **à part, pour toi seul**) : remplace l'ancien dossier par le nouveau. Il contient `XPG-License-Manager-Admin.exe` (l'application), `SDL3.dll`, `XPG-License-Admin.exe` (la console), `LISEZ-MOI-EDITEUR.txt`, `registre.json` et `parametres-exemple.json`. Ne le mets jamais avec l'installateur.

**Les Studios**, si tu veux la 1.33.0 : lance `API-IHM-Studio-Setup-1.33.0.exe` (dans le zip) par-dessus la 1.32.0. Tes projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.33.0.exe` | `19A7DEA45F82044D2DB0C8CC4BB9077157C5231E7838B461149C1593C82CB7DB` | 54 858 752 octets |
| `API-Studio-1.33.0-portable.zip` | `045AD2CC684BC736C4DDDAE64A68FAFEDA3F81301216CA28914AD7909F8C72BA` | 190 fichiers |
| `IHM-Studio-1.33.0-portable.zip` | `B8A48546333A3C0B73ABC51817F70FCB592573DD974E6C2C0DC1D54FFDCAA1CD` | 191 fichiers |
| `XPG-License-Admin-1.33.0-EDITEUR.zip` (**à part, pour toi seul**) | `6659DB8EB1F81DF5CF90F101A2F00E44F9406ACE691DA6CD996CC89B7D63335E` | 5 444 931 octets |

## 6. Limites

- **Rien n'a été essayé sur un vrai Windows**, ni contre le vrai GitHub. L'application a tourné sous Wine seulement : à toi de l'ouvrir une première fois sur ton PC.
- **Licences flottantes, Boutique, Commandes** : pas faites. Il leur faut un service en ligne, qui n'existe pas.
- **Le dialogue « Ouvrir » choisit un seul fichier à la fois.** Pour plusieurs demandes d'un coup, glisse-les ensemble sur la fenêtre.
- **Les clients sont tirés des licences** : pas de fiche client à part (adresse, contact). « Voir ses licences » ouvre la page Licences sans la filtrer sur ce client.
- **Le brouillon des Éditions et Fonctionnalités reste en mémoire** jusqu'à « Signer le registre ». Si tu fermes l'application avant, il est perdu, sans avertissement.
- **Pas de limites par licence dans l'assistant** : une licence garde les limites de son édition (le nombre de vues, par exemple). Pour d'autres limites, fais une édition CUSTOM.
- **Historique** : une licence révoquée, puis remise, puis révoquée de nouveau, n'y apparaît qu'une fois révoquée.
- **Thème clair seulement.**
- **`--version` n'affiche rien dans une invite de commandes**, parce que c'est une application à fenêtres. La version est en bas à gauche de la fenêtre ; la console, elle, l'affiche.
