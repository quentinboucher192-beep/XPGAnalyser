# 1.51.0 : un pointeur vers un objet (et Get()), la colonne du cycle seulement en OnCycle, les types IHM des symboles, le LADDER et le SFC dans API Studio, devenir admin par le seul code d'invitation

Fait à partir de tes demandes du 13/11 : le pointeur `ObjPtr := Vue_Accueil.Carte_1;` refusé (« ObjPtr n'existe pas »,
« chemin incomplet »), « afficher l'onglet des cycles que si je suis dans le onCycle », « que les symboles puissent
porter des types IHM », « dans API Studio ajouter le LADDER, le SFC, à la lecture, écriture et mode éditeur, au
simulateur », « supprime totalement le concept des demandes d'admin », et sur « Ajouter un admin » : « supprimer les
parties 2 et 3 / ajoute un système de recherche pour chercher le PC plus efficace ».

## 0. En bref

**Un pointeur vers un objet d'une vue (IHM Studio)**

- **Ton cas passe** : `ObjPtr := Vue_Accueil.Carte_1;` (sans propriété) dans un script OnCycle, avec `ObjPtr` de type
  `POINTER TO Object` dans l'onglet Variables : plus d'erreur à la saisie, à F7, à Générer ni à l'exécution ;
  `ObjPtr^.Visible := FALSE` cache bien `Carte_1`. Dans la vue du script, le nom court suffit (`Carte_1`). Pareil pour
  une variable `Object` ou `REF_TO Object`, l'objet d'un popup, une instance de symbole.
- **Par le pointeur** : `p^.Visible := FALSE`, `p^.X`, `Hide(p^)`, `MoveBy(p^, …)`, une comparaison avec `NULL` ou un
  autre pointeur, un passage à une fonction.
- **`Get()` sur chaque objet**, comme tu le proposais : `Vue.Objet.Get()`, `Carte_1.Get()`, `p^.Get()` renvoient le
  pointeur (comme `ADR(Vue.Objet)`). L'aide à la saisie le propose après `Vue.Objet.`, `o.` et `p^.` ; le guide F1 a
  une section « Un pointeur vers un objet : Vue.Objet, Get(), ADR » (exemples ST, C, C++).
- Là où une valeur est attendue (un BOOL, un nombre, un texte, un `IF Vue.Objet THEN`), « chemin incomplet
  (Vue.Objet.Propriété) » reste dit, à juste titre.
- **Les deux pannes derrière ton message** : la barre sous l'éditeur ne se remettait à jour qu'en bougeant le curseur
  (d'où « ObjPtr n'existe pas » resté affiché après la déclaration) ; et une variable créée `INT := 0` puis passée en
  pointeur gardait son `0` (refusé à l'exécution : « un DINT ne va pas dans un Object »). Le `0` est maintenant retiré
  au changement de type ; une variable d'un ancien projet qui l'a encore est marquée en rouge.

**La colonne du cycle**

- La colonne « EXÉCUTION » (les étapes du cycle de la vue) ne s'affiche plus que quand un script OnCycle est choisi ;
  pour OnOpen, OnClose ou sans choix, l'éditeur prend sa place. Glisser « + Script de cycle » l'ouvre le temps de
  déposer.

**Les types IHM d'un symbole**

- Un sous-onglet **Types IHM** dans l'éditeur du symbole (structure, énumération, callback, interface ; créer,
  renommer, dupliquer, supprimer, un Ctrl+Z par geste) et une partie **Types IHM** sous le symbole dans l'arbre. La
  liste des Types IHM du projet ne les montre pas.
- **Dans le symbole** (paramètres, fonctions, scripts, formules, ses autres types) : le nom court, `T_Etat`,
  `T_Mode#Marche` ; il l'emporte sur un type du projet du même nom. **Ailleurs** : `MonSymbole.T_Etat` (Compiler
  propose ce nom à la place du nom court) — comme les fonctions d'un symbole, atteintes par une instance, un type
  l'est par son symbole.
- « Choisir un type » ouvert depuis le symbole montre ses types en premier. Renommer le symbole suit partout ; le
  dupliquer, l'exporter / l'importer, le copier entre fenêtres, la corbeille et l'enregistrement emportent ses types.
  Un type encore employé ne se supprime pas : l'onglet Emplois s'ouvre sur ses emplois.

**Le LADDER (LD) dans API Studio**

- Une section LD d'un projet Control Expert s'ouvre **sur son schéma** (« Schéma » / « Texte » dans la barre de la
  section) : rails, contacts, bobines, blocs avec leurs broches et paramètres, comparaisons, opérations, sauts, retours,
  étiquettes, commentaires ; zoom, défilement, bulles au survol.
