# API Studio et IHM Studio 1.35.1 : tes licences actives, et le logiciel bloqué sans licence

Faite à partir de tes retours du 07/11 sur la 1.35.0 :
- l'erreur de l'installateur à la fin (« Runtime error (at 324:790): Cannot open file …\ui\icone-.bmp ») ;
- « dans le menu principal de IHM Studio, fait un rappel de la licence active ou si y'a aucune licence » ;
- « le logiciel ne doit pas être utilisable sans licence, facile à comprendre » ;
- « le poste d'exploitation aussi doit être bloqué sans licence » ;
- « change l'icone de l'exe de l'admin, met l'icone clé » ;
- tes fichiers pour les Studios : `registre.xpgreg`, `ServiceAddress.hpp`, `cle-publique.txt`, `LISEZ-MOI.txt`.

Elle se met à jour par-dessus la 1.35.0 et garde tes données.

## 0. En bref

- **L'installateur ne plante plus à la fin.** L'étape « Licence » ajoutée en 1.35.0 n'avait pas d'icône : la dernière page en cherchait une sans nom. Elle a maintenant la sienne (une clé). La même étape avait poussé la dernière, « Terminé », hors de la colonne de gauche : les onze étapes y tiennent de nouveau. La construction de l'installateur s'arrête désormais si une étape n'a pas son icône ou ne tient pas dans la colonne.
- **Ta clé est dans les Studios.** Ta clé publique (`xpg-1f422b8063a39e7c`, prise de `cle-publique.txt` : j'ai vérifié qu'elle a bien signé ton registre n° 318) et ton registre sont compilés et livrés. Les licences sont donc **actives** : le premier lancement ouvre la **Découverte** : 30 jours d'essai avec ton édition EVALUATION (18 fonctions sur 21 ; restent fermés : IA › Modèles avancés, Licences flottantes, Multi-utilisateur).
- **Sans licence, le logiciel ne s'utilise pas.** Une fois la Découverte finie, ou sans licence valable (expirée, révoquée, bail expiré, aucun poste flottant libre…) : **aucun projet ne s'ouvre ni ne se crée**. Une fenêtre « Licence nécessaire » le dit, avec **Activer ma licence…**. Un projet déjà ouvert reste ouvert et s'enregistre : personne ne perd son travail.
- **Le poste d'exploitation aussi** : sans licence, il ne démarre pas (la même fenêtre « Licence nécessaire »). Avec une licence, il prend son poste flottant et renouvelle son bail tout seul, sans avis (personne devant l'écran) : sinon, un poste allumé jour et nuit se serait bloqué au bout de 30 jours. Un poste déjà en marche quand la licence tombe continue de tourner (un atelier ne s'arrête pas d'un coup) ; il ne redémarre plus sans licence. Refusé, il revient à l'accueil, où l'encart rouge « Sans licence » le dit aussi.
- **L'icône clé** pour tes programmes d'éditeur : `XPG-License-Manager-Admin.exe` (et sa fenêtre) et la console `XPG-License-Admin.exe`, qui n'en avait pas.
- **L'accueil dit toujours où tu en es** : « Découverte : il te reste 30 jours », « Licence : PROFESSIONAL · jusqu'au … », ou, en rouge, « Sans licence : aucun projet ne s'ouvre ». Même une version sans clé le dit (« Licences non activées dans cette version »).
- **Le service n'est pas encore en ligne** (ton `ServiceAddress.hpp` est vide) : dans cette version, une licence s'active **par fichier** (le client fait sa demande, tu signes, il importe). L'activation par code viendra dès que tu m'envoies l'adresse de ton service (§ 1).

## 1. Ce qu'il te reste à faire

1. **Mettre le service en ligne** (le pas à pas : `service-licences\LISEZ-MOI-SERVICE.txt` du zip de ton outil), puis dans ton outil **Paramètres de sécurité › Préparer les fichiers pour les Studios…** et **envoie-moi le nouveau `ServiceAddress.hpp`** (l'adresse n'est pas un secret). La version suivante activera par code.
2. **Ne m'envoie plus `cle-editeur.xpgcle`** : c'est la sauvegarde de ta **clé privée** (chiffrée par ta phrase de passe). Je ne m'en suis pas servi et je ne l'ai mise nulle part ; seuls les fichiers du dossier « Préparer les fichiers pour les Studios » sont utiles (le `LISEZ-MOI.txt` le dit en bas). Si ta phrase de passe est courte ou devinable, dis-le-moi : on refait une clé tant qu'aucune licence n'est encore chez un client.
3. **Ce qui reste ouvert pendant la Découverte** se règle dans ton outil (Éditions › EVALUATION), puis Signer le registre et me l'envoyer.

## 2. Les essais

