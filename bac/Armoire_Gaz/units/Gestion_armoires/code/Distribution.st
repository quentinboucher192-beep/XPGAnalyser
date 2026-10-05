for index := 0 to 1 do

	(* ---------------- ETAT ---------------- *)
	IF armoires[index].manu THEN
		armoires[index].etat := 10;
	ELSIF armoires[index].CycleSurpressionBit THEN
		armoires[index].etat := 11;
	ELSIF armoires[index].en_declenchement THEN
		armoires[index].etat := 2;
	ELSIF armoires[index].en_cours_de_chgt THEN
		armoires[index].etat := 6;
	ELSIF armoires[index].en_cours_de_basculement THEN
		armoires[index].etat := 5;
	ELSIF armoires[index].active THEN
		armoires[index].etat := 3;
	ELSIF NOT armoires[index].active THEN
		armoires[index].etat := 4;
	ELSIF armoires[index].physiquement_prete THEN
		armoires[index].etat := 1;
	ELSE
		armoires[index].etat := 0;
	END_IF;

	armoires[index].PoidsBouteille := REAL_TO_INT(armoires[index].ana.Balance.mes) - armoires[index].seuil_poids_utilise; 

	IF NOT(armoires[index].manu) THEN
		(* en déclenchement *)
		IF armoires[index].etat = 2 THEN
			armoires[index].sorties.V2.F0 := TRUE;
			armoires[index].sorties.V3.F0 := TRUE;
			armoires[index].sorties.V4.F0 := TRUE;
			armoires[index].sorties.V5.F0 := TRUE;
			armoires[index].sorties.V6.F0 := TRUE;
			armoires[index].sorties.V16.F0 := TRUE;
			armoires[index].sorties.V7.F0 := TRUE;
			armoires[index].sorties.VS.F0 := TRUE;
			armoires[index].sorties.V9.F0 := TRUE;
			armoires[index].sorties.VHE.F0 := TRUE;
			armoires[index].sorties.VBTE.F0 := TRUE;
			armoires[index].sorties.PPE.F0 := TRUE;
			armoires[index].sorties.V2.F1 := FALSE;
			armoires[index].sorties.V3.F1 := FALSE;
			armoires[index].sorties.V4.F1 := FALSE;
			armoires[index].sorties.V5.F1 := FALSE;
			armoires[index].sorties.V6.F1 := FALSE;
			armoires[index].sorties.V16.F1 := FALSE;
			armoires[index].sorties.V7.F1 := FALSE;
			armoires[index].sorties.VS.F1 := FALSE;
			armoires[index].sorties.V9.F1 := FALSE;
			armoires[index].sorties.VHE.F1 := FALSE;
			armoires[index].sorties.VBTE.F1 := FALSE;
			armoires[index].sorties.PPE.F1 := FALSE;
		(* actif : en distribution *)
		ELSIF armoires[index].etat = 3 THEN
			armoires[index].sorties.V5.F1 := TRUE;
			armoires[index].sorties.V5.F0 := FALSE;
			armoires[index].sorties.VBTE.F1 := TRUE;
			armoires[index].sorties.VBTE.F0 := FALSE;
			armoires[index].sorties.V6.F1 := TRUE;
			armoires[index].sorties.V6.F0 := FALSE;
			armoires[index].sorties.V16.F1 := TRUE;
			armoires[index].sorties.V16.F0 := FALSE;
		(* inactif : standby *)
		ELSIF armoires[index].etat = 4 THEN
			armoires[index].sorties.V2.F0 := TRUE;
			armoires[index].sorties.V3.F0 := TRUE;
			armoires[index].sorties.V4.F0 := TRUE;
			armoires[index].sorties.V5.F0 := TRUE;
			armoires[index].sorties.V6.F0 := TRUE;
			armoires[index].sorties.V16.F0 := TRUE;
			armoires[index].sorties.V7.F0 := TRUE;
			armoires[index].sorties.VS.F0 := TRUE;
			armoires[index].sorties.V9.F0 := TRUE;
			armoires[index].sorties.VHE.F0 := TRUE;
			armoires[index].sorties.VBTE.F0 := TRUE;
			armoires[index].sorties.PPE.F0 := TRUE;
			armoires[index].sorties.V2.F1 := FALSE;
			armoires[index].sorties.V3.F1 := FALSE;
			armoires[index].sorties.V4.F1 := FALSE;
			armoires[index].sorties.V5.F1 := FALSE;
			armoires[index].sorties.V6.F1 := FALSE;
			armoires[index].sorties.V16.F1 := FALSE;
			armoires[index].sorties.V7.F1 := FALSE;
			armoires[index].sorties.VS.F1 := FALSE;
			armoires[index].sorties.V9.F1 := FALSE;
			armoires[index].sorties.VHE.F1 := FALSE;
			armoires[index].sorties.VBTE.F1 := FALSE;
			armoires[index].sorties.PPE.F1 := FALSE;
		(* en basculement on maintient V6 V16 *)
		ELSIF armoires[index].etat = 5 THEN
			armoires[index].sorties.V5.F1 := FALSE;
			armoires[index].sorties.V5.F0 := TRUE;
			armoires[index].sorties.VBTE.F1 := FALSE;
			armoires[index].sorties.VBTE.F0 := TRUE;
			armoires[index].sorties.V6.F1 := TRUE;
			armoires[index].sorties.V6.F0 := FALSE;
			armoires[index].sorties.V16.F1 := TRUE;
			armoires[index].sorties.V16.F0 := FALSE;
		END_IF;
	END_IF;

	(* double enveloppe *)
	IF armoires[index].entrees.D1[0] THEN
		armoires[index].sorties.V6.F1 := FALSE;
		armoires[index].sorties.V6.F0 := TRUE;
	END_IF;
	IF armoires[index].entrees.D2[0] THEN
		armoires[index].sorties.V16.F1 := FALSE;
		armoires[index].sorties.V16.F0 := TRUE;
	END_IF;
	
	(* lignes *)
	IF generalites.nb_departs < 2 THEN
		armoires[index].sorties.V16.F1 := FALSE;
		armoires[index].sorties.V16.F0 := TRUE;
	END_IF;

	(* vanne bouteille *)
	IF NOT(generalites.pres_vanne_bouteille) THEN
		armoires[index].sorties.VBTE.F1 := FALSE;
		armoires[index].sorties.VBTE.F0 := TRUE;
	END_IF;

	(* robinets *)
	IF NOT(generalites.pres_robinets) THEN
		armoires[index].sorties.VHE.F1 := FALSE;
		armoires[index].sorties.VHE.F0 := TRUE;
	END_IF;

end_for;
