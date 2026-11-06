# API Studio et IHM Studio 1.31.0 : la synchro GitHub privée des licences

Faite à partir de ta demande du 06/11 : « oui, ajoute la synchro GitHub privée dans la 1.31 ».

Tout est dans **l'outil de l'éditeur** (`XPG-License-Admin.exe`, livré à part, pour toi seul). Les Studios ne changent pas, à part leur version et une note : l'activation chez les clients reste par fichier, vérifiée hors ligne.

## 0. En bref

- Ton **dépôt GitHub privé** devient la source centrale de ce que tu signes : le registre, la liste de révocation, chaque licence et le registre des activations.
- Ce qui nomme un client ou un PC (les licences, le registre des activations) y est **chiffré**. Ni GitHub ni quelqu'un qui volerait le jeton ne peut lire ces noms.
- Un envoi = **un commit**. Un autre PC Admin, ou ton PC après une panne, retrouve tout grâce au **code de secours**.
- **N'y vont jamais** : ta clé de l'éditeur, le jeton GitHub, le code de secours.
- **Pas encore essayé contre le vrai GitHub** : aucun jeton ici, et aucun ne doit l'être. Ta première synchro sera la première réelle. `github-etat` la prépare sans rien écrire (§ 6).

## 1. Ce que tu fais une fois (sur ton PC)

