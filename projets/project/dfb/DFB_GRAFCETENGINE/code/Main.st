(* La pause d'un cycle : armee par l'appel precedent (et pas par un
   changement de cet appel-ci), elle se leve a la fin de celui-ci. *)
NeedToResetPause := pauseToReevaluateCondition AND NOT changedNow;

IF NOT(pauseToReevaluateCondition) THEN

    (* 1 SCAN_TRANS *)
    Runtime.Phase := 1;
    finalSrcOnValidatedTr := FALSE;

    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        claimed[i] := FALSE;
    END_FOR;

    FOR i := 0 TO Runtime.GC_MAX_TRANSITIONS - 1 DO

        Transitions[i].Validated := FALSE;
        nSrc := Transitions[i].SourceCount;
        IF nSrc > 3 THEN nSrc := 3; END_IF;

        (* sans source ou sans destination, une transition ne se franchit
           jamais (elle sortirait des etapes de nulle part, ou en perdrait) *)
        IF Transitions[i].Enabled AND (nSrc >= 1) AND (Transitions[i].DestCount >= 1) THEN

            allSrcActive := TRUE;
            anySrcActive := FALSE;

            FOR j := 0 TO nSrc - 1 DO
                srcId := Transitions[i].Sources[j];
                IF (srcId >= 0) AND (srcId < Runtime.GC_MAX_STEPS) THEN
                    anySrcActive := anySrcActive OR Steps[srcId].Active;
                    allSrcActive := allSrcActive AND Steps[srcId].Active;
                END_IF;
            END_FOR;

            (* =========================
               FILTRE TEMPOREL CONDITION : le temps part de la premiere
               fois ou la receptivite est vue vraie (comme un TON)
               ========================= *)
            IF Transitions[i].Condition THEN
                IF NOT Transitions[i].TonStarted THEN
                    Transitions[i].TonStarted := TRUE;
                    Transitions[i].TonElapsed := T#0ms;
                ELSIF Transitions[i].TonElapsed < T#24d - dtScan THEN
                    Transitions[i].TonElapsed := Transitions[i].TonElapsed + dtScan;
                ELSE
                    Transitions[i].TonElapsed := T#24d;
                END_IF;

                IF Transitions[i].TonElapsed >= Transitions[i].ConditionTime THEN
                    Transitions[i].TimedConditionValid := TRUE;
                END_IF;
            END_IF;
            (* ResetOnFall : sans effet, le filtre repart de zero des que la
               receptivite tombe (plus bas), comme en 1.48 *)

            (* =========================
               VALIDATION TRANSITION
               ========================= *)
            IF Transitions[i].JoinKind = 1 THEN
                Transitions[i].Validated :=
                    allSrcActive AND Transitions[i].TimedConditionValid AND Transitions[i].Condition;
                IF NOT(allSrcActive) THEN
                    Transitions[i].TonStarted := FALSE;
                    Transitions[i].TonElapsed := T#0ms;
                    Transitions[i].TimedConditionValid := FALSE;
                END_IF;
            ELSE
                Transitions[i].Validated :=
                    anySrcActive AND Transitions[i].TimedConditionValid AND Transitions[i].Condition;
                IF NOT(anySrcActive) THEN
                    Transitions[i].TonStarted := FALSE;
                    Transitions[i].TonElapsed := T#0ms;
                    Transitions[i].TimedConditionValid := FALSE;
                END_IF;
            END_IF;

        END_IF;

        (* =========================
           FINISH DETECTION, CONFLITS
           ========================= *)
        IF Transitions[i].Validated THEN
            FOR j := 0 TO nSrc - 1 DO
                srcId := Transitions[i].Sources[j];
                IF (srcId >= 0) AND (srcId < Runtime.GC_MAX_STEPS) THEN
                    IF Steps[srcId].Active AND Steps[srcId].IsFinal THEN
                        finalSrcOnValidatedTr := TRUE;
                        Finished := TRUE;
                    END_IF;
                    (* deux transitions franchissables qui prennent la meme
                       etape : receptivites non exclusives *)
                    IF claimed[srcId] THEN
                        Debug.ConflictDetected := TRUE;
                    END_IF;
                    claimed[srcId] := TRUE;
                END_IF;
            END_FOR;
        END_IF;

        IF NOT Transitions[i].Enabled OR NOT Transitions[i].Condition THEN
            Transitions[i].TonStarted := FALSE;
            Transitions[i].TonElapsed := T#0ms;
            Transitions[i].TimedConditionValid := FALSE;
        END_IF;

    END_FOR;
    dtScan := T#0ms;

    (* une etape finale active sans aucune transition de sortie : fini *)
    FOR sIdx := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        IF Steps[sIdx].Active AND Steps[sIdx].IsFinal THEN
            hasExit := FALSE;
            FOR tIdx := 0 TO Runtime.GC_MAX_TRANSITIONS - 1 DO
                IF Transitions[tIdx].Enabled THEN
                    nSrc := Transitions[tIdx].SourceCount;
                    IF nSrc > 3 THEN nSrc := 3; END_IF;
                    FOR j := 0 TO nSrc - 1 DO
                        IF Transitions[tIdx].Sources[j] = sIdx THEN
                            hasExit := TRUE;
                        END_IF;
                    END_FOR;
                END_IF;
            END_FOR;
            IF NOT hasExit THEN
                Finished := TRUE;
            END_IF;
        END_IF;
    END_FOR;

    (* 2 SNAPSHOT *)
    Runtime.Phase := 2;
    FOR i := 0 TO Runtime.GC_MAX_TRANSITIONS - 1 DO
        Transitions[i].SnapshotValid := Transitions[i].Validated;
        IF Transitions[i].Validated THEN
            Transitions[i].TonStarted := FALSE;
            Transitions[i].TonElapsed := T#0ms;
            Transitions[i].TimedConditionValid := FALSE;
        END_IF;
    END_FOR;

    (* 3 COMMIT : TOUTES les sources desactivees, PUIS toutes les
       destinations activees. Une etape desactivee et activee au meme
       cycle reste active, sans repartir (regle 5). *)
    Runtime.Phase := 3;
    anyFired := FALSE;
    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        wasActive[i] := Steps[i].Active;
        resetStep[i] := FALSE;
    END_FOR;

    FOR i := 0 TO Runtime.GC_MAX_TRANSITIONS - 1 DO
        IF Transitions[i].SnapshotValid THEN
            anyFired := TRUE;
            nSrc := Transitions[i].SourceCount;
            IF nSrc > 3 THEN nSrc := 3; END_IF;
            FOR j := 0 TO nSrc - 1 DO
                srcId := Transitions[i].Sources[j];
                IF (srcId >= 0) AND (srcId < Runtime.GC_MAX_STEPS) THEN
                    Steps[srcId].Active := FALSE;
                END_IF;
            END_FOR;
            Debug.LastTransitionFiredId := Transitions[i].Id;
            IF Debug.TotalTransitionsFired < 2147483647 THEN
                Debug.TotalTransitionsFired := Debug.TotalTransitionsFired + 1;
            ELSE
                Debug.TotalTransitionsFired := 0;
            END_IF;
        END_IF;
    END_FOR;

    FOR i := 0 TO Runtime.GC_MAX_TRANSITIONS - 1 DO
        IF Transitions[i].SnapshotValid THEN
            nDst := Transitions[i].DestCount;
            IF nDst > 3 THEN nDst := 3; END_IF;
            IF (Transitions[i].SplitKind <> 1) AND (nDst > 1) THEN
                nDst := 1;       (* SplitKind 0 : la premiere destination seule *)
            END_IF;
            FOR k := 0 TO nDst - 1 DO
                dstId := Transitions[i].Destinations[k];
                IF (dstId >= 0) AND (dstId < Runtime.GC_MAX_STEPS) THEN
                    Steps[dstId].Active := TRUE;
                    IF NOT wasActive[dstId] THEN
                        resetStep[dstId] := TRUE;
                    END_IF;
                END_IF;
            END_FOR;
        END_IF;
    END_FOR;

    IF anyFired THEN
        FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
            IF resetStep[i] THEN
                Steps[i].PrevActive := FALSE;   (* nouvelle activation : front montant *)
                Steps[i].ActiveTime := T#0ms;
            END_IF;
        END_FOR;
        howManyResets := 0;
        FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO
            sid := Actions[aIdx].BoundStepId;
            IF (sid >= 0) AND (sid < Runtime.GC_MAX_STEPS) THEN
                IF resetStep[sid] THEN
                    howManyResets := howManyResets + 1;
                    Actions[aIdx].Finished         := FALSE;
                    Actions[aIdx].Started          := FALSE;
                    Actions[aIdx].Out              := FALSE;
                    Actions[aIdx].OnPulseDone      := FALSE;
                    Actions[aIdx].OffPulseDone     := TRUE;
                    Actions[aIdx].EnableDone       := FALSE;
                    Actions[aIdx].EnablePulseDone  := FALSE;
                    Actions[aIdx].TonStarted       := FALSE;
                    Actions[aIdx].TonElapsed       := T#0ms;
                    Actions[aIdx].PrevEnableCond   := FALSE;
                END_IF;
            END_IF;
        END_FOR;
        changedNow := TRUE;
        pauseToReevaluateCondition := TRUE;
    END_IF;

