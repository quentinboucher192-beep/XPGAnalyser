(* ____________________________________________________________________ *)
(* CHGTA_X0 *)


(* ____________________________________________________________________ *)
(* CHGTA_X1 *)

IF Acts_ChangementA[1].Out THEN
	Close(arm:=armoires[0]);
	
	Ctrl_DetoxalA.Cmd.ModeReq := 0;
	Ctrl_DetoxalA.Cmd.InitReq := TRUE;
	Ctrl_PurgeA.Cmd.ModeReq := 0;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
	Ctrl_PompageA.Cmd.ModeReq := 0;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVA.Cmd.ModeReq := 0;
	Ctrl_HeliumAVA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPA.Cmd.ModeReq := 0;
	Ctrl_HeliumAPA.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 0;
	DEBUG_PURGE_0 := 0;
	DEBUG_POMPAGE_0 := 0;
	DEBUG_HELIUM_0 := 0;
	DEBUG_CHGT := 0;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_A := 0;
	index_pompage_A := 0;

	armoires[0].bouteille_presque_vide := false;
	armoires[0].bouteille_vide := false;
	if not(modification_majeure) and not(armoires[0].retour_sur_chgt) and not(armoires[0].retour_sur_chgt_ap_decl) then
		armoires[0].en_cours_de_chgt := false;
		armoires[0].en_cours_de_chgt_avant := false;
		armoires[0].en_cours_de_chgt_apres := false;
	end_if;

END_IF;

(* ____________________________________________________________________ *)
(* CHGTA_X2 *)

IF Acts_ChangementA[2].Out THEN
	InitDone_DetoxalA := FALSE;

	Ctrl_DetoxalA.Cmd.ModeReq := 1;
	Ctrl_DetoxalA.Cmd.InitReq := TRUE;
	Ctrl_PurgeA.Cmd.ModeReq := 0;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
	Ctrl_PompageA.Cmd.ModeReq := 0;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVA.Cmd.ModeReq := 0;
	Ctrl_HeliumAVA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPA.Cmd.ModeReq := 0;
	Ctrl_HeliumAPA.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 1;
	DEBUG_PURGE_0 := 0;
	DEBUG_POMPAGE_0 := 0;
	DEBUG_HELIUM_0 := 0;
	DEBUG_CHGT := 0;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_A := 0;
	index_pompage_A := 0;
END_IF;

IF Acts_ChangementA[15].Out THEN
	Ctrl_DetoxalA.Cmd.ModeReq := 0;
	DEBUG_DETOXAL_0 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTA_X3 *)

IF Acts_ChangementA[3].Out THEN
	InitDone_PurgeA := FALSE;

	Ctrl_DetoxalA.Cmd.ModeReq := 0;
	Ctrl_DetoxalA.Cmd.InitReq := TRUE;
	Ctrl_PurgeA.Cmd.ModeReq := 1;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
	Ctrl_PompageA.Cmd.ModeReq := 0;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVA.Cmd.ModeReq := 0;
	Ctrl_HeliumAVA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPA.Cmd.ModeReq := 0;
	Ctrl_HeliumAPA.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 1;
	DEBUG_POMPAGE_0 := 0;
	DEBUG_HELIUM_0 := 0;
	DEBUG_CHGT := 0;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_A := 0;
	index_pompage_A := 0;
END_IF;

IF Acts_ChangementA[16].Out THEN
	Ctrl_PurgeA.Cmd.ModeReq := 0;
	DEBUG_PURGE_0 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTA_X4 *)

IF Acts_ChangementA[4].Out THEN
	InitDone_PompageA := FALSE;

	Ctrl_DetoxalA.Cmd.ModeReq := 0;
	Ctrl_DetoxalA.Cmd.InitReq := TRUE;
	Ctrl_PurgeA.Cmd.ModeReq := 0;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
	Ctrl_PompageA.Cmd.ModeReq := 1;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVA.Cmd.ModeReq := 0;
	Ctrl_HeliumAVA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPA.Cmd.ModeReq := 0;
	Ctrl_HeliumAPA.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 1;
	DEBUG_HELIUM_0 := 0;
	DEBUG_CHGT := 0;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_A := 0;
	index_pompage_A := 0;
