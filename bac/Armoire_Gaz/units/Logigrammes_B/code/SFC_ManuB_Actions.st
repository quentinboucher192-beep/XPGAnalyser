(* ____________________________________________________________________ *)
(* MANUB_X0 *)


(* ____________________________________________________________________ *)
(* MANUB_X1 *)
IF Acts_ManuB[0].Out THEN
	if (armoires[1].en_cours_de_chgt) then
		armoires[1].en_cours_de_chgt := false;
		armoires[1].en_cours_de_chgt_avant := false;
		armoires[1].en_cours_de_chgt_apres := false;
		armoires[1].retour_sur_chgt := true;
	end_if;
	if (armoires[0].prete and not(armoires[0].manu)) then
		armoires[1].demande_basculement := TRUE;
	end_if;
	Close(arm:=armoires[1]);
END_IF;
(* ____________________________________________________________________ *)
(* MANUB_X2 *)

IF Acts_ManuB[1].Out THEN
	if (armoires[1].retour_sur_chgt) then
		armoires[1].init_chgt := true;
	end_if;
END_IF;

IF Acts_ManuB[2].Out THEN
	if (manu_armoire_choisie) then
		if (manu_vanne_choisie = 10 and armoires[1].sorties.V2.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.V2.F0 := FALSE;
				armoires[1].sorties.V2.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.V2.F1 := FALSE;
				armoires[1].sorties.V2.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 11 and armoires[1].sorties.V3.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.V3.F0 := FALSE;
				armoires[1].sorties.V3.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.V3.F1 := FALSE;
				armoires[1].sorties.V3.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 12 and armoires[1].sorties.V4.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.V4.F0 := FALSE;
				armoires[1].sorties.V4.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.V4.F1 := FALSE;
				armoires[1].sorties.V4.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 13 and armoires[1].sorties.V5.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.V5.F0 := FALSE;
				armoires[1].sorties.V5.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.V5.F1 := FALSE;
				armoires[1].sorties.V5.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 14 and armoires[1].sorties.VBTE.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.VBTE.F0 := FALSE;
				armoires[1].sorties.VBTE.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.VBTE.F1 := FALSE;
				armoires[1].sorties.VBTE.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 15 and armoires[1].sorties.V6.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.V6.F0 := FALSE;
				armoires[1].sorties.V6.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.V6.F1 := FALSE;
				armoires[1].sorties.V6.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 16 and armoires[1].sorties.V16.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.V16.F0 := FALSE;
				armoires[1].sorties.V16.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.V16.F1 := FALSE;
				armoires[1].sorties.V16.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 17 and armoires[1].sorties.V7.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.V7.F0 := FALSE;
				armoires[1].sorties.V7.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.V7.F1 := FALSE;
				armoires[1].sorties.V7.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 18 and armoires[1].sorties.VS.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.VS.F0 := FALSE;
				armoires[1].sorties.VS.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.VS.F1 := FALSE;
				armoires[1].sorties.VS.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 19 and armoires[1].sorties.V9.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.V9.F0 := FALSE;
				armoires[1].sorties.V9.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.V9.F1 := FALSE;
				armoires[1].sorties.V9.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 20 and armoires[1].sorties.PPE.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.PPE.F0 := FALSE;
				armoires[1].sorties.PPE.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.PPE.F1 := FALSE;
				armoires[1].sorties.PPE.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 21 and armoires[1].sorties.VHE.Autorisee) then
			if (manu_ouv) then
				armoires[1].sorties.VHE.F0 := FALSE;
				armoires[1].sorties.VHE.F1 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[1].sorties.VHE.F1 := FALSE;
				armoires[1].sorties.VHE.F0 := TRUE;
				%M1003 := FALSE;
				%MW3010 := 0;
			end_if;
		end_if;
	end_if;	
end_if;
