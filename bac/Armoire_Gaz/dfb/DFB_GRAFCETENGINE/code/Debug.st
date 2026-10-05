(* 5b HISTO ETAPES ACTIVES (glissant sur MAX_HIST) *)
activeStepQty := 0;
firstStepId := -1;
firstStepName := '';

FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
    IF Steps[i].Rising THEN
        activeStepQty := activeStepQty + 1;
        IF firstStepId = -1 THEN
            firstStepId := Steps[i].Id;
            firstStepName := Steps[i].Name;
        END_IF;
    END_IF;
END_FOR;

IF activeStepQty > 0 THEN

    FOR h := Runtime.GC_MAX_HIST - 1 TO 1 BY -1 DO
        Debug.HistSteps[h].Line := Debug.HistSteps[h - 1].Line;
	Debug.HistSteps[h].NtpcNow := Debug.HistSteps[h - 1].NtpcNow;
    END_FOR;
    Debug.HistSteps[0].Line := firstStepName;
    Debug.HistSteps[0].NtpcNow := Runtime.NtpcNow;

    IF HistFill < Runtime.GC_MAX_HIST THEN
        HistFill := HistFill + 1;
    END_IF;
END_IF;

FOR i := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO

    IF (Actions[i].Out AND NOT Actions[i].PrevOut) THEN

        FOR h := Runtime.GC_MAX_HIST - 1 TO 1 BY -1 DO
            Debug.HistActions[h].Line := Debug.HistActions[h-1].Line;
            Debug.HistActions[h].NtpcNow := Debug.HistActions[h-1].NtpcNow;
        END_FOR;

        IF Actions[i].Out AND NOT Actions[i].PrevOut THEN
            Debug.HistActions[0].Line := '1';
        END_IF;

        stepIdStr := INT_TO_STRING(Steps[Actions[i].BoundStepId].Id);
        actionIdStr := INT_TO_STRING(Actions[i].Id);

        IF LEFT_INT(stepIdStr, 1) = '+' THEN
            stepIdStr := RIGHT_INT(stepIdStr, LEN_INT(stepIdStr) - 1);
        END_IF;

        WHILE (LEN_INT(stepIdStr) > 1) AND (LEFT_INT(stepIdStr, 1) = '0') DO
            stepIdStr := RIGHT_INT(stepIdStr, LEN_INT(stepIdStr) - 1);
        END_WHILE;

        IF LEFT_INT(actionIdStr, 1) = '+' THEN
            actionIdStr := RIGHT_INT(actionIdStr, LEN_INT(actionIdStr) - 1);
        END_IF;

        WHILE (LEN_INT(actionIdStr) > 1) AND (LEFT_INT(actionIdStr, 1) = '0') DO
            actionIdStr := RIGHT_INT(actionIdStr, LEN_INT(actionIdStr) - 1);
        END_WHILE;

        Debug.HistActions[0].Line :=
            CONCAT_STR(
                CONCAT_STR(
                    CONCAT_STR('[',
                        CONCAT_STR(Steps[Actions[i].BoundStepId].Name, ':')
                    ),
                    CONCAT_STR(stepIdStr, ']')
                ),
                CONCAT_STR(
                    Debug.HistActions[0].Line,
                    CONCAT_STR('->',
                        CONCAT_STR(
                            CONCAT_STR(Actions[i].Name, ':'),
                            actionIdStr
                        )
                    )
                )
            );

        Debug.HistActions[0].NtpcNow := Runtime.NtpcNow;

    END_IF;

END_FOR;

(* =========================================================
   DEBUG FOCUS : 1 seule étape active affichée
   ========================================================= *)

Debug.NoActiveStepFault := (Debug.ActiveStepCount = 0);

(* Reset focus *)
Debug.StepFocus.StepId := -1;
Debug.StepFocus.StepName := '';

Debug.StepFocus.Kind1Line := 'K1 : ';
Debug.StepFocus.Kind2Line := 'K2 : ';
Debug.StepFocus.Kind3Line := 'K3 : ';
Debug.StepFocus.Kind4Line := 'K4 : ';
Debug.StepFocus.Kind5Line := 'K5 : ';
Debug.StepFocus.Kind6Line := 'K6 : ';
Debug.StepFocus.Kind7Line := 'K7 : ';
Debug.StepFocus.Kind8Line := 'K8 : ';
Debug.StepFocus.Kind9Line := 'K9 : ';
Debug.StepFocus.Kind10Line := 'K10 : ';
Debug.StepFocus.Kind11Line := 'K11 : ';
Debug.StepFocus.TransitionsLine := 'TRS : ';

focusStep := -1;

(* première étape active *)
FOR sIdx := 0 TO Runtime.GC_MAX_STEPS-1 DO
    IF Steps[sIdx].Active THEN
        focusStep := sIdx;
    END_IF;
END_FOR;

