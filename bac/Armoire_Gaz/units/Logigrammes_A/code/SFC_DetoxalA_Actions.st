(* ____________________________________________________________________ *)
(* DETOX_X0 *)


(* ____________________________________________________________________ *)
(* DETOX_X1 *)
IF Acts_DetoxalA[0].Out THEN
	armoires[0].sorties.V3.F0 := FALSE;
	armoires[0].sorties.V3.F1 := TRUE;
	armoires[0].sorties.V4.F0 := FALSE;
	armoires[0].sorties.V4.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* DETOX_X2 *)
IF Acts_DetoxalA[1].Out THEN
	armoires[0].sorties.V3.F0 := TRUE;
	armoires[0].sorties.V3.F1 := FALSE;
	armoires[0].sorties.V4.F0 := TRUE;
	armoires[0].sorties.V4.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* DETOX_X3 *)
IF Acts_DetoxalA[2].Out THEN
	armoires[0].sorties.V3.F0 := TRUE;
	armoires[0].sorties.V3.F1 := FALSE;
	armoires[0].sorties.V4.F0 := TRUE;
	armoires[0].sorties.V4.F1 := FALSE;
END_IF;
