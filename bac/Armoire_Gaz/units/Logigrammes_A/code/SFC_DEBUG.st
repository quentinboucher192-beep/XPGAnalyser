DEBUG_NOM_ETAPE := -1;
DEBUG_ACT1 := -1;
DEBUG_ACT2 := -1;
DEBUG_ACT3 := -1;
DEBUG_QUESTION := -1;
DEBUG_QUESTION_NUMERIQUE := -1;
DEBUG_NB := -1;
DEBUG_NB_TOTAL := -1;
DEBUG_TEMPSETAPE := -1;
DEBUG_TEMPSACTION1 := -1;
DEBUG_TEMPSACTION2 := -1;
DEBUG_TEMPSACTION3 := -1;
DEBUG_TYPE := -1;
DEBUG_INTERLOCK_OUI := -1;
DEBUG_INTERLOCK_NON := -1;

(* avant changement de bouteille *)
if (armoires[0].en_cours_de_chgt and armoires[0].en_cours_de_chgt_avant and not(armoires[0].en_cours_de_chgt_apres)) then
	
	(* detoxal *)
	if (Steps_DetoxalA[1].Active AND Gc_DetoxalA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 10;
		DEBUG_ACT1 := 10;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalA[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_DetoxalA[2].Active AND Gc_DetoxalA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 11;
		DEBUG_ACT1 := 11;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalA[2].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_DetoxalA[3].Active AND Gc_DetoxalA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 12;
		DEBUG_ACT1 := 12;
		DEBUG_QUESTION := 10;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalA[3].ActiveTime) / UDINT#1000);
	end_if;

	(* purge *)
	if (Steps_PurgeA[1].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_QUESTION := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeA[2].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 20;
		DEBUG_ACT1 := 20;
		DEBUG_ACT2 := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[2].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL4 * 1000) - Acts_PurgeA[0].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[0].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL5 * 1000) - Acts_PurgeA[1].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[1].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PurgeA[3].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 21;
		DEBUG_ACT1 := 21;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL6 * 1000) - Steps_PurgeA[3].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeA[4].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 22;
		DEBUG_ACT1 := 22;
		DEBUG_ACT2 := 21;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[4].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(5 * 1000) - Acts_PurgeA[2].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[2].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PurgeA[5].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 23;
		DEBUG_ACT1 := 23;
		DEBUG_ACT2 := 22;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[5].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL7 * 1000) - Acts_PurgeA[3].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[3].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PurgeA[6].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 24;
		DEBUG_ACT1 := 24;
		DEBUG_ACT2 := 23;
		DEBUG_ACT3 := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[6].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL8 * 1000) - Acts_PurgeA[5].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[5].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL9 * 1000) - Acts_PurgeA[6].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[6].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL10 * 1000) - Acts_PurgeA[7].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[7].Finished THEN DEBUG_TEMPSACTION3:= -1; END_IF;
	end_if;

	if (Steps_PurgeA[7].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 25;
		DEBUG_ACT1 := 25;
		DEBUG_ACT2 := 24;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[7].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL11 * 1000) - Acts_PurgeA[8].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[8].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL12 * 1000) - Acts_PurgeA[9].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[9].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PurgeA[8].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 26;
		DEBUG_ACT1 := 26;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[8].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeA[9].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 27;
		DEBUG_ACT1 := 27;
		DEBUG_QUESTION := 21;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[9].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeA[10].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 28;
		DEBUG_ACT1 := 28;
		DEBUG_QUESTION := 22;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[10].ActiveTime) / UDINT#1000);
	end_if;
	
	if ((Steps_PurgeA[4].Active OR Steps_PurgeA[5].Active OR Steps_PurgeA[6].Active OR Steps_PurgeA[7].Active) AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NB := armoires[0].compteur_cycle_purge;
		DEBUG_NB_TOTAL := config_utilisee.purge[index_purge_A].NBPUR;	
	end_if;

	(* pompage *)
	if (Steps_PompageA[1].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 30;
		DEBUG_ACT1 := 30;
		DEBUG_ACT2 := 30;
		DEBUG_ACT3 := 30;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[1].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP1 * 1000) - Acts_PompageA[0].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[0].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP1 * 1000) - Acts_PompageA[1].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[1].Finished THEN DEBUG_TEMPSACTION3 := -1; END_IF;
	end_if;

	if (Steps_PompageA[2].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 31;
		DEBUG_ACT1 := 31;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[2].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP3 * 1000) - Acts_PompageA[2].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[2].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageA[3].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 32;
		DEBUG_ACT1 := 32;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[3].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[4].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 33;
		DEBUG_ACT1 := 33;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP5 * 1000) - Steps_PompageA[4].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[5].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 34;
		DEBUG_ACT1 := 34;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[5].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[6].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 35;
		DEBUG_ACT1 := 35;
		DEBUG_ACT2 := 31;
		DEBUG_ACT3 := 31;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[6].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP7 * 1000) - Acts_PompageA[6].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[6].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP7 * 1000) - Acts_PompageA[6].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[6].Finished THEN DEBUG_TEMPSACTION3 := -1; END_IF;
	end_if;

	if (Steps_PompageA[7].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 36;
		DEBUG_ACT1 := 36;
		DEBUG_ACT2 := 32;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[7].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP8 * 1000) - Acts_PompageA[7].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[7].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP9 * 1000) - Acts_PompageA[8].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[8].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		IF (Acts_PompageA[8].TONElapsed >= UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP9 * 1000)) THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PompageA[8].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 37;
		DEBUG_ACT1 := 37;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[8].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP11 * 1000) - Acts_PompageA[9].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[9].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageA[9].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 38;
		DEBUG_ACT1 := 38;
		DEBUG_ACT2 := 33;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[9].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[10].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 39;
		DEBUG_ACT1 := 39;
		DEBUG_QUESTION := 30;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[10].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[11].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 40;
		DEBUG_ACT1 := 40;
		DEBUG_ACT2 := 34;
		DEBUG_QUESTION := 31;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[11].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(1 * 1000) - Acts_PompageA[12].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[12].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageA[12].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 41;
		DEBUG_ACT1 := 41;
		DEBUG_QUESTION := 32;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[12].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[13].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 42;
		DEBUG_ACT1 := 42;
		DEBUG_QUESTION := 33;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[13].ActiveTime) / UDINT#1000);
	end_if;

	if ((Steps_PompageA[6].Active OR Steps_PompageA[7].Active OR Steps_PompageA[8].Active) AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NB := armoires[0].compteur_cycle_pompage;
		DEBUG_NB_TOTAL := config_utilisee.pompage[index_pompage_A].NBPPE;	
	end_if;

	(* helium *)
	if (Steps_HeliumAVA[1].Active AND Gc_HeliumAVA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 50;
		DEBUG_ACT1 := 50;
		DEBUG_ACT2 := 50;
		DEBUG_QUESTION := 50;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAVA[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAVA[2].Active AND Gc_HeliumAVA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 51;
		DEBUG_ACT1 := 51;
		DEBUG_QUESTION := 51;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAVA[2].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAVA[5].Active AND Gc_HeliumAVA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 52;
		DEBUG_QUESTION := 52;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAVA[5].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAVA[6].Active AND Gc_HeliumAVA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 53;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAVA[6].ActiveTime) / UDINT#1000);
	end_if;
	
	(* changement de bouteille *)
	if (Steps_ChangementA[1].Active AND Gc_ChangementA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 60;
		DEBUG_ACT1 := 60;
		DEBUG_QUESTION := 60;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_ChangementA[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_ChangementA[6].Active AND Gc_ChangementA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 61;
		DEBUG_ACT1 := 61;
		DEBUG_QUESTION := 61;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_ChangementA[6].ActiveTime) / UDINT#1000);
	end_if;
end_if;
(* après changement de bouteille *)
if (armoires[0].en_cours_de_chgt and armoires[0].en_cours_de_chgt_avant and armoires[0].en_cours_de_chgt_apres) then
	
	(* detoxal *)
	if (Steps_DetoxalA[1].Active AND Gc_DetoxalA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 10;
		DEBUG_ACT1 := 10;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalA[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_DetoxalA[2].Active AND Gc_DetoxalA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 11;
		DEBUG_ACT1 := 11;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalA[2].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_DetoxalA[3].Active AND Gc_DetoxalA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 12;
		DEBUG_ACT1 := 12;
		DEBUG_QUESTION := 10;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalA[3].ActiveTime) / UDINT#1000);
	end_if;

	(* purge *)
	if (Steps_PurgeA[1].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_QUESTION := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeA[2].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 20;
		DEBUG_ACT1 := 20;
		DEBUG_ACT2 := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[2].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL4 * 1000) - Acts_PurgeA[0].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[0].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL5 * 1000) - Acts_PurgeA[1].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[1].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PurgeA[3].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 21;
		DEBUG_ACT1 := 21;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL6 * 1000) - Steps_PurgeA[3].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeA[4].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 22;
		DEBUG_ACT1 := 22;
		DEBUG_ACT2 := 21;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[4].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(5 * 1000) - Acts_PurgeA[2].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[2].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PurgeA[5].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 23;
		DEBUG_ACT1 := 23;
		DEBUG_ACT2 := 22;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[5].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL7 * 1000) - Acts_PurgeA[3].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[3].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PurgeA[6].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 24;
		DEBUG_ACT1 := 24;
		DEBUG_ACT2 := 23;
		DEBUG_ACT3 := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[6].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL8 * 1000) - Acts_PurgeA[5].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[5].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL9 * 1000) - Acts_PurgeA[6].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[6].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL10 * 1000) - Acts_PurgeA[7].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[7].Finished THEN DEBUG_TEMPSACTION3:= -1; END_IF;
	end_if;

	if (Steps_PurgeA[7].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 25;
		DEBUG_ACT1 := 25;
		DEBUG_ACT2 := 24;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[7].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL11 * 1000) - Acts_PurgeA[8].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[8].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_A].DL12 * 1000) - Acts_PurgeA[9].TONElapsed) / UDINT#1000);
		IF Acts_PurgeA[9].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PurgeA[8].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 26;
		DEBUG_ACT1 := 26;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[8].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeA[9].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 27;
		DEBUG_ACT1 := 27;
		DEBUG_QUESTION := 21;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[9].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeA[10].Active AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 28;
		DEBUG_ACT1 := 28;
		DEBUG_QUESTION := 22;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeA[10].ActiveTime) / UDINT#1000);
	end_if;
	
	if ((Steps_PurgeA[4].Active OR Steps_PurgeA[5].Active OR Steps_PurgeA[6].Active OR Steps_PurgeA[7].Active) AND Gc_PurgeA.ModeReqOut = 1) then
		DEBUG_NB := armoires[0].compteur_cycle_purge;
		DEBUG_NB_TOTAL := config_utilisee.purge[index_purge_A].NBPUR;	
	end_if;

	(* pompage *)
	if (Steps_PompageA[1].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 30;
		DEBUG_ACT1 := 30;
		DEBUG_ACT2 := 30;
		DEBUG_ACT3 := 30;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[1].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP1 * 1000) - Acts_PompageA[0].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[0].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP1 * 1000) - Acts_PompageA[1].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[1].Finished THEN DEBUG_TEMPSACTION3 := -1; END_IF;
	end_if;

	if (Steps_PompageA[2].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 31;
		DEBUG_ACT1 := 31;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[2].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP3 * 1000) - Acts_PompageA[2].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[2].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageA[3].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 32;
		DEBUG_ACT1 := 32;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[3].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[4].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 33;
		DEBUG_ACT1 := 33;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP5 * 1000) - Steps_PompageA[4].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[5].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 34;
		DEBUG_ACT1 := 34;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[5].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[6].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 35;
		DEBUG_ACT1 := 35;
		DEBUG_ACT2 := 31;
		DEBUG_ACT3 := 31;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[6].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP7 * 1000) - Acts_PompageA[6].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[6].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP7 * 1000) - Acts_PompageA[6].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[6].Finished THEN DEBUG_TEMPSACTION3 := -1; END_IF;
	end_if;

	if (Steps_PompageA[7].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 36;
		DEBUG_ACT1 := 36;
		DEBUG_ACT2 := 32;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[7].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP8 * 1000) - Acts_PompageA[7].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[7].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP9 * 1000) - Acts_PompageA[8].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[8].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		IF (Acts_PompageA[8].TONElapsed >= UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP9 * 1000)) THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PompageA[8].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 37;
		DEBUG_ACT1 := 37;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[8].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_A].DLP11 * 1000) - Acts_PompageA[9].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[9].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageA[9].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 38;
		DEBUG_ACT1 := 38;
		DEBUG_ACT2 := 33;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[9].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[10].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 39;
		DEBUG_ACT1 := 39;
		DEBUG_QUESTION := 30;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[10].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[11].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 40;
		DEBUG_ACT1 := 40;
		DEBUG_ACT2 := 34;
		DEBUG_QUESTION := 31;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[11].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(1 * 1000) - Acts_PompageA[12].TONElapsed) / UDINT#1000);
		IF Acts_PompageA[12].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageA[12].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 41;
		DEBUG_ACT1 := 41;
		DEBUG_QUESTION := 32;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[12].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageA[13].Active AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 42;
		DEBUG_ACT1 := 42;
		DEBUG_QUESTION := 33;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageA[13].ActiveTime) / UDINT#1000);
	end_if;
	
	if ((Steps_PompageA[6].Active OR Steps_PompageA[7].Active OR Steps_PompageA[8].Active) AND Gc_PompageA.ModeReqOut = 1) then
		DEBUG_NB := armoires[0].compteur_cycle_pompage;
		DEBUG_NB_TOTAL := config_utilisee.pompage[index_pompage_A].NBPPE;	
	end_if;

	(* helium *)
	if (Steps_HeliumAPA[0].Active AND Gc_HeliumAPA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 70;
		DEBUG_QUESTION := 70;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPA[0].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPA[3].Active AND Gc_HeliumAPA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 71;
		DEBUG_QUESTION := 71;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPA[3].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPA[4].Active AND Gc_HeliumAPA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 72;
		DEBUG_ACT1 := 70;
		DEBUG_QUESTION := 72;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPA[4].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.helium.DLHE1 * 1000) - Acts_HeliumAPA[4].TONElapsed) / UDINT#1000);
		IF Acts_HeliumAPA[4].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_HeliumAPA[5].Active AND Gc_HeliumAPA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 73;
		DEBUG_ACT1 := 71;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPA[5].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPA[6].Active AND Gc_HeliumAPA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 74;
		DEBUG_ACT1 := 72;
		DEBUG_ACT2 := 70;
		DEBUG_ACT3 := 70;
		DEBUG_QUESTION := 73;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPA[6].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(Acts_HeliumAPA[6].TONElapsed + Acts_HeliumAPA[7].TONElapsed) / UDINT#1000);
		IF Acts_HeliumAPA[6].Finished AND Acts_HeliumAPA[7].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(Acts_HeliumAPA[8].TONElapsed + Acts_HeliumAPA[9].TONElapsed) / UDINT#1000);
		IF Acts_HeliumAPA[8].Finished AND Acts_HeliumAPA[9].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_HeliumAPA[7].Active AND Gc_HeliumAPA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 75;
		DEBUG_ACT1 := 73;
		DEBUG_QUESTION := 74;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPA[7].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPA[8].Active AND Gc_HeliumAPA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 76;
		DEBUG_QUESTION := 75;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPA[8].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPA[11].Active AND Gc_HeliumAPA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 77;
		DEBUG_QUESTION := 76;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPA[11].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPA[12].Active AND Gc_HeliumAPA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 78;
		DEBUG_QUESTION := 77;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPA[12].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPA[13].Active AND Gc_HeliumAPA.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 79;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPA[13].ActiveTime) / UDINT#1000);
	end_if;

	(* changement de bouteille *)

	if (Steps_ChangementA[7].Active AND Gc_ChangementA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 62;
		DEBUG_ACT1 := 62;
		DEBUG_ACT2 := 60;
		DEBUG_QUESTION_NUMERIQUE := 60;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_ChangementA[7].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_ChangementA[12].Active AND Gc_ChangementA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 63;
		DEBUG_ACT1 := 63;
		DEBUG_QUESTION := 62;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_ChangementA[12].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_ChangementA[13].Active AND Gc_ChangementA.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 64;
		DEBUG_QUESTION := 63;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_ChangementA[13].ActiveTime) / UDINT#1000);
	end_if;
end_if;
