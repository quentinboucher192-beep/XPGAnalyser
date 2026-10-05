# L'aide d'un DDT ou d'un DFB

## Où elle vit

Dans le fichier du bloc, pas à côté.

Une documentation rangée ailleurs — un fichier d'aide, une table, un wiki — se
sépare de ce qu'elle décrit à la première modification faite dans l'urgence.
Celle-ci est dans le `.ddt` : on ne peut pas copier le bloc sans elle, ni le
modifier sans l'avoir sous les yeux.

## À quoi elle ressemble

```
# Derived data type, shared.
name = ST_EQ_Valve2
#! summary = Une vanne deux positions, avec un retour sur chaque position.
#! summary = Attention ; sur une vanne a ressort le retour ferme est inverse.
#! usage = Appeler DFB_EQ_VALVE2 une fois par cycle, avec le tableau en InOut.
#! example = Vannes[0].OpenReq := Ctrl.Ouvrir;
#! since = 1.2
#! author = QM
#! see = ST_EQ_Valve3
#! param FdcOpen = Le retour de position ouverte, cable sur une entree TOR.
#! param FdcOpen = Laisser a FALSE si la vanne n'en a pas : le bloc bascule
#! param FdcOpen = alors en manoeuvre temporisee.
#! fault 1 = Les deux retours sont actifs en meme temps : cablage ou capteur.
#! fault 3 = La temporisation de manoeuvre a expire.
OpenReq ; BOOL ; Member ; FALSE ; demande d ouverture
FdcOpen ; BOOL ; Member ; FALSE ; retour position ouverte
...
```

## Pourquoi `#!` et pas autre chose

**L'aide est écrite dans des commentaires, et ce n'est pas un détail.**

Le format d'un `.ddt` est déjà fixé et déjà lu par trois choses : le parseur
C++, la macro `INIT` du classeur Excel, et l'œil de celui qui l'ouvre dans un
éditeur de texte. Une ligne d'aide doit être invisible pour les deux premiers.

Un lecteur de `.ddt` teste les lignes dans cet ordre :

| test | effet |
|---|---|
| `name ` en tête | le nom de la famille |
| `#` en tête, ou ligne vide | **commentaire, ignoré** |
| `<<<` en tête | le corps ST commence |
| contient un `;` | un paramètre |

Le test du `#` passe **avant** celui du point-virgule. Une aide préfixée par `#`
est donc ignorée par construction — y compris quand son texte contient un
point-virgule, ce qu'une prose française fait sans prévenir. Sans cette
précaution, `Attention ; voir la notice` serait lu comme un paramètre nommé
`Attention`.

Vérifié en rejouant la logique exacte du lecteur existant sur le fichier
ci-dessus : **4 paramètres, exactement les bons**, l'aide invisible.

Le marqueur est `#!` : un commentaire pour tout le monde, une donnée pour nous.
Un `#` seul reste un commentaire ordinaire et le reste.

## Les clés

| clé | sujet | contenu |
|---|---|---|
| `summary` | — | une phrase : ce que c'est |
| `usage` | — | comment s'en servir |
| `example` | — | un appel ST, recopié tel quel |
| `since` | — | la version où c'est apparu |
| `author` | — | qui |
| `see` | — | un item voisin, répétable |
| `param <Nom>` | le paramètre | l'aide longue, quand le 5ᵉ champ ne suffit plus |
| `fault <Code>` | le code | ce que veut dire ce défaut |

**Une clé répétée ajoute une ligne.** C'est tout le mécanisme du texte long : pas
de caractère de continuation à retenir, pas de guillemets à équilibrer, et une
ligne d'aide reste une ligne dans l'éditeur de texte.

**Les clés inconnues sont conservées.** Un fichier écrit par une version plus
récente porte des clés que celle-ci ignore ; les jeter en sauvegardant ferait
perdre le travail de quelqu'un d'autre, silencieusement — le genre de perte
qu'on ne remarque que longtemps après.

## Ce que la réécriture garantit

- **Le fichier n'est pas abîmé.** Bloc ST, déclarations, commentaires
  ordinaires, lignes vides et fins de ligne reviennent à l'octet près. Un
  éditeur de documentation qui reformate le code qu'il documente est un éditeur
  qu'on n'ouvre plus.
- **Un `.ddt` en CRLF reste en CRLF.** Les remplacer ferait apparaître le fichier
  entier comme modifié dans un gestionnaire de versions, pour une phrase
  ajoutée.
- **Une aide vide ne change rien.** Ouvrir la fenêtre d'aide et la refermer sans
  rien taper laisse le fichier intact.
- **L'idempotence.** La *première* réécriture peut réordonner un bloc écrit à la
  main : l'ordre des clés est canonique. Toutes les suivantes rendent exactement
  le même texte — sans quoi une sauvegarde changerait le fichier à chaque fois et
  tout historique deviendrait illisible.
- **Un fichier sans aide** en reçoit une après **tout l'en-tête**, pas juste
  après `name =`. Un fichier réel porte `name = X` puis `version = 1.00` ;
  s'insérer entre les deux couperait l'en-tête pour rien. La pose s'arrête à la
  dernière ligne `clé = valeur` du bloc de tête — un commentaire qui contient un
  `=` et une déclaration dont la valeur initiale en contient un n'en sont pas.
- **Un fichier sans `name =`** reçoit l'aide en tête plutôt que nulle part :
  perdre ce qui vient d'être écrit serait le pire des choix.

## Deux conventions relevées sur les fichiers réels

**Aucun accent.** Les `.ddt` de la bibliothèque sont écrits en français sans
accents — « presente », « defaut », « gravite ». L'aide suit la même convention :
mélanger les deux dans un fichier donnerait l'impression que la moitié est mal
encodée.

**Fins de ligne LF.** Les douze fichiers sont en LF seul. Le tool les préserve
telles quelles ; un fichier en CRLF resterait en CRLF.

## L'API

```cpp
auto p = project::readHelp(contenuDuFichier);
p.help.summary;                  // le résumé
p.help.param("FdcOpen");         // l'aide longue d'un paramètre, ou nullptr
p.help.fault("3");               // ce que veut dire le défaut 3
p.itemName;                      // ce que dit "name ="
p.warnings;                      // les lignes d'aide mal formées

LibraryHelp h = p.help;
h.summary = "Autre chose.";
auto sorti = project::writeHelp(contenuDuFichier, h);   // à réécrire sur disque
```

`LibraryHelp.cpp` ne dépend que de la bibliothèque standard — ni du modèle de
projet, ni de l'interface. C'est ce qui le rend vérifiable sans écran et sans
projet chargé, et c'est ce qui a permis de le tester alors que le reste du
dépôt n'était pas disponible.