END_IF;

(* =========================================================
   0 RETOUR A LA SITUATION INITIALE : le statut (Started, Finished) des
   actions au repos des etapes inactives est efface, pour que le nouveau
   tour parte propre. Les actions des etapes actives et celles qui ont
   encore quelque chose a faire (K6/K7 armees, K11 en cours) n'y sont
   pas touchees. Plus de RETURN : le cycle n'est pas perdu.
   ========================================================= *)
isInitState := TRUE;
FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
    IF Steps[i].Active <> Steps[i].IsInitial THEN
        isInitState := FALSE;
    END_IF;
END_FOR;
InitPulse := isInitState AND NOT PrevInitState;
PrevInitState := isInitState;

IF InitPulse THEN
    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO
        sid := Actions[aIdx].BoundStepId;
        IF (sid >= 0) AND (sid < Runtime.GC_MAX_STEPS) THEN
            IF NOT Steps[sid].Active AND NOT Actions[aIdx].Out AND NOT Actions[aIdx].TonStarted
               AND (Actions[aIdx].OffPulseDone OR ((Actions[aIdx].Kind <> 6) AND (Actions[aIdx].Kind <> 7))) THEN
                Actions[aIdx].Started := FALSE;
                Actions[aIdx].Finished := FALSE;
            END_IF;
        END_IF;
    END_FOR;
    IF InitPulseCount < 32767 THEN
        InitPulseCount := InitPulseCount + 1;
    ELSE
        InitPulseCount := 0;
    END_IF;
