# API Studio et IHM Studio 1.20.1 : les deux limites de la 1.20.0, corrigées

Livrée le 31/10/2026 dans la soirée. Elle suit la 1.20.0 (`docs/LIVRAISON-1.20.0.md`), livrée le même soir.

Ta question : « pourquoi t'as pas corrigé ça ? » – les deux limites que le document de la 1.20.0 laissait :
1. **Appliquer** une grosse proposition et **Prévisualiser** tournaient encore au premier plan, sans barre de progression ;
2. une partie de fichier joint lue à part arrivait en **extrait**, pas en texte entier, et chaque lecture coûtait une demande de plus.

Les deux sont corrigées ici.

## 1. Appliquer et Prévisualiser ne bloquent plus l'appli

À partir de **40 opérations** (comme la vérification de la carte en 1.20.0) :
- **Appliquer** (et « Appliquer malgré les erreurs ») construit le projet modifié **sur un fil de fond**, sur une copie du projet. La carte dit « Application… » avec une **barre d'avancement** et « Tu peux continuer à travailler » ; ses boutons sont grisés pendant ce temps.
- À la fin, si le projet **n'a pas changé** entre-temps, **une seule commande** pose le résultat : un Ctrl+Z reprend toute la proposition, comme avant. Si tu as modifié le projet pendant ce temps, **rien n'est écrasé** : rien n'est appliqué, la carte le dit, et tu peux recommencer.
- Le clic sur Appliquer ne revalide plus la proposition : il reprend la vérification de fond de la carte.
- **Prévisualiser** reprend aussi cette vérification (plus de recalcul complet à l'ouverture, sur le fil de l'interface). Si elle n'est pas finie, la fenêtre l'attend en montrant l'avancement (« Vérification de la proposition sur un fil de fond… 63 % ») ; **Appliquer** y reste grisé jusqu'au résultat.
- En dessous de 40 opérations, tout reste immédiat (quelques millisecondes).

## 2. Le texte exact d'une pièce jointe, à la demande

Quand un fichier texte (TXT, CSV, JSON, XML) ne tient pas entier dans la fenêtre du modèle, les parties en trop partent toujours en extrait (la 1.20.0). Mais l'IA n'est plus limitée à cet extrait : elle a maintenant un outil, **`lire_piece_jointe`** (fichier, de la ligne, à la ligne), pour demander **le texte exact** des lignes dont elle a besoin.
- La réponse est faite **sur ce PC**, sur le fil du fournisseur, **dans la même demande** : pas de lecture préalable de plus, pas d'appel d'outil visible dans la conversation. Tu vois seulement l'état « Lecture exacte de mesures.csv : lignes 5000 à 5001… », puis la réponse de l'IA continue.
- Les blocs de texte envoyés disent à l'IA quelles lignes elle a en entier, lesquelles en extrait, et qu'elle peut lire le texte exact.
- **8 lectures au plus** par demande, et seulement dans la **place qui reste** dans la fenêtre du modèle ; au-delà, c'est dit à l'IA (« plus de place ») et dans la réponse – rien n'est inventé. Un nom de fichier inconnu ou des lignes hors du fichier sont dits avec ce qui existe.
- Avec **Claude** et les **services compatibles OpenAI** qui acceptent les outils. Un modèle sans outils (« JSON dans le texte ») garde le comportement de la 1.20.0.

## 3. Les essais

