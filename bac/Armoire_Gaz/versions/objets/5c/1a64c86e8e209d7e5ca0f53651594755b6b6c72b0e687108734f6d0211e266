IF pauseToReevaluateCondition THEN
	NeedToResetPause := TRUE;
END_IF;

IF NOT(pauseToReevaluateCondition) THEN

	(* 1 SCAN_TRANS *)
	Runtime.Phase := 1;
	FinalSrcOnValidatedTr := FALSE;

	FOR i := 0 TO Runtime.GC_MAX_TRANSITIONS-1 DO

	    Transitions[i].Validated := FALSE;

	    IF Transitions[i].Enabled THEN

	        allSrcActive := TRUE;
	        anySrcActive := FALSE;

	        FOR j := 0 TO Transitions[i].SourceCount-1 DO
	            srcId := Transitions[i].Sources[j];

	            IF (srcId >= 0) AND (srcId < Runtime.GC_MAX_STEPS) THEN

	                anySrcActive := anySrcActive OR Steps[srcId].Active;
	                allSrcActive := allSrcActive AND Steps[srcId].Active;

	            END_IF;
	        END_FOR;

	        (* =========================
	           FILTRE TEMPOREL CONDITION
	           ========================= *)

	        IF Transitions[i].Condition THEN

	            IF NOT Transitions[i].TonStarted THEN
	                Transitions[i].TonStarted := TRUE;
	                Transitions[i].TonElapsed := T#0ms;
	            END_IF;

	            Transitions[i].TonElapsed := Transitions[i].TonElapsed + Runtime.DateTime;

	            IF Transitions[i].TonElapsed >= Transitions[i].ConditionTime THEN
	                Transitions[i].TimedConditionValid := TRUE;
	            END_IF;

	        ELSE

	            IF Transitions[i].ResetOnFall THEN
	                Transitions[i].TonStarted := FALSE;
	                Transitions[i].TonElapsed := T#0ms;
	                Transitions[i].TimedConditionValid := FALSE;
	            END_IF;

	        END_IF;


	        (* =========================
	           VALIDATION TRANSITION
	           ========================= *)

	        IF Transitions[i].JoinKind = 1 THEN

	            Transitions[i].Validated :=
	                allSrcActive AND
	                (
	                    Transitions[i].TimedConditionValid AND
	                    Transitions[i].Condition
	                );

		    IF NOT(allSrcActive) THEN
		        Transitions[i].TonStarted := FALSE;
		        Transitions[i].TonElapsed := T#0ms;
		        Transitions[i].TimedConditionValid := FALSE;
	    	    END_IF;

	        ELSE

	            Transitions[i].Validated :=
	                anySrcActive AND
	                (
	                    Transitions[i].TimedConditionValid AND
	                    Transitions[i].Condition
	                );

		    IF NOT(anySrcActive) THEN
		        Transitions[i].TonStarted := FALSE;
		        Transitions[i].TonElapsed := T#0ms;
		        Transitions[i].TimedConditionValid := FALSE;
	    	    END_IF;
	        END_IF;

	    END_IF;

	    (* =========================
	       FINISH DETECTION
	       ========================= *)

	    IF Transitions[i].Validated THEN

	        finalSrcOnValidatedTr := FALSE;

	        FOR j := 0 TO Transitions[i].SourceCount-1 DO

	            srcId := Transitions[i].Sources[j];

	            IF (srcId >= 0) AND (srcId < Runtime.GC_MAX_STEPS) THEN

	                IF Steps[srcId].Active AND Steps[srcId].IsFinal THEN
	                    finalSrcOnValidatedTr := TRUE;
	                END_IF;

	            END_IF;

	        END_FOR;

	        IF finalSrcOnValidatedTr THEN
	            Finished := TRUE;
	        END_IF;

	    END_IF;
    
	    IF NOT Transitions[i].Enabled OR NOT Transitions[i].Condition THEN
	        Transitions[i].TonStarted := FALSE;
	        Transitions[i].TonElapsed := T#0ms;
	        Transitions[i].TimedConditionValid := FALSE;
	    END_IF;

	END_FOR;

	(* 2 SNAPSHOT *)
	Runtime.Phase := 2;
	FOR i := 0 TO Runtime.GC_MAX_TRANSITIONS-1 DO
	    Transitions[i].SnapshotValid := Transitions[i].Validated;
	    IF Transitions[i].Validated THEN
		    Transitions[i].TonStarted := FALSE;
		    Transitions[i].TonElapsed := T#0ms;
		    Transitions[i].TimedConditionValid := FALSE;
	    END_IF;
	END_FOR;

	(* 3 COMMIT *)
	Runtime.Phase := 3;
	FOR i := 0 TO Runtime.GC_MAX_TRANSITIONS-1 DO
	    IF Transitions[i].SnapshotValid THEN
	        FOR j := 0 TO Transitions[i].SourceCount-1 DO
	            srcId := Transitions[i].Sources[j];
	            IF (srcId >= 0) AND (srcId < Runtime.GC_MAX_STEPS) THEN
	                Steps[srcId].Active := FALSE;
	            END_IF;
	        END_FOR;

	        IF Transitions[i].SplitKind = 1 THEN
		    howManyResets := 0;
		    pauseToReevaluateCondition := true;
	            FOR k := 0 TO Transitions[i].DestCount-1 DO
	                dstId := Transitions[i].Destinations[k];
	                IF (dstId >= 0) AND (dstId < Runtime.GC_MAX_STEPS) THEN
		
			    IF NOT(Steps[dstId].Active) THEN   
			        Steps[dstId].Active := TRUE; 

			        FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO
		                    IF Actions[aIdx].BoundStepId = dstId THEN
				        howManyResets := howManyResets + 1;
				
		                        Actions[aIdx].Finished         := FALSE;
		                        Actions[aIdx].Started          := FALSE;
		                        Actions[aIdx].Out              := FALSE;
		                        Actions[aIdx].OnPulseDone      := FALSE;
		                        Actions[aIdx].OffPulseDone     := FALSE;
		                        Actions[aIdx].EnableDone       := FALSE;
		                        Actions[aIdx].EnablePulseDone  := FALSE;
		                        Actions[aIdx].TonStarted       := FALSE;
		                        Actions[aIdx].TonElapsed       := T#0ms;
		                        Actions[aIdx].PrevEnableCond   := FALSE;
		                        Actions[aIdx].PrevOut          := FALSE;
		                    END_IF;
		                END_FOR;
			    END_IF;

			END_IF;
	            END_FOR;
	        ELSE
	            dstId := Transitions[i].Destinations[0];
	            IF (dstId >= 0) AND (dstId < Runtime.GC_MAX_STEPS) THEN 
	        
			IF NOT(Steps[dstId].Active) THEN
			    howManyResets := 0;
			    pauseToReevaluateCondition := true;
			    Steps[dstId].Active := TRUE;
		            FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO
		                IF Actions[aIdx].BoundStepId = dstId THEN
				    howManyResets := howManyResets + 1;
		                    Actions[aIdx].Finished         := FALSE;
		                    Actions[aIdx].Started          := FALSE;
	        	            Actions[aIdx].Out              := FALSE;
		                    Actions[aIdx].OnPulseDone      := FALSE;
		                    Actions[aIdx].OffPulseDone     := FALSE;
		                    Actions[aIdx].EnableDone       := FALSE;
		                    Actions[aIdx].EnablePulseDone  := FALSE;
		                    Actions[aIdx].TonStarted       := FALSE;
		                    Actions[aIdx].TonElapsed       := T#0ms;
	        	            Actions[aIdx].PrevEnableCond   := FALSE;
		                    Actions[aIdx].PrevOut          := FALSE;
		                END_IF;
		            END_FOR;
			END_IF;

	            END_IF;
	        END_IF;

	        Debug.LastTransitionFiredId := Transitions[i].Id;
	        Debug.TotalTransitionsFired := Debug.TotalTransitionsFired + 1;
	    END_IF;
	END_FOR;

