SectionsAuthorized := TRUE;
ModeReqOut := ModeReq;

(* =========================================================
   0. LES TAILLES : jamais au-dela des tableaux
   ========================================================= *)
IF Runtime.GC_MAX_STEPS > 28 THEN Runtime.GC_MAX_STEPS := 28; END_IF;
IF Runtime.GC_MAX_STEPS < 0 THEN Runtime.GC_MAX_STEPS := 0; END_IF;
IF Runtime.GC_MAX_TRANSITIONS > 28 THEN Runtime.GC_MAX_TRANSITIONS := 28; END_IF;
IF Runtime.GC_MAX_TRANSITIONS < 0 THEN Runtime.GC_MAX_TRANSITIONS := 0; END_IF;
IF Runtime.GC_MAX_ACTIONS > 28 THEN Runtime.GC_MAX_ACTIONS := 28; END_IF;
IF Runtime.GC_MAX_ACTIONS < 0 THEN Runtime.GC_MAX_ACTIONS := 0; END_IF;
IF Runtime.GC_MAX_HIST > 8 THEN Runtime.GC_MAX_HIST := 8; END_IF;
IF Runtime.GC_MAX_HIST < 0 THEN Runtime.GC_MAX_HIST := 0; END_IF;

(* =========================================================
   1. MOTEUR DESACTIVE : tout a zero
   ========================================================= *)
IF NOT Enable THEN

    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        Steps[i].Active := FALSE;
        Steps[i].PrevActive := FALSE;
        Steps[i].Rising := FALSE;
        Steps[i].Falling := FALSE;
        Steps[i].ActiveTime := T#0ms;
        Steps[i].InactiveTime := T#0ms;
    END_FOR;

    FOR i := 0 TO Runtime.GC_MAX_TRANSITIONS - 1 DO
        Transitions[i].Validated := FALSE;
        Transitions[i].SnapshotValid := FALSE;
        Transitions[i].TonStarted := FALSE;
        Transitions[i].TonElapsed := T#0ms;
        Transitions[i].TimedConditionValid := FALSE;
    END_FOR;

    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO
        Actions[aIdx].Started := FALSE;
        Actions[aIdx].Finished := FALSE;
        Actions[aIdx].Out := FALSE;
        Actions[aIdx].PrevOut := FALSE;
        Actions[aIdx].OnPulseDone := FALSE;
        Actions[aIdx].OffPulseDone := TRUE;     (* K6/K7 desarmees *)
        Actions[aIdx].EnableDone := FALSE;
        Actions[aIdx].EnablePulseDone := FALSE;
        Actions[aIdx].TonStarted := FALSE;
        Actions[aIdx].TonElapsed := T#0ms;
        Actions[aIdx].PrevEnableCond := FALSE;
    END_FOR;

    Finished := FALSE;
    InitPulse := FALSE;
    PrevInitState := FALSE;
    finalSrcOnValidatedTr := FALSE;
    InternalInitDone := FALSE;   (* tres important *)
    pauseToReevaluateCondition := FALSE;
    NeedToResetPause := FALSE;
    ConfigChecked := FALSE;      (* revue au retour *)
    dtScan := T#0ms;
    dtActs := T#0ms;
    Debug.ActiveStepCount := 0;
    Debug.ConflictDetected := FALSE;

    SectionsAuthorized := FALSE;
    RETURN;
END_IF;

(* =========================================================
   2. CE QUI NE DURE QU'UN CYCLE
   Les fronts d'etape et les impulsions d'action ne valent que pour le
   cycle ou ils sont calcules. Un cycle ou le moteur ne recalcule pas les
   actions (franchissement, pause, gel, pas a pas, fini) les laisse a FALSE,
   au lieu de les garder vrais : une impulsion collee pendant un gel
   comptait a chaque cycle.
   ========================================================= *)
changedNow := FALSE;
resetAll := FALSE;
forced := FALSE;

FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
    Steps[i].Rising := FALSE;
    Steps[i].Falling := FALSE;
    resetStep[i] := FALSE;
END_FOR;

FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO
    Actions[aIdx].PrevOut := Actions[aIdx].Out;
    IF (Actions[aIdx].Kind = 1) OR (Actions[aIdx].Kind = 3) OR (Actions[aIdx].Kind = 5)
       OR (Actions[aIdx].Kind = 6) OR (Actions[aIdx].Kind = 9) THEN
        Actions[aIdx].Out := FALSE;
    END_IF;
END_FOR;

(* =========================================================
   3. LE TEMPS : la duree depuis l'appel precedent
   ========================================================= *)
