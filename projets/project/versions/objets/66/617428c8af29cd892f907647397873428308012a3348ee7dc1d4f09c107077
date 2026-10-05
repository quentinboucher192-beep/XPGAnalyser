(* 5b HISTO ETAPES ACTIVES (glissant sur MAX_HIST) : une ligne par etape
   qui vient de s'activer *)
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

        FOR h := Runtime.GC_MAX_HIST - 1 TO 1 BY -1 DO
            Debug.HistSteps[h].Line := Debug.HistSteps[h - 1].Line;
            Debug.HistSteps[h].NtpcNow := Debug.HistSteps[h - 1].NtpcNow;
        END_FOR;
        Debug.HistSteps[0].Line := Steps[i].Name;
        Debug.HistSteps[0].NtpcNow := Runtime.NtpcNow;

        IF HistFill < Runtime.GC_MAX_HIST THEN
            HistFill := HistFill + 1;
        END_IF;
    END_IF;
END_FOR;

FOR i := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO

    sid := Actions[i].BoundStepId;

    IF Actions[i].Out AND NOT Actions[i].PrevOut AND (sid >= 0) AND (sid < Runtime.GC_MAX_STEPS) THEN

        FOR h := Runtime.GC_MAX_HIST - 1 TO 1 BY -1 DO
            Debug.HistActions[h].Line := Debug.HistActions[h-1].Line;
            Debug.HistActions[h].NtpcNow := Debug.HistActions[h-1].NtpcNow;
        END_FOR;

        Debug.HistActions[0].Line := '1';

        stepIdStr := INT_TO_STRING(Steps[sid].Id);
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
                        CONCAT_STR(Steps[sid].Name, ':')
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
   DEBUG FOCUS : 1 seule etape active affichee, la PREMIERE
   ========================================================= *)
Debug.ActiveStepCount := 0;
FOR i := 0 TO Runtime.GC_MAX_STEPS - 1 DO
    IF Steps[i].Active THEN
        Debug.ActiveStepCount := Debug.ActiveStepCount + 1;
    END_IF;
END_FOR;
Debug.NoActiveStepFault := (Debug.ActiveStepCount = 0);

(* Reset focus *)
Debug.StepFocus.StepId := -1;
Debug.StepFocus.StepName := '';

Debug.StepFocus.Kind0Line := 'K0 : ';
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

(* premiere etape active *)
FOR sIdx := 0 TO Runtime.GC_MAX_STEPS - 1 DO
    IF Steps[sIdx].Active AND (focusStep = -1) THEN
        focusStep := sIdx;
    END_IF;
END_FOR;

IF focusStep <> -1 THEN
    Debug.StepFocus.StepId := Steps[focusStep].Id;
    Debug.StepFocus.StepName := Steps[focusStep].Name;

    (* Actions nommees de cette etape, groupees par Kind ; une ligne pleine
       (32 caracteres) garde ce qu'elle a *)
    FOR aIdx := 0 TO Runtime.GC_MAX_ACTIONS - 1 DO
        IF (Actions[aIdx].BoundStepId = focusStep) AND (LEN_INT(Actions[aIdx].Name) > 0)
           AND (Actions[aIdx].Kind >= 0) AND (Actions[aIdx].Kind <= 11) THEN

            CASE Actions[aIdx].Kind OF
                0: lineTmp := Debug.StepFocus.Kind0Line;
                1: lineTmp := Debug.StepFocus.Kind1Line;
                2: lineTmp := Debug.StepFocus.Kind2Line;
                3: lineTmp := Debug.StepFocus.Kind3Line;
                4: lineTmp := Debug.StepFocus.Kind4Line;
                5: lineTmp := Debug.StepFocus.Kind5Line;
                6: lineTmp := Debug.StepFocus.Kind6Line;
                7: lineTmp := Debug.StepFocus.Kind7Line;
                8: lineTmp := Debug.StepFocus.Kind8Line;
                9: lineTmp := Debug.StepFocus.Kind9Line;
                10: lineTmp := Debug.StepFocus.Kind10Line;
                11: lineTmp := Debug.StepFocus.Kind11Line;
            END_CASE;

            IF LEN_INT(lineTmp) + LEN_INT(Actions[aIdx].Name) + 2 <= 32 THEN
                lineTmp := CONCAT_STR(CONCAT_STR(lineTmp, Actions[aIdx].Name), ', ');
            END_IF;

            CASE Actions[aIdx].Kind OF
                0: Debug.StepFocus.Kind0Line := lineTmp;
                1: Debug.StepFocus.Kind1Line := lineTmp;
                2: Debug.StepFocus.Kind2Line := lineTmp;
                3: Debug.StepFocus.Kind3Line := lineTmp;
                4: Debug.StepFocus.Kind4Line := lineTmp;
                5: Debug.StepFocus.Kind5Line := lineTmp;
                6: Debug.StepFocus.Kind6Line := lineTmp;
                7: Debug.StepFocus.Kind7Line := lineTmp;
                8: Debug.StepFocus.Kind8Line := lineTmp;
                9: Debug.StepFocus.Kind9Line := lineTmp;
                10: Debug.StepFocus.Kind10Line := lineTmp;
                11: Debug.StepFocus.Kind11Line := lineTmp;
            END_CASE;

        END_IF;
    END_FOR;

    (* Transitions sortantes + destination + CondText, tant que la ligne
       (100 caracteres) a la place *)
    FOR tIdx := 0 TO Runtime.GC_MAX_TRANSITIONS - 1 DO
        isFromStep := FALSE;

        IF Transitions[tIdx].Enabled THEN
            nSrc := Transitions[tIdx].SourceCount;
            IF nSrc > 3 THEN nSrc := 3; END_IF;
            FOR j := 0 TO nSrc - 1 DO
                IF Transitions[tIdx].Sources[j] = focusStep THEN
                    isFromStep := TRUE;
                END_IF;
            END_FOR;

            IF isFromStep AND (Transitions[tIdx].DestCount >= 1) THEN
                srcId := Transitions[tIdx].Sources[0];
                dstId := Transitions[tIdx].Destinations[0];
                IF (srcId >= 0) AND (srcId < Runtime.GC_MAX_STEPS) AND (dstId >= 0) AND (dstId < Runtime.GC_MAX_STEPS) THEN
                    IF LEN_INT(Debug.StepFocus.TransitionsLine) + LEN_INT(Steps[srcId].Name) + LEN_INT(Steps[dstId].Name)
                       + LEN_INT(Transitions[tIdx].CondText) + 10 <= 100 THEN
                        Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, 'TR{');
                        Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, Steps[srcId].Name);
                        Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, '->');
                        Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, Steps[dstId].Name);
                        Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, '} [');
                        Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, Transitions[tIdx].CondText);
                        Debug.StepFocus.TransitionsLine := CONCAT_STR(Debug.StepFocus.TransitionsLine, '] ');
                    END_IF;
                END_IF;
            END_IF;
        END_IF;
    END_FOR;
END_IF;

Runtime.Phase := 0;
Debug.CurrentCycle := Runtime.Cycle;
Debug.Mode := Runtime.Mode;
Debug.Phase := Runtime.Phase;

