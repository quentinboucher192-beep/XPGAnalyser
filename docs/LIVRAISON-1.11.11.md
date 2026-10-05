# XPGAnalyser 1.11.11 — les fonctions des symboles dans les expressions, et l'aide à la saisie

Livrée le 05/10/2026 au soir. Elle corrige votre capture :

| Votre demande | Où |
|---|---|
| « correction, les fonctions des symboles sont dispos partout, même dans les expressions dans le symbole et hors du symbole » — `=GetActiveCount(30)` était refusée (« fonction inconnue : GetActiveCount ; une expression n'appelle que les fonctions standard… ») | les cases fx et les textes à trous, dans le symbole et hors du symbole |
| « les ajouter dans les aides à la saisie » | l'aide à la saisie des cases fx |

Fichiers livrés : `XPGAnalyser-Setup-1.11.11.exe` (l'installateur), ce document et 5 captures.

## 1. Dans le symbole : par leur nom

Une case fx d'un objet du symbole (ou d'une de ses popups) appelle les fonctions du symbole par leur nom : `=GetActiveCount(30)`, `=Etat() * 10`, ou `{Etat()}` dans un texte à trous.

**Le contrôle de la case** (la pastille fx, Compiler, Générer) les connaît maintenant :

| Ce qu'on écrit | Ce qu'il dit |
|---|---|
| `=GetActiveCount(30)` | rien : c'est juste |
| `=Raz()` (une fonction sans retour) | « Raz ne rend pas de valeur : une expression ne peut appeler qu'une fonction qui rend quelque chose » |
| `=GetActiveCount()` | « GetActiveCount prend 1 argument, pas 0 » |
| `=GetActiveCont(30)` | « fonction inconnue : GetActiveCont (veux-tu dire GetActiveCount ?) » |

En marche, chaque instance appelle **sa** fonction : `GetActiveCount(30)` vaut 30 pour Vanne_3, à 0 %, et 130 pour Vanne_4, à 100 %.

Captures :
- `11111_01_symbole_getactivecount.png` : `=GetActiveCount(30)` dans Txt_Score, sans faute ;
- `11111_02_aide_dans_le_symbole.png` : l'aide propose « GetActiveCount · fonction de S_Vanne · INT ».

## 2. Hors du symbole : par l'instance

| Où | Comment l'écrire |
|---|---|
| dans la vue qui pose l'instance | `=Vanne_4.GetActiveCount(30)`, `{Vanne_4.Etat()}` |
| partout ailleurs (une autre vue, une popup) | `=Vue_Vannes.Vanne_4.GetActiveCount(30)` |

Sans l'instance devant, `GetActiveCount(30)` reste inconnue dans la vue : la fonction est celle d'une instance.

**L'aide à la saisie** des cases fx propose, après `Vanne_3.` ou `Vue_Vannes.Vanne_3.`, les fonctions de l'instance, avec leur retour, « virtuelle » ou « redéfinie ici », puis ses variables publiques.

Cela vaut aussi dans une vue ou un symbole **sans paramètre**. Avant, l'aide ne proposait rien dans ce cas, même dans le code.

Captures :
- `11111_03_vue_instance_fonction.png` : `=Vanne_4.GetActiveCount(30)` dans Txt_Compte, sans faute ;
- `11111_04_aide_apres_instance.png` : après `Vanne_3.`, la liste Etat, Fermer, GetActiveCount, Ouvrir (redéfinie ici) ;
- `11111_05_en_marche.png` : en marche, Txt_Score vaut 30 (Vanne_3) et 130 (Vanne_4), et Txt_Compte vaut 130.

## Installer

1. Lancer `XPGAnalyser-Setup-1.11.11.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisir « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.11.exe` (14,7 Mo, 15 433 293 octets) :
`E5D3EF46241B4E74A42D24318795BB5870DCA501A80B9A74BB4996658AEF83FB`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (55 sur 55). Le nouveau contrôle :

| Test | Ce qu'il vérifie |
|---|---|
| `fonctionsExpressions11111` (hmi_editor_test) | dans le symbole : `GetActiveCount(30)` et `GetActiveCount(2) > 1` passent ; une fonction sans retour, un argument manquant, une faute de frappe sont dits ; dans la vue : `S1.GetActiveCount(30) + 1` et `Vue_S.S1.GetActiveCount(2) > 0` passent, `GetActiveCount(30)` seule reste inconnue ; l'aide à la saisie dans un champ du symbole, puis après `S1.` et `Vue_S.S1.` dans une vue sans paramètre ; en marche, `Vue_S.S1.GetActiveCount(30)` = 42 |

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.11` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session `session-11111-fonctions-expressions.txt` a été rejouée sur une copie d'Armoire_Gaz : ce sont les 5 captures.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé en silencieux, pour tous les comptes, par-dessus la 1.11.10 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.11`.
- Seuls les raccourcis du menu Démarrer dont le nom a un accent n'ont pas pu être créés. C'est la limite de Wine déjà vue aux livraisons précédentes, sans rapport avec Windows.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Merci de vérifier sur votre symbole : `=GetActiveCount(30)` ne doit plus être rouge, et l'aide doit la proposer.

Les sources sont dans le dépôt GitHub, branche `claude/hopeful-goodall-h0srq0`, avec la session de captures `tools/sessions/session-11111-fonctions-expressions.txt` et son projet (`tools/sessions/preparer-projet-11111.cpp`).
