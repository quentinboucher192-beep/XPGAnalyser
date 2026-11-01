# API Studio et IHM Studio 1.22.4 : où sont les erreurs, un texte entre guillemets, l'IA qui ne tourne plus en rond

Suite de la 1.22.3, faite à partir de trois demandes du 01/11 :
- ta capture de la prévisualisation : voir les erreurs en couleur, chercher, et voir où un élément est utilisé ;
- ta capture des paramètres d'une popup : `=Vanne` dans une valeur par défaut STRING ;
- « Proposer la suite » qui revient au même point.

Elle se met à jour par-dessus la 1.22.3 et garde tes données.

## 1. Ce qui change pour toi

### La prévisualisation montre où sont les erreurs

- **Dans l'aperçu**, un objet en erreur a un second cadre **rouge**, plus épais, avec une pastille « ! ». Un objet en avertissement a le même cadre en **ambre**. La légende du bas les nomme, et le survol d'un objet liste ses messages.
- Un avertissement qui ne nomme pas ses objets encadre quand même les objets concernés. C'est le cas de « 2 objets créés ne sont reliés à aucune variable » sur ta capture.
- **À gauche**, les lignes liées à une erreur sont en rouge, celles liées à un avertissement en ambre. Cela vaut pour les erreurs, les avertissements, les étapes de l'architecture, les objets, les variables et les types. Le titre d'une catégorie porte la pastille de son pire niveau, même replié.
- **La recherche**, en haut de la liste (Ctrl+F) :
  - elle filtre toutes les catégories : un nom d'objet, de variable, de type ou un texte d'erreur ;
  - les titres des catégories qui ont un résultat restent, et un compte dit « 12 lignes » ;
  - Échap vide la recherche ; un second Échap ferme la fenêtre, comme avant.
- **Les puces Erreurs (n) et Avertissements (n)** ne gardent que ces lignes, l'une, l'autre ou les deux.
- **Choisir une ligne encadre dans l'aperçu ce qu'elle désigne**, en couleur d'accent. Le cadre pulse une seconde puis reste, et l'aperçu s'y recentre (il zoome si c'est petit).
  - Un objet : cet objet.
  - Une variable : les objets qui la lisent (variable liée, formules, textes à trous, actions, arguments).
  - Un type : les objets qui lisent une variable ou un paramètre de ce type.
  - Une étape : ses objets, sinon ceux qui lisent ce qu'elle crée.
  - Une erreur ou un avertissement : ses objets.
- Inversement, **un clic sur un objet de l'aperçu** choisit sa ligne à gauche.

### Un texte entre guillemets, un nom sans

Sur ta capture, la valeur par défaut du paramètre **Nom : STRING** de la popup valait `=Vanne`. Le logiciel l'avait lue comme le **paramètre Vanne** (un sortie_tor), et non comme le texte « Vanne ». Il disait donc « Vanne est sortie_tor ; ce champ attend STRING ». Un texte se montrait sans apostrophes : rien ne distinguait le texte du nom.

- **Dans une case de texte, `"Vanne"` est un texte et `Vanne` est un nom** (un paramètre, une variable). Cela vaut partout : la valeur par défaut d'un paramètre, un argument d'Ouvrir une popup ou d'une instance, une valeur initiale, un membre, une recette.
- **Un texte constant se montre toujours entre guillemets** : `"Azote"`, `"L'eau"`. Ce qui est montré se retape tel quel. Un guillemet dans le texte s'écrit `$"`.
- **Ce que tu tapes :**
  - `"Vanne"` ou `'Vanne'` : le texte, même si Vanne est aussi un nom ;
  - un mot qui n'est pas un nom (`Pompe`) : le texte, qui reçoit ses guillemets ;
  - un nom (`Vanne`) : le nom. S'il n'est pas un texte, la ligne rouge le dit : « Sans guillemets, Vanne est un nom ; le texte s'écrit "Vanne" ». Le carré au bout de la case propose **Prendre comme texte : "Vanne"**.
