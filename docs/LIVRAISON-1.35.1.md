# API Studio et IHM Studio 1.35.1 : tes licences actives, et le logiciel bloqué sans licence

Faite à partir de tes retours du 07/11 sur la 1.35.0 :
- l'erreur de l'installateur à la fin (« Runtime error (at 324:790): Cannot open file …\ui\icone-.bmp ») ;
- « dans le menu principal de IHM Studio, fait un rappel de la licence active ou si y'a aucune licence » ;
- « le logiciel ne doit pas être utilisable sans licence, facile à comprendre » ;
- tes fichiers pour les Studios : `registre.xpgreg`, `ServiceAddress.hpp`, `cle-publique.txt`, `LISEZ-MOI.txt`.

Elle se met à jour par-dessus la 1.35.0 et garde tes données.

## 0. En bref

- **L'installateur ne plante plus à la fin.** L'étape « Licence » ajoutée en 1.35.0 n'avait pas d'icône : la dernière page en cherchait une sans nom. Elle a maintenant la sienne (une clé), et la construction de l'installateur s'arrête désormais si une étape n'a pas son icône.
- **Ta clé est dans les Studios.** Ta clé publique (`xpg-1f422b8063a39e7c`, prise de `cle-publique.txt` : j'ai vérifié qu'elle a bien signé ton registre n° 318) et ton registre sont compilés et livrés. Les licences sont donc **actives** : le premier lancement ouvre la **Découverte** (30 jours d'essai, tout ouvert).
- **Sans licence, le logiciel ne s'utilise pas.** Une fois la Découverte finie, ou sans licence valable (expirée, révoquée, bail expiré, aucun poste flottant libre…) : **aucun projet ne s'ouvre ni ne se crée**. Une fenêtre « Licence nécessaire » le dit, avec **Activer ma licence…**. Un projet déjà ouvert reste ouvert et s'enregistre : personne ne perd son travail.
- **L'accueil dit toujours où tu en es** : « Découverte : il te reste 30 jours », « Licence : PROFESSIONAL · jusqu'au … », ou, en rouge, « Sans licence : aucun projet ne s'ouvre ». Même une version sans clé le dit (« Licences non activées dans cette version »).
- **Le service n'est pas encore en ligne** (ton `ServiceAddress.hpp` est vide) : dans cette version, une licence s'active **par fichier** (le client fait sa demande, tu signes, il importe). L'activation par code viendra dès que tu m'envoies l'adresse de ton service (§ 1).

## 1. Ce qu'il te reste à faire

1. **Mettre le service en ligne** (le pas à pas : `service-licences\LISEZ-MOI-SERVICE.txt` du zip de ton outil), puis dans ton outil **Paramètres de sécurité › Préparer les fichiers pour les Studios…** et **envoie-moi le nouveau `ServiceAddress.hpp`** (l'adresse n'est pas un secret). La version suivante activera par code.
2. **Ne m'envoie plus `cle-editeur.xpgcle`** : c'est la sauvegarde de ta **clé privée** (chiffrée par ta phrase de passe). Je ne m'en suis pas servi et je ne l'ai mise nulle part ; seuls les fichiers du dossier « Préparer les fichiers pour les Studios » sont utiles (le `LISEZ-MOI.txt` le dit en bas). Si ta phrase de passe est courte ou devinable, dis-le-moi : on refait une clé tant qu'aucune licence n'est encore chez un client.
3. **Ce qui reste ouvert pendant la Découverte** se règle dans ton outil (Éditions › EVALUATION), puis Signer le registre et me l'envoyer.

## 2. Les essais

À COMPLÉTER

## 3. Installer

À COMPLÉTER

## 4. Limites

- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine.
- **Le poste d'exploitation** (`--ihm`, le projet FINISH qui tourne sur la machine) n'est pas bloqué sans licence : l'arrêter d'un coup arrêterait un atelier. Dis-moi s'il doit l'être aussi.
- **La ligne de commande d'analyse** (`API-Studio.exe --cli`, sans fenêtre) n'est pas bloquée non plus : elle ne fait qu'écrire un résumé.
- **La Découverte se recommence** en réinstallant Windows (§ 7 de la note 1.35.0).