IF focusStep <> -1 THEN
    Debug.StepFocus.StepId := Steps[focusStep].Id;
    Debug.StepFocus.StepName := Steps[focusStep].Name;

    (* Actions de cette étape, groupées par Kind *)
    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS-1 DO
        IF Actions[aIdx].BoundStepId = focusStep THEN

            IF Actions[aIdx].Kind = 1 THEN
                Debug.StepFocus.Kind1Line := CONCAT_STR(Debug.StepFocus.Kind1Line, Actions[aIdx].Name);
                Debug.StepFocus.Kind1Line := CONCAT_STR(Debug.StepFocus.Kind1Line, ', ');

            ELSIF Actions[aIdx].Kind = 2 THEN
                Debug.StepFocus.Kind2Line := CONCAT_STR(Debug.StepFocus.Kind2Line, Actions[aIdx].Name);
                Debug.StepFocus.Kind2Line := CONCAT_STR(Debug.StepFocus.Kind2Line, '), ');

            ELSIF Actions[aIdx].Kind = 3 THEN
                Debug.StepFocus.Kind3Line := CONCAT_STR(Debug.StepFocus.Kind3Line, Actions[aIdx].Name);
                Debug.StepFocus.Kind3Line := CONCAT_STR(Debug.StepFocus.Kind3Line, ', ');

            ELSIF Actions[aIdx].Kind = 4 THEN
                Debug.StepFocus.Kind4Line := CONCAT_STR(Debug.StepFocus.Kind4Line, Actions[aIdx].Name);
                Debug.StepFocus.Kind4Line := CONCAT_STR(Debug.StepFocus.Kind4Line, '), ');

            ELSIF Actions[aIdx].Kind = 5 THEN
                Debug.StepFocus.Kind5Line := CONCAT_STR(Debug.StepFocus.Kind5Line, Actions[aIdx].Name);
                Debug.StepFocus.Kind5Line := CONCAT_STR(Debug.StepFocus.Kind5Line, ', ');

            ELSIF Actions[aIdx].Kind = 6 THEN
                Debug.StepFocus.Kind6Line := CONCAT_STR(Debug.StepFocus.Kind6Line, Actions[aIdx].Name);
                Debug.StepFocus.Kind6Line := CONCAT_STR(Debug.StepFocus.Kind6Line, '), ');

            ELSIF Actions[aIdx].Kind = 7 THEN
                Debug.StepFocus.Kind7Line := CONCAT_STR(Debug.StepFocus.Kind7Line, Actions[aIdx].Name);
                Debug.StepFocus.Kind7Line := CONCAT_STR(Debug.StepFocus.Kind7Line, '), ');

            ELSIF Actions[aIdx].Kind = 8 THEN
                Debug.StepFocus.Kind8Line := CONCAT_STR(Debug.StepFocus.Kind8Line, Actions[aIdx].Name);
                Debug.StepFocus.Kind8Line := CONCAT_STR(Debug.StepFocus.Kind8Line, '), ');

            ELSIF Actions[aIdx].Kind = 9 THEN
                Debug.StepFocus.Kind9Line := CONCAT_STR(Debug.StepFocus.Kind9Line, Actions[aIdx].Name);
                Debug.StepFocus.Kind9Line := CONCAT_STR(Debug.StepFocus.Kind9Line, '), ');

            ELSIF Actions[aIdx].Kind = 10 THEN
                Debug.StepFocus.Kind10Line := CONCAT_STR(Debug.StepFocus.Kind10Line, Actions[aIdx].Name);
                Debug.StepFocus.Kind10Line := CONCAT_STR(Debug.StepFocus.Kind10Line, '), ');

	    ELSIF Actions[aIdx].Kind = 11 THEN
                Debug.StepFocus.Kind11Line := CONCAT_STR(Debug.StepFocus.Kind11Line, Actions[aIdx].Name);
                Debug.StepFocus.Kind11Line := CONCAT_STR(Debug.StepFocus.Kind11Line, '), ');
            END_IF;

        END_IF;
    END_FOR;

    (* Transitions sortantes + destination + CondText *)
    FOR tIdx := 0 TO Runtime.GC_MAX_TRANSITIONS-1 DO
        isFromStep := FALSE;

        IF Transitions[tIdx].Enabled THEN
            FOR j := 0 TO Transitions[tIdx].SourceCount-1 DO
                IF Transitions[tIdx].Sources[j] = focusStep THEN
                    isFromStep := TRUE;
                END_IF;
            END_FOR;

            IF isFromStep THEN
                IF (Transitions[tIdx].Destinations[0] >= 0) AND (Transitions[tIdx].Destinations[0] < Runtime.GC_MAX_STEPS) THEN
                    srcId := Transitions[tIdx].Sources[0];
		    dstId := Transitions[tIdx].Destinations[0];

                    Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, 'TR{');
                    Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, Steps[srcId].Name);
                    Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, '->');
                    Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, Steps[dstId].Name);
                    Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, '}');
                    Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, ' [');
                    Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, Transitions[tIdx].CondText);
                    Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, '] ');
                END_IF;
            END_IF;
        END_IF;
    END_FOR;
END_IF;

Runtime.Phase := 0;
Debug.CurrentCycle := Runtime.Cycle;
Debug.Mode := Runtime.Mode;
Debug.Phase := Runtime.Phase;