- **Linux : 159 suites sur 159 réussies** (`ctest`, sur la version livrée). Celles des licences : `licensing` (321 contrôles : la clé compilée est bien celle qu'écrit ton outil, octet pour octet), `licenceui` (58 : sans licence, aucun projet ; avec, tout s'ouvre ; l'encart de l'accueil dans chaque cas), `activation` (213), `evaluation` (86), `service_local` (77), `service_studios` (127), `adminservice` (158).
- `service_studios` a échoué une fois pendant que la version Windows se construisait sur le même poste (la machine très chargée), puis a réussi à chaque passage (seul, avec les essais des licences, dans les séries complètes). La cause n'est pas trouvée ; elle est sans doute liée à cette charge.
- **Ta clé** : la clé publique de `cle-publique.txt` vérifie la signature de ton registre n° 318 ; elle est compilée dans les Studios livrés (l'accueil le montre : la Découverte s'ouvre ; sans clé, il dirait « Licences non activées »).
- **Sous Wine, avec les programmes Windows livrés** (captures jointes : `captures-1.35.1.zip`) :
  - **l'installateur en fenêtre jusqu'au bout** : toutes les pages, dont « Ce qui a été installé » (celle de l'erreur) et « C'est installé ! », sans erreur ; les onze étapes visibles à gauche, « Terminé » compris ;
  - **l'installation en silencieux par-dessus la 1.35.0**, avec `/CODEFICHIER=` : elle passe, ta clé d'API est gardée, les programmes installés sont ceux construits (1.35.1), le code est laissé (sa forme vérifiée) et **n'est pas dans le journal de l'installateur** ;
  - **IHM Studio livré**, premier lancement : « Découverte : il te reste 30 jours (jusqu'au 07/12/2026) », et la phrase « Ensuite, sans licence, aucun projet ne s'ouvre » ;
  - **un IHM Studio compilé avec la clé d'essai** (une copie jamais livrée, avec un registre d'essai sans Découverte, pour voir la fin de l'essai sans attendre 30 jours) :
    - sans licence : l'encart rouge « Sans licence : aucun projet ne s'ouvre » ; **Nouveau projet** → la fenêtre « Licence nécessaire » ; rien ne se crée ;
    - **le poste d'exploitation** sans licence (`--ihm`, un projet FINISH de démonstration) : la même fenêtre, il ne démarre pas, et l'accueil montre l'encart rouge ;
    - **avec une licence** (PROFESSIONAL, demandée par ce PC, signée par ton outil avec la clé d'essai, importée) : un projet s'ouvre (« Licence : PROFESSIONAL · jusqu'au 07/11/2027 » en haut), et **le poste d'exploitation démarre** ;
  - **l'icône clé** : extraite de `XPG-License-Manager-Admin.exe` et de `XPG-License-Admin.exe` livrés.

## 3. Installer

**L'outil de l'éditeur** (le zip **à part, pour toi seul**) : remplace l'ancien dossier par le nouveau. Ta clé, ton archive et tes réglages ne bougent pas ; seule l'icône change (et la version).

**Les Studios** : lance `API-IHM-Studio-Setup-1.35.1.exe` (dans le zip) par-dessus la 1.35.0. Tes projets, tes réglages et ta clé d'API restent. **Attention : cette version active les licences.** Chez un client, le premier lancement ouvre sa Découverte de 30 jours ; ensuite, il lui faut une licence (par fichier pour l'instant, § 0). Sur ton propre PC, fais-toi une licence avec ton outil avant la fin de ta Découverte.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.35.1.exe` | `498710920A0F84894197EECC90E0D6197EC3ADD8FE9FD408EFF6EE34E7F12B66` | 55 205 443 octets |
| `API-Studio-1.35.1-portable.zip` | `F9F83C2EB9A5A4DBFFD83BBE0E413A4B705AD1CC01284A6BF2F6E7B12995B6CC` | 191 fichiers |
| `IHM-Studio-1.35.1-portable.zip` | `4C5430D29236FCDA5B815C4E7A5937FB83122A8DEA963256DEB1BD440E28267F` | 192 fichiers |
| `XPG-License-Admin-1.35.1-EDITEUR.zip` (**à part, pour toi seul**) | `263040A04DD310CAE715E6C65A1116C0D82EDBD240ACC37C95054AAA9EDF5746` | 6 090 206 octets |

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine.
- **Un poste d'exploitation déjà en marche** quand sa licence tombe continue de tourner jusqu'à son prochain démarrage.
- **La ligne de commande d'analyse** (`API-Studio.exe --cli`, sans fenêtre) n'est pas bloquée : elle ne fait qu'écrire un résumé d'un programme (elle sert aussi aux essais automatiques).
- **La Découverte se recommence** en réinstallant Windows (§ 7 de la note 1.35.0).
