SectionsAuthorized := TRUE;
ModeReqOut := ModeReq;

IF NOT Enable THEN

    (* reset complet des étapes *)
    FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
        Steps[i].Active := FALSE;
        Steps[i].PrevActive := FALSE;
        Steps[i].Rising := FALSE;
        Steps[i].Falling := FALSE;
        Steps[i].ActiveTime := T#0ms;
        Steps[i].InactiveTime := T#0ms;
    END_FOR;

    (* reset complet des transitions *)
    FOR i := 0 TO Runtime.GC_MAX_TRANSITIONS - 1 DO
        Transitions[i].Validated := FALSE;
        Transitions[i].SnapshotValid := FALSE;
        Transitions[i].TonStarted := FALSE;
        Transitions[i].TonElapsed := T#0ms;
        Transitions[i].TimedConditionValid := FALSE;
    END_FOR;

    (* reset complet des actions *)
    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO
        Actions[aIdx].Started := FALSE;
        Actions[aIdx].Finished := FALSE;
        Actions[aIdx].Out := FALSE;
        Actions[aIdx].PrevOut := FALSE;
        Actions[aIdx].OnPulseDone := FALSE;
        Actions[aIdx].OffPulseDone := FALSE;
        Actions[aIdx].EnableDone := FALSE;
        Actions[aIdx].EnablePulseDone := FALSE;
        Actions[aIdx].TonStarted := FALSE;
        Actions[aIdx].TonElapsed := T#0ms;
        Actions[aIdx].PrevEnableCond := FALSE;
    END_FOR;

    (* reset état global *)
    Finished := FALSE;
    InitPulse := FALSE;
    PrevInitState := FALSE;
    FinalSrcOnValidatedTr := FALSE;
    InternalInitDone := FALSE;   (* très important *)

    SectionsAuthorized := FALSE;
    RETURN;
END_IF;

