# XPGAnalyser 1.11.12 — le plantage du clic sur Fonctions ou Popups d'un symbole

Livrée le 08/10/2026. Elle corrige vos deux rapports de plantage du 05/10, à 22 h 59 et 23 h 00.

| Votre demande | Où |
|---|---|
| « fait un fix stp » (deux rapports : `EXCEPTION_ACCESS_VIOLATION` dans `msvcrt.dll strlen`, lecture de `0xFFFFFFFFFFFFFFFF`) | un clic dans l'arbre sur **Fonctions** ou **Popups** d'un symbole |

Fichiers livrés : `XPGAnalyser-Setup-1.11.12.exe` (l'installateur), ce document et 3 captures.

## 1. Ce qui plantait

Les deux rapports ont la même pile d'appels :
1. un clic dans l'arbre (`TreeView`, puis `onTreeSelection`) ;
2. l'ouverture du nœud (`openHmiNode`, puis `openHmiView`) ;
3. un texte construit depuis un pointeur invalide (`strlen`).

En ouvrant une partie de vue, `openHmiView` met une phrase d'aide dans la barre d'état. Il la lisait dans une table de **5** phrases : Objets, Scripts, Animations, Calques, Groupes. La 1.11.10 a ajouté **Fonctions** et **Popups** sous un symbole, sans compléter la table. Un clic sur l'une de ces deux parties lisait donc la case 5 ou 6, hors de la table. Dans l'exe 1.11.11, la case 5 contient du texte et non une adresse : Windows signale cette lecture comme une lecture de `0xFFFFFFFFFFFFFFFF`, d'où le plantage.

Le plantage a été reproduit sous Wine avec l'exe 1.11.11, sur un clic sur `IHM/Symboles/S_Vanne/Fonctions` : même pile, cadre par cadre.

## 2. La correction

- Chaque partie de l'arbre a sa phrase, écrite dans un `switch` : le compilateur signale désormais une partie oubliée.
- Le clic sur **Fonctions** ouvre le sous-onglet Fonctions du symbole, et le clic sur **Popups** son sous-onglet Popups.

Captures :
- `11112_01_clic_fonctions.png` : clic sur Fonctions, le sous-onglet « Fonctions (4) » s'ouvre et la barre d'état le dit ;
- `11112_02_clic_popups.png` : clic sur Popups, le sous-onglet « Popups (1) » s'ouvre.

## 3. Tout l'arbre, nœud par nœud

Pour chercher d'autres plantages du même genre, les sessions de test ont une nouvelle commande, `arbre-parcourir`. Pour chaque nœud d'un sous-arbre, elle :
1. construit son menu du clic droit ;
2. calcule sa carte (le survol) ;
3. le clique.

Elle a parcouru tout l'arbre **IHM** d'Armoire_Gaz, soit 19 009 nœuds (vues, objets, scripts, fonctions et popups des symboles, variables d'instances…), puis le dossier **Simulation**, soit 9 nœuds : aucun plantage.

Capture `11112_03_apres_parcours_19009_noeuds.png` : l'application tourne toujours après le parcours, avec 107 onglets ouverts.

## Installer

1. Lancer `XPGAnalyser-Setup-1.11.12.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisir « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.12.exe` (14,7 Mo, 15 434 503 octets) :
`7C8557BAF1036CF4AE16BB98F45E4DDD414D8F2282317CD5B65DE6D6AC170E3E`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (55 sur 55). Le nouveau contrôle :

| Test | Ce qu'il vérifie |
|---|---|
| `plantagePartiesSymbole11112` (hmi_editor_test) | chaque partie, d'Objets à Popups, a sa phrase, et aucune n'est lue au-delà ; les 7 parties d'un symbole dans l'arbre ; le sous-onglet Fonctions, puis Popups ; tout l'arbre IHM se lit sans faute |

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.12` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session `tools/sessions/session-11112-plantage-arbre.txt` a été rejouée sur une copie d'Armoire_Gaz : ce sont les captures et le parcours des 19 009 nœuds.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé pour tous les comptes, par-dessus la 1.11.11 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.12`.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Merci de refaire chez vous ce qui plantait : un clic sur Fonctions ou Popups d'un symbole dans l'arbre.

## La suite

Votre demande sur la Simulation IHM (génération incrémentale, compilation, diagnostics, Sorties et Console, `IHM_LOG`, rémanences) est en cours. La maquette interactive arrive tout de suite, et le code suit dans les prochaines versions, par lots testables.
