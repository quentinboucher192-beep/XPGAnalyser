# 1.49.0 : trois programmes (Studios, Admin+, XPG Admin), l'ajout d'un admin par deux codes, repartir de zéro

Fait à partir de : « le .exe de l'admin+ reste tel quel mais accessible que à l'admin+, il faut un .exe pour les admin
classiques transmis lors d'un ajout d'admin, à son installation ça fait la coordination avec l'admin+ avec les codes à
saisir etc, et c'est un autre exécutable séparé dans les versions on a les 3 exécutables studios, admin+, admin » (tes
choix : deux codes ; le code d'invitation saisi dans l'installateur), et « dans la 1.49 reset toutes les données
sauvegardées sur les licences, les admins, clients, pc, activations, demandes, commandes, mais garde les données sur
admin+ (sur le service et la clé) ».

## 0. En bref

**Trois programmes**

- **Les Studios** (tes clients) : rien ne change.
- **L'Admin+** (toi seul) : le programme d'avant, tel quel, avec son installateur `XPG-License-Admin-Setup-…-EDITEUR.exe`.
  Lancé sur un PC **sans la clé de l'éditeur**, il ne propose que « Restaurer la clé de l'éditeur… » et « Créer la clé
  de l'éditeur… » (le premier PC) — jamais « Demander à devenir admin ».
- **XPG Admin** (tes admins) : un **programme à part** (`XPG-Admin.exe`, son installateur `XPG-Admin-Setup-….exe`, son
  icône, son dossier). Il n'utilise jamais la clé de l'éditeur, même si elle traîne sur le PC : rien de ce qui est à
  l'admin+ n'y apparaît (créer ou restaurer la clé de l'éditeur, ajouter un admin, publier une version, certifier le
  service…) ; il garde ce que ses droits permettent et la dérogation courte « Gérer le service ».
- **Versions** : trois emplacements, **Studios**, **Admin+**, **Admin** (.exe ou .zip). Chacun n'est proposé qu'à qui le
  concerne : les Studios aux clients, XPG Admin aux admins, l'Admin+ à toi seul (il ne se télécharge qu'avec ta clé).
  Les Studios d'avant (1.44 à 1.48) voient toujours la nouvelle version.

**Ajouter un admin : deux codes**

1. **Toi** : « Ajouter un admin… » envoie l'installateur **XPG Admin** (dans les notifications de son Studio, ou par un
   lien de 48 h) avec un **code d'invitation** `INV-XXXX-XXXX` : montré une seule fois chez toi (« Copier »), valable
   48 h, une seule fois, écrit aussi dans le message (« Ton code d'invitation : … (à saisir dans l'installateur) »). Le
   service n'en garde que l'empreinte.
2. **Lui** : l'installateur XPG Admin demande ce code (page « Code d'invitation », vérifiée). Au premier lancement, son
   Admin crée sa clé et envoie sa demande avec le code : un code inconnu, périmé ou déjà servi est refusé avec la raison,
   et se retape. Accepté : son écran montre **un code de 6 chiffres** (« Dis ce code à ton admin+ : 123 456 »).
3. **Toi** : dans Demandes d'admin, la demande montre son invitation ; « Accepter… » te demande de **taper les 6
   chiffres** (à la place de la case « j'ai vérifié l'empreinte ») ; faux : rien n'est signé. Bon : ta clé signe, il
   devient admin, son Admin le voit tout seul.

Les demandes d'avant la 1.49 (un Admin plus ancien) se valident toujours avec l'empreinte.

**Repartir de zéro**

**Service en ligne › Repartir de zéro…** (l'Admin+ seul ; un bandeau te le propose une fois au premier lancement de
l'Admin+ 1.49 ; XPG Admin ne l'a jamais). Pas d'effacement automatique : c'est toi qui décides.

