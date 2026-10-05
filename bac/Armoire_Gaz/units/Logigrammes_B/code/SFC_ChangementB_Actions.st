(* ____________________________________________________________________ *)
(* CHGTB_X0 *)


(* ____________________________________________________________________ *)
(* CHGTB_X1 *)

IF Acts_ChangementB[1].Out THEN
	Close(arm:=armoires[1]);
	
	Ctrl_DetoxalB.Cmd.ModeReq := 0;
	Ctrl_DetoxalB.Cmd.InitReq := TRUE;
	Ctrl_PurgeB.Cmd.ModeReq := 0;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
	Ctrl_PompageB.Cmd.ModeReq := 0;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVB.Cmd.ModeReq := 0;
	Ctrl_HeliumAVB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPB.Cmd.ModeReq := 0;
	Ctrl_HeliumAPB.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 0;
	DEBUG_PURGE_0 := 0;
	DEBUG_POMPAGE_0 := 0;
	DEBUG_HELIUM_0 := 0;
	DEBUG_CHGT := 0;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_B := 0;
	index_pompage_B := 0;
	
	armoires[1].bouteille_presque_vide := false;
	armoires[1].bouteille_vide := false;
	if not(modification_majeure) and not(armoires[1].retour_sur_chgt) and not(armoires[1].retour_sur_chgt_ap_decl) then
		armoires[1].en_cours_de_chgt := false;
		armoires[1].en_cours_de_chgt_avant := false;
		armoires[1].en_cours_de_chgt_apres := false;
	end_if;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTB_X2 *)

IF Acts_ChangementB[2].Out THEN
	InitDone_DetoxalB := FALSE;

	Ctrl_DetoxalB.Cmd.ModeReq := 1;
	Ctrl_DetoxalB.Cmd.InitReq := TRUE;
	Ctrl_PurgeB.Cmd.ModeReq := 0;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
	Ctrl_PompageB.Cmd.ModeReq := 0;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVB.Cmd.ModeReq := 0;
	Ctrl_HeliumAVB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPB.Cmd.ModeReq := 0;
	Ctrl_HeliumAPB.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 1;
	DEBUG_PURGE_0 := 0;
	DEBUG_POMPAGE_0 := 0;
	DEBUG_HELIUM_0 := 0;
	DEBUG_CHGT := 0;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_B := 0;
	index_pompage_B := 0;
END_IF;

IF Acts_ChangementB[15].Out THEN
	Ctrl_DetoxalB.Cmd.ModeReq := 0;
	DEBUG_DETOXAL_0 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTB_X3 *)

IF Acts_ChangementB[3].Out THEN
	InitDone_PurgeB := FALSE;

	Ctrl_DetoxalB.Cmd.ModeReq := 0;
	Ctrl_DetoxalB.Cmd.InitReq := TRUE;
	Ctrl_PurgeB.Cmd.ModeReq := 1;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
	Ctrl_PompageB.Cmd.ModeReq := 0;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVB.Cmd.ModeReq := 0;
	Ctrl_HeliumAVB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPB.Cmd.ModeReq := 0;
	Ctrl_HeliumAPB.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 1;
	DEBUG_POMPAGE_0 := 0;
	DEBUG_HELIUM_0 := 0;
	DEBUG_CHGT := 0;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_B := 0;
	index_pompage_B := 0;
END_IF;

IF Acts_ChangementB[16].Out THEN
	Ctrl_PurgeB.Cmd.ModeReq := 0;
	DEBUG_PURGE_0 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTB_X4 *)

IF Acts_ChangementB[4].Out THEN
	InitDone_PompageB := FALSE;

	Ctrl_DetoxalB.Cmd.ModeReq := 0;
	Ctrl_DetoxalB.Cmd.InitReq := TRUE;
	Ctrl_PurgeB.Cmd.ModeReq := 0;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
	Ctrl_PompageB.Cmd.ModeReq := 1;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVB.Cmd.ModeReq := 0;
	Ctrl_HeliumAVB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPB.Cmd.ModeReq := 0;
	Ctrl_HeliumAPB.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 1;
	DEBUG_HELIUM_0 := 0;
	DEBUG_CHGT := 0;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_B := 0;
	index_pompage_B := 0;
