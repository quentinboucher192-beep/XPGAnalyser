# XPGAnalyser 1.11.8 — Variables IHM : un membre interne ou attribué à l'équipement, Recalculer la place mémoire

Livrée le 05/10/2026. Elle reprend vos demandes sur Variables IHM :

| Votre demande | Où |
|---|---|
| « dans Variables IHM, je veux pouvoir modifier les adresses %MW %M etc même si je suis dans une structure » | la case Adresse d'un membre, et maintenant d'un membre composé (V[2]) |
| « je veux pouvoir désactiver certains membres de la liaison de comm » ; « la ligne sélectionnée deviendrait une variable interne ; chaque membre peut choisir s'il est interne ou attribué à l'équipement de la structure » | clic droit sur la ligne ; la ligne Liaison de l'inspecteur |
| « un bouton recalculer la place mémoire quand on est sur une structure, après avoir décoché ce qu'on veut avoir en interne » | la barre de Variables IHM ; le clic droit ; l'inspecteur |

Les livraisons suivantes :

| Version | Contenu |
|---|---|
| 1.11.9 | les actions : le script dans une petite fenêtre, le clavier virtuel, Maths, les actions rangées en arbre |
| 1.11.10 | les popups et les fonctions propres à un symbole |

Fichiers livrés : `XPGAnalyser-Setup-1.11.8.exe` (l'installateur), ce document et 7 captures.

Les exemples reprennent votre capture : `V : ARRAY[0..63] OF Vanne`, liée à l'Esclave virtuel 1 à partir de %MW17. Une Vanne compte POSITION (INT), OUV (BOOL), NOM (STRING, 16 mots), CMD_OUV et CMD_FERM (BOOL), soit 19 mots par vanne : V occupe les mots 17 à 1232.

## 1. Un membre interne, ou attribué à l'équipement

Chaque membre d'une structure ou d'un tableau lié choisit :

| | Attribué à l'équipement (comme avant) | Interne |
|---|---|---|
| Sa valeur | lue et écrite dans l'équipement | gardée dans l'IHM, comme une variable IHM |
| Le plan Modbus | une case | pas de case (rien n'est lu ni écrit) |
| Variables IHM | `↳ V`, son adresse | `interne (IHM)`, sans adresse, « dans l'IHM » |

**Comment faire :**
- **Clic droit sur la ligne** (une ou plusieurs lignes choisies) :
  - **Rendre interne (gardé dans l'IHM)** : cette case, `V[0].NOM` ;
  - **Rendre interne dans toutes les cases** : le même membre de toutes les vannes, `V[*].NOM` ;
  - **Attribuer à Esclave virtuel 1** : le rend à l'équipement ;
  - **Attribuer dans toutes les cases** : idem, pour toutes les vannes.
- **Ou la ligne « Liaison » de l'inspecteur**, avec trois choix : attribué à Esclave virtuel 1, interne (cette case), interne dans toutes les cases (V[*].NOM).

**Ce qui le montre :**
- un membre composé le dit (`↳ V · 1 interne`) ;
- l'inspecteur de la variable liste ses membres internes ;
- le Plan d'adressage (Équipements) montre « membre interne de V ».

**Le détail :**
- **Un seul Ctrl+Z** défait chaque choix.
- **Attribuer une seule case** d'un membre interne partout (V[3].NOM quand `V[*].NOM` est interne) défait le choix « partout » en ses autres cases. V[3].NOM est attribuée, les 63 autres restent internes.
- **En marche**, un membre interne vit dans l'IHM : un script l'écrit et le relit sans liaison, sans erreur. Il n'est pas dans l'esclave simulé.
- **Générer** signale un membre interne qui ne désigne plus rien (un membre renommé dans le type).

Captures :
- `1118_01_v0_nom_attribuee.png` : V[0].NOM, attribuée, à %MW19 ;
- `1118_02_clic_droit_rendre_interne.png` : le clic droit sur la ligne ;
- `1118_03_v0_nom_interne.png` : V[0].NOM interne ; V[0].CMD_OUV reste à %MW35.0 ;
- `1118_07_inspecteur_liaison.png` : la ligne Liaison et ses trois choix.

## 2. Recalculer la place mémoire

Rendre un membre interne **ne déplace rien** : sa place reste réservée. Ainsi, les adresses déjà utilisées par l'automate ou par l'équipement ne bougent pas toutes seules.

**« Recalculer la place mémoire »** rend les mots que n'occupent plus que des membres internes, et les membres suivants se resserrent. Le bouton est dans la barre de Variables IHM, quand une structure liée ou l'un de ses membres est choisi. On le trouve aussi au clic droit et à la ligne « Place des membres internes » de l'inspecteur.

| V : ARRAY[0..63] OF Vanne, à %MW17 | V[0].CMD_OUV | V[1] part de | La place |
|---|---|---|---|
| d'origine | %MW35.0 | %MW36 | mots 17 à 1232 |
| NOM interne partout, place réservée | %MW35.0 | %MW36 | mots 17 à 1232 |
| NOM interne partout, **place recalculée** | **%MW19.0** | **%MW20** | **mots 17 à 208** (3 mots par vanne) |

Le message le dit : « V : place recalculée - mots 17 à 208 (avant : mots 17 à 1232) ».

**Une fois recalculée**, la place suit les membres internes : un membre rendu interne ensuite rend aussi la sienne. « Place d'origine », au clic droit ou dans l'inspecteur, remet chaque membre à sa place, et Ctrl+Z aussi.

Captures :
- `1118_04_nom_interne_partout.png` : NOM interne dans toutes les vannes, place réservée ;
- `1118_05_place_recalculee.png` : la place recalculée, mots 17 à 208.

## 3. Les adresses dans une structure

L'adresse d'un membre simple se corrigeait déjà, dans la case Adresse de sa ligne ou dans l'inspecteur. Désormais, celle d'un **membre composé** se change aussi :
- c'est **son départ** : `V[2]` à %MW500, et ses cases le suivent (V[2].POSITION %MW500, V[2].OUV %MW501.0…) ;
- les autres vannes gardent leur place ;
- une case corrigée à la main garde son adresse, même sous un départ donné ;
- un crayon ✎ le signale.

| Ce qu'on tape | Ce qui se passe |
|---|---|
| un bit (%M100) pour le départ d'une structure de mots | refusé : le départ est un mot |
| l'adresse d'un membre interne | refusé : l'attribuer d'abord |

Capture : `1118_06_depart_v2.png`.

## Installer

1. Lancer `XPGAnalyser-Setup-1.11.8.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisir « Informations complémentaires », puis « Exécuter quand même ».

Les projets enregistrés par la 1.11.8 gardent leurs membres internes et la place recalculée (les clés `internes` et `place_recalculee` de la variable). Une version plus ancienne qui les relit les ignore : elle remet tous les membres sur l'équipement, à leur place d'origine.

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.8.exe` (14,6 Mo, 15 306 803 octets) :
`9F41C32025DF336E1F4D6677FD2E8EA4585C3AF3423AC00BFCA74CA8DBAEE592`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (55 sur 55). Les nouveaux contrôles :

| Test | Ce qu'il vérifie |
|---|---|
| `membresInternes1118` (hmi_test) | votre V à %MW17 : la place d'origine (mots 17 à 1232, 320 cases dans le plan) ; V[0].NOM interne (sa place réservée, 319 cases) ; recalculée (V[1] à %MW20) ; `[*].NOM` (256 cases, mots 17 à 208) ; le départ de V[2] à %MW500 et une case corrigée sous lui ; en marche, V[5].NOM écrite dans l'IHM ; enregistré puis relu ; Générer |
| `variablesInternes1118` (hmi_editor_test) | le clic droit sur V[0].NOM et ses entrées ; la ligne après le choix ; « dans toutes les cases » ; Recalculer et son message ; deux Ctrl+Z ; attribuer une seule case d'un motif ; la ligne Liaison ; le départ de V[2] et ses refus |

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.8` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session `session-1118-membres-internes.txt` a été rejouée sur une copie d'Armoire_Gaz, avec V comme dans votre capture : ce sont les 7 captures.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé en silencieux, pour tous les comptes, par-dessus la 1.11.7 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.8`.
- Seuls les raccourcis du menu Démarrer dont le nom a un accent n'ont pas pu être créés. C'est la limite de Wine déjà vue aux livraisons précédentes, sans rapport avec Windows.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Merci de vérifier sur votre projet : rendre NOM interne, puis recalculer la place, et voir si l'esclave lit encore ses vannes aux nouvelles adresses.

Les sources sont dans le dépôt GitHub, branche `claude/hopeful-goodall-h0srq0`, avec la session de captures `tools/sessions/session-1118-membres-internes.txt` et son projet (`tools/sessions/preparer-projet-1118.cpp`).