IF NOT InternalInitDone THEN
    Runtime.TsNow := TsNow;      (* premier cycle : duree nulle *)
END_IF;
Runtime.Mode := ModeReq;
Runtime.TsPrev := Runtime.TsNow;
Runtime.TsNow := TsNow;
Runtime.NtpcNow := NtpcNow;
Runtime.DateTime := Runtime.TsNow - Runtime.TsPrev;
IF Runtime.DateTime > T#10s THEN
    (* plus de 10 s entre deux appels : TsNow a recule (TsSys remis a zero) ou
       l'appel a ete suspendu. Le temps du moteur s'arrete, comme fige : une
       receptivite temporisee ne se valide pas d'un coup. *)
    Runtime.DateTime := T#0ms;
END_IF;
(* le temps doit avancer : TsNow fige (une constante passee a TsNow) laisse
   toutes les temporisations a zero. Signale apres 100 appels. *)
IF Runtime.DateTime = T#0ms THEN
    IF frozenTimeCount < 100 THEN
        frozenTimeCount := frozenTimeCount + 1;
    ELSE
        Debug.WatchdogDetected := TRUE;
    END_IF;
ELSE
    frozenTimeCount := 0;
    Debug.WatchdogDetected := FALSE;
END_IF;
IF Runtime.Cycle < 2147483647 THEN
    Runtime.Cycle := Runtime.Cycle + 1;
ELSE
    Runtime.Cycle := 0;
END_IF;

(* =========================================================
   4. PREMIER CYCLE : toutes les etapes initiales
   ========================================================= *)
IF NOT InternalInitDone THEN
    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        Steps[i].Active := Steps[i].IsInitial;
        Steps[i].PrevActive := FALSE;
        Steps[i].ActiveTime := T#0ms;
        Steps[i].InactiveTime := T#0ms;
    END_FOR;
    resetAll := TRUE;
    changedNow := TRUE;
    InternalInitDone := TRUE;
    Finished := FALSE;
END_IF;

(* =========================================================
   5. LA CONFIGURATION : revue au premier cycle, a InitReq,
   ResetAllReq, ResetInitialReq et a la reactivation
   ========================================================= *)
IF InitReq OR ResetAllReq OR ResetInitialReq THEN
    ConfigChecked := FALSE;
END_IF;

IF NOT ConfigChecked THEN
    Debug.ConfigFault := FALSE;

    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        IF Steps[i].Id < 0 THEN
            Debug.ConfigFault := TRUE;
        END_IF;
    END_FOR;

    FOR i := 0 TO Runtime.GC_MAX_TRANSITIONS - 1 DO
        IF Transitions[i].Enabled THEN
            IF (Transitions[i].SourceCount < 1) OR (Transitions[i].DestCount < 1)
               OR (Transitions[i].SourceCount > 3) OR (Transitions[i].DestCount > 3) THEN
                Debug.ConfigFault := TRUE;
            END_IF;
            nSrc := Transitions[i].SourceCount;
            IF nSrc > 3 THEN nSrc := 3; END_IF;
            FOR j := 0 TO nSrc - 1 DO
                srcId := Transitions[i].Sources[j];
                IF (srcId < 0) OR (srcId >= Runtime.GC_MAX_STEPS) THEN
                    Debug.ConfigFault := TRUE;
                END_IF;
            END_FOR;
            nDst := Transitions[i].DestCount;
            IF nDst > 3 THEN nDst := 3; END_IF;
            FOR j := 0 TO nDst - 1 DO
                dstId := Transitions[i].Destinations[j];
                IF (dstId < 0) OR (dstId >= Runtime.GC_MAX_STEPS) THEN
                    Debug.ConfigFault := TRUE;
                END_IF;
            END_FOR;
        END_IF;
    END_FOR;

    FOR i := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO
        sid := Actions[i].BoundStepId;
        IF (sid < -1) OR (sid >= Runtime.GC_MAX_STEPS) THEN
            Debug.ConfigFault := TRUE;
        END_IF;
        IF (Actions[i].Kind < 0) OR (Actions[i].Kind > 11) THEN
            Debug.ConfigFault := TRUE;
        END_IF;
    END_FOR;

    ConfigChecked := TRUE;
END_IF;

IF Debug.ConfigFault THEN
    SectionsAuthorized := FALSE;
    RETURN;
END_IF;

(* =========================================================
   6. LE TEMPS DES ETAPES, sur la situation de l'intervalle ecoule
   (avant les changements de cet appel). Pas quand le moteur est fige,
   en pas a pas ou fini - sauf la pause d'un cycle deja armee, qui passe
   toujours.
   ========================================================= *)
held := (FreezeReq OR PauseReq OR ((Runtime.Mode = 2) AND NOT StepReq) OR Finished)
        AND NOT pauseToReevaluateCondition;

IF NOT held THEN
    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        IF Steps[i].Active THEN
            IF Steps[i].ActiveTime < T#24d - Runtime.DateTime THEN
                Steps[i].ActiveTime := Steps[i].ActiveTime + Runtime.DateTime;
            ELSE
                Steps[i].ActiveTime := T#24d;    (* bute : ne repasse pas par 0 *)
            END_IF;
            Steps[i].InactiveTime := T#0ms;
        ELSE
            IF Steps[i].InactiveTime < T#24d - Runtime.DateTime THEN
                Steps[i].InactiveTime := Steps[i].InactiveTime + Runtime.DateTime;
            ELSE
                Steps[i].InactiveTime := T#24d;
            END_IF;
            Steps[i].ActiveTime := T#0ms;
        END_IF;
    END_FOR;
    IF dtScan < T#24d - Runtime.DateTime THEN dtScan := dtScan + Runtime.DateTime; END_IF;
    IF dtActs < T#24d - Runtime.DateTime THEN dtActs := dtActs + Runtime.DateTime; END_IF;
END_IF;

(* =========================================================
   7. LES COMMANDES : prises a chaque appel, meme pendant la pause
   ========================================================= *)
IF InitReq OR ResetAllReq THEN
    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        Steps[i].Active := Steps[i].IsInitial;
        Steps[i].PrevActive := FALSE;
        Steps[i].ActiveTime := T#0ms;
    END_FOR;
    resetAll := TRUE;
    changedNow := TRUE;
    Finished := FALSE;
    Debug.ConflictDetected := FALSE;
END_IF;

IF ResetInitialReq THEN
    (* retour a la situation initiale ; une etape initiale deja active
       continue, ses actions ne repartent pas *)
    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        resetStep[i] := NOT (Steps[i].IsInitial AND Steps[i].Active);
        Steps[i].Active := Steps[i].IsInitial;
    END_FOR;
    changedNow := TRUE;
    Finished := FALSE;
END_IF;

(* =========================================================
   8. LES FORCAGES : direct, puis retour, puis avance. Un forcage relance
   un moteur fini. Seules les etapes nouvellement activees repartent de
   zero.
   ========================================================= *)
IF (ForceStepFromId >= 0) AND (ForceStepToId >= 0) AND
   (ForceStepFromId < Runtime.GC_MAX_STEPS) AND (ForceStepToId < Runtime.GC_MAX_STEPS) THEN

    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        wasActive[i] := Steps[i].Active;
    END_FOR;
    Steps[ForceStepFromId].Active := FALSE;
    Steps[ForceStepToId].Active := TRUE;
    IF (ForceStepFromId = ForceStepToId) OR NOT wasActive[ForceStepToId] THEN
        (* From = To : l'etape est relancee *)
        resetStep[ForceStepToId] := TRUE;
        Steps[ForceStepToId].PrevActive := FALSE;
        Steps[ForceStepToId].ActiveTime := T#0ms;
    END_IF;
    forced := TRUE;

ELSIF ForceBackReq THEN
    (* la premiere etape active revient aux sources de la premiere
       transition qui y mene *)
    doneBack := FALSE;
    FOR sIdx := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        IF Steps[sIdx].Active AND (NOT doneBack) THEN
            FOR tIdx := 0 TO Runtime.GC_MAX_TRANSITIONS - 1 DO
                IF Transitions[tIdx].Enabled AND (NOT doneBack) THEN
                    isDest := FALSE;
                    nDst := Transitions[tIdx].DestCount;
                    IF nDst > 3 THEN nDst := 3; END_IF;
                    FOR k := 0 TO nDst - 1 DO
                        IF Transitions[tIdx].Destinations[k] = sIdx THEN
                            isDest := TRUE;
                        END_IF;
                    END_FOR;

                    IF isDest THEN
                        FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
                            wasActive[i] := Steps[i].Active;
                        END_FOR;
                        Steps[sIdx].Active := FALSE;
                        nSrc := Transitions[tIdx].SourceCount;
                        IF nSrc > 3 THEN nSrc := 3; END_IF;
                        FOR j := 0 TO nSrc - 1 DO
                            srcId := Transitions[tIdx].Sources[j];
                            IF (srcId >= 0) AND (srcId < Runtime.GC_MAX_STEPS) THEN
                                Steps[srcId].Active := TRUE;
                                IF NOT wasActive[srcId] THEN
                                    resetStep[srcId] := TRUE;
                                END_IF;
                            END_IF;
                        END_FOR;
                        doneBack := TRUE;
                    END_IF;
                END_IF;
            END_FOR;
        END_IF;
    END_FOR;
    forced := doneBack;

ELSIF ForceNextReq THEN
    (* la premiere etape active passe aux destinations de la premiere
       transition qui en part *)
    doneNext := FALSE;
    FOR sIdx := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        IF Steps[sIdx].Active AND (NOT doneNext) THEN
            FOR tIdx := 0 TO Runtime.GC_MAX_TRANSITIONS - 1 DO
                IF Transitions[tIdx].Enabled AND (NOT doneNext) THEN
                    isFromStep := FALSE;
                    nSrc := Transitions[tIdx].SourceCount;
                    IF nSrc > 3 THEN nSrc := 3; END_IF;
                    FOR j := 0 TO nSrc - 1 DO
                        IF Transitions[tIdx].Sources[j] = sIdx THEN
                            isFromStep := TRUE;
                        END_IF;
                    END_FOR;

                    IF isFromStep THEN
                        FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
                            wasActive[i] := Steps[i].Active;
                        END_FOR;
                        Steps[sIdx].Active := FALSE;
                        nDst := Transitions[tIdx].DestCount;
                        IF nDst > 3 THEN nDst := 3; END_IF;
                        IF (Transitions[tIdx].SplitKind <> 1) AND (nDst > 1) THEN
                            nDst := 1;
                        END_IF;
                        FOR k := 0 TO nDst - 1 DO
                            dstId := Transitions[tIdx].Destinations[k];
                            IF (dstId >= 0) AND (dstId < Runtime.GC_MAX_STEPS) THEN
                                Steps[dstId].Active := TRUE;
                                IF NOT wasActive[dstId] THEN
                                    resetStep[dstId] := TRUE;
                                END_IF;
                            END_IF;
                        END_FOR;
                        doneNext := TRUE;
                    END_IF;
                END_IF;
            END_FOR;
        END_IF;
    END_FOR;
    forced := doneNext;
END_IF;

IF forced THEN
    changedNow := TRUE;
    Finished := FALSE;
END_IF;

(* =========================================================
   9. LES ACTIONS DES ETAPES QUI REPARTENT, et la pause d'un cycle
   ========================================================= *)
IF changedNow THEN
    (* une etape qui repart est une NOUVELLE activation : front montant et
       temps a zero, meme si elle a ete quittee puis reprise entre deux
       cycles ou les fronts n'ont pas ete calcules (forcages successifs) *)
    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        IF resetStep[i] AND Steps[i].Active THEN
            Steps[i].PrevActive := FALSE;
            Steps[i].ActiveTime := T#0ms;
        END_IF;
    END_FOR;
    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO
        sid := Actions[aIdx].BoundStepId;
        doReset := resetAll;
        IF (sid >= 0) AND (sid < Runtime.GC_MAX_STEPS) THEN
            IF resetStep[sid] THEN doReset := TRUE; END_IF;
        END_IF;
        IF doReset THEN
            Actions[aIdx].Started := FALSE;
            Actions[aIdx].Finished := FALSE;
            Actions[aIdx].Out := FALSE;
            Actions[aIdx].OnPulseDone := FALSE;
            Actions[aIdx].OffPulseDone := TRUE;     (* K6/K7 desarmees *)
            Actions[aIdx].EnableDone := FALSE;
            Actions[aIdx].EnablePulseDone := FALSE;
            Actions[aIdx].TonStarted := FALSE;
            Actions[aIdx].TonElapsed := T#0ms;
            Actions[aIdx].PrevEnableCond := FALSE;
        END_IF;
    END_FOR;
    pauseToReevaluateCondition := TRUE;
END_IF;

(* =========================================================
   10. FIGE, EN PAUSE, EN PAS A PAS, FINI : on rend la main. La pause
   d'un cycle deja armee passe outre : la nouvelle situation agit au
   moins un cycle.
   ========================================================= *)
IF (FreezeReq OR PauseReq OR ((Runtime.Mode = 2) AND NOT StepReq))
   AND NOT pauseToReevaluateCondition THEN
    SectionsAuthorized := FALSE;
    RETURN;
END_IF;

IF Finished AND NOT pauseToReevaluateCondition THEN
    SectionsAuthorized := FALSE;
    RETURN;
END_IF;