END_IF;

IF Acts_ChangementB[17].Out THEN
	Ctrl_PompageB.Cmd.ModeReq := 0;
	DEBUG_POMPAGE_0 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTB_X5 *)

IF Acts_ChangementB[5].Out THEN
	InitDone_HeliumAVB := FALSE;

	Ctrl_DetoxalB.Cmd.ModeReq := 0;
	Ctrl_DetoxalB.Cmd.InitReq := TRUE;
	Ctrl_PurgeB.Cmd.ModeReq := 0;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
	Ctrl_PompageB.Cmd.ModeReq := 0;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVB.Cmd.ModeReq := 1;
	Ctrl_HeliumAVB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPB.Cmd.ModeReq := 0;
	Ctrl_HeliumAPB.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 1;
	DEBUG_CHGT := 0;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_B := 2;
	index_pompage_B := 2;
END_IF;

IF Acts_ChangementB[18].Out THEN
	Ctrl_HeliumAVB.Cmd.ModeReq := 0;
	DEBUG_HELIUM_0 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTB_X6 *)

IF Acts_ChangementB[6].Out THEN	
	Ctrl_DetoxalB.Cmd.ModeReq := 0;
	Ctrl_DetoxalB.Cmd.InitReq := TRUE;
	Ctrl_PurgeB.Cmd.ModeReq := 0;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
	Ctrl_PompageB.Cmd.ModeReq := 0;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVB.Cmd.ModeReq := 0;
	Ctrl_HeliumAVB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPB.Cmd.ModeReq := 0;
	Ctrl_HeliumAPB.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 2;
	DEBUG_CHGT := 1;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;
END_IF;

IF Steps_ChangementB[6].Active THEN
	armoires[1].sorties.V2.F0 := FALSE;
	armoires[1].sorties.V2.F1 := TRUE;
	armoires[1].sorties.V4.F0 := FALSE;
	armoires[1].sorties.V4.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTB_X7 *)

IF Acts_ChangementB[7].Out THEN

	Ctrl_DetoxalB.Cmd.ModeReq := 0;
	Ctrl_DetoxalB.Cmd.InitReq := TRUE;
	Ctrl_PurgeB.Cmd.ModeReq := 0;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
	Ctrl_PompageB.Cmd.ModeReq := 0;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVB.Cmd.ModeReq := 0;
	Ctrl_HeliumAVB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPB.Cmd.ModeReq := 0;
	Ctrl_HeliumAPB.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 2;
	DEBUG_CHGT := 1;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	armoires[1].en_cours_de_chgt_apres := TRUE;
	armoires[1].sorties.V2.F0 := TRUE;
	armoires[1].sorties.V2.F1 := FALSE;
	armoires[1].sorties.V4.F0 := TRUE;
	armoires[1].sorties.V4.F1 := FALSE;
END_IF;


IF Acts_ChangementB[8].Out THEN
	armoires[1].seuil_poids_utilise := armoires[1].seuil_poids_saisi;
	armoires[1].etalonBalance := REAL_TO_INT(armoires[1].ana.Balance.mes) - armoires[1].seuil_poids_utilise; 
	DEBUG_CHGT := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTB_X8 *)

IF Acts_ChangementB[9].Out THEN
	InitDone_DetoxalB := FALSE;

	Ctrl_DetoxalB.Cmd.ModeReq := 1;
	Ctrl_DetoxalB.Cmd.InitReq := TRUE;
	Ctrl_PurgeB.Cmd.ModeReq := 0;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
	Ctrl_PompageB.Cmd.ModeReq := 0;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVB.Cmd.ModeReq := 0;
	Ctrl_HeliumAVB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPB.Cmd.ModeReq := 0;
	Ctrl_HeliumAPB.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 2;
	DEBUG_CHGT := 2;
	DEBUG_DETOXAL_1 := 1;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_B := 1;
	index_pompage_B := 1;
