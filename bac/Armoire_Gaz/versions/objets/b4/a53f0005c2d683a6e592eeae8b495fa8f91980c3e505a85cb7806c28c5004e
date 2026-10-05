DEBUG_NOM_ETAPE_SURP_A := -1;
DEBUG_ACT1_SURP_A := -1;
DEBUG_ACT2_SURP_A := -1;
DEBUG_ACT3_SURP_A := -1;
DEBUG_TEMPSETAPE_SURP_A := -1;
DEBUG_TEMPSACT1_SURP_A := -1;
DEBUG_TEMPSACT2_SURP_A := -1;
DEBUG_TEMPSACT3_SURP_A := -1;
DEBUG_TYPE_SURP_A := -1;
if (armoires[0].CycleSurpressionBit) then

	(* avant changement de bouteille *)
	if (armoires[0].CycleSurpressionBit) then
	
		(* surpression *)
		if (Steps_SurpA[2].Active AND Gc_SurpressionA.ModeReqOut = 1) then
			DEBUG_NOM_ETAPE_SURP_A := 100;
			DEBUG_ACT1_SURP_A := 100;
			DEBUG_ACT2_SURP_A := 100;
			DEBUG_ACT3_SURP_A := 100;
			DEBUG_TEMPSETAPE_SURP_A := UDINT_TO_DINT(TIME_TO_UDINT(Steps_SurpA[2].ActiveTime) / UDINT#1000);

			DEBUG_TEMPSACT2_SURP_A := UDINT_TO_DINT(TIME_TO_UDINT(Acts_SurpA[4].TONElapsed) / UDINT#1000);
			IF Acts_SurpA[4].Finished THEN DEBUG_TEMPSACT2_SURP_A := -1; END_IF;
			DEBUG_TEMPSACT3_SURP_A := UDINT_TO_DINT(TIME_TO_UDINT(Acts_SurpA[5].TONElapsed) / UDINT#1000);
			IF Acts_SurpA[5].Finished THEN DEBUG_TEMPSACT3_SURP_A := -1; END_IF;
			end_if;
		
		(* surpression *)
		if (Steps_SurpA[6].Active AND Gc_SurpressionA.ModeReqOut = 1) then
			DEBUG_NOM_ETAPE_SURP_A := 101;
			DEBUG_ACT1_SURP_A := 101;
			DEBUG_TEMPSETAPE_SURP_A := UDINT_TO_DINT(TIME_TO_UDINT(Steps_SurpA[6].ActiveTime) / UDINT#1000);
		end_if;

		(* surpression *)
		if (Steps_SurpA[4].Active AND Gc_SurpressionA.ModeReqOut = 1) then
			DEBUG_NOM_ETAPE_SURP_A := 102;
			DEBUG_ACT1_SURP_A := 102;
			DEBUG_ACT2_SURP_A := 101;
			DEBUG_ACT3_SURP_A := 101;
			DEBUG_TEMPSETAPE_SURP_A := UDINT_TO_DINT(TIME_TO_UDINT(Steps_SurpA[4].ActiveTime) / UDINT#1000);
		end_if;

	end_if;
end_if;

DEBUG_NOM_ETAPE_SURP_B := -1;
DEBUG_ACT1_SURP_B := -1;
DEBUG_ACT2_SURP_B := -1;
DEBUG_ACT3_SURP_B := -1;
DEBUG_TEMPSETAPE_SURP_B := -1;
DEBUG_TEMPSACT1_SURP_B := -1;
DEBUG_TEMPSACT2_SURP_B := -1;
DEBUG_TEMPSACT3_SURP_B := -1;
DEBUG_TYPE_SURP_B := -1;
if (armoires[1].CycleSurpressionBit) then

	(* avant changement de bouteille *)
	if (armoires[1].CycleSurpressionBit) then
	
		(* surpression *)
		if (Steps_SurpB[2].Active AND Gc_SurpressionB.ModeReqOut = 1) then
			DEBUG_NOM_ETAPE_SURP_B := 100;
			DEBUG_ACT1_SURP_B := 100;
			DEBUG_ACT2_SURP_B := 100;
			DEBUG_ACT3_SURP_B := 100;
			DEBUG_TEMPSETAPE_SURP_B := UDINT_TO_DINT(TIME_TO_UDINT(Steps_SurpB[2].ActiveTime) / UDINT#1000);

			DEBUG_TEMPSACT2_SURP_B := UDINT_TO_DINT(TIME_TO_UDINT(Acts_SurpB[4].TONElapsed) / UDINT#1000);
			IF Acts_SurpB[4].Finished THEN DEBUG_TEMPSACT2_SURP_B := -1; END_IF;
			DEBUG_TEMPSACT3_SURP_B := UDINT_TO_DINT(TIME_TO_UDINT(Acts_SurpB[5].TONElapsed) / UDINT#1000);
			IF Acts_SurpB[5].Finished THEN DEBUG_TEMPSACT3_SURP_B := -1; END_IF;
			end_if;
		
		(* surpression *)
		if (Steps_SurpB[6].Active AND Gc_SurpressionB.ModeReqOut = 1) then
			DEBUG_NOM_ETAPE_SURP_B := 101;
			DEBUG_ACT1_SURP_B := 101;
			DEBUG_TEMPSETAPE_SURP_B := UDINT_TO_DINT(TIME_TO_UDINT(Steps_SurpB[6].ActiveTime) / UDINT#1000);
		end_if;

		(* surpression *)
		if (Steps_SurpB[4].Active AND Gc_SurpressionB.ModeReqOut = 1) then
			DEBUG_NOM_ETAPE_SURP_B := 102;
			DEBUG_ACT1_SURP_B := 102;
			DEBUG_ACT2_SURP_B := 101;
			DEBUG_ACT3_SURP_B := 101;
			DEBUG_TEMPSETAPE_SURP_B := UDINT_TO_DINT(TIME_TO_UDINT(Steps_SurpB[4].ActiveTime) / UDINT#1000);

			DEBUG_TEMPSACT1_SURP_B := UDINT_TO_DINT(TIME_TO_UDINT(Acts_SurpB[8].TONElapsed) / UDINT#1000);
			IF Acts_SurpB[8].Finished THEN DEBUG_TEMPSACT1_SURP_B := -1; END_IF;
			DEBUG_TEMPSACT2_SURP_B := UDINT_TO_DINT(TIME_TO_UDINT(Acts_SurpB[9].TONElapsed) / UDINT#1000);
			IF Acts_SurpB[9].Finished THEN DEBUG_TEMPSACT2_SURP_B := -1; END_IF;
		end_if;

	end_if;
end_if;
