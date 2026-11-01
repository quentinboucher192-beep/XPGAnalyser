# API Studio et IHM Studio 1.21.1 : tout le classeur, pas une seule feuille

Livrée le 01/11/2026. Elle suit la 1.21.0 (`docs/LIVRAISON-1.21.0.md`), livrée dans la nuit.

Ta question : « pourquoi il ne lit que 1 feuille et pas tout le classeur ? » (la capture : l'IA décrit la seule feuille « Sorties Analogiques » de `2025 06 118 ALI 003 B LUNA.xlsx`).

## 1. Pourquoi une seule feuille

C'était le fonctionnement de la 1.21.0, pas un hasard :
- la détection choisissait **une feuille** : la plus sûre, puis la plus longue ;
- elle ne lisait en plus que les feuilles **au même en-tête, sur la même ligne** (les « jumelles » : *Lectures* et *Écritures* d'une table d'échange) ;
- et l'IA connectée ne recevait plus le texte brut du classeur, seulement un résumé de **cette** feuille – elle ne savait même pas que les autres existaient.

Dans une liste d'E/S en plusieurs feuilles (entrées TOR, sorties TOR, entrées analogiques, sorties analogiques…), les en-têtes diffèrent d'une feuille à l'autre : seule une feuille était donc lue. C'est ce que l'IA t'a dit, honnêtement, mais sans pouvoir aller plus loin.

## 2. Ce qui change

- **Toutes les feuilles du même genre sont lues**, chacune avec **sa propre correspondance** de colonnes, même si leurs en-têtes diffèrent (la jumelle garde la même ; les autres ont leur détection). L'analyse le dit : « 5 lignes lues », puis « Liste d'E/S : feuille « Entrées TOR » (3 lignes), en-tête ligne 2 ; lues aussi : « Sorties Analogiques » (2 lignes) ».
- **Les autres feuilles sont citées, avec la raison** : « feuille « vars » non lue : aucun en-tête reconnu », « … : masquée », « … : Fichier réseau (confiance 80 %), un autre genre ».
- **L'IA connectée connaît toutes les feuilles** : leur nom, leur genre, leur ligne d'en-tête, leur nombre de lignes, lues ou non ; les premières lignes de **chaque** feuille lue ; et la règle « ne décris jamais le classeur entier d'après une seule feuille ». Une feuille non lue se lit avec l'outil `lignes_classeur` (son paramètre `feuille`), case par case si elle n'a pas d'en-tête.
- **L'IA intégrée** (« Que contient le fichier joint ? ») montre quelques lignes de chaque feuille lue et liste les autres.
- **Les propositions** (« Crée les variables du fichier joint », les types IHM) prennent les lignes de **toutes** les feuilles lues.
- Une feuille **masquée** n'est jamais lue d'office (elle est citée).

Un classeur joint avec la 1.21.0 garde le résumé de son analyse d'alors (« 8 lignes lues ») ; l'IA voit quand même toutes ses feuilles et les lit. Pour un résumé à jour, joins-le de nouveau.

## 3. Les essais

@@ESSAIS@@

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.21.1.exe` : l'installateur (il met à jour la 1.21.0 au même endroit et garde tes données) ;
- `API-Studio-1.21.1-portable.zip` et `IHM-Studio-1.21.1-portable.zip` ;
- ce document.

Windows SmartScreen peut avertir (installateur non signé) : « Informations complémentaires », puis « Exécuter quand même ». Le format des projets ne change pas (33).

@@SHA@@

### Les vérifications sous Windows (Wine)

@@WINE@@

Ces vérifications sont faites sous Wine, **pas sur un vrai Windows**.

## 5. Les limites

- La détection d'une feuille reste à mots-clés : une feuille au genre mal reconnu n'est pas lue avec les autres (elle est citée, et l'IA peut la lire).
- Un **modèle de fichier** enregistré décrit une feuille ; les autres feuilles du même genre sont lues par leur détection.
- Ton classeur LUNA n'est pas dans cette session : l'essai a été fait sur un classeur fabriqué à l'image de ta capture (entrées TOR, sorties analogiques à un autre en-tête, une feuille libre, une feuille masquée) – aucune donnée client dans le dépôt.

## 6. Ce qui a été optimisé pour livrer plus tôt

- La cause lue directement dans le code (la règle des « jumelles » et le résumé d'une seule feuille), corrigée là où elle était : la lecture, l'analyse, les consignes de l'IA, l'outil des lignes et l'IA intégrée, sans toucher au reste.
- Les deux fichiers d'aide F1 (12 Go chacun) compilés seuls, puis **Linux et Windows compilés en même temps** sur les deux places de la machine ; l'installateur et la vérification sous Wine enchaînés sans attendre.
- Pas de captures d'écran.