END_IF;

IF Acts_ChangementB[19].Out THEN
	Ctrl_DetoxalB.Cmd.ModeReq := 0;
	DEBUG_DETOXAL_1 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTB_X9 *)

IF Acts_ChangementB[10].Out THEN
	InitDone_PurgeB := FALSE;

	Ctrl_DetoxalB.Cmd.ModeReq := 0;
	Ctrl_DetoxalB.Cmd.InitReq := TRUE;
	Ctrl_PurgeB.Cmd.ModeReq := 1;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
	Ctrl_PompageB.Cmd.ModeReq := 0;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVB.Cmd.ModeReq := 0;
	Ctrl_HeliumAVB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPB.Cmd.ModeReq := 0;
	Ctrl_HeliumAPB.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 2;
	DEBUG_CHGT := 2;
	DEBUG_DETOXAL_1 := 2;
	DEBUG_PURGE_1 := 1;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_B := 1;
	index_pompage_B := 1;
END_IF;

IF Acts_ChangementB[20].Out THEN
	Ctrl_PurgeB.Cmd.ModeReq := 0;
	DEBUG_PURGE_1 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTB_X10 *)

IF Acts_ChangementB[11].Out THEN
	InitDone_PompageB := FALSE;

	Ctrl_DetoxalB.Cmd.ModeReq := 0;
	Ctrl_DetoxalB.Cmd.InitReq := TRUE;
	Ctrl_PurgeB.Cmd.ModeReq := 0;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
	Ctrl_PompageB.Cmd.ModeReq := 1;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVB.Cmd.ModeReq := 0;
	Ctrl_HeliumAVB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPB.Cmd.ModeReq := 0;
	Ctrl_HeliumAPB.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 2;
	DEBUG_CHGT := 2;
	DEBUG_DETOXAL_1 := 2;
	DEBUG_PURGE_1 := 2;
	DEBUG_POMPAGE_1 := 1;
	DEBUG_HELIUM_1 := 0;

	index_purge_B := 1;
	index_pompage_B := 1;
END_IF;

IF Acts_ChangementB[21].Out THEN
	Ctrl_PompageB.Cmd.ModeReq := 0;
	DEBUG_POMPAGE_1 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTB_X11 *)

IF Acts_ChangementB[12].Out THEN
	InitDone_HeliumAPB := FALSE;

	Ctrl_DetoxalB.Cmd.ModeReq := 0;
	Ctrl_DetoxalB.Cmd.InitReq := TRUE;
	Ctrl_PurgeB.Cmd.ModeReq := 0;
	Ctrl_PurgeB.Cmd.InitReq := TRUE;
	Ctrl_PompageB.Cmd.ModeReq := 0;
	Ctrl_PompageB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVB.Cmd.ModeReq := 0;
	Ctrl_HeliumAVB.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPB.Cmd.ModeReq := 1;
	Ctrl_HeliumAPB.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 2;
	DEBUG_CHGT := 2;
	DEBUG_DETOXAL_1 := 2;
	DEBUG_PURGE_1 := 2;
	DEBUG_POMPAGE_1 := 2;
	DEBUG_HELIUM_1 := 1;

	index_purge_B := 2;
	index_pompage_B := 2;
END_IF;

IF Acts_ChangementB[22].Out THEN
	Ctrl_HeliumAPB.Cmd.ModeReq := 0;
	DEBUG_HELIUM_1 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTB_X12 *)
IF Acts_ChangementB[13].Out THEN
	Close(arm:=armoires[1]);
	armoires[1].etalonPT1 := armoires[1].ana.PT1.mes;
END_IF;

(* CHGTB_X13 *)
IF Acts_ChangementB[14].Out THEN
	Close(arm:=armoires[1]);
END_IF;
