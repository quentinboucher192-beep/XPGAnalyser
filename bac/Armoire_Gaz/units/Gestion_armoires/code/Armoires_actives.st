FOR index := 0 TO 1 DO

    armoires[index].active := armoires[index].prete AND index_armoire_utilisee = index;

    IF armoires[index].active THEN
        armoires[index].CurrentTime := armoires[index].CurrentTime + UDINT_TO_TIME(CycleMs);
        
        armoires[index].CurrentTimeHours   := TIME_TO_UDINT(armoires[index].CurrentTime) / 3600000;
        armoires[index].CurrentTimeMinutes := (TIME_TO_UDINT(armoires[index].CurrentTime) MOD 3600000) / 60000;
    END_IF;

    IF reset_timer[index] THEN
        armoires[index].TotalTimeMs := 0;
    END_IF; 

    IF armoires[index].PrevActive AND NOT armoires[index].active THEN
        armoires[index].TotalTimeMs := armoires[index].TotalTimeMs + TIME_TO_UDINT(armoires[index].CurrentTime);
        armoires[index].CurrentTime := T#0ms;
        armoires[index].CurrentTimeHours := 0;
        armoires[index].CurrentTimeMinutes := 0;
    END_IF;

    armoires[index].TotalTimeHours := armoires[index].TotalTimeMs / 3600000;

    armoires[index].PrevActive := armoires[index].active;

END_FOR;

