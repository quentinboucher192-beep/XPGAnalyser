(* ____________________________________________________________________ *)
(* POMPAGEB_X1 *)
IF Acts_PompageB[0].Out THEN
	armoires[1].sorties.V2.F0 := TRUE;
	armoires[1].sorties.V3.F0 := TRUE;
	armoires[1].sorties.V4.F0 := FALSE;
	armoires[1].sorties.V5.F0 := TRUE;
	armoires[1].sorties.V6.F0 := TRUE;
	armoires[1].sorties.V16.F0 := TRUE;
	armoires[1].sorties.V7.F0 := TRUE;
	armoires[1].sorties.VS.F0 := TRUE;
	armoires[1].sorties.VHE.F0 := TRUE;
	armoires[1].sorties.VBTE.F0 := TRUE;
	armoires[1].sorties.PPE.F0 := TRUE;
	armoires[1].sorties.V2.F1 := FALSE;
	armoires[1].sorties.V3.F1 := FALSE;
	armoires[1].sorties.V4.F1 := TRUE;
	armoires[1].sorties.V5.F1 := FALSE;
	armoires[1].sorties.V6.F1 := FALSE;
	armoires[1].sorties.V16.F1 := FALSE;
	armoires[1].sorties.V7.F1 := FALSE;
	armoires[1].sorties.VS.F1 := FALSE;
	armoires[1].sorties.VHE.F1 := FALSE;
	armoires[1].sorties.VBTE.F1 := FALSE;
	armoires[1].sorties.PPE.F1 := FALSE;
END_IF;

IF Acts_PompageB[1].Out THEN
	armoires[1].sorties.PPE.F0 := FALSE;
	armoires[1].sorties.PPE.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEB_X2 *)
IF Acts_PompageB[2].Out THEN
	armoires[1].sorties.V9.F0 := FALSE;
	armoires[1].sorties.V9.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEB_X3 *)

IF Acts_PompageB[3].Out THEN
	armoires[1].sorties.PPE.F0 := TRUE;
	armoires[1].sorties.PPE.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEB_X5 *)
IF Acts_PompageB[4].Out THEN
	armoires[1].compteur_cycle_pompage := 0;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEB_X6 *)
IF Acts_PompageB[5].Out THEN
	armoires[1].sorties.V9.F0 := TRUE;
	armoires[1].sorties.V9.F1 := FALSE;
	armoires[1].sorties.V4.F0 := FALSE;
	armoires[1].sorties.V4.F1 := TRUE;
	armoires[1].sorties.PPE.F0 := FALSE;
	armoires[1].sorties.PPE.F1 := TRUE;
	armoires[1].compteur_cycle_pompage := armoires[1].compteur_cycle_pompage + 1;
END_IF;

IF Acts_PompageB[6].Out THEN
	armoires[1].sorties.V2.F0 := FALSE;
	armoires[1].sorties.V2.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEB_X7 *)

IF Acts_PompageB[7].Out THEN
	armoires[1].sorties.V2.F0 := TRUE;
	armoires[1].sorties.V2.F1 := FALSE;
END_IF;

IF Acts_PompageB[8].Out THEN
	armoires[1].sorties.V9.F0 := FALSE;
	armoires[1].sorties.V9.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEB_X8 *)

IF Acts_PompageB[9].Out THEN
	armoires[1].sorties.V9.F0 := TRUE;
	armoires[1].sorties.V9.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEB_X9 *)

IF Acts_PompageB[10].Out THEN
	armoires[1].sorties.V2.F0 := TRUE;
	armoires[1].sorties.V2.F1 := FALSE;
	armoires[1].sorties.V9.F0 := TRUE;
	armoires[1].sorties.V9.F1 := FALSE;
	armoires[1].sorties.PPE.F0 := TRUE;
	armoires[1].sorties.PPE.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEB_X10 *)

IF Acts_PompageB[11].Out THEN
	Close(arm:=armoires[1]);
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEB_X11 *)

IF Acts_PompageB[12].Out THEN
	armoires[1].sorties.V9.F0 := TRUE;
	armoires[1].sorties.V9.F1 := FALSE;
END_IF;

IF Acts_PompageB[13].Out THEN
	armoires[1].sorties.V2.F0 := FALSE;
	armoires[1].sorties.V2.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEB_X12 *)

IF Acts_PompageB[14].Out THEN
	Close(arm:=armoires[1]);
END_IF;

(* ____________________________________________________________________ *)
(* POMPAGEB_X13 *)

IF Acts_PompageB[15].Out THEN
	Close(arm:=armoires[1]);
END_IF;
