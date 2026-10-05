# XPGAnalyser 1.11.3 — première livraison : le correctif urgent

Livrée le 05/10/2026. Cette livraison corrige les paramètres d'une instance de symbole (les captures de STEST_1). La suite de la 1.11.3 arrive dans une seconde livraison, sous le même numéro : le sélecteur de valeur, la création d'une variable inconnue, l'export et l'import des scripts.

## Ce qui est corrigé

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

### Nouveau : le carré de légende

Au bout de chaque case de l'inspecteur, un carré dit d'où vient la valeur.

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

Son infobulle donne le type, la source et l'erreur s'il y en a une. **Le clic sur le carré arrive avec la seconde livraison** : la liste des carrés, puis le sélecteur de valeur.

## Installer

1. Lancer `XPGAnalyser-Setup-1.11.3.exe`. Il met à jour la 1.11.2 installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisir « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de l'installateur : voir `dist/SHA256SUMS.txt` (rappelée dans le message de livraison).

## Comment cette version a été fabriquée

- **L'exe** : compilé sous Linux avec MinGW-w64 (GCC 13, `outils/mingw/cross_mingw.sh`), comme les installateurs du lot 8 et de la 1.11.2. Le runtime C++ est lié dans l'exe ; seul `SDL3.dll` est à côté.
- **L'installateur** : `installateur/XPGAnalyser.iss`, compilé par Inno Setup 6.4.1 sous Wine (`outils/mingw/package_mingw.py`). Il est fait du même dossier de livraison que `package.bat` : l'exe, SDL3.dll, resources, libs, maintenance, licences, et `manifeste.json` avec la taille et le SHA-256 de chaque fichier.
- **Sous Windows**, `outils\package.bat` reste la voie normale. Il compile avec MSVC et produit le même installateur.

## Vérifications

- Tests Linux (GCC 13) : la suite CTest complète passe.
  - Pour l'éditeur IHM : 5 916 contrôles, dont les nouveaux de `symParametres1113` : Voiture, `=UINTS`, UINTS sans fx, `1,5`, `vrai`, gCoef dans un tableau, l'ancien `Name := Voiture`, les noms de l'automate, les huit carrés.
  - Les attentes des tests 1.11.2 qui vérifiaient l'ancien comportement (`Voiture` gardé comme nom) sont mises à jour : c'est le comportement demandé qui a changé.
- L'exe Windows n'a pas été lancé sur un vrai Windows ici. Merci de vérifier à l'ouverture d'un projet, et de me renvoyer `collect_diagnostics.bat` en cas de souci.

## La seconde livraison (même 1.11.3)

- **Le clic sur le carré** : la liste des carrés et leur sens, puis le **sélecteur de valeur**. C'est un arbre de tout ce qui est disponible (API, IHM, SYS., symbole/vue), filtré sur le type attendu et défiltrable, avec une recherche. Un champ résultat modifiable reçoit une formule ou un repère, avec l'aide à la saisie, les erreurs et leurs corrections.
- **La création d'une variable inconnue** : choix du type et de la zone (API ou IHM), un seul Ctrl+Z.
- **Exporter et importer les scripts** des vues, popups, symboles, vue modèle, en-tête et pied de page modèles, et des opérateurs.
- La maquette HTML interactive mise à jour.
