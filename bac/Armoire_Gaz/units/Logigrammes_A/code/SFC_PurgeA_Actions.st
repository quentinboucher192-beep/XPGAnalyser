(* ____________________________________________________________________ *)
(* PURGEA_X2 *)
IF Acts_PurgeA[0].Out THEN
	armoires[0].sorties.V7.F0 := FALSE;
	armoires[0].sorties.V7.F1 := TRUE;
END_IF;

IF Acts_PurgeA[1].Out THEN
	armoires[0].sorties.V3.F0 := FALSE;
	armoires[0].sorties.V3.F1 := TRUE;
	armoires[0].sorties.V4.F0 := FALSE;
	armoires[0].sorties.V4.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* PURGEA_X4 *)
IF Acts_PurgeA[2].Out THEN
	armoires[0].sorties.V3.F0 := TRUE;
	armoires[0].sorties.V3.F1 := FALSE;
	armoires[0].compteur_cycle_purge := 0;
END_IF;

(* ____________________________________________________________________ *)
(* PURGEA_X5 *)

IF Acts_PurgeA[3].Out THEN
	armoires[0].sorties.V2.F0 := FALSE;
	armoires[0].sorties.V2.F1 := TRUE;
END_IF;

IF Acts_PurgeA[4].Out THEN
	armoires[0].compteur_cycle_purge := armoires[0].compteur_cycle_purge + 1;
END_IF;

(* ____________________________________________________________________ *)
(* PURGEA_X6 *)

IF Acts_PurgeA[6].Out THEN
	armoires[0].sorties.V2.F0 := TRUE;
	armoires[0].sorties.V2.F1 := FALSE;
END_IF;

IF Acts_PurgeA[7].Out THEN
	armoires[0].sorties.V3.F0 := FALSE;
	armoires[0].sorties.V3.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* PURGEA_X7 *)
IF Acts_PurgeA[9].Out THEN
	armoires[0].sorties.V3.F0 := TRUE;
	armoires[0].sorties.V3.F1 := FALSE;
END_IF;


(* ____________________________________________________________________ *)
(* PURGEA_X8 *)

IF Acts_PurgeA[10].Out THEN
	armoires[0].sorties.V7.F0 := TRUE;
	armoires[0].sorties.V7.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* PURGEA_X9 *)

IF Acts_PurgeA[11].Out THEN
	Close(arm:=armoires[0]);
END_IF;

(* ____________________________________________________________________ *)
(* PURGEA_X10 *)

IF Acts_PurgeA[12].Out THEN
	Close(arm:=armoires[0]);
END_IF;
