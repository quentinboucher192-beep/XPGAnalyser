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
if (armoires[1].en_cours_de_chgt and armoires[1].en_cours_de_chgt_avant and not(armoires[1].en_cours_de_chgt_apres)) then
	
	(* detoxal *)
	if (Steps_DetoxalB[1].Active AND Gc_DetoxalB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 10;
		DEBUG_ACT1 := 10;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalB[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_DetoxalB[2].Active AND Gc_DetoxalB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 11;
		DEBUG_ACT1 := 11;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalB[2].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_DetoxalB[3].Active AND Gc_DetoxalB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 12;
		DEBUG_ACT1 := 12;
		DEBUG_QUESTION := 10;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalB[3].ActiveTime) / UDINT#1000);
	end_if;

	(* purge *)
	if (Steps_PurgeB[1].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_QUESTION := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeB[2].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 20;
		DEBUG_ACT1 := 20;
		DEBUG_ACT2 := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[2].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL4 * 1000) - Acts_PurgeB[0].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[0].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL5 * 1000) - Acts_PurgeB[1].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[1].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PurgeB[3].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 21;
		DEBUG_ACT1 := 21;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL6 * 1000) - Steps_PurgeB[3].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeB[4].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 22;
		DEBUG_ACT1 := 22;
		DEBUG_ACT2 := 21;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[4].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(5 * 1000) - Acts_PurgeB[2].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[2].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PurgeB[5].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 23;
		DEBUG_ACT1 := 23;
		DEBUG_ACT2 := 22;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[5].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL7 * 1000) - Acts_PurgeB[3].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[3].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PurgeB[6].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 24;
		DEBUG_ACT1 := 24;
		DEBUG_ACT2 := 23;
		DEBUG_ACT3 := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[6].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL8 * 1000) - Acts_PurgeB[5].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[5].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL9 * 1000) - Acts_PurgeB[6].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[6].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL10 * 1000) - Acts_PurgeB[7].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[7].Finished THEN DEBUG_TEMPSACTION3:= -1; END_IF;
	end_if;

	if (Steps_PurgeB[7].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 25;
		DEBUG_ACT1 := 25;
		DEBUG_ACT2 := 24;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[7].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL11 * 1000) - Acts_PurgeB[8].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[8].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL12 * 1000) - Acts_PurgeB[9].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[9].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PurgeB[8].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 26;
		DEBUG_ACT1 := 26;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[8].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeB[9].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 27;
		DEBUG_ACT1 := 27;
		DEBUG_QUESTION := 21;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[9].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeB[10].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 28;
		DEBUG_ACT1 := 28;
		DEBUG_QUESTION := 22;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[10].ActiveTime) / UDINT#1000);
	end_if;
	
	if ((Steps_PurgeB[4].Active OR Steps_PurgeB[5].Active OR Steps_PurgeB[6].Active OR Steps_PurgeB[7].Active) AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NB := armoires[1].compteur_cycle_purge;
		DEBUG_NB_TOTAL := config_utilisee.purge[index_purge_B].NBPUR;	
	end_if;

	(* pompage *)
	if (Steps_PompageB[1].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 30;
		DEBUG_ACT1 := 30;
		DEBUG_ACT2 := 30;
		DEBUG_ACT3 := 30;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[1].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP1 * 1000) - Acts_PompageB[0].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[0].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP1 * 1000) - Acts_PompageB[1].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[1].Finished THEN DEBUG_TEMPSACTION3 := -1; END_IF;
	end_if;

	if (Steps_PompageB[2].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 31;
		DEBUG_ACT1 := 31;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[2].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP3 * 1000) - Acts_PompageB[2].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[2].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageB[3].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 32;
		DEBUG_ACT1 := 32;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[3].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[4].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 33;
		DEBUG_ACT1 := 33;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP5 * 1000) - Steps_PompageB[4].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[5].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 34;
		DEBUG_ACT1 := 34;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[5].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[6].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 35;
		DEBUG_ACT1 := 35;
		DEBUG_ACT2 := 31;
		DEBUG_ACT3 := 31;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[6].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP7 * 1000) - Acts_PompageB[6].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[6].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP7 * 1000) - Acts_PompageB[6].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[6].Finished THEN DEBUG_TEMPSACTION3 := -1; END_IF;
	end_if;

	if (Steps_PompageB[7].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 36;
		DEBUG_ACT1 := 36;
		DEBUG_ACT2 := 32;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[7].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP8 * 1000) - Acts_PompageB[7].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[7].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP9 * 1000) - Acts_PompageB[8].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[8].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		IF (Acts_PompageB[8].TONElapsed >= UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP9 * 1000)) THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PompageB[8].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 37;
		DEBUG_ACT1 := 37;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[8].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP11 * 1000) - Acts_PompageB[9].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[9].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageB[9].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 38;
		DEBUG_ACT1 := 38;
		DEBUG_ACT2 := 33;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[9].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[10].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 39;
		DEBUG_ACT1 := 39;
		DEBUG_QUESTION := 30;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[10].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[11].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 40;
		DEBUG_ACT1 := 40;
		DEBUG_ACT2 := 34;
		DEBUG_QUESTION := 31;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[11].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(1 * 1000) - Acts_PompageB[12].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[12].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageB[12].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 41;
		DEBUG_ACT1 := 41;
		DEBUG_QUESTION := 32;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[12].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[13].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 42;
		DEBUG_ACT1 := 42;
		DEBUG_QUESTION := 33;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[13].ActiveTime) / UDINT#1000);
	end_if;

	if ((Steps_PompageB[6].Active OR Steps_PompageB[7].Active OR Steps_PompageB[8].Active) AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NB := armoires[1].compteur_cycle_pompage;
		DEBUG_NB_TOTAL := config_utilisee.pompage[index_pompage_B].NBPPE;	
	end_if;

	(* helium *)
	if (Steps_HeliumAVB[1].Active AND Gc_HeliumAVB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 50;
		DEBUG_ACT1 := 50;
		DEBUG_ACT2 := 50;
		DEBUG_QUESTION := 50;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAVB[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAVB[2].Active AND Gc_HeliumAVB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 51;
		DEBUG_ACT1 := 51;
		DEBUG_QUESTION := 51;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAVB[2].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAVB[5].Active AND Gc_HeliumAVB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 52;
		DEBUG_QUESTION := 52;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAVB[5].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAVB[6].Active AND Gc_HeliumAVB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 53;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAVB[6].ActiveTime) / UDINT#1000);
	end_if;
	
	(* changement de bouteille *)
	if (Steps_ChangementB[1].Active AND Gc_ChangementB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 60;
		DEBUG_ACT1 := 60;
		DEBUG_QUESTION := 60;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_ChangementB[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_ChangementB[6].Active AND Gc_ChangementB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 61;
		DEBUG_ACT1 := 61;
		DEBUG_QUESTION := 61;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_ChangementB[6].ActiveTime) / UDINT#1000);
	end_if;
end_if;
(* après changement de bouteille *)
if (armoires[1].en_cours_de_chgt and armoires[1].en_cours_de_chgt_avant and armoires[1].en_cours_de_chgt_apres) then
	
	(* detoxal *)
	if (Steps_DetoxalB[1].Active AND Gc_DetoxalB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 10;
		DEBUG_ACT1 := 10;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalB[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_DetoxalB[2].Active AND Gc_DetoxalB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 11;
		DEBUG_ACT1 := 11;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalB[2].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_DetoxalB[3].Active AND Gc_DetoxalB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 12;
		DEBUG_ACT1 := 12;
		DEBUG_QUESTION := 10;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_DetoxalB[3].ActiveTime) / UDINT#1000);
	end_if;

	(* purge *)
	if (Steps_PurgeB[1].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_QUESTION := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[1].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeB[2].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 20;
		DEBUG_ACT1 := 20;
		DEBUG_ACT2 := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[2].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL4 * 1000) - Acts_PurgeB[0].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[0].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL5 * 1000) - Acts_PurgeB[1].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[1].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PurgeB[3].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 21;
		DEBUG_ACT1 := 21;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL6 * 1000) - Steps_PurgeB[3].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeB[4].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 22;
		DEBUG_ACT1 := 22;
		DEBUG_ACT2 := 21;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[4].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(5 * 1000) - Acts_PurgeB[2].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[2].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PurgeB[5].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 23;
		DEBUG_ACT1 := 23;
		DEBUG_ACT2 := 22;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[5].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL7 * 1000) - Acts_PurgeB[3].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[3].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PurgeB[6].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 24;
		DEBUG_ACT1 := 24;
		DEBUG_ACT2 := 23;
		DEBUG_ACT3 := 20;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[6].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL8 * 1000) - Acts_PurgeB[5].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[5].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL9 * 1000) - Acts_PurgeB[6].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[6].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL10 * 1000) - Acts_PurgeB[7].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[7].Finished THEN DEBUG_TEMPSACTION3:= -1; END_IF;
	end_if;

	if (Steps_PurgeB[7].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 25;
		DEBUG_ACT1 := 25;
		DEBUG_ACT2 := 24;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[7].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL11 * 1000) - Acts_PurgeB[8].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[8].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.purge[index_purge_B].DL12 * 1000) - Acts_PurgeB[9].TONElapsed) / UDINT#1000);
		IF Acts_PurgeB[9].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PurgeB[8].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 26;
		DEBUG_ACT1 := 26;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[8].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeB[9].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 27;
		DEBUG_ACT1 := 27;
		DEBUG_QUESTION := 21;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[9].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PurgeB[10].Active AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 28;
		DEBUG_ACT1 := 28;
		DEBUG_QUESTION := 22;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PurgeB[10].ActiveTime) / UDINT#1000);
	end_if;
	
	if ((Steps_PurgeB[4].Active OR Steps_PurgeB[5].Active OR Steps_PurgeB[6].Active OR Steps_PurgeB[7].Active) AND Gc_PurgeB.ModeReqOut = 1) then
		DEBUG_NB := armoires[1].compteur_cycle_purge;
		DEBUG_NB_TOTAL := config_utilisee.purge[index_purge_B].NBPUR;	
	end_if;

	(* pompage *)
	if (Steps_PompageB[1].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 30;
		DEBUG_ACT1 := 30;
		DEBUG_ACT2 := 30;
		DEBUG_ACT3 := 30;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[1].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP1 * 1000) - Acts_PompageB[0].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[0].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP1 * 1000) - Acts_PompageB[1].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[1].Finished THEN DEBUG_TEMPSACTION3 := -1; END_IF;
	end_if;

	if (Steps_PompageB[2].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 31;
		DEBUG_ACT1 := 31;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[2].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP3 * 1000) - Acts_PompageB[2].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[2].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageB[3].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 32;
		DEBUG_ACT1 := 32;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[3].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[4].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 33;
		DEBUG_ACT1 := 33;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP5 * 1000) - Steps_PompageB[4].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[5].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 34;
		DEBUG_ACT1 := 34;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[5].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[6].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 35;
		DEBUG_ACT1 := 35;
		DEBUG_ACT2 := 31;
		DEBUG_ACT3 := 31;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[6].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP7 * 1000) - Acts_PompageB[6].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[6].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION3 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP7 * 1000) - Acts_PompageB[6].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[6].Finished THEN DEBUG_TEMPSACTION3 := -1; END_IF;
	end_if;

	if (Steps_PompageB[7].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 36;
		DEBUG_ACT1 := 36;
		DEBUG_ACT2 := 32;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[7].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP8 * 1000) - Acts_PompageB[7].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[7].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP9 * 1000) - Acts_PompageB[8].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[8].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
		IF (Acts_PompageB[8].TONElapsed >= UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP9 * 1000)) THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_PompageB[8].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 37;
		DEBUG_ACT1 := 37;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[8].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.pompage[index_pompage_B].DLP11 * 1000) - Acts_PompageB[9].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[9].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageB[9].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 38;
		DEBUG_ACT1 := 38;
		DEBUG_ACT2 := 33;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[9].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[10].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 39;
		DEBUG_ACT1 := 39;
		DEBUG_QUESTION := 30;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[10].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[11].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 40;
		DEBUG_ACT1 := 40;
		DEBUG_ACT2 := 34;
		DEBUG_QUESTION := 31;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[11].ActiveTime) / UDINT#1000);

		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(1 * 1000) - Acts_PompageB[12].TONElapsed) / UDINT#1000);
		IF Acts_PompageB[12].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_PompageB[12].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 41;
		DEBUG_ACT1 := 41;
		DEBUG_QUESTION := 32;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[12].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_PompageB[13].Active AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 42;
		DEBUG_ACT1 := 42;
		DEBUG_QUESTION := 33;
		DEBUG_TYPE := 0;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_PompageB[13].ActiveTime) / UDINT#1000);
	end_if;
	
	if ((Steps_PompageB[6].Active OR Steps_PompageB[7].Active OR Steps_PompageB[8].Active) AND Gc_PompageB.ModeReqOut = 1) then
		DEBUG_NB := armoires[1].compteur_cycle_pompage;
		DEBUG_NB_TOTAL := config_utilisee.pompage[index_pompage_B].NBPPE;	
	end_if;

	(* helium *)
	if (Steps_HeliumAPB[0].Active AND Gc_HeliumAPB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 70;
		DEBUG_QUESTION := 70;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPB[0].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPB[3].Active AND Gc_HeliumAPB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 71;
		DEBUG_QUESTION := 71;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPB[3].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPB[4].Active AND Gc_HeliumAPB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 72;
		DEBUG_ACT1 := 70;
		DEBUG_QUESTION := 72;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPB[4].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(UDINT_TO_TIME(config_utilisee.helium.DLHE1 * 1000) - Acts_HeliumAPB[4].TONElapsed) / UDINT#1000);
		IF Acts_HeliumAPB[4].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
	end_if;

	if (Steps_HeliumAPB[5].Active AND Gc_HeliumAPB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 73;
		DEBUG_ACT1 := 71;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPB[5].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPB[6].Active AND Gc_HeliumAPB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 74;
		DEBUG_ACT1 := 72;
		DEBUG_ACT2 := 70;
		DEBUG_ACT3 := 70;
		DEBUG_QUESTION := 73;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPB[6].ActiveTime) / UDINT#1000);
		
		DEBUG_TEMPSACTION1 := UDINT_TO_DINT(TIME_TO_UDINT(Acts_HeliumAPB[6].TONElapsed + Acts_HeliumAPB[7].TONElapsed) / UDINT#1000);
		IF Acts_HeliumAPB[6].Finished AND Acts_HeliumAPB[7].Finished THEN DEBUG_TEMPSACTION1 := -1; END_IF;
		DEBUG_TEMPSACTION2 := UDINT_TO_DINT(TIME_TO_UDINT(Acts_HeliumAPB[8].TONElapsed + Acts_HeliumAPB[9].TONElapsed) / UDINT#1000);
		IF Acts_HeliumAPB[8].Finished AND Acts_HeliumAPB[9].Finished THEN DEBUG_TEMPSACTION2 := -1; END_IF;
	end_if;

	if (Steps_HeliumAPB[7].Active AND Gc_HeliumAPB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 75;
		DEBUG_ACT1 := 73;
		DEBUG_QUESTION := 74;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPB[7].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPB[8].Active AND Gc_HeliumAPB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 76;
		DEBUG_QUESTION := 75;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPB[8].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPB[11].Active AND Gc_HeliumAPB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 77;
		DEBUG_QUESTION := 76;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPB[11].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPB[12].Active AND Gc_HeliumAPB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 78;
		DEBUG_QUESTION := 77;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPB[12].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_HeliumAPB[13].Active AND Gc_HeliumAPB.ModeReqOut = 1) then
		DEBUG_NOM_ETAPE := 79;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_HeliumAPB[13].ActiveTime) / UDINT#1000);
	end_if;

	(* changement de bouteille *)

	if (Steps_ChangementB[7].Active AND Gc_ChangementB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 62;
		DEBUG_ACT1 := 62;
		DEBUG_ACT2 := 60;
		DEBUG_QUESTION_NUMERIQUE := 60;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_ChangementB[7].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_ChangementB[12].Active AND Gc_ChangementB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 63;
		DEBUG_ACT1 := 63;
		DEBUG_QUESTION := 62;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_ChangementB[12].ActiveTime) / UDINT#1000);
	end_if;

	if (Steps_ChangementB[13].Active AND Gc_ChangementB.ModeReqOut = 1) then
		DEBUG_INTERLOCK_NON := 0;
		DEBUG_NOM_ETAPE := 64;
		DEBUG_QUESTION := 63;
		DEBUG_TEMPSETAPE := UDINT_TO_DINT(TIME_TO_UDINT(Steps_ChangementB[13].ActiveTime) / UDINT#1000);
	end_if;
end_if;
