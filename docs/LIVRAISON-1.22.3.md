# API Studio et IHM Studio 1.22.3 : le .XPG joint lu aussi pour les missions

Correctif de la 1.22.2, fait à partir de ta capture du 01/11 : « Je ne peux pas lire `programme.XPG` ». Il se met à jour par-dessus la 1.22.2 et garde tes données.

## 1. Ce qui était faux

Un export de Control Expert joint (`.XPG`) est lu par le logiciel comme « l'automate de la séance » : ses variables globales et ses types (DDT) vont dans ce que l'IA reçoit. Il y avait deux trous.

- **Le lancement d'une mission ne lisait pas le fichier.** Seul l'envoi d'un message ordinaire le faisait. Joint au moment de créer la mission, le `.XPG` partait avec la demande sans être lu : l'IA n'en voyait que le nom, disait ne pas pouvoir le lire et proposait d'attendre ta réponse.
- **La liste des fichiers joints envoyée à l'IA présentait le `.XPG` comme « pas encore analysé ».** Or ses consignes lui disent qu'un fichier non lu « n'a pas été lu : dis-le ». Même avec l'automate dans son contexte, elle pouvait donc répondre qu'elle ne pouvait pas le lire.

Le même défaut existait aussi après une relance du logiciel : l'automate lu n'est gardé que le temps de la séance, et reprendre la conversation le lendemain ne le relisait pas.

## 2. Ce qui change

- **Avant chaque appel à l'IA** (un message, la création d'une mission, son plan, une proposition, une correction, une actualisation), le `.XPG` le plus récent de la conversation est lu comme l'automate de la séance, s'il ne l'est pas déjà. À défaut, c'est celui des pièces de la mission. La barre de l'Assistant le dit : « J'ai lu l'automate de programme.XPG : N variables globales et M types (DDT) ». Un fichier déjà lu n'est pas relu.
- **La liste des fichiers joints le dit lu** : « export de l'automate LU par le logiciel : ses variables globales et ses types (DDT) sont plus bas ; sers-t'en, ne dis pas que tu ne peux pas le lire ».
- Sur ta capture, l'IA aurait ainsi eu les variables et les DDT de `programme.XPG` pour planifier la mission sur ton projet Test.

## 3. Les essais

Sous Linux : **122 suites d'essais sur 122** réussies (CTest).

`ai_missions_e2e_test` gagne un essai qui refait ton cas. Résultat : 245 vérifications (237 en 1.22.2).
1. Le `programme.XPG` est joint, puis la mission est créée.
2. L'automate est lu avant l'appel, et la liste des fichiers envoyée à l'IA le dit « LU ».
3. Au message suivant, il n'est pas relu.
4. Après une relance simulée, il est relu avant l'appel.

## 4. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.22.3.exe` : l'installateur (47 285 137 octets). Il met à jour la 1.22.2 au même endroit ;
- `API-Studio-1.22.3-portable.zip` et `IHM-Studio-1.22.3-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.22.3.exe` | `840943B3D1559AF74E58BFAFE361E9CC2642504B9475520525C4108D8E146479` |
| `API-Studio-1.22.3-portable.zip` | `68AB6910D4CA8F56808223EE0C3BB79ACFE800691BE0AB8717A3EBE37FB235B4` |
| `IHM-Studio-1.22.3-portable.zip` | `47C6B39E27DC8E122F48479C94B4D9B326ACA5870CE62235BBA0A9A606CCF037` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.22.2 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.22.3 » et « IHM Studio 1.22.3 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.22.2 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 5. Limites

- **Le contenu brut du `.XPG` n'est pas envoyé à l'IA** : elle reçoit les variables globales et les DDT lus par le logiciel, dans la limite de taille du contexte (le reste est compté). Le code des sections et des POU n'est pas envoyé.
- **Un seul automate par séance** : le `.XPG` le plus récent de la conversation.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows**, et jamais avec une vraie IA. L'essai simule la lecture du fichier ; le vrai lecteur de `.XPG` est celui de la 1.16.0, déjà vérifié sur le projet d'essai.
- **Pas de captures d'écran**, selon ta consigne permanente.
