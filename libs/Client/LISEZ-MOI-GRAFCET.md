# Le moteur GRAFCET : les bugs trouves, les corrections, ce que ca change pour MAST

DFB_GRAFCETENGINE 1.48 -> 1.49, BUILDING 0.23 -> 0.24, ST_GC_DebugStepFocus
0.13 -> 0.14, RTCDECODED 0.11 -> 0.12. Les autres DDT (ST_GC_Step,
ST_GC_Transition, ST_GC_Action...) ne changent pas : seule leur aide a ete
reecrite.

**L'interface ne change pas.** Memes entrees, memes sorties, memes tableaux :
aucun appel a modifier dans tes programmes. Le seul champ ajoute
(`Kind0Line`) l'est EN FIN de ST_GC_DebugStepFocus.

Ce fichier est en ASCII, comme tout `libs/`.

---

## 1. Comment le moteur tourne - et ta pause d'un cycle

Trois sections (Init, Main, Debug), trois phases par cycle (scruter les
transitions, figer celles qui sont franchissables, les franchir), puis les
fronts d'etape et les actions.

### Ton "waitForNextCycle" : pauseToReevaluateCondition

Oui, je l'ai vu, et c'est le coeur du moteur. Le probleme qu'il regle :

- tes receptivites (`Trans[k].Condition`) et tes conditions d'action
  (`Acts[k].EnableCond`) sont calculees par ton programme AVANT l'appel du
  moteur, donc sur la situation du cycle d'avant ;
- quand une transition est franchie, les actions de la nouvelle etape sont
  remises a zero (Started, Finished...). Une condition comme
  `Acts[1].EnableCond := Acts[0].Started AND Acts[0].Finished` a ete calculee
  AVANT cette remise a zero : elle est perimee. Si les actions tournaient tout
  de suite, un K11 partirait sur une condition fausse.

Ta reponse : apres un franchissement, un cycle de pause. Au cycle suivant, le
moteur ne scrute pas les transitions, il calcule seulement les fronts et les
actions de la nouvelle situation - avec des conditions que ton programme vient
de recalculer. Consequence voulue : chaque etape activee agit au moins un
cycle, et un franchissement prend deux cycles.

**En 1.49, je l'ai garde et generalise.** Tout changement de situation arme la
pause : franchissement, demarrage, InitReq, ResetAllReq, ResetInitialReq,
forcages. C'est ce qui corrige G03 et G04 plus bas. Et la pause n'empeche plus
le moteur de lire ses commandes (G02).

