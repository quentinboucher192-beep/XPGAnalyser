# XPGAnalyser 1.11.3 — livraison complète

Livrée le 05/10/2026, en deux temps sous le même numéro :

1. **Le correctif urgent** (déjà envoyé) : les paramètres d'une instance de symbole (les captures de STEST_1).
2. **La suite** (cet installateur) :
   - le clic sur le carré de légende et le sélecteur de valeur ;
   - la création d'une variable inconnue ;
   - l'export et l'import des scripts et des opérateurs.

Cet installateur contient les deux : il remplace celui de la première livraison.

Fichiers livrés :

| Fichier | Contenu |
|---|---|
| `XPGAnalyser-Setup-1.11.3.exe` | l'installateur Windows |
| `LIVRAISON-1.11.3.md` | ce document |
| `maquette-1.11.3.html` | la maquette interactive (s'ouvre dans un navigateur, sans installation) |

## 1. Le correctif urgent : les paramètres d'un symbole

### Le fx disparaissait après la saisie

- **Avant (1.11.2)** : `=UINTS` dans *Value · ARRAY[0..9] OF UINT* était réécrit `UINTS`. La pastille fx s'éteignait, et le filtre « $ Repères » changeait de compte.
- **Maintenant** : une formule reste une formule. La case garde `=UINTS`, la pastille fx reste pleine, et rouvrir la case montre `=UINTS`.

### « Voiture » n'était pas accepté comme constante

Sans fx, ce qu'on tape est une **constante, convertie dans le type du paramètre**, comme le texte d'un objet.

| Type du paramètre | On tape | Ce qui est gardé |
|---|---|---|
| STRING | `Voiture` | `'Voiture'` (affiché `Voiture`, sans apostrophes) |
| REAL | `1,5` | `1.5` |
| BOOL | `vrai`, `oui`, `1` | `TRUE` |
| UINT, INT… | `150` | `150`, vérifié dans les bornes (70000 est refusé pour un UINT) |
| TIME | `5s` | `T#5s` |
| Énumération IHM | `Auto` | `T_MODE#Auto` |
| ANY | n'importe quoi | tel quel, comme avant |

Deux règles complètent le tableau :
- **Un nom de variable reste la variable.** `UINTS`, ou une variable de l'automate, tapé sans fx, est lié à la variable.
- **Les anciennes saisies sont relues de la même façon.** `Name := Voiture`, ou les arguments positionnels `Voiture;50` de la 1.11.2, valent la constante `'Voiture'`, dans l'inspecteur comme en marche.

### Un tableau en paramètre

- `UINTS` (une variable IHM de type `ARRAY[0..9] OF UINT`) va à *Value · ARRAY[0..9] OF UINT* sans erreur.
- Une variable d'un autre type (`gCoef`, un REAL) est signalée en rouge sous la case : « gCoef est REAL ; ce champ attend ARRAY[0..9] OF UINT ». Les variables du bon type sont proposées.
- Une constante ne remplit pas un tableau. Le message le dit : « utilisez fx ».

## 2. Le carré de légende, et son clic

Au bout de chaque case de l'inspecteur, un carré dit d'où vient la valeur. Son infobulle donne le type, la source et l'erreur s'il y en a une.

| Carré | Signification |
|---|---|
| **C** | constante, convertie dans le type de la case |
| **fx** | formule : un calcul, un appel, plusieurs sources (des points sous la lettre montrent les zones lues) |
| **$** | formule ou texte avec des repères `$…$` |
| **A** | variable de l'automate (API) |
| **I** | variable de l'IHM |
| **S** | variable système (`SYS.`) |
| **V** | paramètre du symbole ou de la vue, variable publique, `THIS` |
| **!** | erreur : nom inconnu, conversion impossible, type qui ne convient pas, hors bornes |

**Un clic sur le carré ouvre la liste des carrés**, chacun avec son sens.
- En tête, la ligne de la case : ce qu'elle vaut maintenant, par exemple « Variable IHM : UINTS · ARRAY[0..9] OF UINT · Initiale 0 · Tableau de travail de l'IHM ».
- Le carré de la case est marqué « ✓ ici ».
- Si la case contient un nom inconnu, la liste propose d'abord « Créer « X »… ».
- Choisir un carré ouvre le sélecteur sur cette source : **A** l'automate, **I** l'IHM, **S** le système, **V** le symbole ou la vue, **C** une constante, **$** un repère.
- « Ouvrir le sélecteur… » l'ouvre sur toutes les sources.

La saisie directe dans la case ne change pas : le carré est une aide en plus.

## 3. Le sélecteur de valeur

Une fenêtre « Choisir la valeur de « Value » », avec le type attendu en haut à droite.

**L'arbre** contient tout ce que la case peut lire, comme l'explorateur d'objets et de variables :

| Groupe | Contenu |
|---|---|
| API (automate) | les variables globales, par nom ; les cases d'un tableau (16 au plus) ; les membres d'un DDT ou d'un FB |
| Variables IHM | les variables, leurs cases et leurs membres |
| Système | `SYS.` par domaine (heure, utilisateur…) |
| Symbole / vue | les paramètres du symbole, les variables publiques de la vue, les paramètres des instances |

Les filtres et la recherche :
- **Filtré d'office sur le type attendu.** Pour *Value*, on ne voit que les `ARRAY[0..9] OF UINT` : UINTS, AUTRES…
- La case « Type attendu » décochée, ou « Tout montrer », affiche tout : l'API, `SYS.`, les variables IHM…
- Les puces de source (Tout, A, I, S, V, C) restreignent l'arbre à une zone.
- La barre de recherche cherche dans le nom et le chemin (`Armoires[1].Pression`, `SYS.Time`…).

**Le détail** de la variable choisie donne son type, son adresse, sa valeur initiale et son commentaire. Quatre boutons agissent sur le résultat : *Remplacer le résultat*, *Insérer à la suite*, *Insérer comme repère*, *Convertir* (par exemple `INT_TO_STRING`).

**Le champ Résultat** reste modifiable : on peut y écrire une équation ou y intégrer un repère `$…$`.
- Le bouton fx choisit entre formule et constante.
- L'aide à la saisie y fonctionne comme dans les cases : noms, membres, fonctions.
- Le carré de légende du résultat se met à jour à chaque frappe, avec sa ligne d'explication.
- **Les erreurs s'affichent avec leurs corrections**, applicables d'un clic :
  - remplacer un nom mal tapé par le plus proche, par exemple « Remplacer par UINTS (IHM) » ;
  - prendre un mot comme texte ;
  - convertir le type ;
  - ramener une valeur dans les bornes ;
  - créer la variable qui manque.
- Un résultat en erreur ne ferme pas la fenêtre. « Valider quand même » reste possible.

Raccourcis : un double-clic sur un nœud le prend et valide ; Ctrl+Entrée valide.

**Valider** écrit l'expression dans la case cliquée au départ. Ctrl+Z l'annule.

## 4. Créer une variable qui n'existe pas

Un nom inconnu, validé dans le sélecteur ou choisi via « Créer « X »… » dans la liste des carrés, ouvre la fenêtre « Créer la variable « X » ». Elle demande :

| Champ | Contenu |
|---|---|
| Zone | **IHM** ou **API** (API quand un programme d'automate est ouvert) |
| Type | celui de la case est proposé en premier ; la liste suit la zone (types de base et types IHM, ou types de l'automate) |
| Valeur initiale | pour une variable IHM |
| Adresse | pour une variable API, facultative (`%MW…`) |
| Commentaire | — |

La variable est créée, puis la case est écrite : **un seul Ctrl+Z annule les deux**.

## 5. Exporter et importer les scripts

**Où :**
- **Les scripts d'une vue** : dans le volet *Scripts* de chaque vue, popup, symbole, vue modèle, en-tête modèle et pied de page modèle, les boutons **Exporter…** et **Importer…** sont dans la barre d'outils, à côté de *Vider*.
- **Les opérateurs** : dans l'onglet *Opérateurs* d'un symbole et dans le volet *Opérateurs* d'un type IHM, les mêmes boutons.

**Le fichier** porte l'extension `.xpgst`.
- C'est du texte ST lisible et modifiable dans n'importe quel éditeur.
- Un bloc de commentaire précède chaque script et dit ce qu'il est.

```
(* XPGAnalyser 1.11.3 - scripts de la vue Popup_vanne (popup) *)
(*# xpgst format=1 genre=scripts-vue source="Popup_vanne" role=popup *)
(*# script evenement=OnOpen langage=ST *)
(* A l'ouverture : le titre *)
gNom := 'Vanne V1';
(*# script evenement=OnCycle langage=ST *)
gAlarme := UINTS[0] > 100;
(*# fin *)
```

Un opérateur est précédé de sa signature :

```
(*# operateur op="+" gauche=T_VECTEUR droite=REAL resultat=T_VECTEUR description="..." *)
```

**Importer compare avant d'agir.**
- La fenêtre d'import liste chaque script du fichier comme *neuf*, *identique* ou *différent* ; on coche ceux qu'on prend.
- On choisit ensuite **Remplacer** le script existant, ou **Ajouter à la suite**.
- Les opérateurs sont comparés par leur signature (op, gauche, droite).
  - Un opérateur différent remplace l'existant.
  - Un opérateur neuf que le type refuse est écarté, et la raison est dite.
- Tout l'import se fait en une seule modification : **un Ctrl+Z l'annule**.
- Un fichier `.st` ou `.txt` sans bloc `(*# … *)` est importé comme un seul script, dans l'événement choisi.

## 6. La maquette interactive

`maquette-1.11.3.html` s'ouvre dans un navigateur et rejoue :
- l'inspecteur de STEST_1 avec ses carrés ;
- la liste des carrés ;
- le sélecteur : filtre, recherche, résultat, erreurs et corrections ;
- la création d'une variable ;
- l'export et l'import des scripts.

C'est une maquette : le logiciel livré fait foi.

## Installer

1. Lancer `XPGAnalyser-Setup-1.11.3.exe`. Il met à jour une 1.11.2 ou une première 1.11.3 installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisir « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.3.exe` (15,2 Mo, 15 182 763 octets) :
`A2A13E5A6690FB85DEB026C057D05DC1805F8FD0989909CA0026C9EFD52BB7EB`

> L'installateur du correctif urgent (empreinte `167472F8…D2CE9`) est remplacé par celui-ci.

## Comment cette version a été fabriquée

- **L'exe** : compilé sous Linux avec MinGW-w64 (GCC 13, `outils/mingw/cross_mingw.sh`), comme les installateurs du lot 8 et de la 1.11.2. Le runtime C++ est lié dans l'exe ; seul `SDL3.dll` est à côté.
- **L'installateur** : `installateur/XPGAnalyser.iss`, compilé par Inno Setup 6.4.1 sous Wine (`outils/mingw/package_mingw.py`). Il est fait du même dossier de livraison que `package.bat` : l'exe, SDL3.dll, resources, libs, maintenance, licences, et `manifeste.json` avec la taille et le SHA-256 de chaque fichier.
  - Le script `.iss` accepte maintenant Inno Setup 6.4 en plus de 6.5 et suivantes. Deux appels réservés à la 6.5 sont gardés par `#if Ver >= 6.5` : l'alignement à droite d'un compteur, et la taille donnée à `CreateCustomForm`. Avec l'Inno Setup 6.7.2 de `package.bat`, rien ne change.
- **Sous Windows**, `outils\package.bat` reste la voie normale. Il compile avec MSVC et produit le même installateur.
- **Les sources** sont dans le dépôt GitHub, branche `claude/hopeful-goodall-h0srq0`.

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (55 sur 55). Pour l'éditeur IHM, 5 985 contrôles passent, sans échec. Les nouveaux contrôles :

| Groupe | Ce qu'il vérifie |
|---|---|
| `symParametres1113` | Voiture, `=UINTS`, UINTS sans fx, `1,5`, `vrai`, gCoef dans un tableau, l'ancien `Name := Voiture`, les noms de l'automate, les huit carrés |
| `valuePicker1113` | le clic sur le carré, la liste des carrés (sa place à l'écran, sa ligne d'information), le filtre sur le type, Tout montrer, les sources, la recherche, les erreurs et leurs corrections, Valider, un nom inconnu qui demande la création |
| `valuePickerApi1113` | le sélecteur sur MAST.XPG : un membre de DDT et un membre d'une case de tableau arrivent dans Résultat avec leur chemin entier |
| `scriptsExportImport1113` | l'aller-retour d'un fichier `.xpgst` (scripts d'une popup, de la vue modèle, de l'en-tête et du pied de page modèles, opérateurs d'un type), la comparaison neuf / identique / différent, Remplacer, Ajouter à la suite, un seul Ctrl+Z, un `.st` sans bloc, un format plus récent refusé |

Les attentes des tests 1.11.2 qui vérifiaient l'ancien comportement (`Voiture` gardé comme nom) sont mises à jour : c'est le comportement demandé qui a changé.

**L'exe Windows, lancé sous Wine :**
- `--version` répond `XPGAnalyser 1.11.3` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session `tools/sessions/session-1113-carre-selecteur.txt` a été rejouée sur une copie d'Armoire_Gaz préparée par `tools/sessions/preparer-projet-1113.cpp` (STEST, Vue_STEST/STEST_1, UINTS, AUTRES, gCoef, Popup_vanne et ses scripts). Les captures montrent :
  - les carrés de l'inspecteur ;
  - la liste des carrés ;
  - le sélecteur filtré, puis tout montré ;
  - l'erreur gCoef et ses corrections ;
  - le nom inconnu, puis la fenêtre de création ;
  - les boutons Exporter… et Importer… des scripts de Popup_vanne.

**Défauts trouvés en rejouant la session, et corrigés dans cette livraison :**
- La liste des carrés s'ouvrait sans se dessiner : le menu n'avait pas de place à l'écran.
- La ligne d'information répétait le nom du carré (« Variable IHM · Variable IHM : UINTS… »).
- Dans l'arbre du sélecteur, les membres des DDT de l'automate n'avaient pas de nom, et un clic aurait mis le membre seul (`gaz` au lieu de `ConfigArmoireUtilisee.gaz`) dans Résultat.
- Les 800 variables de l'automate s'affichaient dans l'ordre du fichier ; elles sont maintenant rangées par nom.
- En cherchant la cause du premier défaut : le clic droit sur la vue (Dupliquer…, Copier, Supprimer) avait le même problème depuis la 1.10.2 ; son menu se dessine maintenant (ligne ajoutée aux notes de version).

**L'installateur, lancé sous Wine** en silencieux pour tous les comptes, **par-dessus la 1.11.3 du correctif urgent** :
- le journal dit « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.3` ;
- l'exe installé analyse MAST.XPG (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB).
- Seuls les raccourcis du menu Démarrer dont le nom a un accent (Réparer, Vérifier l'installation…) n'ont pas pu être créés. C'est une limite de Wine dans ce conteneur, sans rapport avec Windows : le même script a installé la 1.11.2.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Merci de vérifier à l'ouverture d'un projet, et de me renvoyer `collect_diagnostics.bat` en cas de souci.