- **Un texte se construit en formule**, comme dans ton exemple : `="test" + "test2" + vanne2.ToString()`.
  - `x.ToString()` est `TO_STRING(x)` ; pour une énumération, c'est son toString.
  - Les guillemets et `.ToString()` marchent dans toutes les expressions de l'IHM : formules, scripts, conditions.
  - Le code de l'automate garde le ST de Control Expert : `'texte'`.
- **L'IA apprend la même règle** (règle 20 de ses consignes) :
  - un texte constant s'écrit entre apostrophes ; sans elles, c'est un nom ;
  - un texte se construit en formule : `'Vanne ' + Numero.ToString()` ;
  - le texte affiché d'un objet (label, titre) reste un texte à trous, sans apostrophes.
- **L'Assistant vérifie aussi ce que l'IA écrit :**
  - une valeur par défaut STRING qui serait un nom d'un autre type (`Vanne`, un T_Vanne) est **refusée**, avec l'explication. C'est exactement le cas de ta capture ;
  - un mot inconnu est pris comme texte, et un avertissement le dit ;
  - les valeurs par défaut de l'IA sont gardées comme celles d'une case : `"Vanne"` devient `'Vanne'`, et `2,5` devient `2.5`.

### « Proposer la suite » ne tourne plus en rond

À l'arrivée de chaque proposition (une mission, ou « Proposer la suite » dans une conversation), le logiciel la compare aux précédentes. Il voit un **cercle** dans cinq cas :

1. **Les mêmes opérations** qu'une proposition précédente, qu'elle ait été refusée, en échec, annulée ou appliquée. La place des objets ne compte pas : deux propositions qui ne diffèrent que par x et y sont la même.
2. **Les mêmes erreurs** que la tentative précédente pour la même étape.
3. **Une étape proposée 3 fois sans aboutir.** Le compte repart de zéro quand elle aboutit ou quand tu donnes une consigne.
4. **Le projet revenu au même point** pour la même étape, une troisième fois.
5. Dans une conversation, **la même suite** annoncée de nouveau.

Ce qui se passe alors :
- **La proposition est retenue.** Elle ne s'applique pas, même en mode Direct, et la suite ne repart pas toute seule.
- **Sa carte porte « Tourne en rond » et dit le cercle**, par exemple :
  - « P2 refait P1 (refusée ; la même opération, la même erreur : Vanne.Cmd) » ;
  - « S1 a été proposée 3 fois sans aboutir (P1, P2, P3) ».
- La mission le note dans son historique, et une notification dit « La mission tourne en rond ».
- **Tu choisis sur la carte :**
  - **Modifier la demande** : la saisie reçoit « Pour S1 : » ; écris ta consigne puis Entrée, elle part avec la suite ;
  - **Corriger à la main…** : la prévisualisation ;
  - **Ignorer cette étape** ;
  - **Forcer la fin de la mission…** ;
  - **Proposer quand même**.
- **La demande suivante dit à l'IA ce qui a déjà été proposé** pour ces étapes, et pourquoi cela a échoué : le sort, les opérations, les erreurs exactes. Elle reçoit aussi la consigne de ne pas reproposer la même chose (changer d'approche ou dire ce qui bloque). Ce rappel garde les 4 dernières tentatives au plus.
- Ce qu'il faut pour reconnaître un cercle après une relance est gardé dans le fichier de la mission. Un fichier 1.22.x se relit.

## 2. Les essais

Sous Linux : **122 suites d'essais sur 122** réussies (CTest).

- **Prévisualisation :** `ai_corrections_test` refait ta capture. Il vérifie :
  - le cadre rouge et le cadre ambre de 3 px, lus dans le dessin ;
  - l'avertissement global qui encadre l'objet non relié ;
  - la recherche, les puces, Échap et Ctrl+F ;
  - la sélection d'une variable, d'un type, d'une étape et d'un message, avec le recentrage ;
  - le clic sur un objet.
  Résultat : 157 vérifications (113 en 1.22.1).