- **L'éditeur** (palette et clavier) : contacts ouvert / fermé / fronts (C, Maj+C, P, N), bobines normale / inverse /
  S / R / fronts (B, Maj+B, S, R), bloc F (la liste des types ; l'instance d'un bloc fonction déclarée dans le même
  Ctrl+Z), comparaison K, opération O, fil H, lien vertical V, branche en parallèle U (OU), inverser I, changer de
  type T, supprimer, nouvelle ligne (Inser), supprimer une ligne, déplacer (glisser ou Alt+flèches). Le nom d'une
  variable se tape avec l'aide à la saisie ; Échap annule. **Chaque geste, un Ctrl+Z.**
- **Lecture et écriture** : une section relue puis réécrite reste **identique octet pour octet** ; seules les lignes
  modifiées sont réécrites, selon les conventions d'un vrai export Control Expert. Une nouvelle section LD est un
  schéma vide (au lieu d'un texte). Références croisées, « Aller à », renommer et statistiques voient le ladder.
- **Le simulateur** : les sections LD tournent dans l'ordre de la tâche comme dans Control Expert (ligne par ligne,
  colonne par colonne ; séries, parallèles, S / R, fronts avec leur mémoire). Blocs TON, TOF, CTU, R_TRIG, SR, les
  instances de DFB, les fonctions (ADD_INT, GT, SEL, LIMIT…), comparaisons et opérations ; sauts et RETURN (une boucle
  de sauts est coupée et dite). Sur le schéma : **les fils sous tension en vert**, les valeurs à côté des contacts, des
  bobines et des broches ; 1, 0 ou L (ou un double-clic) force une variable.

**Le SFC (GRAFCET de Control Expert) dans API Studio**

- Une section SFC s'ouvre **sur son grafcet**, chaque objet dans sa case de la grille de Control Expert : étapes
  (initiales, macro-étapes), transitions avec leur condition (variable, ST, LD), branches OU (un trait) et ET (deux
  traits), sauts, liens avec leurs coudes, actions avec qualificatif et durée, commentaires ; zoom, Ctrl+F, export PNG,
  « Aller à » et références croisées.
- **L'éditeur** : ajouter une étape et sa transition sous la sélection (le reste descend), une branche OU ou ET, un
  saut ; la condition (une variable, NOT variable, ou une expression ST, rangée dans une section ST de transition) ;
  les actions (qualificatif, variable ou section, durée) ; les propriétés d'une étape (nom suivi par les sauts et le
  code ST, initiale, durées min / max, délai, commentaire) ; supprimer, déplacer. Le volet Code modifie les sections
  ST des transitions et des actions. **Chaque geste, un Ctrl+Z.** « Nouvelle section › SFC » crée un grafcet avec une
  étape initiale. Le fichier reste identique octet pour octet hors des éléments modifiés.
- **Le simulateur**, comme Control Expert (un jeton) : les étapes initiales au premier cycle, une évolution par cycle,
  la branche OU la plus à gauche si plusieurs transitions sont vraies, les branches ET ensemble ; `Etape.x`, `Etape.t`,
  `Etape.tminErr`, `Etape.tmaxErr` lisibles en ST et en LD ; les qualificatifs N, S, R, L, D, P, P1, P0, DS, SD, SL ;
  les transitions et actions en ST ou en LD. Sur le grafcet : les étapes actives avec leur temps, l'état de chaque
  transition, les actions en cours, les défauts de supervision ; **Initialiser**, **Activer** / **Désactiver** une étape
  à la main.

**Plus de demandes d'admin : le code d'invitation fait l'admin**

- **Ajouter un admin…** (Admin+) n'a plus qu'**une étape** : le nom, le courriel, le profil (ses droits), comment lui
  envoyer XPG Admin, et **le PC de son Studio**. Les parties 2 et 3 (l'empreinte, Accepter / Refuser la demande / Plus
  tard) sont supprimées.
- **La recherche du PC** remplace la liste déroulante : elle cherche dans le nom et l'identifiant du PC, le client, la
  licence, la version du Studio et la dernière activité, sans tenir compte des majuscules ni des accents ; les PC les
  plus récemment actifs d'abord ; ce qui correspond est surligné ; le compte (« 3 sur 40 PC ») ; Haut / Bas / Page
  préc. / Page suiv. / Entrée / Échap, et la molette.
