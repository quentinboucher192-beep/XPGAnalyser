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
  1. `github-config --depot … --code-secours XPGS1-…` ;
  2. `github-jeton` ;
  3. `github-synchroniser --recevoir-seulement`.
  
  `liste` marche aussitôt. Pour **signer** sur ce PC, il lui faut aussi ta clé de l'éditeur (ta copie de secours).
- **Une licence perdue par un client** : elle est dans l'archive (et dans le dépôt). Tu la lui renvoies à l'identique.

## 3. Ce qui est dans le dépôt

| Fichier | Contenu |
|---|---|
| `registre/registre.xpgreg`, `revocations/revocations.xpgrev` | signés, **en clair** : aucun client n'y est nommé |
| `licences/<ID>.xpglic.chiffre` | chaque licence, **chiffrée** (XChaCha20-Poly1305, libsodium) |
| `activations/activations.jsonl.chiffre` | le registre des activations, **chiffré** |
| `index.xpgsync` | la liste des fichiers et leurs empreintes, **authentifiée** : un fichier changé hors de l'outil est refusé |
| `LISEZ-MOI.md` | ce qu'est ce dépôt (écrit une fois) |

Ce qui reste visible dans ton dépôt privé :
- les identifiants de licence (`XPG-2026-00042`), les numéros et les dates ;
- les messages de commit : des comptes et des identifiants, **jamais un nom de client**.

## 4. Quand deux PC publient

L'outil lit le dépôt, prend ce qui est plus récent, puis publie le reste. Les règles :
- le registre et la liste de révocation : **le plus grand numéro** gagne ;
- une licence : **la plus récente** gagne ;
- le registre des activations : **les lignes des deux côtés** sont gardées, sans perte ni doublon (le fichier n'est jamais réécrit : on ajoute à la fin) ;
- deux versions différentes d'une même date : ton PC garde la sienne, et le dit ;
- si un autre PC a publié pendant ton envoi : la branche refuse, l'outil relit, fusionne et recommence ;
- un fichier modifié sur GitHub à la main est refusé ; un PC qui a la bonne version le republie.

## 5. Les essais

@ESSAIS@

## 6. Installer

@INSTALLER@

## 7. Limites

- **Pas essayé contre le vrai GitHub.** Le faux GitHub suit l'API documentée (Git Data et Contents) et calcule les vrais identifiants Git. Si GitHub refuse quelque chose à ta première synchro, l'outil dit pourquoi : jeton refusé, droits manquants, dépôt introuvable ou public, branche absente. Envoie-moi son message.
- **Rien n'a été essayé sur un vrai Windows** : seulement sous Linux et sous Wine (WinHTTP et DPAPI de Wine).
- **Les clients ne lisent pas le dépôt.** Un dépôt privé ne se lit pas sans jeton, et aucun jeton n'est livré. Une révocation arrive toujours chez un client par un fichier importé. La révocation rapide, l'activation en ligne et voir chaque lancement demandent le service de licences en ligne (pas fait).
- **Le code de secours** est le seul moyen de relire les licences et le registre des activations du dépôt sur un autre PC. Ta clé de l'éditeur, elle, reste liée à ton compte Windows (DPAPI) : garde une image complète de ton PC.
- **`outils/lib/package.ps1`** (la construction sous Windows) n'a pas pu être exécuté ici : il n'y a pas de PowerShell.