Il faut d'abord ta clé de l'éditeur (`creer-cle`, § 0 de la livraison 1.30.0). Ensuite, le pas à pas complet est dans `LISEZ-MOI-EDITEUR.txt` (zip de l'éditeur), partie « La source centrale » :

1. Sur github.com, crée un dépôt **privé**, réservé aux licences (par exemple `licences-xpg`). L'outil refuse un dépôt public.
2. Crée un **jeton à grain fin** qui ne voit **que** ce dépôt :
   - chemin : Settings › Developer settings › Personal access tokens › Fine-grained tokens ;
   - permission : **Contents : Read and write**, rien d'autre ;
   - date d'expiration : un an, par exemple.
3. `XPG-License-Admin.exe github-config --depot TON-COMPTE/licences-xpg` : il crée la **clé de synchro** et affiche son **code de secours** (`XPGS1-…`). **Note-le hors de ton PC** (gestionnaire de mots de passe, ou papier au coffre).
4. `XPG-License-Admin.exe github-jeton` : colle le jeton (il ne s'affiche pas), puis Entrée. Il est protégé pour ton compte Windows (DPAPI).
5. `XPG-License-Admin.exe archiver FICHIER` pour chaque fichier signé avant cette version (le registre, les licences).
6. `XPG-License-Admin.exe github-etat`, puis `github-synchroniser`.

## 2. Au quotidien

- Ajoute `--publier` à `signer-licence`, `signer-revocations` ou `enregistrer-demande`. Le fichier est rangé dans l'**archive** de l'outil et publié dans la foulée. Sans `--publier`, l'outil te rappelle `github-synchroniser`.
- **Un autre PC Admin**, ou ton PC réinstallé :
  1. `github-config --depot … --code-secours XPGS1-…` (ajoute `--remplacer` si ce PC a déjà sa propre clé de synchro : l'ancienne est gardée à côté) ;
  2. `github-jeton` ;
  3. `github-synchroniser --recevoir-seulement`.
  
  `liste` marche aussitôt. Pour **signer** sur ce PC, il lui faut aussi ta clé de l'éditeur (ta copie de secours).
- **Une licence perdue par un client** : elle est dans l'archive (et dans le dépôt). Tu la lui renvoies à l'identique.

## 3. Ce qui est dans le dépôt

| Fichier | Contenu |
|---|---|
| `registre/registre.xpgreg` | signé, **en clair** : aucun client n'y est nommé |
| `licences/<ID>.xpglic.chiffre` | chaque licence, **chiffrée** (XChaCha20-Poly1305, libsodium) |
| `revocations/revocations.xpgrev.chiffre` | la liste de révocation, **chiffrée** (sa raison peut nommer un client) |
| `activations/activations.jsonl.chiffre` | le registre des activations, **chiffré** |
| `index.xpgsync` | la liste des fichiers et leurs empreintes, **authentifiée** : un fichier changé hors de l'outil est refusé |
| `LISEZ-MOI.md` | ce qu'est ce dépôt (écrit une fois) |

Ce qui reste visible dans ton dépôt privé :
- les identifiants de licence (`XPG-2026-00042`), les numéros et les dates : **ne mets pas le nom du client dans l'identifiant** ;
- les messages de commit : des comptes et des identifiants, **jamais un nom de client**.

## 4. Quand deux PC publient

L'outil lit le dépôt, prend ce qui est plus récent, puis publie le reste. Les règles :
- le registre et la liste de révocation : **le plus grand numéro** gagne ;
- une licence : **la plus récente** gagne ;
- le registre des activations : **les lignes des deux côtés** sont gardées, sans perte ni doublon (le fichier n'est jamais réécrit : on ajoute à la fin) ;
- deux versions différentes d'une même date : ton PC garde la sienne, et le dit ;
- si un autre PC a publié pendant ton envoi : la branche refuse, l'outil relit, fusionne et recommence ;
- un fichier modifié sur GitHub à la main est refusé ; un PC qui a la bonne version le republie ;
- un fichier signé par une clé d'éditeur que ton PC ne connaît pas : pas reçu, et jamais remplacé par une copie plus ancienne ;
- une branche protégée : l'outil le dit, avec le message de GitHub.

`signer-* --publier` rend le code **3** si le fichier est signé et rangé mais que la publication a échoué : ne signe pas de nouveau, relance `github-synchroniser`.

## 5. Les essais

- **Linux** : 145 suites sur 145 réussies (`ctest`), dont une nouvelle, `githubsync` (73 contrôles). Elle n'utilise pas le réseau : un faux GitHub tourne dans le processus. Ce qu'elle couvre :
  - **deux postes Admin** : le premier publie dans un dépôt vide, le second reçoit par le code de secours, signe à son tour et publie, le premier reçoit ; ensuite, plus aucun commit d'un côté ni de l'autre ;
  - **dans le dépôt** : aucun nom de client ni de PC en clair, ni la raison d'une révocation ; le jeton n'apparaît dans aucun fichier ni aucune requête ;
  - **un envoi concurrent** : l'outil relit, fusionne et recommence ;
  - **les refus** : un mauvais jeton, un dépôt public, une autre clé de synchro (puis `--remplacer`), une branche protégée, une licence chiffrée modifiée à la main (refusée, puis réparée par un poste qui a la bonne), un index modifié (tout arrêté) ;
  - **un poste d'une autre clé d'éditeur** : il n'écrase rien et ne crée pas d'aller-retour.
- **L'outil réel, en console**, contre `outils/licences/faux_github.py`, un faux GitHub local qui calcule les vrais identifiants Git et simule un envoi concurrent :
  - le même parcours de bout en bout : 14 commandes réussies, et les 2 refus attendus (« pas configurée », « pas de jeton ») ;
  - sous Linux, par le transport libcurl ;
  - sous **Wine**, avec l'exe Windows livré : WinHTTP pour le réseau, et DPAPI pour le jeton et la clé de synchro ;
  - le journal de l'essai sous Wine est joint (`essai-outil-wine.txt`, codes de secours masqués).
- **Une relecture indépendante du code** a trouvé six problèmes ; ils sont corrigés et couverts par les essais :
  - un aller-retour de commits entre deux postes de clés d'éditeur différentes ;
  - une branche protégée prise pour un envoi concurrent ;
  - ce qui avait été reçu disparaissait du rapport si l'envoi échouait ensuite ;
  - la liste de révocation partait en clair, alors que sa raison peut nommer un client ;
  - le premier commit sur un dépôt vide pouvait échouer avec GitHub ;
  - un nouveau poste pouvait se retrouver bloqué avec sa propre clé de synchro.
- **L'installation par-dessus la 1.30.0, sous Wine** :
  - l'installation passe, ta clé d'API est gardée ;
  - les programmes installés sont ceux construits ; versions 1.31.0, agent 1.1.0 ;
  - `--cli`, le bac à sable, les associations de fichiers et les sauvegardes marchent ;
  - rien de l'outil de l'éditeur n'est installé, et les programmes livrés ne contiennent ni la clé d'essai ni la marque de l'outil.

## 6. Installer

Comme d'habitude : lance `API-IHM-Studio-Setup-1.31.0.exe` (dans le zip) par-dessus la 1.30.0. Tes projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.31.0.exe` | `B33A5388B478DA7B80515345890D1C4475AA2E27E354A7721050AB96BFDF9B89` | 54 698 570 octets |
| `API-Studio-1.31.0-portable.zip` | `47360101EEDDCE12C3C57B7068B876BCF4F7B555693A42A7DCCCCC7934B42537` | 190 fichiers |
| `IHM-Studio-1.31.0-portable.zip` | `ECDC4C10FA1E2A66E9D7FB47A782B4BA9D675FAE170B58173638195278DDB175` | 191 fichiers |
| `XPG-License-Admin-1.31.0-EDITEUR.zip` (**à part, pour toi seul**) | `67F3BA21C88432DE1707FADB836E20DCEB852B4FA013A48EE5013A5246AAE906` | 1 261 223 octets |

**L'outil de l'éditeur** remplace celui de la 1.30.0. Mets le nouveau `XPG-License-Admin.exe` à la place de l'ancien : ta clé et ton registre des activations ne bougent pas (`%LOCALAPPDATA%\XPG\LicenseAdmin`). Ne le mets jamais avec l'installateur.

## 7. Limites

- **Pas essayé contre le vrai GitHub.** Le faux GitHub suit l'API documentée (Git Data et Contents) et calcule les vrais identifiants Git. Si GitHub refuse quelque chose à ta première synchro, l'outil dit pourquoi : jeton refusé, droits manquants, dépôt introuvable ou public, branche absente. Envoie-moi son message.
- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine (WinHTTP et DPAPI de Wine).
- **Les clients ne lisent pas le dépôt.** Un dépôt privé ne se lit pas sans jeton, et aucun jeton n'est livré. Une révocation arrive toujours chez un client par un fichier importé. La révocation rapide, l'activation en ligne et voir chaque lancement demandent le service de licences en ligne (pas fait).
- **Le code de secours** est le seul moyen de relire les licences et le registre des activations du dépôt sur un autre PC. Ta clé de l'éditeur, elle, reste liée à ton compte Windows (DPAPI) : garde une image complète de ton PC.
- **`outils/lib/package.ps1`** (la construction sous Windows) n'a pas pu être exécuté ici : il n'y a pas de PowerShell.
