# La passerelle GitHub des licences (1.34.0)

Depuis la 1.34.0, les logiciels de tes clients (API Studio, IHM Studio) et ton outil de l'éditeur s'échangent tout par **un dépôt GitHub public, la « passerelle »** :

- le client **envoie sa demande** d'activation depuis son logiciel (Licence… › Demande en ligne) ; il n'y a plus de fichier à t'envoyer ;
- tu la vois dans ton outil (**Demandes en ligne**) ; il n'y a rien à importer ;
- tu l'**acceptes** : la licence est signée **et publiée** pour ce PC. Le logiciel du client l'installe seul au démarrage suivant (ou avec « Vérifier maintenant »). Il n'y a pas de fichier à importer ;
- tu peux aussi la **refuser** en donnant une raison. Le client voit où en est sa demande, et peut l'**annuler** ;
- **une licence pour plusieurs PC** : le client demande « ajouter ce PC à ma licence XPG-… ». Tu l'acceptes, et la nouvelle licence est publiée à **tous** ses PC, qui la récupèrent seuls ;
- une révocation, un PC retiré, un nouveau registre sont publiés de la même façon.

La demande par fichier (`.xpgreq` / `.xpglic`) reste possible, pour un PC sans Internet.

## Ce qui est public, ce qui ne l'est pas

Le dépôt est **public** : les logiciels des clients le lisent sans mot de passe.

| Dans le dépôt | Lisible par tous ? |
|---|---|
| `passerelle.xpgpas` : la configuration, **signée par ta clé** | oui, mais le jeton des demandes y est masqué (voir plus bas) |
| `registre.xpgreg` et `revocations.xpgrev`, signés | oui, comme dans l'installateur |
| `postes/<poste>.xpgpos` : l'état de chaque PC, **signé** | seulement l'état (reçue, acceptée…) et ses dates |
| la licence d'un PC, la raison d'un refus | **non** : scellées pour la clé de ce PC seul |
| les issues (une par demande) | seulement un identifiant de poste et des dates |
| le nom du client, du PC, son contact, son message | **non** : scellés pour ta clé seule |

L'identifiant d'un poste est une empreinte de la clé du PC. Il ne dit rien de son matériel.

Tout ce que lit un logiciel client est **signé par ta clé** : une fausse licence, un faux état ou une fausse configuration sont refusés.

### Le jeton des demandes : ce qu'il faut savoir

Pour ouvrir une issue, il faut un jeton GitHub. Les logiciels de tes clients utilisent **le jeton des demandes** :
- c'est un jeton à grain fin, limité **aux issues de ce dépôt**. Il ne peut écrire aucun fichier, donc ni licence ni configuration ;
- il est publié dans `passerelle.xpgpas`, masqué : les robots de GitHub ne le prennent pas pour une fuite. **Quelqu'un qui cherche peut tout de même le lire** ;
- avec ce jeton, on peut seulement ouvrir, commenter ou fermer des issues de ce dépôt. Au pire, quelqu'un pourrait ouvrir de fausses demandes : ton outil les montre, tu les refuses ou tu les ignores. Une demande modifiée ou forgée ne passe pas les signatures ;
- s'il est abusé, crée-en un autre, puis « Publier la configuration ». Les logiciels prennent le nouveau à leur prochaine vérification. Révoque ensuite l'ancien sur GitHub.

## Mettre en place (une fois)

1. **Crée le dépôt public** sur github.com, par exemple `ton-compte/xpg-licences-passerelle` :
   - « Public », avec un README ;
   - dans Settings › General › Features, garde **Issues** coché.
2. **Ton jeton d'éditeur**, celui de la synchro privée (1.31) : ajoute-lui le nouveau dépôt.
   - Chemin : Settings › Developer settings › Personal access tokens › Fine-grained tokens › ton jeton › Edit › Repository access ;
   - permissions sur les deux dépôts : **Contents : Read and write** et **Issues : Read and write**.
3. **Crée le jeton des demandes** : Generate new token.
   - Nom : « XPG demandes clients » ; expiration : 1 an, note la date ;
   - Repository access : **Only select repositories**, la passerelle seulement ;
   - Repository permissions : **Issues : Read and write**, et rien d'autre (Metadata : Read-only est ajouté par GitHub).
4. Dans **XPG License Manager Admin**, page Synchronisation GitHub, carte « Passerelle des clients » :
   - le dépôt (`ton-compte/xpg-licences-passerelle`) et le jeton des demandes, puis **Enregistrer la passerelle** ;
   - **Vérifier**, puis **Publier la configuration**. Elle publie aussi ton registre et ta liste de révocation.
5. **Pour que les logiciels de tes clients s'en servent**, ils doivent connaître ta clé publique et l'adresse de la passerelle :
   - Paramètres de sécurité › **Préparer les fichiers pour les Studios…** écrit `PublisherKey.hpp`, `registre.xpgreg` et `GatewayAddress.hpp` ;
   - ce sont des fichiers **publics**, sans aucun secret. Donne-les au développeur : ils sont compilés dans la version suivante.

   Tant que ce n'est pas fait, les logiciels livrés ont les licences « non activées » : tout est ouvert, pas d'évaluation, pas de passerelle.

## Au quotidien

- Un client envoie une demande : GitHub t'envoie un courriel (une nouvelle issue dans ton dépôt).
- Dans l'outil, page **Demandes en ligne**, clique **Relever les demandes**. Ensuite :
  - **Accepter…** : l'assistant de licence s'ouvre avec sa demande ; à la signature, la licence est publiée et l'issue fermée ;
  - **Refuser…** : écris la raison ; seul le client la lira.
- Le client voit « Reçue par l'éditeur », puis « Acceptée ». Sa licence s'installe au démarrage suivant.

## Limites

- **Les licences flottantes** (N postes en même temps, sur un même compteur) ne sont **pas faites**. GitHub ne convient pas à un compteur partagé : un seul jeton pour tous, des limites de débit, aucune façon sûre de compter les postes ouverts. Il faudra un vrai service en ligne.
- **Un PC sans Internet** (ou dont le pare-feu bloque github.com) : la demande en ligne échoue, et le logiciel le dit. La demande par fichier reste.
- **Délai** : GitHub sert les fichiers publics avec un cache d'environ 5 minutes. Une licence publiée peut mettre quelques minutes à être vue.
- **Limite de GitHub** : 5 000 appels par heure pour ton jeton. Les logiciels des clients lisent sans jeton : ils ne comptent pas, sauf pour envoyer, commenter ou annuler une demande.
- **L'évaluation** est comptée sur le PC. Quelqu'un qui efface toutes ses traces (plusieurs copies, dont une dans le registre de Windows) peut la recommencer.
