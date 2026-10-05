(* ____________________________________________________________________ *)
(* HELIUM_AVB_X0 *)


(* ____________________________________________________________________ *)
(* HELIUM_AVB_X1 *)
IF Acts_HeliumAVB[0].Out AND Gc_HeliumAVB.ModeReqOut = 1  THEN
	Close(arm:=armoires[1]);
	armoires[1].sorties.VHE.F0 := FALSE;
	armoires[1].sorties.VHE.F1 := TRUE;
	armoires[1].sorties.V4.F0 := FALSE;
	armoires[1].sorties.V4.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_AVB_X2 *)
IF Acts_HeliumAVB[1].Out THEN
	Close(arm:=armoires[1]);
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_AVB_X3 *)
IF Acts_HeliumAVB[2].Out THEN
	index_purge_B := 2;
	InitDone_PurgeB := FALSE;
	Ctrl_PurgeB.Cmd.ModeReq := 1;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
END_IF;

IF Acts_HeliumAVB[3].Out THEN
	Ctrl_PurgeB.Cmd.ModeReq := 0;
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_AVB_X4 *)
IF Acts_HeliumAVB[4].Out THEN
	index_pompage_B := 2;
	InitDone_PompageB := FALSE;
	Ctrl_PompageB.Cmd.ModeReq := 1;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
END_IF;

IF Acts_HeliumAVB[5].Out THEN
	Ctrl_PompageB.Cmd.ModeReq := 0;
END_IF;