END_IF;

IF Acts_ChangementA[17].Out THEN
	Ctrl_PompageA.Cmd.ModeReq := 0;
	DEBUG_POMPAGE_0 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTA_X5 *)

IF Acts_ChangementA[5].Out THEN
	InitDone_HeliumAVA := FALSE;

	Ctrl_DetoxalA.Cmd.ModeReq := 0;
	Ctrl_DetoxalA.Cmd.InitReq := TRUE;
	Ctrl_PurgeA.Cmd.ModeReq := 0;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
	Ctrl_PompageA.Cmd.ModeReq := 0;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVA.Cmd.ModeReq := 1;
	Ctrl_HeliumAVA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPA.Cmd.ModeReq := 0;
	Ctrl_HeliumAPA.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 1;
	DEBUG_CHGT := 0;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_A := 2;
	index_pompage_A := 2;
END_IF;

IF Acts_ChangementA[18].Out THEN
	Ctrl_HeliumAVA.Cmd.ModeReq := 0;
	DEBUG_HELIUM_0 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTA_X6 *)

IF Acts_ChangementA[6].Out THEN
	Ctrl_DetoxalA.Cmd.ModeReq := 0;
	Ctrl_DetoxalA.Cmd.InitReq := TRUE;
	Ctrl_PurgeA.Cmd.ModeReq := 0;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
	Ctrl_PompageA.Cmd.ModeReq := 0;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVA.Cmd.ModeReq := 0;
	Ctrl_HeliumAVA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPA.Cmd.ModeReq := 0;
	Ctrl_HeliumAPA.Cmd.InitReq := TRUE;

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

IF Steps_ChangementA[6].Active THEN
	armoires[0].sorties.V2.F0 := FALSE;
	armoires[0].sorties.V2.F1 := TRUE;
	armoires[0].sorties.V4.F0 := FALSE;
	armoires[0].sorties.V4.F1 := TRUE;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTA_X7 *)

IF Acts_ChangementA[7].Out THEN

	Ctrl_DetoxalA.Cmd.ModeReq := 0;
	Ctrl_DetoxalA.Cmd.InitReq := TRUE;
	Ctrl_PurgeA.Cmd.ModeReq := 0;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
	Ctrl_PompageA.Cmd.ModeReq := 0;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVA.Cmd.ModeReq := 0;
	Ctrl_HeliumAVA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPA.Cmd.ModeReq := 0;
	Ctrl_HeliumAPA.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 2;
	DEBUG_CHGT := 1;
	DEBUG_DETOXAL_1 := 0;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	armoires[0].en_cours_de_chgt_apres := TRUE;
	armoires[0].sorties.V2.F0 := TRUE;
	armoires[0].sorties.V2.F1 := FALSE;
	armoires[0].sorties.V4.F0 := TRUE;
	armoires[0].sorties.V4.F1 := FALSE;
END_IF;


IF Acts_ChangementA[8].Out THEN
	armoires[0].seuil_poids_utilise := armoires[0].seuil_poids_saisi;
	armoires[0].etalonBalance := REAL_TO_INT(armoires[0].ana.Balance.mes) - armoires[0].seuil_poids_utilise;  
	DEBUG_CHGT := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTA_X8 *)

IF Acts_ChangementA[9].Out THEN
	InitDone_DetoxalA := FALSE;

	Ctrl_DetoxalA.Cmd.ModeReq := 1;
	Ctrl_DetoxalA.Cmd.InitReq := TRUE;
	Ctrl_PurgeA.Cmd.ModeReq := 0;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
	Ctrl_PompageA.Cmd.ModeReq := 0;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVA.Cmd.ModeReq := 0;
	Ctrl_HeliumAVA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPA.Cmd.ModeReq := 0;
	Ctrl_HeliumAPA.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 2;
	DEBUG_CHGT := 2;
	DEBUG_DETOXAL_1 := 1;
	DEBUG_PURGE_1 := 0;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_A := 1;
	index_pompage_A := 1;
END_IF;