- **Les guillemets :**
  - `cellrule_test` : `"Vanne"` lu comme texte, `Vanne` comme nom, et ce qui est montré se relit à l'identique (`"L'eau"`, `"dit $"oui$""`, `"Prix 5$$"`, un repère `$Nom$`, deux lignes). Résultat : 188 contrôles ;
  - `hmi_test` : `"test" + "test2" + Etat.ToString()` vaut `testtest25` ;
  - `ai_options_test` refait ta capture : la valeur par défaut `Vanne` d'un paramètre STRING, Vanne étant un T_Vanne, est refusée avec l'explication. `'Vanne'`, `"Vanne"`, `Pompe` et une formule sont gardés comme il faut, et les consignes contiennent la règle 20. Résultat : 251 vérifications (244 en 1.22.2) ;
  - `hmi_editor_test` : les cases des arguments et des paramètres montrent les textes entre guillemets. Résultat : 9 565 contrôles.
- **L'anti-cercle :**
  - `ai_missions_test` vérifie les empreintes (la même proposition réordonnée, ou ne différant que par x et y, a la même empreinte), les cinq signes, la relecture du fichier et le rappel des tentatives dans la demande. Résultat : 414 vérifications (363 en 1.22.2) ;
  - `ai_missions_e2e_test` : l'IA d'essai renvoie deux fois la même proposition. La seconde est retenue, rien ne s'applique, la suite ne repart pas, « Proposer quand même » la propose, et la demande suivante contient « Déjà proposé ». Résultat : 303 vérifications (245 en 1.22.3).

## 3. Installer

Fichiers livrés :
- `API-IHM-Studio-Setup-1.22.4.exe` : l'installateur (47 430 549 octets). Il met à jour la 1.22.3 au même endroit ;
- `API-Studio-1.22.4-portable.zip` et `IHM-Studio-1.22.4-portable.zip` (167 fichiers chacun) ;
- ce document.

| Fichier | SHA-256 |
|---|---|
| `API-IHM-Studio-Setup-1.22.4.exe` | `E45216FDF3C98E6D3278390F85FBB12B519A2205163653AEEA52F3979AA63357` |
| `API-Studio-1.22.4-portable.zip` | `B58925C7CCD144074BA0131FC7D830E1A1A4B9E3638F01EA3A0C0938EB77A0AE` |
| `IHM-Studio-1.22.4-portable.zip` | `111AC4C322BC19595EA95DAA054229D4446D10E548ADC0D13286B1F2D676D0A8` |

Vérifié **sous Wine (pas sur un vrai Windows)**, en silencieux, par-dessus la 1.22.3 livrée :

- l'installation réussit ;
- une clé API d'essai est gardée à l'identique ;
- les deux exécutables installés sont identiques à ceux construits ;
- `--version` répond « API Studio 1.22.4 » et « IHM Studio 1.22.4 » ;
- `API-Studio.exe --cli` relit le projet d'essai (891 variables, 29 POU, 75 sections, 28 DDT, 8 DFB) ;
- le bac à sable de l'exécutable Windows fait l'analyse, la compilation et l'exécution ;
- les associations `.xihm` et `.xapi` sont en place ;
- la copie de secours de la 1.22.3 est gardée.

L'interface n'a pas été ouverte dans une fenêtre.

## 4. Limites

- **Un changement visible partout :** un texte constant se montre maintenant entre guillemets, dans les cases comme dans les invites grises (« "Sans nom" · valeur par défaut »). Tes projets ne changent pas : les textes étaient déjà gardés entre apostrophes.
- **Un mot qui n'est pas un nom reçoit ses guillemets.** Une variable mal tapée dans une case de texte (`Nivau`) devient donc le texte `"Nivau"`. Les guillemets le montrent.
- **« Modifier la demande » dans une mission** est nouveau : la consigne écrite après « Pour S1 : » est gardée dans la mission et part avec la suite. Un autre message l'oublie.
- **Une carte retenue n'a pas de bouton Refuser** tant que tu n'as pas choisi.
- **La détection des cercles** porte sur les propositions de l'IA connectée, pas sur l'IA intégrée.
- **La recherche de la prévisualisation repart vide** à chaque ouverture, pour ne pas filtrer une autre proposition.
- **Les vérifications ont été faites sous Wine, pas sur un vrai Windows**, et jamais avec une vraie IA. Les nouveaux écrans n'ont été vus que dessinés hors écran par les essais.
- **Pas de captures d'écran**, selon ta consigne permanente.