**107 suites sur 107.** Celles qui changent :
- `aireseau` (234 vérifications) : une proposition de 80 opérations appliquée sur un fil de fond (« Application… » dessiné, le projet remplacé à la fin en une commande, un Ctrl+Z la reprend), un projet modifié pendant l'application (rien d'écrasé, dit), la Prévisualisation qui attend la vérification puis autorise Appliquer ; la lecture exacte sur le faux réseau, avec **Anthropic et compatible OpenAI** : l'outil offert, l'appel intercepté, le texte exact des lignes 5000 et 5001 renvoyé en résultat d'outil, la suite de la réponse, une seule fin, aucun appel d'outil visible ; la bibliothèque elle-même (sans casse, bornée à la fin du fichier, fichier inconnu, lignes hors du fichier, plus de place) ;
- `hmieditor` : 77 versions, 541 notes.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.20.1.exe` : l'installateur ;
- `API-Studio-1.20.1-portable.zip` et `IHM-Studio-1.20.1-portable.zip` : chaque application, sans installation ;
- ce document.

1. Lance `API-IHM-Studio-Setup-1.20.1.exe` : il met à jour la version installée au même endroit et garde tes données (le dossier `IA\`, tes conversations, ton apprentissage, tes clés API, les incidents).
2. Windows SmartScreen peut avertir (installateur non signé) : « Informations complémentaires », puis « Exécuter quand même ».

Le format des projets ne change pas (33).

Empreinte SHA-256 de `API-IHM-Studio-Setup-1.20.1.exe` (42,6 Mo, 44 651 551 octets) :
`93C63D91E26FB93C559CB6DCAB5A02296AD71A4838B68B8D07E4B63F72F17DB8`

Les zips portables (167 fichiers chacun) :
- `API-Studio-1.20.1-portable.zip` (33,0 Mo) :
  `18D221DB77F65E88BB067D6C726B81B1CC1A22AE948F68C689695E5EE96F1C8C`
- `IHM-Studio-1.20.1-portable.zip` (33,0 Mo) :
  `BDDAC2904D1F6BC6EAE07906BEA8A7FBE5A4D53613140271B73C61251827670A`

### Les vérifications sous Windows (Wine)

**Les deux exe Windows** sont compilés sous Linux avec MinGW-w64 et lancés sous Wine : `--version` répond `API Studio 1.20.1` et `IHM Studio 1.20.1` ; `API-Studio.exe --cli MAST.XPG` analyse le projet d'essai comme avant (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB).

**L'installateur** (Inno Setup 6.4.1 sous Wine) a été lancé en silencieux, pour tous les comptes, par-dessus la 1.20.0 livrée, avec **une clé API d'essai** déposée dans `%APPDATA%\XpgAnalyzer\IA\cles` : « Installation process succeeded ».
- La clé est **intacte** après l'installation (même empreinte avant et après) ; le journal dit « Clés API : 1 gardée (protégée par Windows ; la réinstallation n'y touche pas) ».
- Les deux exe et `pdfium.dll` installés sont identiques à ceux qui ont été essayés ; les associations `.xihm` et `.xapi` sont là ; la copie de secours de la 1.20.0 est faite.

Ces vérifications sont faites sous Wine, pas sur un vrai Windows.

## 5. Les fichiers modifiés

- `AiAssistant` (l'application de fond : la copie, l'avancement, la pose en une commande, le projet changé entre-temps) ;
- `AiOperations` (`appliedCopy`, `commandSetting`, `applyStaged` : appliquer sans revalider deux fois) ;
- `AiPanel` (la carte « Application… », ses boutons grisés, le clic qui reprend la vérification) ;
- `AiPreviewDialog` (la fenêtre qui attend la vérification de fond) ;
- `AiTextParts` (la bibliothèque des pièces jointes, l'outil `lire_piece_jointe`, la lecture bornée), `AiAnthropic` et `AiOpenAI` (l'appel intercepté et la suite de la demande) ;
- la version, les notes, l'aide ;
- les essais `ai_reseau_test`, `hmi_editor_test`.

## 6. Les limites

- Une proposition de **moins de 40 opérations** s'applique encore sur le fil de l'interface : c'est immédiat (quelques millisecondes), il n'y a rien à attendre.
- Si l'automate relié ne donne pas sa liste de noms, la vérification de fond ne démarre pas (elle se fait alors comme avant, au premier plan).
- La lecture exacte dépend de l'IA : c'est elle qui décide de lire. Elle est bornée (8 lectures, la place qui reste), et ne marche qu'avec les modèles qui acceptent les outils.
- Rien n'a été essayé sur un vrai Windows (seulement sous Wine, en silencieux) ; pas de captures, à ta demande.

## 7. Ce qui a été optimisé pour livrer plus tôt

- La compilation Windows et la vérification de l'installateur ont tourné **en fond**, pendant que je lisais le code de tes deux demandes suivantes (la conversation, les incidents).
- Rien de recréé : la vérification de fond de la carte (1.20.0) est réutilisée par Appliquer et par la Prévisualisation ; la boucle des appels d'outil des fournisseurs sert à la lecture exacte.
- Pas de captures ; l'installateur vérifié en silencieux sous Wine ; une compilation Windows incrémentale.