- **D'abord une sauvegarde complète exacte** (celle de la 1.44) ; la fenêtre dit où elle est ; « Restaurer une
  sauvegarde… » remet tout. Sans sauvegarde réglée, ou sans le mot **REPARTIR** tapé, rien ne se fait.
- **Effacé, sur le service et sur ton PC** : les licences et leurs fichiers, les activations, les jetons flottants, les PC
  (Découverte compris), les clients, les demandes de licence, les commandes et leurs articles, les révocations (une liste
  neuve, vide), les admins (une liste neuve, vide ; leurs demandes, les invitations, la dérogation, le trousseau), les
  messages aux PC, les statistiques, le journal des admins (il repart avec la ligne « réinitialisation par l'admin+ »).
- **Gardé** : ta clé d'éditeur, ton identité d'admin+, la clé de synchro, la configuration du service (son adresse, son
  certificat, les adresses de secours), le catalogue (offres, options, fonctionnalités, réductions), les réglages de la
  boutique et tes produits Stripe, les versions publiées et leurs fichiers, tes réglages.
- Les Studios déjà activés perdent leur licence à leur prochaine vérification (le service ne la connaît plus).
- Après une restauration : publie une fois la liste des admins (n'importe quel changement) pour que les Studios qui
  ont vu la liste vide acceptent de nouveau les licences signées par un admin (docs/ADMINS.md § 13).

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin+ 1.49.0** (par-dessus la 1.48.1), puis les **Studios 1.49.0**.
2. **Service en ligne › Déployer le service** (la 1.49.0) : les invitations, les trois sortes de versions et « Repartir
   de zéro » en ont besoin.
3. **Versions** : publier la 1.49.0 avec ses trois fichiers (Studios, Admin+, et **XPG Admin** : `XPG-Admin-Setup-1.49.0.exe`,
   dans le zip `XPG-Admin-Setup-1.49.0.zip` de cette livraison — lui, tu peux le donner à tes admins).
4. Si tu veux repartir de zéro : **Service en ligne › Repartir de zéro…** (ou le bandeau du premier lancement).
5. Tes admins actuels recevront XPG Admin à la prochaine mise à jour, sans refaire de demande (s'ils ne sont pas effacés
   par « Repartir de zéro »).

## 2. Les essais

- **Linux : 194 suites sur 194 réussies** (`ctest`, sur la version livrée ; journal `essais-1.49.0.txt`). Celles de cette version :
  - **le service** (`node --test`, 259 sur 259) : les invitations (créée par l'admin+ seul ; un code inconnu, périmé,
    déjà servi : refusé avec sa phrase ; le service n'en garde que l'empreinte), le code de 6 chiffres calculé pareil en
    JavaScript et en C++ (une valeur connue), les trois sortes de versions (l'Admin+ ne se télécharge qu'avec la clé de
    l'éditeur ; chacun ne voit que la sienne ; les Studios d'avant voient toujours la version), « Repartir de zéro »
    (tout ce qui est effacé l'est, tout ce qui est gardé reste ; refusé sans l'admin+ ou sans le mot ; la sauvegarde
    remet tout) ;
  - **l'Admin contre le vrai service local** : `admininvitation` (39 contrôles : l'invitation, XPG Admin lit le code et
    envoie sa demande, ses 6 chiffres ; l'admin+ tape un mauvais code : rien n'est signé ; la case seule ne suffit plus ;
    le bon code : XPG Admin se voit admin ; un code inconnu puis retapé, déjà servi, périmé), `adminguiedition` (42 :
    XPG Admin n'a rien de l'admin+, même avec la clé de l'éditeur dans son dossier ; l'Admin+ sans la clé : seulement
    Restaurer et Créer), `adminrepartir` (85 : « Repartir de zéro » de bout en bout, l'archive du PC vidée, le reste
    intact, le bandeau une seule fois, XPG Admin ne l'a jamais), `adminguiversions` (87 : les trois emplacements),
    `adminguiadmins` (139).
- **Sous Wine** : les trois programmes construits ; les Studios installés en silencieux par-dessus la 1.48.1 (ta clé
  d'API gardée) ; l'Admin+ par-dessus la 1.48.1 (mise à jour, désinstallation, réinstallation) ; **XPG Admin** installé
  en silencieux avec un code fictif (`/INVITATION=inv-abcd-efgh` → `INV-ABCD-EFGH` écrit pour le premier lancement).
  Captures : les pages de l'installateur XPG Admin (`1490_8_…page-1` à `page-6`, la page « Code d'invitation » avec un
  code fictif tapé, jusqu'au Récapitulatif, jamais l'installation) et celles de l'Admin+ (`1490_9_…page-1` à `page-5`) ;
  l'Admin+ avec le vrai service local : Versions (`1490_01`), « Ajouter un admin » et le code d'invitation montré une
  fois (`1490_02` ; sur ce service d'essai, aucune version n'est publiée : l'Admin le dit en rouge), la fenêtre
  « Repartir de zéro » (`1490_03`, jamais confirmée), l'Admin+ sans la clé de l'éditeur (`1490_04`) ; le premier
  lancement de XPG Admin (`1490_05`) ; le Studio livré (`1490_00`), lancé avec une adresse locale où rien ne répond :
  **ton service n'a pas été appelé**.

## 3. Installer

**Les Studios** : lance `API-IHM-Studio-Setup-1.49.0.exe` (dans le zip) par-dessus la 1.48.1. Tes projets, tes
réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.49.0.exe` | `7020941C9169F393B69BCBD954B2ADFBE110B33516247A06E1682E715559ADD1` | 63 815 725 octets |
| `API-Studio-1.49.0-portable.zip` | `960373097E6CDB68F9FB69B1D5D32B917672DD5D47CEA3EA2833EB5214DC9CFC` | 250 fichiers |
| `IHM-Studio-1.49.0-portable.zip` | `5330EB66AACC48949B02EA0B8EA183A9EF8B643A325DCC94F414F1C21342FC21` | 251 fichiers |

**L'Admin (pour toi seul, envoyé à part)** : lance `XPG-License-Admin-Setup-1.49.0-EDITEUR.exe` par-dessus la 1.48.1 ;
le dossier de ta clé n'est pas touché. Ne le donne à personne : pour un nouvel admin, Administrateurs › Ajouter un
admin… lui envoie l'installateur publié dans Versions (sans ta clé).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-License-Admin-Setup-1.49.0-EDITEUR.exe` | `F42B7266A6D49045D1D1C84C8AF16AA0D1D0A9F4B736E6BEE45A5714746970BA` | 8 888 314 octets |

**XPG Admin** (pour tes admins, à publier dans Versions) : `XPG-Admin-Setup-1.49.0.exe` (dans le zip).

| Fichier | SHA-256 | Taille |
|---|---|---|
| `XPG-Admin-Setup-1.49.0.exe` | `9AD62080F41A30E1559DB617B62F12D5056D3C1AF8AA7AF45AD45839EADDB96D` | 8 170 174 octets |

## 4. Limites

- **Pas d'essai complet sous Wine** : les parcours sont essayés sous Linux contre le vrai service lancé sur ce poste ;
  sous Wine, l'installation des trois programmes, leurs pages d'installation et des captures.
- XPG Admin et l'Admin+ partagent leur code : XPG Admin **cache et refuse** ce qui est à l'admin+, il n'en est pas
  physiquement privé ; la vraie protection reste ta clé d'éditeur, qu'il ne charge jamais.
- Les écrans nouveaux de l'invitation (le code montré, les 6 chiffres) sont essayés au niveau de l'Admin (sa logique,
  contre le vrai service), pas par un essai de l'interface ; les captures les montrent.
- Ton Studio, s'il est encore en 1.48, se verra proposer une fois XPG Admin au lieu de l'Admin+ ; en 1.49, c'est l'Admin+.
- **Rien sur un vrai Windows** : sous Wine seulement.