END_IF;

IF NeedToResetPause THEN
	NeedToResetPause := FALSE;
	pauseToReevaluateCondition := FALSE;
END_IF;

IF NOT(pauseToReevaluateCondition) THEN

	(* 4 UPDATE_STEPS *)
	Runtime.Phase := 4;
	Debug.ActiveStepCount := 0;
	FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO

	    Steps[i].Rising  := Steps[i].Active AND (NOT Steps[i].PrevActive);
	    Steps[i].Falling := (NOT Steps[i].Active) AND Steps[i].PrevActive;

	    IF Steps[i].Active THEN
	        Steps[i].ActiveTime   := Steps[i].ActiveTime + Runtime.DateTime;
	        Steps[i].InactiveTime := T#0ms;
	        Debug.ActiveStepCount := Debug.ActiveStepCount + 1;
	    ELSE
	        Steps[i].InactiveTime := Steps[i].InactiveTime + Runtime.DateTime;
	        Steps[i].ActiveTime   := T#0ms;
	    END_IF;

	    Steps[i].PrevActive := Steps[i].Active;

	END_FOR;

	(* 5 SCAN_ACTIONS *)
	Runtime.Phase := 5;
	FOR i := 0 TO Runtime.GC_MAX_ACTIONS-1 DO
	    Actions[i].PrevOut := Actions[i].Out;
	    Actions[i].Out := FALSE;

	    sid := Actions[i].BoundStepId;

	    IF (sid >= 0) AND (sid < Runtime.GC_MAX_STEPS) THEN
        
		(* démarrage sur front montant de la condition *)
	        IF Steps[sid].Active THEN

	            IF Actions[i].EnableCond AND (NOT Actions[i].PrevEnableCond) THEN
	                Actions[i].TonStarted := TRUE;
	                Actions[i].TonElapsed := T#0ms;
	            END_IF;

	        END_IF;

		IF Actions[i].Kind <> 11 THEN
		        IF Actions[i].EnableCond AND Steps[sid].Active AND NOT(Actions[i].Finished) THEN
		            IF NOT Actions[i].EnableDone THEN
		                Actions[i].EnableDone := TRUE;
		                Actions[i].TonElapsed := T#0ms;
		            END_IF;
		            Actions[i].TonElapsed := Actions[i].TonElapsed + Runtime.DateTime;
		        ELSE
		            Actions[i].EnableDone := FALSE;
		            Actions[i].TonElapsed := T#0ms;
		            Actions[i].EnablePulseDone := FALSE;
		        END_IF;
		END_IF;

		IF Actions[i].Kind = 11 THEN
		        IF Actions[i].TonStarted AND NOT(Actions[i].Finished) THEN

		            Actions[i].TonElapsed := Actions[i].TonElapsed + Runtime.DateTime;

		            IF Actions[i].TonElapsed >= Actions[i].Delay THEN
		                Actions[i].TonStarted := FALSE;
		                Actions[i].TonElapsed := T#0ms;
		            END_IF;

		        END_IF;
		END_IF;

	        IF Actions[i].EnableCond THEN

	            IF Steps[sid].Active THEN
	                Actions[i].OffPulseDone := FALSE;
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
	                Actions[i].Out := Steps[sid].Active AND (Steps[sid].ActiveTime <= Actions[i].Delay);

	            ELSIF Actions[i].Kind = 5 THEN
	                Actions[i].Out := Steps[sid].Falling;

	            ELSIF Actions[i].Kind = 6 THEN
	                IF (NOT Steps[sid].Active) AND (NOT Actions[i].OffPulseDone) AND (Steps[sid].InactiveTime >= Actions[i].Delay) THEN
	                    Actions[i].Out := TRUE;
	                    Actions[i].OffPulseDone := TRUE;
	                END_IF;

	            ELSIF Actions[i].Kind = 7 THEN
	                Actions[i].Out := (NOT Steps[sid].Active) AND (Steps[sid].InactiveTime > T#0ms) AND (Steps[sid].InactiveTime <= Actions[i].Delay);
	            ELSIF Actions[i].Kind = 8 THEN
	                Actions[i].Out :=
	                    Steps[sid].Active AND
	                    (Actions[i].TonElapsed >= Actions[i].Delay);
		    ELSIF Actions[i].Kind = 9 THEN
	                IF Steps[sid].Active THEN
	                    IF (NOT Actions[i].EnablePulseDone) AND (Actions[i].TonElapsed >= Actions[i].Delay) THEN

	                        Actions[i].Out := TRUE;
	                        Actions[i].EnablePulseDone := TRUE;

	                    END_IF;
	                END_IF;
	            ELSIF Actions[i].Kind = 10 THEN
	                Actions[i].Out :=
	                    Steps[sid].Active AND
	                    (Actions[i].TonElapsed <= Actions[i].Delay);
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
		        10: Actions[i].Finished := (NOT Actions[i].EnableCond) OR (Actions[i].TonElapsed > Actions[i].Delay) OR (NOT Steps[sid].Active);
	                11: Actions[i].Finished := NOT Actions[i].TonStarted;

		    END_CASE;
	        END_IF;

		Actions[i].PrevEnableCond := Actions[i].EnableCond;
	    END_IF;
	END_FOR;

END_IF;
