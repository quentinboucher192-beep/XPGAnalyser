# API Studio et IHM Studio 1.34.0 : GitHub devient la passerelle des licences, et l'évaluation commence à l'installation

Faite à partir de tes demandes du 07/11 :
- « je veux aussi avoir les demandes sur github, github doit servir de passerelle pour tous les echanges » ;
- « quand j'installe le logiciel classique je dois etre en periode d'evaluation » ;
- « une licence accepté n'a pas besoin de passer par un fichier que la personne doit importé ».

Elle se met à jour par-dessus la 1.33.0 et garde tes données.

## 0. En bref

- **L'évaluation dès l'installation.** Sans licence, un Studio est en évaluation depuis son premier lancement, pour la durée de ton édition d'évaluation (30 jours).
  - À **chaque ouverture**, il dit les jours qui restent : sur l'accueil, dans la cloche, et dans la fenêtre Licence.
  - Ensuite, il revient à l'édition FREE. Rien ne se ferme, et le projet reste ouvert.
- **La demande de licence en ligne**, dans la fenêtre Licence, nouvel onglet **Demande en ligne**.
  - Le client remplit sa société, le nom du PC, un contact et un message, puis **Envoyer à l'éditeur**.
  - **Une seule demande par PC.** Il voit où elle en est et peut **l'annuler**.
- **Tu la vois sans rien importer**, dans ton outil, nouvelle page **Demandes en ligne** : **Relever les demandes**, puis **Accepter…** ou **Refuser…** (avec une raison).
- **Une licence acceptée arrive seule.** Elle est signée et publiée pour ce PC. Son Studio l'installe au démarrage suivant, ou avec « Vérifier maintenant ». Aucun fichier n'est à importer.
- **Les licences pour plusieurs PC** : un client demande « ajouter ce PC à ma licence XPG-… ». Tu l'acceptes, et la nouvelle licence est publiée à tous ses PC, qui la récupèrent seuls. Une révocation, un PC retiré et un nouveau registre passent aussi par la passerelle.
- **Rien d'un client n'est lisible sur GitHub.** Les noms, les licences et les messages sont scellés. Tout ce que lit un Studio est signé par ta clé.
- **Ce qu'il te reste à faire** (§ 1) : créer le dépôt public et le jeton des demandes, régler la passerelle dans ton outil, puis **me donner trois fichiers publics** pour que la version suivante des Studios s'en serve.
- **Rien n'a été essayé sur un vrai Windows ni contre le vrai GitHub** : seulement sous Linux, et sous Wine contre un faux GitHub local (§ 4).

## 1. Ce qu'il te reste à faire (une fois)

Le pas à pas complet est dans [PASSERELLE-GITHUB.md](PASSERELLE-GITHUB.md) (joint), et dans `LISEZ-MOI-EDITEUR.txt` du zip de l'éditeur. En court :

1. **Crée sur GitHub un dépôt PUBLIC** pour la passerelle, par exemple `ton-compte/xpg-licences-passerelle`, avec les Issues activées.
2. **Ton jeton GitHub** (celui de la synchro privée) : ajoute-lui ce dépôt, avec **Contents** et **Issues** en « Read and write ».
3. **Crée le jeton des demandes** : un jeton à grain fin, limité à ce dépôt, avec **Issues : Read and write** et **rien d'autre**. Les logiciels de tes clients s'en servent pour ouvrir leurs demandes. Il est publié masqué, mais lisible par qui cherche : il ne permet d'ouvrir, commenter et fermer que des issues de ce dépôt (§ 6).
4. **Dans XPG License Manager Admin**, page Synchronisation GitHub, carte « Passerelle des clients » :
   - le dépôt et le jeton des demandes, puis **Enregistrer la passerelle** ;
   - **Vérifier**, puis **Publier la configuration**.
5. **Paramètres de sécurité › Préparer les fichiers pour les Studios…** écrit trois fichiers **publics, sans aucun secret** :
   - `PublisherKey.hpp`, ta clé publique ;
   - `registre.xpgreg`, ton registre signé ;
   - `GatewayAddress.hpp`, l'adresse de ta passerelle.

   **Envoie-les-moi**, ou commite-les aux chemins indiqués dans le `LISEZ-MOI.txt` du dossier. Je les compile dans la version suivante des Studios.

