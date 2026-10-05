(* ____________________________________________________________________ *)
(* HELIUM_AVA_X0 *)


(* ____________________________________________________________________ *)
(* HELIUM_AVA_X1 *)
IF Acts_HeliumAVA[0].Out AND Gc_HeliumAVA.ModeReqOut = 1 THEN
	Close(arm:=armoires[0]);
	armoires[0].sorties.VHE.F0 := FALSE;
	armoires[0].sorties.VHE.F1 := TRUE;
	armoires[0].sorties.V4.F0 := FALSE;
	armoires[0].sorties.V4.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_AVA_X2 *)
IF Acts_HeliumAVA[1].Out THEN
	Close(arm:=armoires[0]);
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_AVA_X3 *)
IF Acts_HeliumAVA[2].Out THEN
	index_purge_A := 2;
	InitDone_PurgeA := FALSE;
	Ctrl_PurgeA.Cmd.ModeReq := 1;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
END_IF;

IF Acts_HeliumAVA[3].Out THEN
	Ctrl_PurgeA.Cmd.ModeReq := 0;
END_IF;

(* ____________________________________________________________________ *)
(* HELIUM_AVA_X4 *)
IF Acts_HeliumAVA[4].Out THEN
	index_pompage_A := 2;
	InitDone_PompageA := FALSE;
	Ctrl_PompageA.Cmd.ModeReq := 1;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
END_IF;

IF Acts_HeliumAVA[5].Out THEN
	Ctrl_PompageA.Cmd.ModeReq := 0;
END_IF;
