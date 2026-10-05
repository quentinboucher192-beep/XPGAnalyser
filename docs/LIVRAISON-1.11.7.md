# XPGAnalyser 1.11.7 — le forçage commun, le blocage « Délier », les repères dans les paramètres de popup, la case Variable des actions

Livrée le 05/10/2026. Elle reprend vos demandes faites après la 1.11.6 :

| Votre demande | Où |
|---|---|
| « les forçages, si les variables sont communes dans les 3 onglets […] il faut que ce soit un forçage commun, et il faut passer prioritaire par rapport aux scripts » | Simulation IHM › Variables IHM, Esclaves simulés |
| Le rapport de blocage du 05/10 à 16:59, « à corriger dans la prochaine livraison » | Délier une variable pendant la simulation |
| « quand je fais tourner l'IHM il n'envoie pas le paramètre car il y a les $$ […] je veux pouvoir laisser des références dans les paramètres des popups » | Les actions Ouvrir une popup, Changer de popup, Naviguer |
| « le champ Variable dans lequel je veux voir le fx et les références » | Inspecteur › Actions |
| « savoir le nombre de lignes de code h, hpp, c, cpp du projet » | Écran d'accueil, À propos |

Les livraisons suivantes sont décalées :

| Version | Contenu |
|---|---|
| 1.11.8 | Variables IHM : les adresses %MW, %M… des membres d'une structure, des membres hors de la liaison (gardés dans l'IHM), le bouton « Recalculer la place mémoire » |
| 1.11.9 | les actions : le script dans une petite fenêtre, le clavier virtuel, Maths, les actions rangées en arbre |
| 1.11.10 | les popups et les fonctions propres à un symbole |

