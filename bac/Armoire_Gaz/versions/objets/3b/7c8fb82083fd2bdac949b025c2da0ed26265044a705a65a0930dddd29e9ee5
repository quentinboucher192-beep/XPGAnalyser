FOR index := 0 TO 1 DO

    (* ____________________________________________________________________ *)
    (* SURV_X0 *)
    IF Acts_Surv[0].Out THEN
        armoires[index].compteur_cycle_surpression := 0;
    END_IF;

    (* ____________________________________________________________________ *)
    (* SURV_X1 *)
    IF Acts_Surv[1].Out THEN
        IF armoires[index].compteur_cycle_surpression >= config_utilisee.surpression.NBS
           AND NOT armoires[index].defs.surpression THEN

            armoires[index].defs.surpression := TRUE;
            armoires[index].compteur_cycle_surpression := 0;
        END_IF;
    END_IF;

    (* ____________________________________________________________________ *)
    (* SURV_X2 *)
    IF Acts_Surv[2].Out THEN
        armoires[index].compteur_cycle_surpression := 0;
    END_IF;

END_FOR;
