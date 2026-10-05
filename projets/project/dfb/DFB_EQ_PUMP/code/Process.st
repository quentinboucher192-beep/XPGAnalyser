n := Count;
IF n > LAST + 1 THEN n := LAST + 1; END_IF;
IF n < 0 THEN n := 0; END_IF;

FaultCount := 0;
ReadyCount := 0;

FOR i := 0 TO n - 1 DO

(* Ce qui est vrai de toute famille, ecrit une fois: l'acquittement sur front,
   la memorisation du defaut, le temps dans l'etat. *)
IF Eq[i].AckFault AND NOT Eq[i].PrevAck THEN
    Eq[i].FaultLatch := FALSE;
    Eq[i].FaultCode := 0;
    (* et redonne tout son delai a une nouvelle tentative : sans ca, le
       chrono du defaut continuait, et le defaut revenait des
       l'acquittement. *)
    Eq[i].FbkElapsed := 0;
    Eq[i].FlowElapsed := 0;
END_IF;
Eq[i].PrevAck := Eq[i].AckFault;

IF NOT Eq[i].Enable THEN
    (* Desactive: aucune sortie, aucun defaut. Un equipement consigne ne doit
       pas remplir le journal d'alarmes pendant qu'on travaille dessus. *)
    Eq[i].Fault := FALSE;
    Eq[i].FaultCode := 0;
    Eq[i].FaultLatch := FALSE;
END_IF;

req := Eq[i].RunReq;
IF Eq[i].ManMode THEN
    req := Eq[i].ManRunReq;
END_IF;

(* Le thermique est un contact NF: FALSE veut dire declenche. Le lire a l'envers
   est l'erreur qui fait tourner une machine avec sa protection ouverte. *)
IF NOT Eq[i].Thermal THEN
    Eq[i].FaultCode := 1;
    Eq[i].FaultLatch := TRUE;
END_IF;

(* Anti court-cycle. Un moteur redemarre trois fois par minute est un moteur
   qu'on remplace deux fois par an. *)
IF NOT Eq[i].Run THEN
    IF Eq[i].OffElapsed < MAXMS - CycleMs THEN
        Eq[i].OffElapsed := Eq[i].OffElapsed + CycleMs;
    ELSE
        Eq[i].OffElapsed := MAXMS;
    END_IF;
ELSE
    Eq[i].OffElapsed := 0;
END_IF;

allowed := Eq[i].Enable AND NOT Eq[i].FaultLatch;
IF Eq[i].MinOffMs > 0 AND NOT Eq[i].Run AND Eq[i].OffElapsed < Eq[i].MinOffMs THEN
    allowed := FALSE;
END_IF;

Eq[i].Run := allowed AND req;

IF Eq[i].HasFbk THEN
    IF Eq[i].Run AND NOT Eq[i].Fbk THEN
        IF Eq[i].FbkElapsed < MAXMS - CycleMs THEN
            Eq[i].FbkElapsed := Eq[i].FbkElapsed + CycleMs;
        ELSE
            Eq[i].FbkElapsed := MAXMS;
        END_IF;
        IF Eq[i].FbkElapsed > Eq[i].FbkMs THEN
            Eq[i].FaultCode := 2;
            Eq[i].FaultLatch := TRUE;
        END_IF;
    ELSE
        Eq[i].FbkElapsed := 0;
    END_IF;

    (* Un retour sans commande: le contacteur est colle. Le moteur tourne et
       personne ne l'a demande - c'est plus grave que l'inverse. *)
    IF NOT Eq[i].Run AND Eq[i].Fbk AND Eq[i].OffElapsed > Eq[i].FbkMs THEN
        Eq[i].FaultCode := 3;
        Eq[i].FaultLatch := TRUE;
    END_IF;

    Eq[i].Running := Eq[i].Run AND Eq[i].Fbk;
ELSE
    Eq[i].Running := Eq[i].Run;
END_IF;

IF Eq[i].Running AND NOT Eq[i].PrevRunning THEN
    Eq[i].Starts := Eq[i].Starts + 1;
END_IF;
IF Eq[i].Running THEN
    (* RunMs plafonne a 2 000 000 000 ms, soit 555 heures : au-dela il ne
       bouge plus, au lieu de devenir negatif. RunS compte les SECONDES et ne
       deborde pas avant 136 ans : c'est lui qu'il faut relayer a une rotation
       ou a l'IHM. *)
    IF Eq[i].RunMs < MAXMS - CycleMs THEN
        Eq[i].RunMs := Eq[i].RunMs + CycleMs;
    ELSE
        Eq[i].RunMs := MAXMS;
    END_IF;
    Eq[i].RunFracMs := Eq[i].RunFracMs + CycleMs;
    IF Eq[i].RunFracMs >= 1000 THEN
        Eq[i].RunS := Eq[i].RunS + DINT_TO_UDINT(Eq[i].RunFracMs / 1000);
        Eq[i].RunFracMs := Eq[i].RunFracMs MOD 1000;
    END_IF;
END_IF;
Eq[i].PrevRunning := Eq[i].Running;

(* Protections propres a une pompe. La marche a sec detruit une garniture en
   quelques minutes, donc elle coupe AVANT de compter le temps: pas de delai. *)
IF NOT Eq[i].SuctionOk AND Eq[i].Run THEN
    Eq[i].FaultCode := 4;
    Eq[i].FaultLatch := TRUE;
END_IF;

IF Eq[i].MinFlow > 0.0 AND Eq[i].Running THEN
    IF Eq[i].Flow < Eq[i].MinFlow THEN
        IF Eq[i].FlowElapsed < MAXMS - CycleMs THEN
            Eq[i].FlowElapsed := Eq[i].FlowElapsed + CycleMs;
        ELSE
            Eq[i].FlowElapsed := MAXMS;
        END_IF;
        IF Eq[i].FlowElapsed > Eq[i].FlowDelayMs THEN
            Eq[i].FaultCode := 5;
            Eq[i].FaultLatch := TRUE;
        END_IF;
    ELSE
        Eq[i].FlowElapsed := 0;
    END_IF;
ELSE
    Eq[i].FlowElapsed := 0;
END_IF;

IF Eq[i].MaxPressure > 0.0 AND Eq[i].Pressure > Eq[i].MaxPressure THEN
    Eq[i].FaultCode := 6;
    Eq[i].FaultLatch := TRUE;
END_IF;


(* Fault est la copie PUBLIEE de FaultLatch, posee en fin de cycle.
   Les sorties, elles, se coupent sur FaultLatch - c'est-a-dire dans le meme
   cycle que le defaut. Les couper sur Fault laissait l'equipement commande un
   cycle de plus : vingt millisecondes pendant lesquelles une vanne reste
   pilotee alors qu'on vient de decouvrir qu'on ne sait pas ou elle est. *)

(* Le defaut est MEMORISE. Un thermique qui se rearme tout seul en refroidissant
   effacerait la trace de son declenchement, et personne ne saurait pourquoi la
   machine s'est arretee cette nuit-la. *)
(* Un equipement desactive ne publie pas de defaut, meme si la condition est
   toujours la. Le prologue efface la memorisation, mais le corps la repose au
   meme cycle : c'est ici qu'il faut le dire, une fois, plutot que d'entourer
   chaque famille d'un IF Enable. *)
(* UN DEFAUT COUPE LA SORTIE DANS LE CYCLE MEME ou il apparait. Ceux qui se
   detectent APRES le calcul de la commande - retour absent, marche a sec,
   debit, pression, ecart - la laissaient un cycle de plus. *)
IF Eq[i].FaultLatch THEN
    Eq[i].Run := FALSE;
    Eq[i].Running := FALSE;
END_IF;

Eq[i].Fault := Eq[i].FaultLatch AND Eq[i].Enable;
Eq[i].Ready := Eq[i].Enable AND NOT Eq[i].Fault;

(* L'ETAT PUBLIE, et le temps passe dedans. StateMs n'etait jamais remis a
   zero - il comptait depuis la mise en route du bloc - et debordait au bout de
   24,8 jours. Il repart maintenant a chaque changement d'etat, et plafonne. *)
IF NOT Eq[i].Enable THEN
    st := 0;
ELSIF Eq[i].Fault THEN
    st := 1;
ELSIF Eq[i].Running THEN
    st := 3;
ELSE
    st := 2;
END_IF;
IF st <> Eq[i].PrevState THEN
    Eq[i].StateMs := 0;
    Eq[i].PrevState := st;
ELSIF Eq[i].StateMs < MAXMS - CycleMs THEN
    Eq[i].StateMs := Eq[i].StateMs + CycleMs;
ELSE
    Eq[i].StateMs := MAXMS;
END_IF;
Eq[i].State := st;

IF Eq[i].Fault THEN
    FaultCount := FaultCount + 1;
END_IF;
IF Eq[i].Ready THEN
    ReadyCount := ReadyCount + 1;
END_IF;

END_FOR;