Fichiers livrés : `XPGAnalyser-Setup-1.11.7.exe` (l'installateur), ce document et 9 captures.

## 1. Le blocage en déliant une variable (rapport du 05/10, 16:59)

**Ce que dit le rapport.** C'était la 1.11.6 : la simulation IHM tournait (scripts Pos et Initialisation), vous avez fait « Délier V », et la boucle principale s'est arrêtée. La pile du fil principal, lue avec les symboles de l'exe 1.11.6 :

```
App::frame → EquipmentHost::tick → hmi::comm::Link::stop → std::thread::join
```

La copie mémoire (`.dmp`) montre où étaient les autres fils à ce moment-là :
- le fil de la liaison Modbus était au milieu d'une écriture (`Link::doWrites` → `Client::writeRegister`) ;
- l'esclave simulé était dans son temps de réponse.

**La cause.** Délier change le plan de la liaison : l'ancienne liaison est arrêtée, puis refaite. L'arrêt attendait que la liaison finisse **tout son cycle**, c'est-à-dire toutes les écritures en attente et toutes les lectures. Chacune payait le temps de réponse de l'esclave simulé, et un script qui écrit sans cesse en remplit la file. D'où les 8 s, et plus.

**Le correctif.**
- La liaison s'arrête **entre deux requêtes** : elle finit seulement celle en cours.
- La boucle principale **ne l'attend plus**. L'ancienne liaison finit sur son fil, puis est libérée au tour suivant.

Le test `blocageDelier1117` reproduit votre cas : un esclave qui répond en 300 ms, 15 écritures en attente, puis Délier V.

| | Avant | Après |
|---|---|---|
| Délier V : la boucle principale | bloquée 4,7 s | reprend tout de suite (moins de 0,25 s) |
| Arrêter la liaison | tout le cycle (15 × 300 ms) | la requête en cours seulement |

Une écriture encore en attente au moment où la liaison est refaite est abandonnée. Elle visait l'ancien plan.

## 2. Le forçage commun, prioritaire sur les scripts

Une variable IHM liée à un esclave simulé est **la même case** que sa ligne dans Esclaves simulés. Par exemple, Four3 est liée à la Centrale PM5560 à %MW3200, donc Four3.Temperature est la case %MF3200 de l'esclave.

**Ce qui est commun aux deux onglets :**
- **Forcer** dans Variables IHM force la case de l'esclave, et l'inverse. Les deux onglets la montrent forcée, en orange.
- **Un mouvement** (sinus, rampe…) posé dans Variables IHM est celui de l'esclave. Il est converti en brut si la variable a une mise à l'échelle.
- **Libérer** d'un côté libère de l'autre.
- **La valeur** : Variables IHM montre tout de suite la valeur forcée dans l'esclave. Une variable liée se relit aussi sur son équipement, même quand aucune vue ne la montre. Avant, elle gardait sa valeur du démarrage.

**Prioritaire sur les scripts.** Une variable forcée garde sa valeur quand un script l'écrit, que le forçage vienne de Variables IHM ou de son esclave. L'écriture est ignorée, sans erreur. Dans Variables API, l'automate simulé faisait déjà passer le forçage avant.

Captures :
- `1117_07_force_dans_variables_ihm.png` : Four3.Temperature forcée à 80 dans Variables IHM ;
- `1117_08_meme_case_dans_l_esclave.png` : la même case, « F 80 », dans Esclaves simulés ;
- `1117_09_force_dans_l_esclave_vu_ihm.png` : Four3.Marche forcée à 1 dans l'esclave, « TRUE (forcée) » dans Variables IHM.

## 3. Un repère dans les paramètres d'une popup

`IN_V := $Four2.Vannes[1]$` (un repère, comme Dupliquer… les pose) passe maintenant en marche, en référence. Ses `$` sont ignorés là où l'argument est lu, et le texte enregistré les garde pour Dupliquer….

| Où | Avant | Maintenant |
|---|---|---|
| En marche | `$V[0]$` était calculé comme une valeur : rien ne passait | la popup reçoit `V[0]` en référence, elle le lit et l'écrit |
| Paramètres de Popup | « 1 à revoir » | rien à revoir |
| Générer | une erreur sur l'argument | rien à signaler |

Cela vaut pour Ouvrir une popup, Changer de popup, Naviguer et `IHM_POPUP`, ainsi que pour les valeurs par défaut des paramètres.

Captures :
- `1117_03_parametre_repere_editeur.png` : IN_V (T_Vanne, REF) avec son repère ;
- `1117_06_popup_repere_en_marche.png` : Vanne 2 ouvre la popup, « Ouvrir » écrit Four2.Vannes[2].Ouverte (TRUE, aussi sous la vue).

## 4. La case Variable d'une action : fx et références

La case **Variable** de Mettre à 1, Mettre à 0, Basculer, Incrémenter, Décrémenter et Affecter est maintenant comme la **Condition** :
- elle a la pastille **fx** et son type au bout du nom (BOOL pour Mettre à 1, Mettre à 0 et Basculer, nombre pour Incrémenter et Décrémenter) ;
- le bouton **×** la vide ;
- **l'aide** propose les variables et, dans un symbole ou une popup, ses **références** et leurs membres. Par exemple, `IN_V.` propose Ouverte, Fermee, Position (« ne convient pas : INT » pour un BOOL attendu).

**Le « = » devant.** Dans votre exemple, `=Vanne.CMD_OUV` était gardé tel quel, et l'action écrivait dans une variable nommée « =Vanne.CMD_OUV » : elle ne marchait pas. Le « = » est maintenant retiré à la saisie. Une action déjà enregistrée ainsi est réparée à l'ouverture du projet, et en marche.

Captures : `1117_04_variable_fx.png`, `1117_05_variable_aide_references.png`.

## 5. Le nombre de lignes de code

L'écran d'accueil (sous la version) et À propos donnent le nombre de lignes de code de l'application, par extension. Ce jour-là : 454 160 lignes dans 824 fichiers (.h 0, .hpp 58 982, .c 0, .cpp 395 178). Le compte se refait avant chaque livraison (`outils/compter_lignes.py`).

Captures : `1117_01_accueil_lignes_de_code.png`, `1117_02_a_propos_lignes_de_code.png`.

## Installer

1. Lancer `XPGAnalyser-Setup-1.11.7.exe`. Il met à jour la version installée : même identité d'installation, sauvegarde de l'ancienne version, données reprises sans être déplacées.
2. Windows SmartScreen peut avertir, parce que l'installateur n'est pas signé. Choisir « Informations complémentaires », puis « Exécuter quand même ».

Empreinte SHA-256 de `XPGAnalyser-Setup-1.11.7.exe` (14,6 Mo, 15 287 925 octets) :
`50BC59C1A9C4A321B071559A607A3A964C01575F65132B1FF538E65642B8D65B`

## Vérifications

**Tests Linux** (GCC 13) : la suite CTest complète passe (55 sur 55). Les nouveaux contrôles :

| Test | Ce qu'il vérifie | Sans le correctif |
|---|---|---|
| `blocageDelier1117` (hmi_editor_test) | Délier pendant que la liaison écrit vers un esclave lent : la boucle reprend en moins de 0,25 s ; arrêter la liaison n'attend que la requête en cours | 4,7 s de blocage |
| `forcageCommun1117` (hmi_editor_test) | forcer et libérer dans un onglet le fait dans l'autre ; le mouvement de l'esclave ; Variables IHM montre la valeur forcée tout de suite, puis relue sur la liaison ; un script qui écrit une variable forcée est ignoré | 8 échecs |
| `reperesArgumentsPopup1117` (hmi_test) | `IN_V := $V[2]$` : rien à revoir, rien à Générer ; en marche, la popup écrit V[2].CMD_OUV et lit V[2].OUV | 5 échecs |
| `variableFx1117` (hmi_editor_test) | la case Variable a la pastille fx et son type, comme la Condition ; « = » retiré ; l'aide propose la référence Vanne et ses membres ; un projet relu sans le « = » ; Paramètres de Popup sans « à revoir » | |
| `lignesDeCode1117` (hmi_test) | les quatre extensions, le total | |

**L'exe Windows**, compilé sous Linux avec MinGW-w64 et lancé sous Wine :
- `--version` répond `XPGAnalyser 1.11.7` ;
- `--cli MAST.XPG` analyse le projet d'essai (891 variables) ;
- la session `session-1117-forcage-commun.txt` a été rejouée sur une copie d'Armoire_Gaz : ce sont les 9 captures.

**L'installateur**, compilé par Inno Setup 6.4.1 sous Wine, puis installé en silencieux :
- il s'est installé en silencieux, pour tous les comptes : « Installation process succeeded » ;
- l'exe installé est identique à celui qui a été testé, et répond `1.11.7`.
- Seuls les raccourcis du menu Démarrer dont le nom a un accent n'ont pas pu être créés. C'est la limite de Wine déjà vue aux livraisons précédentes, sans rapport avec Windows.

**Pas encore fait :** ni l'exe ni l'installateur n'ont été lancés sur un vrai Windows. Merci de vérifier en ouvrant un projet, et en refaisant « Délier » pendant la simulation. En cas de souci, renvoyez-moi le rapport de blocage ou le résultat de `collect_diagnostics.bat`. Je garde l'exe 1.11.7 avec ses symboles pour lire un prochain rapport.

Les sources sont dans le dépôt GitHub, branche `claude/hopeful-goodall-h0srq0`, avec la session de captures `tools/sessions/session-1117-forcage-commun.txt` et son projet (`tools/sessions/preparer-projet-1117.cpp`, après celui de la 1.11.5).