### Le rythme, cycle par cycle

    demarrage      cycle 1 : etapes initiales activees (rien d'autre)
                   cycle 2 : leurs actions tournent (la pause)
                   cycle 3 : premiere scrutation des transitions
    franchissement cycle n   : la transition est franchie ; les sorties
                               restent celles du cycle d'avant
                   cycle n+1 : la pause - fronts et actions de la nouvelle
                               situation, pas de scrutation
                   cycle n+2 : scrutation normale

Les sorties d'une etape quittee tombent au cycle n+1, quand montent celles de
l'etape suivante : pas de trou entre deux etapes qui commandent la meme sortie.

---

## 2. Les bugs trouves, un par un

Chaque bug a son essai dans `tests/moteurgrafcet_test.cpp`, sous le meme numero.
Lance sur la 1.48, l'essai echoue : c'est la preuve. Sur la 1.49, il passe.

### A. L'evolution du grafcet

**G01 - Deux franchissements au meme cycle dependaient de l'ordre du tableau.**
X1 et X2 actives, Ta : X1 -> X2 et Tb : X2 -> X3 franchissables ensemble.
Selon que Ta est avant ou apres Tb dans le tableau, X2 restait active ou
disparaissait. Le commit desactivait les sources et activait les destinations
transition par transition. Regle 5 du GRAFCET : une etape desactivee et
activee au meme cycle reste active. *Correction* : toutes les sources
desactivees, PUIS toutes les destinations activees ; une etape qui reste
active n'est pas relancee.

**G02 - Une commande qui arrive pendant le cycle de pause etait perdue.**
Toute la section Init etait sautee pendant la pause : InitReq, ResetAllReq,
forcages, StepReq n'etaient pas lus, et ton IHM les remet a zero apres
l'appel. Un InitReq tombant juste apres un franchissement ne faisait rien.
*Correction* : les commandes sont lues a chaque appel. Pendant le gel et le
pas a pas aussi pour les forcages : ils ne sont plus perdus.

**G03 - InitReq dans la situation initiale franchissait sur des receptivites
perimees.**
X0 attend `Acts[0].Started AND Acts[0].Finished AND X`. La purge de X0 est
finie, X arrive en meme temps qu'InitReq : InitReq remettait la purge a zero,
mais la transition etait scrutee au meme cycle avec la receptivite calculee
avant (purge finie) et partait tout de suite, sans refaire la purge.
*Correction* : InitReq arme la pause.

**G04 - Une etape initiale quittee aussitot ne faisait jamais ses actions au
demarrage.**
Au cycle 2, la transition sortante etait franchie avant que les actions de
l'etape initiale aient tourne une seule fois. Dans MAST, c'est `RAZ CPT` de
Surveillance (X0 -> X1 sur TRUE). *Correction* : le demarrage arme la pause.

**G14 - ForceNextReq et ForceBackReq relancaient les actions de TOUTES les
etapes actives.**
Le commentaire disait "uniquement si action appartient a une etape
nouvellement activee", le code prenait toutes les etapes actives : une branche
parallele voyait ses impulsions repartir et ses actions limitees se bloquer.
*Correction* : seules les etapes nouvellement activees repartent.

**G15 - Un forcage sur un moteur fini ne le relancait pas.**
L'etape forcee s'activait, mais Finished restait vrai : le moteur ne tournait
plus. *Correction* : un forcage remet Finished a FALSE.

**G22 - Une etape finale sans transition de sortie ne mettait jamais
Finished.**
Seule une transition SORTANT d'une etape finale le mettait. Un grafcet fils
qui s'arrete sur son etape finale laissait le pere attendre pour toujours.
*Correction* : une etape finale active sans aucune transition de sortie met
aussi Finished. Tes grafcets (etape finale -> X0 sur TRUE) ne changent pas.

**G24 - ResetInitialReq relancait les etapes initiales deja actives.**
Leurs actions repartaient de zero alors que l'etape continuait : une action
limitee (K4) deja finie ne repartait plus jamais (son temps d'etape etait deja
depasse). *Correction* : une etape initiale deja active continue, ses actions
aussi.

### B. Les actions

**G05 - Les impulsions duraient plus d'un cycle.**
Dans tous les cycles ou le moteur ne recalcule pas les actions (franchissement,
gel, pas a pas, fini, forcage), les sorties gardaient leur valeur. Un K1 d'une
etape quittee aussitot sortait DEUX cycles (un `NB++` compterait deux fois ;
dans MAST, `NB=0` de Pompage X5 s'execute deux fois). Apres Finished, le K1 de
l'etape d'arrivee restait vrai indefiniment. Rising et Falling aussi.
*Correction* : impulsions (K1, K3, K5, K6, K9), Rising et Falling ne durent
qu'un cycle, quoi qu'il arrive.

**G07 - K4 (LIMITEDON) avec un retard nul ou court ne sortait jamais.**
`Out := Active AND ActiveTime <= Delay` : le temps d'etape valait deja un
cycle au premier cycle visible. Avec Delay = 0 (ou moins d'un cycle), jamais
de sortie, donc jamais Started, donc une transition qui attend
`Started AND Finished` restait bloquee. **Dans MAST, un DLPx a 0 dans la
configuration bloque Pompage ou Purge a l'etape correspondante.**
*Correction* : une action limitee sort au moins le cycle d'activation.

**G08 - K11 (CONDLIMITEDONTP) avec un retard nul : meme blocage.**
L'impulsion se terminait avant d'avoir ete sortie. `OPPE` de Pompage X1 avec
DLP1 = 0 : bloque. *Correction* : au moins un cycle.

**G09 - K10 (CONDLIMITEDON) ressortait indefiniment apres sa limite ; retard
nul : blocage.**
Une fois Finished, le temps etait remis a zero a chaque cycle, et
`TonElapsed <= Delay` redevenait vrai : la sortie revenait et restait.
*Correction* : plus de sortie une fois finie ; au moins un cycle.

**G10 - K9 (CONDDELAYPULSE) avec un retard nul sortait a chaque cycle.**
Meme mecanisme : le drapeau "impulsion faite" etait efface une fois l'action
finie. *Correction* : une impulsion par activation.

**G11 - K11 : un second front de la condition collait la sortie.**
Apres une premiere impulsion finie, un nouveau front relancait le TP, mais
Finished bloquait son decompte : sortie vraie pour toujours, meme apres avoir
quitte l'etape. *Correction* : une impulsion par activation.

**G12 - K6 (PULSEOFF) et K7 (LIMITEDOFF) sortaient pour des etapes jamais
actives.**
Au demarrage, et apres InitReq ou un retour a la situation initiale, chaque
action K6/K7 d'une etape inactive partait : une impulsion, ou Delay de sortie.
Une vanne "a garder ouverte 5 s apres l'etape" s'ouvrait 5 s a la mise sous
tension. *Correction* : K6 et K7 ne s'arment que par une activite reelle de
l'etape.

**G13 - Le retour a la situation initiale coupait et relancait des actions.**
Le moteur remettait a zero toutes les actions (sauf les K1 des etapes
initiales, un premier palliatif) et faisait RETURN : les actions continues de
l'etape initiale tombaient un cycle, un K3 initial partait deux fois, les K6
se rearmaient (deuxieme impulsion), les K11 en cours etaient coupes, et un
cycle etait perdu. *Correction* : seul le statut (Started, Finished) des
actions au repos des etapes inactives est efface, sans RETURN.

### C. Le temps

**G18 - Les temps repassaient par 0 au bout de 49,7 jours.**
TIME est sur 32 bits. Une etape active depuis 49,7 jours (X0 qui attend un
depart) voyait son ActiveTime repartir de 0 : un K4 de X0 ressortait. Une
etape inactive depuis 49,7 jours faisait ressortir son K7 : une vanne qui se
rouvre toute seule. *Correction* : tous les temps butent a 24 jours.

**G25 - Le temps d'une etape comptait un cycle de trop.**
Au cycle de pause, le moteur ajoutait la duree du cycle PRECEDENT
l'activation, puis deux cycles au suivant : ActiveTime valait 20, 60, 80 ms
au lieu de 20, 40, 60. Une temporisation d'etape finissait un cycle trop tot,
une action limitee durait un cycle de moins. La receptivite temporisee
comptait son premier cycle comme deja ecoule. *Correction* : les temps partent
de l'evenement (activation ; receptivite ou condition vue vraie, comme un TON).
L'ecart est de 20 ms : invisible sur tes temporisations en secondes.

**G26 - TsNow qui recule validait d'un coup toutes les temporisations.**
TsNow - TsPrev en TIME deborde : si TsSys est remis a zero (ton `%S21`), la
duree du cycle vaut 49 jours et toutes les receptivites temporisees et tous
les DELAYON partent. *Correction* : un ecart de plus de 10 s entre deux appels
compte pour 0 (le temps s'arrete, comme fige).

**G27 - Un TsNow qui n'avance pas ne se voyait pas.**
Une constante passee a TsNow (l'exemple de l'aide le faisait !) laisse toutes
les temporisations a zero, sans rien dire. *Correction* :
`Debug.WatchdogDetected` apres 100 appels sans que le temps avance.

**RTCDECODED 0.12 - CycleMs sautait a chaque remise a l'heure.**
CycleMs est calcule sur la milliseconde du jour. Au premier appel il valait
l'heure du jour entiere ; au changement d'heure ete/hiver, a une
synchronisation NTP ou a un reglage manuel, il sautait d'une heure ou de pres
de 24 h. Cumule dans TsSys, ce saut validait toutes les temporisations de tes
grafcets en pleine sequence. *Correction* : 0 au premier appel, et un ecart de
plus de 10 s compte pour 0. (Ce bloc appelle RRTC_DT_MS, que le simulateur ne
connait pas : cette correction n'est pas essayee en simulation.)

### D. La robustesse

**G16 - Un defaut de configuration restait jusqu'au redemarrage a froid.**
ConfigChecked ne repassait jamais a FALSE : une configuration corrigee n'etait
pas revue, et InitReq n'etait meme pas lu (il est apres le RETURN du defaut).
*Correction* : la configuration est revue a InitReq, ResetAllReq,
ResetInitialReq et a la reactivation.

**G17 - Des tailles au-dela des tableaux faisaient lire et ecrire hors
tableau.**
GC_MAX_STEPS a 30, GC_MAX_HIST a 12, SourceCount a 5 : acces hors des
tableaux. *Correction* : tailles ramenees a 28 et 8, comptes bornes a 3,
ConfigFault pour un compte hors de 1..3 ou un Kind hors de 0..11 ; une
transition sans source ou sans destination n'est jamais franchie.

**G19 - Les compteurs debordaient.**
InitPulseCount (INT, au bout de 32767 retours a l'etat initial),
Runtime.Cycle et Debug.TotalTransitionsFired (DINT). *Correction* : ils
repassent a 0.

### E. La mise au point

**G06 - L'historique notait des doublons, et une seule etape quand plusieurs
s'activaient ensemble.**
Rising restait vrai dans le cycle du franchissement suivant : X1 etait notee
deux fois. *Correction* : une ligne par etape activee, sans doublon.

**G20 - Le focus prenait la DERNIERE etape active (le commentaire dit la
premiere), les actions continues n'y figuraient pas, les lignes K2, K4, K6 a
K11 portaient un ')' parasite et debordaient leurs 32 caracteres.**
*Correction* : premiere etape active, `Kind0Line` (en fin de
ST_GC_DebugStepFocus), lignes propres qui gardent ce qui tient.

**G21 - Debug.ConflictDetected n'etait jamais positionne.**
Deux transitions d'une meme etape franchissables au meme cycle (receptivites
non exclusives sur une divergence en OU) activent les DEUX branches, en
silence. *Correction* : ConflictDetected, garde jusqu'a InitReq.

### F. BUILDING

**B1 - Plus de 3 sources ou destinations ecrivaient hors du tableau.**
`d=1,2,3,4` ecrivait `Destinations[3]`. *Correction* : les 3 premieres sont
gardees, le compte dit le vrai nombre, le moteur passe en ConfigFault.

**B2 - La divergence en ET etait impossible.**
BUILDING mettait toujours SplitKind a 0 : `d=1,2` n'activait que la premiere
destination. *Correction* : SplitKind a 1 des deux destinations.

**B3 - Une action sans `s=` etait liee a l'etape 0.**
BoundStepId gardait sa valeur (0 par defaut). *Correction* : -1, aucune etape.

**B4 - Un nom trop long etait ignore en silence.**
Dans MAST, `n=NB=FV9OV4NB++` et `c=PT1>=SPPE6` : nom et texte restaient
vides dans la mise au point. *Correction* : coupes a 4 / 8 caracteres.

**B5 - Les espaces cassaient la lecture.** `id=0 | n=X0 | i=1` : aucune cle
reconnue. *Correction* : espaces toleres autour des cles.

**B6 - Ce que le texte ne disait pas gardait la valeur du texte precedent.**
*Correction* : le texte decrit tout l'element.

**B7 - Textes tordus** (vides, `|||`, `s=,,,`, 9 sources, cle sans valeur) :
verifie, ni blocage ni sortie de tableau.

### Garde tel quel, volontairement

- **ResetOnFall** reste sans effet : une receptivite qui retombe remet
  toujours sa temporisation a zero. "Honorer" le champ changerait toutes tes
  transitions (il vaut FALSE par defaut).
- **SplitKind 0** avec plusieurs destinations active toujours la premiere
  seule (BUILDING ne produit plus ce cas).
- **Divergence en OU** : toutes les transitions franchissables partent
  (regle du GRAFCET), la ou Control Expert donne la priorite a la plus a
  gauche. ConflictDetected le signale.
- **Une auto-boucle** (`s=3|d=3`) ne relance pas l'etape (regle 5). Pour
  relancer une etape : `ForceStepFromId = ForceStepToId`.

---

## 3. Ce que ca change pour MAST

### Ce que tu verras de different

- Un cycle de plus au demarrage (la pause, G04).
- Les temporisations d'etape exactes : un cycle (20 ms) plus tard qu'avant.
- Un InitReq, un reset ou un forcage prennent un cycle de pause.
- Les forcages marchent aussi en pas a pas et fige.
- Un DLPx a 0 ne bloque plus.

Rien d'autre : l'essai R1 fait tourner une chaine a la maniere de Pompage
(K4, K11 enchaines, NB++, etape finale, relance par InitReq) sur les deux
versions, avec le meme resultat.

### Ce qui est a corriger dans le projet MAST lui-meme

**M1 - Le forcage direct depuis l'IHM ne marche jamais.** Dans chaque section
SFC, `Ctrl_X.Cmd.ForceStepFromId := -1;` et `ForceStepToId := -1;` sont
ecrits AVANT l'appel du moteur : la demande de l'IHM est effacee avant d'etre
lue. A deplacer APRES l'appel, avec les autres remises a zero
d'impulsions (17 sections).

**M2 - 915 appels de Builder a chaque cycle.** Chaque appel analyse un texte
(des dizaines d'instructions sur chaines). En simulation, construire a chaque
cycle coute autant que le moteur lui-meme. A construire une fois :

    IF NOT Construit_PompageA OR modification_majeure THEN
        Builder(...); ...
        Construit_PompageA := TRUE;
    END_IF;

(les 58 retards qui viennent de `config_utilisee` se recalculent quand la
configuration change). BUILDING 0.24 fait un peu plus de travail par appel
que la 0.23 (espaces, bornes) : raison de plus.

**M3 - La garde `Steps_X[k].ActiveTime >= t#1s` du grafcet pere.**
Elle est la parce que le fils ne lit son InitReq qu'a son propre appel,
APRES la scrutation du pere : pendant un cycle, `Gc_Fils.Finished` est encore
celui du tour precedent. La garde exacte, sans attendre une seconde :

    Trans_ChangementA[2].Condition := Gc_DetoxalA.Finished
                                      AND NOT Ctrl_DetoxalA.Cmd.InitReq ...

(InitReq est encore vrai tant que le fils ne l'a pas lu.) La seconde marche
aussi ; elle ne se justifie plus.

**M4 - TsSys depend de l'horloge.** `TsSys := TsSys + UDINT_TO_TIME(Rtc.Out.CycleMs)`
: avec RTCDECODED 0.12 les sauts d'horloge ne passent plus. Plus simple
encore : le `DtMs` de DFB_SYS_CLOCK, qui ecarte deja les reprises.

**M5 - Des noms coupes dans la mise au point** (B4) : `NB=FV9OV4NB++` devient
`NB=FV9OV`. A raccourcir si tu veux les lire en entier.

---

## 4. Les essais

    moteurgrafcet_test <dossier libs> [--graine N] [--tours N] [--trace N]

- **Un scenario par bug** (G01 a G27, B1 a B7), plus M1 (l'usage MAST) et R1,
  R3 (non-regression). Le bilan final donne passe / ECHEC par numero.
- **Le hasard (F1)** : des grafcets tires au sort (jusqu'a 28 etapes,
  divergences et convergences en ET, retours, auto-boucles, etapes finales,
  receptivites temporisees), des receptivites, des commandes, des gels, du pas
  a pas et des coupures tires au sort a chaque cycle. La situation est
  comparee a chaque cycle a un modele de reference ecrit en C++ d'apres les
  regles ci-dessus, et les actions a des invariants (une impulsion ne dure
  qu'un cycle, K6/K7 seulement apres une activite reelle...).
- **Les configurations fausses (F2)** : tailles, comptes, numeros d'etape et
  Kind aberrants : aucune lecture ni ecriture hors tableau.

Resultat sur la 1.48 / 0.23 : 35 scenarios en echec sur 38 (les trois qui
passent sont les essais de non-regression et d'usage), et le modele diverge
sur tous les grafcets tires. Sur la 1.49 / 0.24 : les 38 passent, et plus de
200 000 cycles tires au sort sans ecart.

**Le simulateur lui-meme a ete durci** (`src/sim/Runtime.cpp`) : une ECRITURE
hors des bornes d'un tableau creait la case en silence (comme une variable de
boucle). Le bug B1 passait donc inapercu. Elle arrete maintenant le cycle et
le dit.

---

## 5. Generer un grafcet sans l'ecrire a la main

Trois macros de `libs/Macros` ecrivent tout ce qui est autour du moteur :
la construction (une fois, gardee par une signature), les receptivites
AVANT l'appel, l'appel avec toutes les commandes de `Ctrl_x`, l'etat pour
l'IHM, les sorties des actions, et les remises a zero APRES l'appel (M1 est
donc regle d'office).

- **ImporterGrafcet** : les grafcets decrits dans le classeur (onglets
  Grafcet Etapes, Grafcet Transitions, Grafcet Actions, Grafcets). Une
  sous-routine `Grafcet_<nom>` par grafcet. Raccourcis dans les
  receptivites : `FIN(A2)`, `ACTIF(X3)`, `DUREE(X3) >= T#5s`, `FINI(Autre)`
  (qui ecrit la garde exacte de M3).
- **GenererGrafcet** : le squelette d'un grafcet sans classeur (des etapes en
  sequence, receptivites a FALSE), a completer a la main.
- **PreparerGrafcets** : la bibliotheque, `GC_Builder`, `GC_Temps` (le TsNow
  de tous les moteurs, qui avance de `Horloge.DtMs` : M4) et la section
  `Grafcets` qui appelle chaque sous-routine.

Le temps de chaque grafcet vient de `GC_Temps`, un seul pour tous : deux
grafcets qui se regardent voient le meme temps.

Exemples : `excel/lus-par-l-outil/02-grafcet-seul.xlsm` (une cuve : ET, OU,
temporisations, actions continue, retardee, limitee, a la desactivation,
conditionnelle) et `01-station-complete.xlsm`
(deux grafcets qui se lancent l'un l'autre avec INIT et FINI). Les deux ont
tourne en simulation.