- **À l'installation** : la personne tape son code (INV-XXXX-XXXX, 48 h, une seule fois) dans l'installateur de
  XPG Admin ; **au premier lancement, XPG Admin l'emploie et elle est admin**, avec les droits du profil choisi. Il ne
  reste que la signature de ta clé d'éditeur, que **ton Admin+ pose seul** : en moins de 15 s s'il est ouvert, sinon à
  son prochain lancement. Entre-temps elle voit « Tu es admin : ajout en cours ». Le journal des admins dit « a ajouté
  l'admin X (code d'invitation, invité par Y) ».
- Pourquoi l'Admin+ signe, et pas une invitation déjà signée d'avance : une invitation signée d'avance aurait permis au
  service, ou à quiconque tient sa clé, de fabriquer des admins ; et les XPG Admin 1.50 ne la comprendraient pas. Ta
  clé privée reste où elle est, nulle part ailleurs.
- Un code faux, expiré ou déjà employé est refusé avec un message clair, et se retape sur la page Administrateurs.
- La page « Demandes d'admin » n'existe plus (menu, badge, compteurs, alertes, console). Les anciennes demandes faites
  avec un code sont signées par l'Admin+ au prochain contact ; celles sans code sont fermées (« mets à jour »).

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin+ 1.51.0** (par-dessus la 1.50.1), puis les **Studios 1.51.0**.
2. **Service en ligne › Déployer le service** : **à faire cette fois**, le service a changé (l'inscription d'un admin
   par son code). Tant qu'il n'est pas redéployé, un XPG Admin 1.51.0 ne peut pas devenir admin.
3. **Versions** : publier la 1.51.0 avec ses trois fichiers (Studios, Admin+, XPG Admin).
4. Quand tu invites quelqu'un : laisse ton Admin+ ouvert (il le signe en moins de 15 s) ou ouvre-le plus tard.

## 2. Les essais

@@ESSAIS@@

## 3. Installer

@@EMPREINTES@@

**XPG Admin** (pour tes admins, à publier dans Versions) : `XPG-Admin-Setup-1.51.0.exe` (dans le zip).

| Fichier | SHA-256 | Taille |
|---|---|---|
@@XPGADMIN@@

## 4. Limites

- **Le pointeur vers un objet** : pas vers un objet à l'intérieur d'une instance de symbole (`Vue.Instance.Fond`) ; un
  seul type `Object` (pas de `POINTER TO Rectangle`) ; un objet passé à une fonction qui attend un nombre
  (`ABS(Vue.Objet)`) n'est plus signalé avant l'exécution, c'est l'exécution qui le refuse.
- **Les types IHM d'un symbole** : dans les popups, les alarmes et les opérateurs d'un symbole, et dans les surcharges
  d'une instance, le nom court ne marche que si aucun autre type ne porte le même nom ; l'aide à la saisie propose le
  nom complet (`S_Pompe.T_Etat`) plutôt que le nom court ; à l'import, un symbole au dessin identique mais aux types
  différents est « identique, gardé » (ses types ne sont pas mis à jour) ; une version 1.50 qui ouvre le projet lit
  ces types comme ceux d'un espace `S_Pompe` et perd leur lien au symbole si elle réenregistre.
- **Le LADDER** : le LD dans le corps d'un DFB ne s'exécute pas encore (un avertissement le dit) ; les appels de
  sous-programme (`callCoil`) ne sont pas simulés (un avertissement) ; pas de clic droit (la palette et le clavier).
- **Le SFC** : une macro-étape se simule comme une étape simple ; les sections FBD et IL de transitions ou d'actions
  sont gardées mais pas simulées (fausses, un diagnostic le dit) ; une section LD de transition se lit dans l'éditeur
  SFC sans s'y modifier ; les liens et commentaires se voient, se choisissent et se suppriment, mais ne se tracent pas
  à la main ; modifier pendant la simulation repart de la situation initiale ; un jeton seulement. Aucun vrai export
  avec des branches ET ou des macro-étapes n'a été trouvé : leur placement est déduit des règles, les branches OU, les
  liens, les actions et les commentaires suivent de vrais exports publics.
- **LADDER et SFC** : la relecture par Control Expert d'une section modifiée dans API Studio n'a pas pu être vérifiée
  (pas de Control Expert ici).
- **Devenir admin** : l'installateur ne vérifie que la forme du code (il n'a pas de réseau) : le code est vérifié au
  premier lancement ; la signature attend que ton Admin+ soit ouvert ; il faut le service redéployé (§ 1).
- **Rien sur un vrai Windows** : sous Wine seulement.