END_IF;

IF NeedToResetPause THEN
    NeedToResetPause := FALSE;
    pauseToReevaluateCondition := FALSE;
END_IF;

IF NOT(pauseToReevaluateCondition) THEN

    (* 4 UPDATE_STEPS : les fronts. Les temps sont comptes en tete
       d'appel (Init, 6). *)
    Runtime.Phase := 4;
    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        Steps[i].Rising  := Steps[i].Active AND (NOT Steps[i].PrevActive);
        Steps[i].Falling := (NOT Steps[i].Active) AND Steps[i].PrevActive;
        Steps[i].PrevActive := Steps[i].Active;
    END_FOR;

    (* 5 SCAN_ACTIONS *)
    Runtime.Phase := 5;
    FOR i := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO
        Actions[i].Out := FALSE;

        sid := Actions[i].BoundStepId;

        IF (sid >= 0) AND (sid < Runtime.GC_MAX_STEPS) THEN

            (* le temps de la condition (K8, K9, K10) : depuis qu'elle est
               vue vraie sur l'etape active. 0 au premier cycle. Une fois
               l'action finie, il ne bouge plus. *)
            firstEn := FALSE;
            IF Actions[i].Kind <> 11 THEN
                IF Actions[i].EnableCond AND Steps[sid].Active THEN
                    IF NOT Actions[i].Finished THEN
                        IF NOT Actions[i].EnableDone THEN
                            Actions[i].EnableDone := TRUE;
                            Actions[i].TonElapsed := T#0ms;
                            firstEn := TRUE;
                        ELSIF Actions[i].TonElapsed < T#24d - dtActs THEN
                            Actions[i].TonElapsed := Actions[i].TonElapsed + dtActs;
                        ELSE
                            Actions[i].TonElapsed := T#24d;
                        END_IF;
                    END_IF;
                ELSE
                    Actions[i].EnableDone := FALSE;
                    Actions[i].TonElapsed := T#0ms;
                    Actions[i].EnablePulseDone := FALSE;
                END_IF;
            END_IF;

            (* K11 : une impulsion de Delay sur front de la condition, une
               par activation ; elle va au bout meme si l'etape est quittee *)
            IF Actions[i].Kind = 11 THEN
                IF Actions[i].TonStarted THEN
                    IF Actions[i].TonElapsed < T#24d - dtActs THEN
                        Actions[i].TonElapsed := Actions[i].TonElapsed + dtActs;
                    END_IF;
                    IF Actions[i].TonElapsed >= Actions[i].Delay THEN
                        Actions[i].TonStarted := FALSE;
                    END_IF;
                ELSIF Steps[sid].Active AND Actions[i].EnableCond AND (NOT Actions[i].PrevEnableCond)
                      AND (NOT Actions[i].Finished) THEN
                    Actions[i].TonStarted := TRUE;
                    Actions[i].TonElapsed := T#0ms;
                END_IF;
            END_IF;

            IF Actions[i].EnableCond THEN

                IF Steps[sid].Active THEN
                    Actions[i].OffPulseDone := FALSE;   (* arme K6/K7 *)
                END_IF;

                IF Steps[sid].Falling THEN
                    Actions[i].OnPulseDone := FALSE;
                END_IF;

                IF Actions[i].Kind = 0 THEN
                    Actions[i].Out := Steps[sid].Active;

                ELSIF Actions[i].Kind = 1 THEN
                    Actions[i].Out := Steps[sid].Rising;

                ELSIF Actions[i].Kind = 2 THEN
                    Actions[i].Out := Steps[sid].Active AND (Steps[sid].ActiveTime >= Actions[i].Delay);

                ELSIF Actions[i].Kind = 3 THEN
                    IF Steps[sid].Active AND (NOT Actions[i].OnPulseDone) AND (Steps[sid].ActiveTime >= Actions[i].Delay) THEN
                        Actions[i].Out := TRUE;
                        Actions[i].OnPulseDone := TRUE;
                    END_IF;

                ELSIF Actions[i].Kind = 4 THEN
                    (* au moins le cycle d'activation, meme avec un retard nul *)
                    Actions[i].Out := Steps[sid].Active AND
                        (Steps[sid].Rising OR (Steps[sid].ActiveTime <= Actions[i].Delay));

                ELSIF Actions[i].Kind = 5 THEN
                    Actions[i].Out := Steps[sid].Falling;

                ELSIF Actions[i].Kind = 6 THEN
                    IF (NOT Steps[sid].Active) AND (NOT Actions[i].OffPulseDone) AND (Steps[sid].InactiveTime >= Actions[i].Delay) THEN
                        Actions[i].Out := TRUE;
                        Actions[i].OffPulseDone := TRUE;
                    END_IF;

                ELSIF Actions[i].Kind = 7 THEN
                    (* seulement apres une vraie activite de l'etape (armee),
                       au moins le cycle de desactivation *)
                    IF (NOT Steps[sid].Active) AND (NOT Actions[i].OffPulseDone) THEN
                        IF Steps[sid].Falling OR (Steps[sid].InactiveTime <= Actions[i].Delay) THEN
                            Actions[i].Out := TRUE;
                        ELSE
                            Actions[i].OffPulseDone := TRUE;
                        END_IF;
                    END_IF;

                ELSIF Actions[i].Kind = 8 THEN
                    Actions[i].Out :=
                        Steps[sid].Active AND
                        (Actions[i].TonElapsed >= Actions[i].Delay);

                ELSIF Actions[i].Kind = 9 THEN
                    IF Steps[sid].Active AND (NOT Actions[i].Finished) THEN
                        IF (NOT Actions[i].EnablePulseDone) AND (Actions[i].TonElapsed >= Actions[i].Delay) THEN
                            Actions[i].Out := TRUE;
                            Actions[i].EnablePulseDone := TRUE;
                        END_IF;
                    END_IF;

                ELSIF Actions[i].Kind = 10 THEN
                    Actions[i].Out :=
                        Steps[sid].Active AND (NOT Actions[i].Finished) AND
                        (firstEn OR (Actions[i].TonElapsed < Actions[i].Delay));
                END_IF;
            END_IF;

            IF Actions[i].Kind = 11 THEN
                Actions[i].Out := Actions[i].TonStarted;
            END_IF;

            IF (NOT(Actions[i].Started) AND Actions[i].Out) THEN Actions[i].Started := TRUE; END_IF;

            IF Actions[i].Started AND NOT(Actions[i].Finished) THEN
                CASE Actions[i].Kind OF
                    0: Actions[i].Finished := NOT Steps[sid].Active;
                    1: Actions[i].Finished := Steps[sid].Rising;
                    2: Actions[i].Finished := Steps[sid].Active AND (Steps[sid].ActiveTime >= Actions[i].Delay);
                    3: Actions[i].Finished := Actions[i].OnPulseDone;
                    4: Actions[i].Finished := Steps[sid].Active AND (Steps[sid].ActiveTime > Actions[i].Delay);
                    5: Actions[i].Finished := Steps[sid].Falling;
                    6: Actions[i].Finished := Actions[i].OffPulseDone;
                    7: Actions[i].Finished := (NOT Steps[sid].Active) AND (Steps[sid].InactiveTime > Actions[i].Delay);
                    8: Actions[i].Finished := (NOT Steps[sid].Active);
                    9: Actions[i].Finished := Actions[i].EnablePulseDone;
                    10: Actions[i].Finished := (NOT Actions[i].EnableCond) OR (Actions[i].TonElapsed >= Actions[i].Delay) OR (NOT Steps[sid].Active);
                    11: Actions[i].Finished := NOT Actions[i].TonStarted;
                END_CASE;
            END_IF;

            Actions[i].PrevEnableCond := Actions[i].EnableCond;
        END_IF;
    END_FOR;
    dtActs := T#0ms;

END_IF;
