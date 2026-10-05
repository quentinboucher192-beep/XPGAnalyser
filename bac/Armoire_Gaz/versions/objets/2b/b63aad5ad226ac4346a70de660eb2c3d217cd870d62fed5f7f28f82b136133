FOR IndexForcage := 0 TO 15 DO
    IF NOT((Commands_TOR16_I4_TOGGLE AND SHL(DINT#1, IndexForcage)) <> DINT#0) THEN
        Tempon_TOR16_I4_IN[IndexForcage] := Tempon_TOR16_I4[IndexForcage];
    END_IF;
END_FOR;

IF Commands_TOR16_I4_F1 <> 0 THEN
	FOR IndexForcage := 0 TO 15 DO
	    IF (Commands_TOR16_I4_F1 AND SHL(DINT#1, IndexForcage)) <> DINT#0 THEN
	        IF IndexForcage <= 15 THEN
		    Tempon_TOR16_I4_IN[IndexForcage] := TRUE;
		END_IF;
	    END_IF;
	END_FOR;
END_IF;

IF Commands_TOR16_I4_F0 <> 0 THEN
	FOR IndexForcage := 0 TO 15 DO
	    IF (Commands_TOR16_I4_F0 AND SHL(DINT#1, IndexForcage)) <> DINT#0 THEN
	        IF IndexForcage <= 15 THEN
	            Tempon_TOR16_I4_IN[IndexForcage] := FALSE;
	        END_IF;
	    END_IF;
	END_FOR;
END_IF;

Tempon_TOR16_I4_OUT := Tempon_TOR16_I4_IN;
Commands_TOR16_I4_STATES := DWORD#0;

FOR IndexForcage := 0 TO 15 DO
    IF Tempon_TOR16_I4_OUT[IndexForcage] THEN
        IF IndexForcage <= 15 THEN
            Commands_TOR16_I4_STATES := Commands_TOR16_I4_STATES OR SHL(DWORD#1, IndexForcage);
	END_IF;
    END_IF;
END_FOR;

(* visibilité *)
Commands_TOR16_I4_STATES := DWORD#0;

FOR IndexForcage := 0 TO 15 DO
    IF Tempon_TOR16_I4_OUT[IndexForcage] THEN
        Commands_TOR16_I4_STATES :=
            Commands_TOR16_I4_STATES OR SHL(DWORD#1, IndexForcage);
    END_IF;
END_FOR;

TON_24V (IN := NOT(Tempon_TOR16_I4_OUT[0]), PT := t#3s);
TON_PORTE_A (IN := Tempon_TOR16_I4_OUT[5], PT := t#3s);
TON_EXT_A (IN := NOT(Tempon_TOR16_I4_OUT[12]), PT := t#3s);
TON_D10_A (IN := NOT(Tempon_TOR16_I4_OUT[8]) AND ConfigArmoireUtilisee.nb_departs >= 1, PT := t#3s);
TON_D11_A (IN := NOT(Tempon_TOR16_I4_OUT[7]) AND ConfigArmoireUtilisee.nb_departs >= 1, PT := t#3s);
TON_D20_A (IN := NOT(Tempon_TOR16_I4_OUT[10]) AND ConfigArmoireUtilisee.nb_departs >= 2, PT := t#3s);
TON_D21_A (IN := NOT(Tempon_TOR16_I4_OUT[9]) AND ConfigArmoireUtilisee.nb_departs >= 2, PT := t#3s);
TON_GAZROOM_A (IN := NOT(Tempon_TOR16_I4_OUT[2]), PT := t#3s);
TON_GAZBOX_A (IN := NOT(Tempon_TOR16_I4_OUT[3]), PT := t#3s);

Armoires[0].entrees.Manque_tension := TON_24V.Q;
Armoires[0].entrees.Auto_manu := Tempon_TOR16_I4_OUT[1];
Armoires[0].entrees.Extraction_inhibee := Tempon_TOR16_I4_OUT[15];
Armoires[0].entrees.Porte := TON_PORTE_A.Q;
Armoires[0].entrees.Detoxal := NOT(Tempon_TOR16_I4_OUT[11]) AND ConfigArmoireUtilisee.pres_detoxal;
Armoires[0].entrees.Extraction := TON_EXT_A.Q;
Armoires[0].entrees.Pirani := Tempon_TOR16_I4_OUT[13];
Armoires[0].entrees.Span_N2 := Tempon_TOR16_I4_OUT[14];
Armoires[0].entrees.D1[0] := TON_D10_A.Q;
Armoires[0].entrees.D1[1] := TON_D11_A.Q;
Armoires[0].entrees.D2[0] := TON_D20_A.Q;
Armoires[0].entrees.D2[1] := TON_D21_A.Q;
Armoires[0].entrees.AU := NOT(Tempon_TOR16_I4_OUT[4]);
Armoires[0].entrees.Fuite_gazroom := TON_GAZROOM_A.Q;
Armoires[0].entrees.Fuite_gazbox := TON_GAZBOX_A.Q;

FOR IndexForcage := 0 TO 15 DO
    IF NOT((Commands_TOR16_I7_TOGGLE AND SHL(DINT#1, IndexForcage)) <> DINT#0) THEN
        Tempon_TOR16_I7_IN[IndexForcage] := Tempon_TOR16_I7[IndexForcage];
    END_IF;
END_FOR;

IF Commands_TOR16_I7_F1 <> 0 THEN
	FOR IndexForcage := 0 TO 15 DO
	    IF (Commands_TOR16_I7_F1 AND SHL(DINT#1, IndexForcage)) <> DINT#0 THEN
	        IF IndexForcage <= 15 THEN
		    Tempon_TOR16_I7_IN[IndexForcage] := TRUE;
		END_IF;
	    END_IF;
	END_FOR;
END_IF;

IF Commands_TOR16_I7_F0 <> 0 THEN
	FOR IndexForcage := 0 TO 15 DO
	    IF (Commands_TOR16_I7_F0 AND SHL(DINT#1, IndexForcage)) <> DINT#0 THEN
	        IF IndexForcage <= 15 THEN
	            Tempon_TOR16_I7_IN[IndexForcage] := FALSE;
	        END_IF;
	    END_IF;
	END_FOR;
END_IF;

Tempon_TOR16_I7_OUT := Tempon_TOR16_I7_IN;
Commands_TOR16_I7_STATES := DWORD#0;

FOR IndexForcage := 0 TO 15 DO
    IF Tempon_TOR16_I7_OUT[IndexForcage] THEN
        IF IndexForcage <= 15 THEN
            Commands_TOR16_I7_STATES := Commands_TOR16_I7_STATES OR SHL(DWORD#1, IndexForcage);
	END_IF;
    END_IF;
END_FOR;

(* visibilité *)
Commands_TOR16_I7_STATES := DWORD#0;

FOR IndexForcage := 0 TO 15 DO
    IF Tempon_TOR16_I7_OUT[IndexForcage] THEN
        Commands_TOR16_I7_STATES :=
            Commands_TOR16_I7_STATES OR SHL(DWORD#1, IndexForcage);
    END_IF;
END_FOR;

TON_PORTE_B (IN := Tempon_TOR16_I7_OUT[5], PT := t#3s);
TON_EXT_B (IN := NOT(Tempon_TOR16_I7_OUT[12]), PT := t#3s);
TON_D10_B (IN := NOT(Tempon_TOR16_I7_OUT[8]) AND ConfigArmoireUtilisee.nb_departs >= 1, PT := t#3s);
TON_D11_B (IN := NOT(Tempon_TOR16_I7_OUT[7]) AND ConfigArmoireUtilisee.nb_departs >= 1, PT := t#3s);
TON_D20_B (IN := NOT(Tempon_TOR16_I7_OUT[10]) AND ConfigArmoireUtilisee.nb_departs >= 2, PT := t#3s);
TON_D21_B (IN := NOT(Tempon_TOR16_I7_OUT[9]) AND ConfigArmoireUtilisee.nb_departs >= 2, PT := t#3s);
TON_GAZROOM_B (IN := NOT(Tempon_TOR16_I7_OUT[2]), PT := t#3s);
TON_GAZBOX_B (IN := NOT(Tempon_TOR16_I7_OUT[3]), PT := t#3s);

Armoires[1].entrees.Manque_tension := TON_24V.Q;
Armoires[1].entrees.Auto_manu := Tempon_TOR16_I7_OUT[1];
Armoires[1].entrees.Extraction_inhibee := Tempon_TOR16_I7_OUT[15];
Armoires[1].entrees.Porte := TON_PORTE_B.Q;
Armoires[1].entrees.Detoxal := NOT(Tempon_TOR16_I7_OUT[11]) AND ConfigArmoireUtilisee.pres_detoxal;
Armoires[1].entrees.Extraction := NOT(Tempon_TOR16_I7_OUT[12]);
Armoires[1].entrees.Pirani := Tempon_TOR16_I7_OUT[13];
Armoires[1].entrees.Span_N2 := Tempon_TOR16_I7_OUT[14];
Armoires[1].entrees.D1[0] := TON_D10_B.Q;
Armoires[1].entrees.D1[1] := TON_D11_B.Q;
Armoires[1].entrees.D2[0] := TON_D20_B.Q;
Armoires[1].entrees.D2[1] := TON_D21_B.Q;
Armoires[1].entrees.AU := NOT(Tempon_TOR16_I7_OUT[4]);
Armoires[1].entrees.Fuite_gazroom := TON_GAZROOM_B.Q;
Armoires[1].entrees.Fuite_gazbox := TON_GAZBOX_B.Q;
