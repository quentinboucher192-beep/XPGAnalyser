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

**115 suites d'essais, toutes réussies** sous Linux. Pour la 1.21.1 :

| Suite | Ce qu'elle vérifie | Résultat |
|---|---|---|
| ai_modeles_fichiers_test (section 9, nouvelle) | un classeur de 4 feuilles fabriqué à l'image du tien : entrées TOR, sorties analogiques à un autre en-tête, une feuille libre, une feuille masquée ; les deux feuilles d'E/S lues (5 lignes) ; l'analyse qui cite les deux autres et pourquoi ; les quatre feuilles connues de l'IA connectée, avec les premières lignes de chacune ; la règle dans les consignes ; l'IA intégrée ; les variables des deux feuilles ; l'outil qui lit la feuille libre case par case | réussie |
| ai_modeles_fichiers_test (le reste) | un classeur de reports dont la feuille « Bits » (même genre, autre en-tête) est maintenant lue avec les deux autres : 7 variables et 7 adresses au lieu de 5 | 161 vérifications, 0 échec |
| hmi_editor_test | les notes de version (563, 80 versions), l'aide F1 (269 sujets) | réussie |

Le bac à sable de l'exe Windows a été relancé sous Wine (même fonction, même résultat qu'en 1.21.0).

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.21.1.exe` : l'installateur (il met à jour la 1.21.0 au même endroit et garde tes données) ;
- `API-Studio-1.21.1-portable.zip` et `IHM-Studio-1.21.1-portable.zip` ;
- ce document.

Windows SmartScreen peut avertir (installateur non signé) : « Informations complémentaires », puis « Exécuter quand même ». Le format des projets ne change pas (33).

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.21.1.exe` (44,1 Mo, 46 238 658 octets) :
`16060B1A6492D1EBA71A959852E576C2FBAB5A1224CB5AF6146CA7C5C8EC79DA`

Les zips portables (167 fichiers chacun) :
- `API-Studio-1.21.1-portable.zip` (34,2 Mo) :
  `42AAAFFCBBF600745D944D0C1CC8C30AF06D909D2AA3CBCC50F26A5422E1BDB2`
- `IHM-Studio-1.21.1-portable.zip` (34,2 Mo) :
  `E525B0E882834870CB5BCA1CF068DE2FE357A3B9AD139968BF76C197E5A873DE`

### Les vérifications sous Windows (Wine)

Les deux exe Windows (MinGW-w64) répondent `API Studio 1.21.1` et `IHM Studio 1.21.1` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB).

L'installateur (Inno Setup 6.4.1) a été lancé en silencieux **par-dessus la 1.21.0 livrée**, avec une clé API d'essai : « Installation process succeeded » ; la clé est intacte (même empreinte avant et après, « Clés API : 1 gardée ») ; les exe et `pdfium.dll` installés sont ceux qui ont été essayés ; les associations `.xihm` et `.xapi` sont là ; la copie de secours de la 1.21.0 est faite.

Le bac à sable de l'exe installé (`IHM-Studio.exe --bac-a-sable`) : analyse, compilation et exécution réussies, le cas juste réussi (3,0), le cas faux en échec comme prévu (1,0 au lieu de 5,0).

Ces vérifications sont faites sous Wine, **pas sur un vrai Windows**.

## 5. Les limites

- La détection d'une feuille reste à mots-clés : une feuille au genre mal reconnu n'est pas lue avec les autres (elle est citée, et l'IA peut la lire).
- Un **modèle de fichier** enregistré décrit une feuille ; les autres feuilles du même genre sont lues par leur détection.
- Ton classeur LUNA n'est pas dans cette session : l'essai a été fait sur un classeur fabriqué à l'image de ta capture (entrées TOR, sorties analogiques à un autre en-tête, une feuille libre, une feuille masquée) – aucune donnée client dans le dépôt.

## 6. Ce qui a été optimisé pour livrer plus tôt

- La cause lue directement dans le code (la règle des « jumelles » et le résumé d'une seule feuille), corrigée là où elle était : la lecture, l'analyse, les consignes de l'IA, l'outil des lignes et l'IA intégrée, sans toucher au reste.
- Les deux fichiers d'aide F1 (12 Go chacun) compilés seuls, puis **Linux et Windows compilés en même temps** sur les deux places de la machine ; l'installateur et la vérification sous Wine enchaînés sans attendre.
- Pas de captures d'écran.