IF Acts_ChangementA[19].Out THEN
	Ctrl_DetoxalA.Cmd.ModeReq := 0;
	DEBUG_DETOXAL_1 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTA_X9 *)

IF Acts_ChangementA[10].Out THEN
	InitDone_PurgeA := FALSE;

	Ctrl_DetoxalA.Cmd.ModeReq := 0;
	Ctrl_DetoxalA.Cmd.InitReq := TRUE;
	Ctrl_PurgeA.Cmd.ModeReq := 1;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
	Ctrl_PompageA.Cmd.ModeReq := 0;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVA.Cmd.ModeReq := 0;
	Ctrl_HeliumAVA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPA.Cmd.ModeReq := 0;
	Ctrl_HeliumAPA.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 2;
	DEBUG_CHGT := 2;
	DEBUG_DETOXAL_1 := 2;
	DEBUG_PURGE_1 := 1;
	DEBUG_POMPAGE_1 := 0;
	DEBUG_HELIUM_1 := 0;

	index_purge_A := 1;
	index_pompage_A := 1;
END_IF;

IF Acts_ChangementA[20].Out THEN
	Ctrl_PurgeA.Cmd.ModeReq := 0;
	DEBUG_PURGE_1 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTA_X10 *)

IF Acts_ChangementA[11].Out THEN
	InitDone_PompageA := FALSE;

	Ctrl_DetoxalA.Cmd.ModeReq := 0;
	Ctrl_DetoxalA.Cmd.InitReq := TRUE;
	Ctrl_PurgeA.Cmd.ModeReq := 0;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
	Ctrl_PompageA.Cmd.ModeReq := 1;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVA.Cmd.ModeReq := 0;
	Ctrl_HeliumAVA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPA.Cmd.ModeReq := 0;
	Ctrl_HeliumAPA.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 2;
	DEBUG_CHGT := 2;
	DEBUG_DETOXAL_1 := 2;
	DEBUG_PURGE_1 := 2;
	DEBUG_POMPAGE_1 := 1;
	DEBUG_HELIUM_1 := 0;

	index_purge_A := 1;
	index_pompage_A := 1;
END_IF;

IF Acts_ChangementA[21].Out THEN
	Ctrl_PompageA.Cmd.ModeReq := 0;
	DEBUG_POMPAGE_1 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTA_X11 *)

IF Acts_ChangementA[12].Out THEN
	InitDone_HeliumAPA := FALSE;

	Ctrl_DetoxalA.Cmd.ModeReq := 0;
	Ctrl_DetoxalA.Cmd.InitReq := TRUE;
	Ctrl_PurgeA.Cmd.ModeReq := 0;
	Ctrl_PurgeA.Cmd.InitReq := TRUE;
	Ctrl_PompageA.Cmd.ModeReq := 0;
	Ctrl_PompageA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAVA.Cmd.ModeReq := 0;
	Ctrl_HeliumAVA.Cmd.InitReq := TRUE;
	Ctrl_HeliumAPA.Cmd.ModeReq := 1;
	Ctrl_HeliumAPA.Cmd.InitReq := TRUE;

	DEBUG_DETOXAL_0 := 2;
	DEBUG_PURGE_0 := 2;
	DEBUG_POMPAGE_0 := 2;
	DEBUG_HELIUM_0 := 2;
	DEBUG_CHGT := 2;
	DEBUG_DETOXAL_1 := 2;
	DEBUG_PURGE_1 := 2;
	DEBUG_POMPAGE_1 := 2;
	DEBUG_HELIUM_1 := 1;

	index_purge_A := 2;
	index_pompage_A := 2;
END_IF;

IF Acts_ChangementA[22].Out THEN
	Ctrl_HeliumAPA.Cmd.ModeReq := 0;
	DEBUG_HELIUM_1 := 2;
END_IF;

(* ____________________________________________________________________ *)
(* CHGTA_X12 *)
IF Acts_ChangementA[13].Out THEN
	Close(arm:=armoires[0]);
	armoires[0].etalonPT1 := armoires[0].ana.PT1.mes;
END_IF;

(* CHGTA_X13 *)
IF Acts_ChangementA[14].Out THEN
	Close(arm:=armoires[0]);
END_IF;

