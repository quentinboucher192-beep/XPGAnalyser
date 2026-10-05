(* ____________________________________________________________________ *)
(* HELIUM_APB_X0 *)


(* ____________________________________________________________________ *)
(* HELIUM_APB_X1 *)

IF Acts_HeliumAPB[0].Out THEN
	index_purge_B := 2;
	InitDone_PurgeB := FALSE;
	Ctrl_PurgeB.Cmd.ModeReq := 1;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
END_IF;

IF Acts_HeliumAPB[1].Out THEN
	Ctrl_PurgeB.Cmd.ModeReq := 0;
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_APB_X2 *)

IF Acts_HeliumAPB[2].Out THEN
	index_pompage_B := 2;
	InitDone_PompageB := FALSE;
	Ctrl_PompageB.Cmd.ModeReq := 1;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
END_IF;

IF Acts_HeliumAPB[3].Out THEN
	Ctrl_PompageB.Cmd.ModeReq := 0;
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_APB_X4 *)

IF Acts_HeliumAPB[4].Out THEN
	armoires[1].sorties.V2.F0 := TRUE;
	armoires[1].sorties.V2.F1 := FALSE;
	armoires[1].sorties.V4.F0 := TRUE;
	armoires[1].sorties.V4.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_APB_X5 *)

IF Acts_HeliumAPB[5].Out THEN
	armoires[1].sorties.VHE.F0 := TRUE;
	armoires[1].sorties.VHE.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_APB_X6 *)

IF Acts_HeliumAPB[6].Out THEN
	armoires[1].sorties.V7.F0 := FALSE;
	armoires[1].sorties.V7.F1 := TRUE;
END_IF;

IF Acts_HeliumAPB[7].Out THEN
	armoires[1].sorties.V3.F0 := FALSE;
	armoires[1].sorties.V3.F1 := TRUE;
	armoires[1].sorties.V4.F0 := FALSE;
	armoires[1].sorties.V4.F1 := TRUE;
END_IF;

IF Acts_HeliumAPB[8].Out THEN
	armoires[1].sorties.V3.F0 := TRUE;
	armoires[1].sorties.V3.F1 := FALSE;
	armoires[1].sorties.V4.F0 := TRUE;
	armoires[1].sorties.V4.F1 := FALSE;
END_IF;

IF Acts_HeliumAPB[9].Out THEN
	armoires[1].sorties.V7.F0 := TRUE;
	armoires[1].sorties.V7.F1 := FALSE;
END_IF;

IF Acts_HeliumAPB[10].Out THEN
	armoires[1].sorties.VHE.F0 := FALSE;
	armoires[1].sorties.VHE.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_APB_X7 *)

IF Acts_HeliumAPB[11].Out THEN
	armoires[1].sorties.VHE.F0 := TRUE;
	armoires[1].sorties.VHE.F1 := FALSE;
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_APB_X9 *)

IF Acts_HeliumAPB[12].Out THEN
	index_purge_B := 2;
	InitDone_PurgeB := FALSE;
	Ctrl_PurgeB.Cmd.ModeReq := 1;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
END_IF;

IF Acts_HeliumAPB[13].Out THEN
	Ctrl_PurgeB.Cmd.ModeReq := 0;
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_APB_X10 *)

IF Acts_HeliumAPB[14].Out THEN
	index_pompage_B := 2;
	InitDone_PompageB := FALSE;
	Ctrl_PompageB.Cmd.ModeReq := 1;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
END_IF;

IF Acts_HeliumAPB[15].Out THEN
	Ctrl_PompageB.Cmd.ModeReq := 0;
END_IF;
