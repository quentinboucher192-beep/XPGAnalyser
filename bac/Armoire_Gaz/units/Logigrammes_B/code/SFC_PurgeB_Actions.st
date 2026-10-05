(* ____________________________________________________________________ *)
(* PURGEB_X2 *)
IF Acts_PurgeB[0].Out THEN
	armoires[1].sorties.V7.F0 := FALSE;
	armoires[1].sorties.V7.F1 := TRUE;
END_IF;

IF Acts_PurgeB[1].Out THEN
	armoires[1].sorties.V3.F0 := FALSE;
	armoires[1].sorties.V3.F1 := TRUE;
	armoires[1].sorties.V4.F0 := FALSE;
	armoires[1].sorties.V4.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* PURGEB_X4 *)
IF Acts_PurgeB[2].Out THEN
	armoires[1].sorties.V3.F0 := TRUE;
	armoires[1].sorties.V3.F1 := FALSE;
	armoires[1].compteur_cycle_purge := 0;
END_IF;

(* ____________________________________________________________________ *)
(* PURGEB_X5 *)

IF Acts_PurgeB[3].Out THEN
	armoires[1].sorties.V2.F0 := FALSE;
	armoires[1].sorties.V2.F1 := TRUE;
END_IF;

IF Acts_PurgeB[4].Out THEN
	armoires[1].compteur_cycle_purge := armoires[1].compteur_cycle_purge + 1;
END_IF;

(* ____________________________________________________________________ *)
(* PURGEB_X6 *)

IF Acts_PurgeB[6].Out THEN
	armoires[1].sorties.V2.F0 := TRUE;
	armoires[1].sorties.V2.F1 := FALSE;
END_IF;

IF Acts_PurgeB[7].Out THEN
	armoires[1].sorties.V3.F0 := FALSE;
	armoires[1].sorties.V3.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* PURGEB_X7 *)
IF Acts_PurgeB[9].Out THEN
	armoires[1].sorties.V3.F0 := TRUE;
	armoires[1].sorties.V3.F1 := FALSE;
END_IF;


(* ____________________________________________________________________ *)
(* PURGEB_X8 *)

IF Acts_PurgeB[10].Out THEN
	armoires[1].sorties.V7.F0 := TRUE;
	armoires[1].sorties.V7.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* PURGEB_X9 *)

IF Acts_PurgeB[11].Out THEN
	Close(arm:=armoires[1]);
END_IF;

(* ____________________________________________________________________ *)
(* PURGEB_X10 *)

IF Acts_PurgeB[12].Out THEN
	Close(arm:=armoires[1]);
END_IF;
