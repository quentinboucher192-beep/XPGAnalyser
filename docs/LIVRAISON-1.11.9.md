# XPGAnalyser 1.11.9 — les actions : l'opération en arbre, le script dans sa fenêtre, Maths, le clavier virtuel

Livrée le 05/10/2026. Elle reprend vos demandes sur les actions :

| Votre demande | Où |
|---|---|
| « trier les actions disponibles avec un treeview utile et efficace » | Inspecteur › Actions, la case Opération |
| « exécuter un script : pouvoir ouvrir un petit modal pour éditer le script, avoir tous les mêmes principes que les autres scripts, accès à toutes les variables, aide à la saisie, références » | la ligne Code ST, son bouton … |
| « maths : aide aux formules avec mini modal, pouvoir ajouter n paramètres qui seront des références, avec un mode test pour tester la formule » | l'opération Maths, ses lignes Formule et Références |
| « ouvrir un clavier virtuel, champ de saisie, et on met des paramètres » | l'opération Clavier virtuel |

La livraison suivante, la 1.11.10, apportera les popups et les fonctions propres à un symbole.

Fichiers livrés : `XPGAnalyser-Setup-1.11.9.exe` (l'installateur), ce document et 8 captures.

## 1. L'opération en arbre

Un clic sur la case **Opération** d'une action ouvre l'arbre des opérations :

| Famille | Exemples |
|---|---|
| Variables (8) | Mettre à 1, Mettre à 0, Basculer, Affecter, Incrémenter, Décrémenter, Maths, Clavier virtuel |
| Navigation (4) | Naviguer, Vue précédente, Vue suivante, Vue d'accueil |
| Popups (7) | Ouvrir une popup, Changer de popup, Fermer la popup… |
| Scripts (2), Alarmes (4), Données (5), Médias (5), Utilisateur et poste (6) | |

**Comment s'en servir :**
- **La famille** de l'opération en cours est ouverte.
- **La recherche** en haut cherche dans le nom, la famille et ce que l'opération fait. Par exemple, « popup » garde la famille Popups et ses 7 opérations.
- **En bas**, la phrase de l'opération choisie dit ce qu'elle fait.
- **Pour choisir** : un double-clic, Entrée ou « Choisir ». Les flèches passent d'une opération à l'autre, Échap annule.

Changer d'opération repart sans les réglages de l'ancienne : les références de Maths ne deviennent pas des réglages du clavier.

Captures : `1119_02_operation_arbre.png`, `1119_03_operation_arbre_recherche.png`.

## 2. Le script d'une action, dans sa fenêtre

Pour **Exécuter un script**, le bouton **…** de la ligne Code ST ouvre le script dans une fenêtre :
- **l'éditeur des scripts** de l'application : la couleur du ST, les numéros de ligne, l'aide à la saisie (Ctrl+Espace, les membres après un point, les signatures) ;
- **sous l'éditeur**, ce qu'est le nom sous le curseur (une variable IHM, une référence, son type) ;
- **les fautes**, comme Compiler : la syntaxe, les chemins, les noms ;
- **à droite**, tout ce que le script peut lire :
  - les **références** d'abord, en couleur : ce sont les paramètres de la vue ou de la popup, par exemple `Four` (T_Four) dans Pop_Reglage ;
  - puis les variables IHM, l'automate et SYS. ;
  - une recherche filtre la liste, et un double-clic insère le nom.

« Valider » (Ctrl+Entrée) écrit le script dans l'action, et Ctrl+Z le reprend. Échap annule.

Capture : `1119_06_script_fenetre.png` (`Four.Consigne := 850;`, la référence Four en tête, « Aucune faute »).

## 3. Maths

L'opération **Maths** calcule une formule sur des **références nommées** et écrit le résultat dans une variable :

| Ligne | Exemple |
|---|---|
| Variable (le résultat) | `Sortie` |
| Formule | `(Mesure - Consigne) * 2` |
| Références | `Mesure := M; Consigne := C` |

**Une référence** est obligatoirement une variable, comme `Armoires[0].ana.PT1.mes`, jamais un calcul : le calcul va dans la formule. En marche, la formule se lit avec les variables à la place des noms.

**Le bouton … de Formule ou de Références** ouvre la fenêtre de Maths :
- **le résultat**, et chaque référence : son nom, sa variable, une valeur de test ; « + Ajouter une référence », × pour la retirer ;
- **la formule**, avec l'aide à la saisie ; les fonctions (ABS, SQRT, MIN, MAX, LIMIT, SIN, COS, TAN, EXP, LN, LOG, TRUNC, SEL, MUX) s'insèrent d'un clic ;
- **les fautes** : une référence qui n'est pas une variable, un nom en double, un nom réservé ;
- **le mode test** : « Tester » calcule la formule avec les valeurs de test à la place des références. Exemple : `= 5   ((12.5) - (10)) * 2`.

« Valider » refuse tant qu'il reste une faute.

Captures :
- `1119_01_maths_inspecteur.png` : les lignes de Maths ;
- `1119_04_maths_fenetre_test.png` : la fenêtre et le mode test.

## 4. Le clavier virtuel

L'opération **Clavier virtuel** ouvre, en marche, un champ de saisie par-dessus la vue, avec son clavier. Ce qu'on tape va dans la variable à la validation.

**Ses réglages, dans l'inspecteur :**

| Réglage | Ce qu'il fait |
|---|---|
| Variable | où va la valeur tapée |
| Titre | un texte à trous : `Code du badge`, `Consigne de {Four1.Nom}` ; vide : le nom de la variable |
| Clavier | auto (selon le type de la variable), numérique, complet (lettres) |
| Min, Max | les limites (un nombre ou une expression) ; vides : aucune |
| Unité | montrée au bout du champ |
| Caractères cachés | des points à la place des caractères (un code) |

**En marche :**
- Le champ montre la valeur du moment : la première frappe la remplace.
- **Entrée** ou « Valider » écrit la valeur. Une valeur hors limites, ou qui ne se lit pas, est refusée, et le champ le dit en restant ouvert.
- **Échap** ou « Annuler » ferme sans rien écrire.
- **Le champ est modal** : on ne clique pas la vue derrière.
- **Le journal** garde la valeur écrite (un code caché y est masqué).

Captures :
- `1119_05_clavier_reglages.png` : les réglages ;
- `1119_07_clavier_en_marche.png` : « Code du badge », 1234 tapé, le clavier numérique ;
- `1119_08_resultats_en_marche.png` : Sortie = 5.0 (Maths), Code = 1234 (le clavier), Compteur = 1 (le script).

## Installer

1. Lancer `XPGAnalyser-Setup-1.11.9.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisir « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.9.exe` (14,7 Mo, 15 385 413 octets) :
`534521896B0A9402E5587FFFA0A40A5E1AAAC93AD1202DE0BCF82287D4ACF6F9`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (55 sur 55). Les nouveaux contrôles :

| Test | Ce qu'il vérifie |
|---|---|
| `actionsMathsClavier1119` (hmi_test) | chaque opération dans une seule famille, avec sa phrase ; les références lues et réécrites ; la formule avec les variables (MesureX reste) ; une référence calculée refusée, un nom en double dit ; le mode test (= 5) ; les réglages du clavier ; en marche, Maths (Sortie = 5), puis le clavier (150 refusé hors limites, 42 écrit, Échap n'écrit rien) |
| `actionsFenetres1119` (hmi_editor_test) | sans hôte, la liste comme avant ; l'arbre (toutes les familles, la recherche « clavier », la phrase) ; choisir Clavier virtuel, puis ses réglages ; Maths : la fenêtre, deux références, le mode test, Valider ; le script : la référence Moteur à droite, les fautes, Valider, Ctrl+Z |

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.9` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session `session-1119-actions.txt` a été rejouée sur une copie d'Armoire_Gaz : ce sont les 8 captures.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé en silencieux, pour tous les comptes, par-dessus la 1.11.8 : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.9`.
- Seuls les raccourcis du menu Démarrer dont le nom a un accent n'ont pas pu être créés. C'est la limite de Wine déjà vue aux livraisons précédentes, sans rapport avec Windows.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Merci de vérifier, en particulier le clavier virtuel sur l'écran tactile du poste d'exploitation.

Les sources sont dans le dépôt GitHub, branche `claude/hopeful-goodall-h0srq0`, avec la session de captures `tools/sessions/session-1119-actions.txt` et son projet (`tools/sessions/preparer-projet-1119.cpp`).