IF NOT(pauseToReevaluateCondition) THEN

	(* Init auto 1er scan: active TOUTES les étapes initiales *)
	IF NOT InternalInitDone THEN
	    FOR i := 0 TO Runtime.GC_MAX_STEPS-1 DO
	        Steps[i].Active := Steps[i].IsInitial;   (* toutes les initiales à TRUE *)
	        Steps[i].PrevActive := FALSE;
	        Steps[i].ActiveTime := T#0ms;
	        Steps[i].InactiveTime := T#0ms;
	    END_FOR;
	    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS-1 DO
	        Actions[aIdx].Started := FALSE;
		Actions[aIdx].Finished := FALSE;
		Actions[aIdx].Out := FALSE;
	        Actions[aIdx].OnPulseDone := FALSE;
	        Actions[aIdx].OffPulseDone := FALSE;
		Actions[aIdx].EnableDone := FALSE;
	        Actions[aIdx].EnablePulseDone := FALSE;
		Actions[aIdx].TonStarted := FALSE;
	        Actions[aIdx].TonElapsed := T#0ms;
	        Actions[aIdx].PrevEnableCond := FALSE;
	    END_FOR;
	    InternalInitDone := TRUE;
	    Finished := FALSE;
	END_IF;

	Runtime.Mode := ModeReq;
	Runtime.TsPrev := Runtime.TsNow;
	Runtime.TsNow := TsNow;
	Runtime.NtpcNow := NtpcNow;
	Runtime.DateTime := Runtime.TsNow - Runtime.TsPrev;
	Runtime.Cycle := Runtime.Cycle + 1;

	IF NOT ConfigChecked THEN
	    Debug.ConfigFault := FALSE;

	    (* check steps id *)
	    FOR i := 0 TO Runtime.GC_MAX_STEPS-1 DO
	        IF (Steps[i].Id < 0) THEN
	            Debug.ConfigFault := TRUE;
	        END_IF;
	    END_FOR;

	    (* check transitions *)
	    FOR i := 0 TO Runtime.GC_MAX_TRANSITIONS-1 DO
	        IF Transitions[i].Enabled THEN
	            IF (Transitions[i].SourceCount < 1) OR (Transitions[i].DestCount < 1) THEN
	                Debug.ConfigFault := TRUE;
	            END_IF;

	            FOR j := 0 TO Transitions[i].SourceCount-1 DO
	                srcId := Transitions[i].Sources[j];
	                IF (srcId < 0) OR (srcId >= Runtime.GC_MAX_STEPS) THEN
	                    Debug.ConfigFault := TRUE;
	                END_IF;
	            END_FOR;

	            FOR j := 0 TO Transitions[i].DestCount-1 DO
	                dstId := Transitions[i].Destinations[j];
	                IF (dstId < 0) OR (dstId >= Runtime.GC_MAX_STEPS) THEN
	                    Debug.ConfigFault := TRUE;
	                END_IF;
	            END_FOR;
	        END_IF;
	    END_FOR;

	    (* check actions *)
	    FOR i := 0 TO Runtime.GC_MAX_ACTIONS-1 DO
	        sid := Actions[i].BoundStepId;
	        IF (sid < -1) OR (sid >= Runtime.GC_MAX_STEPS) THEN
	            Debug.ConfigFault := TRUE;
	        END_IF;
	    END_FOR;    

	    ConfigChecked := TRUE;
	END_IF;

	IF Debug.ConfigFault THEN
	    SectionsAuthorized := FALSE; 
	    RETURN;
	END_IF;

	IF InitReq OR ResetAllReq THEN
	    FOR i := 0 TO Runtime.GC_MAX_STEPS-1 DO
	        Steps[i].Active := Steps[i].IsInitial;
	        Steps[i].PrevActive := FALSE;
	        Steps[i].ActiveTime := T#0ms;
	    END_FOR;
	    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS-1 DO
	        Actions[aIdx].Started := FALSE;
		Actions[aIdx].Finished := FALSE;
		Actions[aIdx].Out := FALSE;
	        Actions[aIdx].OnPulseDone := FALSE;
	        Actions[aIdx].OffPulseDone := FALSE;
		Actions[aIdx].EnableDone := FALSE;
	        Actions[aIdx].EnablePulseDone := FALSE;
		Actions[aIdx].TonStarted := FALSE;
	        Actions[aIdx].TonElapsed := T#0ms;
	        Actions[aIdx].PrevEnableCond := FALSE;
	    END_FOR;
	    Finished := FALSE;
	END_IF;

	IF ResetInitialReq THEN
	    FOR i := 0 TO Runtime.GC_MAX_STEPS-1 DO
	        Steps[i].Active := Steps[i].IsInitial;
	    END_FOR;
	    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS-1 DO
	        Actions[aIdx].Started := FALSE;
		Actions[aIdx].Finished := FALSE;
		Actions[aIdx].Out := FALSE;
	        Actions[aIdx].OnPulseDone := FALSE;
	        Actions[aIdx].OffPulseDone := FALSE;
		Actions[aIdx].EnableDone := FALSE;
	        Actions[aIdx].EnablePulseDone := FALSE;
		Actions[aIdx].TonStarted := FALSE;
	        Actions[aIdx].TonElapsed := T#0ms;
	        Actions[aIdx].PrevEnableCond := FALSE;
	    END_FOR;
	    Finished := FALSE;
	END_IF;

	IF FreezeReq OR PauseReq THEN RETURN; END_IF;
	IF (Runtime.Mode = 2) AND (NOT StepReq) THEN RETURN; END_IF; (* MANUAL *)

	(* 1) Force direct From -> To *)
	IF (ForceStepFromId >= 0) AND (ForceStepToId >= 0) AND
	   (ForceStepFromId < Runtime.GC_MAX_STEPS) AND (ForceStepToId < Runtime.GC_MAX_STEPS) THEN

	    Steps[ForceStepFromId].Active := FALSE;
	    Steps[ForceStepToId].Active := TRUE;

	    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS-1 DO

	        sid := Actions[aIdx].BoundStepId;

	        IF (sid = ForceStepToId) THEN

	            Actions[aIdx].Started := FALSE;
		    Actions[aIdx].Finished := FALSE;
		    Actions[aIdx].Out := FALSE;
	            Actions[aIdx].OnPulseDone := FALSE;
	            Actions[aIdx].OffPulseDone := FALSE;
		    Actions[aIdx].EnableDone := FALSE;
	            Actions[aIdx].EnablePulseDone := FALSE;
		    Actions[aIdx].TonStarted := FALSE;
	            Actions[aIdx].TonElapsed := T#0ms;
	            Actions[aIdx].PrevEnableCond := FALSE;

	        END_IF;

	    END_FOR;

	    SectionsAuthorized := FALSE; 
	    RETURN;
	END_IF;

	(* 2) Force Back: étape active -> source de la transition entrante *)
	IF ForceBackReq THEN
	    doneBack := FALSE;

	    FOR sIdx := 0 TO Runtime.GC_MAX_STEPS-1 DO
	        IF Steps[sIdx].Active AND (NOT doneBack) THEN
	            FOR tIdx := 0 TO Runtime.GC_MAX_TRANSITIONS-1 DO
	                IF Transitions[tIdx].Enabled THEN
	                    isDest := FALSE;

	                    FOR k := 0 TO Transitions[tIdx].DestCount-1 DO
	                        IF Transitions[tIdx].Destinations[k] = sIdx THEN
	                            isDest := TRUE;
	                        END_IF;
	                    END_FOR;

	                    IF isDest THEN
	                        Steps[sIdx].Active := FALSE;

	                        FOR j := 0 TO Transitions[tIdx].SourceCount-1 DO
	                            srcId := Transitions[tIdx].Sources[j];
	                            IF (srcId >= 0) AND (srcId < Runtime.GC_MAX_STEPS) THEN
	                                Steps[srcId].Active := TRUE;
	                            END_IF;
	                        END_FOR;

	                        doneBack := TRUE;
	                        EXIT;
	                    END_IF;
	                END_IF;
	            END_FOR;
	        END_IF;
	    END_FOR;

	    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS-1 DO

	        sid := Actions[aIdx].BoundStepId;

	        IF (sid >= 0) AND (sid < Runtime.GC_MAX_STEPS) THEN

	            (* reset uniquement sur entrée réelle du step source *)
	            IF Steps[sid].Active THEN

	                Actions[aIdx].Started := FALSE;
			Actions[aIdx].Finished := FALSE;
			Actions[aIdx].Out := FALSE;
	                Actions[aIdx].OnPulseDone := FALSE;
	                Actions[aIdx].OffPulseDone := FALSE;
			Actions[aIdx].EnableDone := FALSE;
	                Actions[aIdx].EnablePulseDone := FALSE;
			Actions[aIdx].TonStarted := FALSE;
	                Actions[aIdx].TonElapsed := T#0ms;
	                Actions[aIdx].PrevEnableCond := FALSE;

	            END_IF;

	        END_IF;

	    END_FOR;

	    SectionsAuthorized := FALSE; 
	    RETURN;
	END_IF;

	(* 3) Force Next: étape active -> destination de la transition sortante *)
	IF ForceNextReq THEN
	    doneNext := FALSE;

	    FOR sIdx := 0 TO Runtime.GC_MAX_STEPS-1 DO
	        IF Steps[sIdx].Active AND (NOT doneNext) THEN
	            FOR tIdx := 0 TO Runtime.GC_MAX_TRANSITIONS-1 DO
	                IF Transitions[tIdx].Enabled THEN
	                    isFromStep := FALSE;

	                    FOR j := 0 TO Transitions[tIdx].SourceCount-1 DO
	                        IF Transitions[tIdx].Sources[j] = sIdx THEN
	                            isFromStep := TRUE;
	                        END_IF;
	                    END_FOR;

	                    IF isFromStep THEN
	                        Steps[sIdx].Active := FALSE;

	                        IF Transitions[tIdx].SplitKind = 1 THEN
	                            FOR k := 0 TO Transitions[tIdx].DestCount-1 DO
	                                dstId := Transitions[tIdx].Destinations[k];
	                                IF (dstId >= 0) AND (dstId < Runtime.GC_MAX_STEPS) THEN
	                                    Steps[dstId].Active := TRUE;
	                                END_IF;
	                            END_FOR;
	                        ELSE
	                            dstId := Transitions[tIdx].Destinations[0];
	                            IF (dstId >= 0) AND (dstId < Runtime.GC_MAX_STEPS) THEN
	                                Steps[dstId].Active := TRUE;
	                            END_IF;
	                        END_IF;

	                        doneNext := TRUE;
	                        EXIT;
	                    END_IF;
	                END_IF;
	            END_FOR;
	        END_IF;
	    END_FOR;

	    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS-1 DO

	        sid := Actions[aIdx].BoundStepId;

	        IF (sid >= 0) AND (sid < Runtime.GC_MAX_STEPS) THEN

	            (* uniquement si action appartient à une étape nouvellement activée *)
	            IF Steps[sid].Active THEN

	                Actions[aIdx].Started := FALSE;
			Actions[aIdx].Finished := FALSE;
			Actions[aIdx].Out := FALSE;
	                Actions[aIdx].OnPulseDone := FALSE;
	                Actions[aIdx].OffPulseDone := FALSE;
			Actions[aIdx].EnableDone := FALSE;
			Actions[aIdx].EnablePulseDone := FALSE;
			Actions[aIdx].TonStarted := FALSE;
	                Actions[aIdx].TonElapsed := T#0ms;
	                Actions[aIdx].PrevEnableCond := FALSE;

	            END_IF;

	        END_IF;

	    END_FOR;

	    SectionsAuthorized := FALSE; 
	    RETURN;
	END_IF;

	IF Finished THEN
	    SectionsAuthorized := FALSE; 
	    RETURN;
	END_IF;

	(* 0 DETECTION RETOUR CYCLE INITIALE *)
	isInitState := TRUE;

	FOR i := 0 TO Runtime.GC_MAX_STEPS-1 DO
	    IF Steps[i].Active <> Steps[i].IsInitial THEN
	        isInitState := FALSE;
	    END_IF;
	END_FOR;

	InitPulse := isInitState AND NOT PrevInitState;
	PrevInitState := isInitState;

	IF InitPulse THEN
	    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS-1 DO

	        sid := Actions[aIdx].BoundStepId;
        
	        IF (sid >= 0) AND (sid < Runtime.GC_MAX_STEPS) THEN
		    IF NOT (Steps[sid].IsInitial AND Actions[aIdx].Kind = 1) THEN
	                Actions[aIdx].Started := FALSE;
	                Actions[aIdx].Finished := FALSE;
	                Actions[aIdx].Out := FALSE;
	                Actions[aIdx].OnPulseDone := FALSE;
	                Actions[aIdx].OffPulseDone := FALSE;
	                Actions[aIdx].EnableDone := FALSE;
	                Actions[aIdx].EnablePulseDone := FALSE;
			Actions[aIdx].TonStarted := FALSE;
	                Actions[aIdx].TonElapsed := T#0ms;
	                Actions[aIdx].PrevEnableCond := FALSE;
		    END_IF;
		END_IF;
	    END_FOR;

	    InitPulseCount := InitPulseCount + 1;
	    SectionsAuthorized := FALSE; 
	    RETURN;
	END_IF;

END_IF;
