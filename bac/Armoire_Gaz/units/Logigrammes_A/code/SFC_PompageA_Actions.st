(* ____________________________________________________________________ *)
(* POMPAGEA_X1 *)
IF Acts_PompageA[0].Out THEN
	armoires[0].sorties.V2.F0 := TRUE;
	armoires[0].sorties.V3.F0 := TRUE;
	armoires[0].sorties.V4.F0 := FALSE;
	armoires[0].sorties.V5.F0 := TRUE;
	armoires[0].sorties.V6.F0 := TRUE;
	armoires[0].sorties.V16.F0 := TRUE;
	armoires[0].sorties.V7.F0 := TRUE;
	armoires[0].sorties.VS.F0 := TRUE;
	armoires[0].sorties.VHE.F0 := TRUE;
	armoires[0].sorties.VBTE.F0 := TRUE;
	armoires[0].sorties.PPE.F0 := TRUE;
	armoires[0].sorties.V2.F1 := FALSE;
	armoires[0].sorties.V3.F1 := FALSE;
	armoires[0].sorties.V4.F1 := TRUE;
	armoires[0].sorties.V5.F1 := FALSE;
	armoires[0].sorties.V6.F1 := FALSE;
	armoires[0].sorties.V16.F1 := FALSE;
	armoires[0].sorties.V7.F1 := FALSE;
	armoires[0].sorties.VS.F1 := FALSE;
	armoires[0].sorties.VHE.F1 := FALSE;
	armoires[0].sorties.VBTE.F1 := FALSE;
	armoires[0].sorties.PPE.F1 := FALSE;
END_IF;

IF Acts_PompageA[1].Out THEN
	armoires[0].sorties.PPE.F0 := FALSE;
	armoires[0].sorties.PPE.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEA_X2 *)
IF Acts_PompageA[2].Out THEN
	armoires[0].sorties.V9.F0 := FALSE;
	armoires[0].sorties.V9.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEA_X3 *)

IF Acts_PompageA[3].Out THEN
	armoires[0].sorties.PPE.F0 := TRUE;
	armoires[0].sorties.PPE.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEA_X5 *)
IF Acts_PompageA[4].Out THEN
	armoires[0].compteur_cycle_pompage := 0;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEA_X6 *)
IF Acts_PompageA[5].Out THEN
	armoires[0].sorties.V9.F0 := TRUE;
	armoires[0].sorties.V9.F1 := FALSE;
	armoires[0].sorties.V4.F0 := FALSE;
	armoires[0].sorties.V4.F1 := TRUE;
	armoires[0].sorties.PPE.F0 := FALSE;
	armoires[0].sorties.PPE.F1 := TRUE;
	armoires[0].compteur_cycle_pompage := armoires[0].compteur_cycle_pompage + 1;
END_IF;

IF Acts_PompageA[6].Out THEN
	armoires[0].sorties.V2.F0 := FALSE;
	armoires[0].sorties.V2.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEA_X7 *)

IF Acts_PompageA[7].Out THEN
	armoires[0].sorties.V2.F0 := TRUE;
	armoires[0].sorties.V2.F1 := FALSE;
END_IF;

IF Acts_PompageA[8].Out THEN
	armoires[0].sorties.V9.F0 := FALSE;
	armoires[0].sorties.V9.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEA_X8 *)

IF Acts_PompageA[9].Out THEN
	armoires[0].sorties.V9.F0 := TRUE;
	armoires[0].sorties.V9.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEA_X9 *)

IF Acts_PompageA[10].Out THEN
	armoires[0].sorties.V2.F0 := TRUE;
	armoires[0].sorties.V2.F1 := FALSE;
	armoires[0].sorties.V9.F0 := TRUE;
	armoires[0].sorties.V9.F1 := FALSE;
	armoires[0].sorties.PPE.F0 := TRUE;
	armoires[0].sorties.PPE.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEA_X10 *)

IF Acts_PompageA[11].Out THEN
	Close(arm:=armoires[0]);
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEA_X11 *)

IF Acts_PompageA[12].Out THEN
	armoires[0].sorties.V9.F0 := TRUE;
	armoires[0].sorties.V9.F1 := FALSE;
END_IF;

IF Acts_PompageA[13].Out THEN
	armoires[0].sorties.V2.F0 := FALSE;
	armoires[0].sorties.V2.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEA_X12 *)

IF Acts_PompageA[14].Out THEN
	Close(arm:=armoires[0]);
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEA_X13 *)

IF Acts_PompageA[15].Out THEN
	Close(arm:=armoires[0]);
END_IF;
