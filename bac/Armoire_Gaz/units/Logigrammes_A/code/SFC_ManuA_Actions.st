(* ____________________________________________________________________ *)
(* MANUA_X0 *)


(* ____________________________________________________________________ *)
(* MANUA_X1 *)
IF Acts_ManuA[0].Out THEN
	if (armoires[0].en_cours_de_chgt) then
		armoires[0].en_cours_de_chgt := false;
		armoires[0].en_cours_de_chgt_avant := false;
		armoires[0].en_cours_de_chgt_apres := false;
		armoires[0].retour_sur_chgt := true;
	end_if;
	if (armoires[1].prete and not(armoires[1].manu)) then
		armoires[0].demande_basculement := TRUE;
	end_if;
	Close(arm:=armoires[0]);
END_IF;

(* ____________________________________________________________________ *)
(* MANUA_X2 *)

IF Acts_ManuA[1].Out THEN
	if (armoires[0].retour_sur_chgt) then
		armoires[0].init_chgt := true;
	end_if;
END_IF;

IF Acts_ManuA[2].Out THEN
	if (manu_armoire_choisie) then
		if (manu_vanne_choisie = 10 and armoires[0].sorties.V2.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.V2.F0 := FALSE;
				armoires[0].sorties.V2.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.V2.F1 := FALSE;
				armoires[0].sorties.V2.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 11 and armoires[0].sorties.V3.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.V3.F0 := FALSE;
				armoires[0].sorties.V3.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.V3.F1 := FALSE;
				armoires[0].sorties.V3.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 12 and armoires[0].sorties.V4.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.V4.F0 := FALSE;
				armoires[0].sorties.V4.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.V4.F1 := FALSE;
				armoires[0].sorties.V4.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 13 and armoires[0].sorties.V5.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.V5.F0 := FALSE;
				armoires[0].sorties.V5.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.V5.F1 := FALSE;
				armoires[0].sorties.V5.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 14 and armoires[0].sorties.VBTE.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.VBTE.F0 := FALSE;
				armoires[0].sorties.VBTE.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.VBTE.F1 := FALSE;
				armoires[0].sorties.VBTE.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 15 and armoires[0].sorties.V6.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.V6.F0 := FALSE;
				armoires[0].sorties.V6.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.V6.F1 := FALSE;
				armoires[0].sorties.V6.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 16 and armoires[0].sorties.V16.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.V16.F0 := FALSE;
				armoires[0].sorties.V16.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.V16.F1 := FALSE;
				armoires[0].sorties.V16.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 17 and armoires[0].sorties.V7.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.V7.F0 := FALSE;
				armoires[0].sorties.V7.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.V7.F1 := FALSE;
				armoires[0].sorties.V7.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 18 and armoires[0].sorties.VS.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.VS.F0 := FALSE;
				armoires[0].sorties.VS.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.VS.F1 := FALSE;
				armoires[0].sorties.VS.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 19 and armoires[0].sorties.V9.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.V9.F0 := FALSE;
				armoires[0].sorties.V9.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.V9.F1 := FALSE;
				armoires[0].sorties.V9.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 20 and armoires[0].sorties.PPE.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.PPE.F0 := FALSE;
				armoires[0].sorties.PPE.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.PPE.F1 := FALSE;
				armoires[0].sorties.PPE.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		elsif (manu_vanne_choisie = 21 and armoires[0].sorties.VHE.Autorisee) then
			if (manu_ouv) then
				armoires[0].sorties.VHE.F0 := FALSE;
				armoires[0].sorties.VHE.F1 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			elsif (manu_ferm) then
				armoires[0].sorties.VHE.F1 := FALSE;
				armoires[0].sorties.VHE.F0 := TRUE;
				%M1002 := FALSE;
				%MW3010 := 0;
			end_if;
		end_if;
	end_if;	
end_if;

