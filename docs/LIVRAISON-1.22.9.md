# API Studio et IHM Studio 1.22.9 : Unités et formats, une seule page

Suite de la 1.22.8, faite à partir de ta question du 02/11 : « dans IHM › Configuration il y a Unités et formats et Unités de mesure, est-ce vraiment utile d'avoir les 2 ? sinon combine pour ne faire qu'un seul onglet ». Elle se met à jour par-dessus la 1.22.8 et garde tes données.

## 1. Ce qui change pour toi

### Pourquoi il y avait deux pages

Les deux pages ne faisaient pas la même chose :
- **Unités et formats** dit comment chaque variable **s'affiche** : l'unité écrite à côté de la valeur, le format (0.00), la plage et les seuils. Les objets qui montrent la variable les reprennent.
- **Unités de mesure** est la **bibliothèque des unités physiques** : les familles (pression, débit…), leurs unités et leurs conversions (1 bar = 1000 mbar). Le calcul EN(…), la colonne Unité des variables et les Unités strictes s'en servent.

Leurs noms se ressemblaient trop, et deux entrées pour les unités, c'était une de trop.

### Une seule entrée, trois onglets

**Configuration › Unités et formats** a maintenant trois onglets :
- **Formats des variables** : c'est l'ancienne page Unités et formats. Elle s'ouvre d'abord ;
- **Unités de mesure** : c'est l'ancienne page Unités de mesure (les familles, les unités, la fiche, Unités strictes) ;
- **Convertir** : le banc d'essai d'une conversion, comme avant.

L'entrée **Unités de mesure** disparaît de l'arbre : Configuration a maintenant 12 entrées au lieu de 13.

Tout ce qui menait à l'ancienne page ouvre maintenant l'onglet Unités de mesure :
- **Gérer les unités…** dans le sélecteur d'unités ;
- les messages « unité inconnue », qui disent maintenant « Configuration › Unités et formats › Unités de mesure » ;
- une session rouverte qui avait l'ancienne page ouverte.

Un constat de Générer sur l'unité d'une variable ouvre l'onglet Formats des variables, sur sa ligne.

Rien ne change dans tes projets : seules les pages sont regroupées.

## 2. Les essais

Sous Linux : **130 suites d'essais sur 130** réussies (CTest).

- **`hmi_editor_test`** : 9 585 contrôles (9 580 en 1.22.8). Ce qu'il vérifie de nouveau :
  - l'arbre n'a plus qu'une entrée pour les unités ;
  - la page a ses trois onglets dans l'ordre et s'ouvre sur Formats des variables ;
  - chaque onglet s'ouvre bien ;
  - une ligne de format s'ajoute depuis son onglet ;
  - les comptes de l'arbre (12 entrées sous Configuration) sont à jour.
- L'ancienne page seule (Bibliothèque et Convertir) garde ses essais d'avant.
- **`ai_assistant_test`** : au premier passage, une vérification (« traité ») a échoué, une fois sur trois. Ce n'était pas lié à cette version. Depuis la 1.22.8, un PDF joint est aussi lu comme un modèle de vues possible, sur un fil de fond. Quand la machine est chargée, cette lecture peut finir après le délai que l'essai supposait. L'essai attend maintenant la fin réelle de la lecture (5 s au plus). Il passe 5 fois sur 5 sous charge. Le logiciel ne change pas : il attendait déjà cette lecture avant d'envoyer la demande.

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.22.9.exe` : l'installateur (48 234 353 octets). Il met à jour la 1.22.8 au même endroit ;
- `API-Studio-1.22.9-portable.zip` et `IHM-Studio-1.22.9-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.22.9.exe` | `B97B040BBC6A3BF3F68765D37724C674DAB77C43B10151726D65DCBEB80CB65C` |
| `API-Studio-1.22.9-portable.zip` | `D140F3D4A3B9196813C56BC1A77E74DA97123ACE3D977DD1AB0A500BC3ECEAE1` |
| `IHM-Studio-1.22.9-portable.zip` | `E5B24710F0D3FB6E8F6F9B5FF02F10BAA7D524FD083EBB29360E4D8AC6D055E3` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.22.8 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.22.9 » et « IHM Studio 1.22.9 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.22.8 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **La touche F1** sur la page mène à l'aide « Unités et formats des variables », quel que soit l'onglet ouvert. Cette aide présente les trois onglets et renvoie au sujet « Les unités de mesure dans les types » pour la bibliothèque.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows.** La nouvelle page n'a été vue que dessinée hors écran par les essais.
- **Pas de captures d'écran**, selon ta consigne permanente.