> **Tant que l'étape 5 n'est pas faite, les Studios livrés n'ont pas ta clé** : leurs licences sont « non activées » (tout est ouvert), comme jusqu'ici. Ils n'ont donc ni évaluation ni passerelle. Dans cette 1.34.0, tout a été essayé avec une clé d'**essai** et un faux GitHub (§ 4) : le code est prêt, il attend ta clé et ton dépôt.

## 2. Ce que voit ton client

- **À l'installation** : le Studio est en évaluation.
  - Un encart orange, bien visible sur l'accueil : « Évaluation : il te reste 30 jours », avec un bouton **Licence…**.
  - La même phrase en bas de l'accueil, dans la cloche et en tête de la fenêtre Licence, à chaque ouverture.
- **Licence… › Demande en ligne** : Société / client (obligatoire), Licence existante (pour ajouter ce PC à une licence de sa société), Nom de ce PC, Contact, Message, puis **Envoyer à l'éditeur**.
- **Sa demande en cours** : son numéro, sa date, son état (« Envoyée — en attente de l'éditeur », « Reçue par l'éditeur », « Acceptée », « Refusée » avec ta raison), **Annuler la demande**, **Vérifier maintenant**.
- **Acceptée** : au lancement suivant, « Ta licence XPG-… est installée (reçue de l'éditeur). » La licence est valide, sans aucun fichier.
- **Sans Internet**, ou si le pare-feu bloque GitHub : la demande en ligne dit qu'elle n'a pas pu partir, et la demande par fichier reste là.

## 3. Ce que tu fais dans ton outil

- **Demandes en ligne** (nouvelle entrée du menu, après Activations) :
  - **Relever les demandes** : chaque demande est vérifiée (la signature du PC) et ouverte avec ta clé. Le client voit alors « Reçue par l'éditeur ».
  - **Accepter…** : l'assistant de licence s'ouvre avec sa demande. Si elle nomme une licence existante, c'est en mode « ajouter un PC ».
    - À la signature (« Signer et publier »), la licence est publiée pour **tous** ses PC, et la demande est fermée.
  - **Refuser…** : écris ta raison ; seul le client la lira.
  - **Voir sur GitHub** : ouvre la demande sur github.com.
- **Le tableau de bord** compte les demandes en ligne en attente, avec la date du dernier relevé.
- **Tout ce que tu signes est publié seul** quand la passerelle est réglée : une licence, un PC ajouté ou retiré, une révocation, un registre. La publication ne bloque jamais la fenêtre ; un échec est dit, et les fichiers restent dans l'archive comme avant.
- **La console** a les mêmes gestes : `passerelle-config`, `passerelle-jeton-demandes`, `passerelle-verifier`, `passerelle-publier-config`, `passerelle-demandes`, `passerelle-publier-licence`, `passerelle-refuser`, `kit-studios`.

## 4. Les essais

- **Linux : @NSUITES@ suites sur @NSUITES@ réussies** (`ctest`, sur la version livrée). Six sont nouvelles :
  - `evaluation` (@N_EVAL@ contrôles) : le premier lancement, les jours qui restent, la fin, l'horloge reculée, une copie effacée qui ne relance rien, une licence qui passe avant ;
  - `passerelle` (@N_GW@) : les formats, le sceau (pour un PC, pour l'éditeur, pas pour un autre), le masque du jeton, les caractères de contrôle refusés ;
  - `fauxpasserelle` (@N_FAUX@) : le faux GitHub local qui sert aux essais sous Wine ;
  - `passerelle_studios` (@N_STUDIO@) : envoyer, une seule demande par PC, annuler, la licence installée seule, un refus, des états faux, plus anciens, d'un autre poste ou rejoués, hors ligne, la limite de GitHub, deux Studios sur le même PC, et le jeton qui ne sort jamais ;
  - `adminpasserelle` (@N_ADMIN@) : régler, vérifier, publier, relever, accepter (un PC, puis une licence partagée), refuser, révoquer, retirer un PC, et les attaques de la relecture (une demande rejouée, une issue modifiée, une avalanche de demandes, une demande d'ajout à vérifier) ;
  - `adminguidemandes` (@N_PAGE@) : la page Demandes en ligne.
- **Une relecture de sécurité** du code de la passerelle, faite après coup, a trouvé un problème grave, trois moyens et cinq mineurs. Ils sont tous corrigés, et chacun a son essai :
  - **le grave** : une ancienne demande remise en ligne aurait fait publier « reçue » par-dessus une licence pas encore récupérée ;
  - **les moyens** : une demande modifiée après coup, une avalanche de fausses demandes, l'identifiant de licence et la liste de révocation en clair ;
  - **les mineurs** : des caractères de contrôle dans la console, une licence acceptée mais pas installée qui n'était pas réessayée, des numéros qui repartaient à 1 après un nettoyage du dépôt, deux Studios sur le même PC, l'arrêt du fil de fond.
- **Sous Wine, avec les programmes Windows (WinHTTP), contre le faux GitHub local** (journal joint : `essai-passerelle-wine.txt`) :
  - **ton outil** (la console, avec une clé d'essai) : régler la passerelle, vérifier, publier la configuration, relever, signer, publier, refuser ;
  - **un IHM Studio compilé avec la clé d'essai** (une copie jamais livrée) :
    - au premier lancement : « Évaluation : il te reste 30 jours » ;
    - il envoie sa demande, puis, relancé après ton acceptation, sa licence est **Valide**, sans aucun fichier ;
    - un deuxième PC annule sa demande ; un troisième est refusé et voit ta raison ;
  - **ce qui est public** : aucun nom de client, contact, message, nom de PC ni jeton dans le dépôt, dans les issues ou dans le journal du serveur ;
  - **l'installation par-dessus la 1.33.0** : elle passe, ta clé d'API est gardée, les programmes installés sont ceux construits (1.34.0), et rien de l'outil de l'éditeur n'est installé ;
  - **la synchro privée de l'archive (1.31)** : @CLI@ ;
  - captures jointes : `captures-1.34.0.zip`.


@INSTALLER@

## 6. Limites

- **Rien n'a été essayé sur un vrai Windows ni contre le vrai GitHub** : seulement sous Linux, et sous Wine contre un faux GitHub local. Les premiers essais réels seront les tiens (§ 1).
- **Les Studios livrés n'ont pas encore ta clé** : tant que tu ne m'as pas donné les trois fichiers (§ 1, étape 5), ni l'évaluation ni la passerelle ne s'appliquent chez tes clients.
- **Les licences flottantes** (N postes en même temps) ne sont **pas faites**. GitHub ne convient pas à un compteur partagé : un seul jeton pour tous, des limites de débit, aucune façon sûre de compter les postes ouverts. Il faudra un vrai service en ligne.
- **Le jeton des demandes est lisible par qui cherche.** Il ne permet que les issues de la passerelle, et ton outil se garde des abus connus (§ 4). Mais quelqu'un pourrait :
  - ouvrir de fausses demandes, que tu ignores ;
  - le faire révoquer par GitHub en le publiant en clair. Il te faudrait alors en créer un autre et republier la configuration.
- **Une demande « ajouter ce PC à ma licence » n'est pas une preuve** : vérifie auprès de ton client (l'outil te le demande).
- **L'évaluation est comptée sur le PC** : quelqu'un qui efface toutes ses traces (plusieurs copies, dont une dans le registre de Windows) peut la recommencer.
- **Délai** : GitHub sert les fichiers publics avec un cache d'environ 5 minutes. Une licence acceptée peut mettre quelques minutes à être vue.
- **La liste de révocation scellée** est la liste entière : un PC révoqué y lit aussi les autres révocations (identifiants, raisons). Une liste par licence demanderait de changer le format.
- **L'ancien `revocations.xpgrev` public**, si tu en avais publié un pendant tes essais, reste dans l'historique git du dépôt. L'outil efface le fichier, pas son historique.
- **La console** `signer-licence` ne demande pas la confirmation d'une demande d'ajout à une licence ; seule l'application la demande, et la console l'affiche dans sa liste.
