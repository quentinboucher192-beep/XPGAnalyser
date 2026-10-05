for index := 0 to 1 do

	armoires[index].en_declenchement := false;	
	
	(* acquittement *)
	if (acquit or acquittements[index]) then
		if (armoires[index].ana.PT3.mes < INT_TO_REAL(config_utilisee.surpression.SP3) and armoires[index].defs.surpression) then
			armoires[index].defs.surpression := false;
		end_if;

		if (not(armoires[index].entrees.fuite_gazbox) and armoires[index].defs.fuite_gazbox) then
			armoires[index].defs.fuite_gazbox := false;
		end_if;

		if (not(armoires[index].entrees.fuite_gazroom) and armoires[index].defs.fuite_gazroom) then
			armoires[index].defs.fuite_gazroom := false;
		end_if;

		if (not(armoires[index].entrees.extraction) and armoires[index].defs.manque_extraction) then
			armoires[index].defs.manque_extraction := false;
		end_if;

		if (not(armoires[index].entrees.manque_tension) and armoires[index].defs.manque_tension) then
			armoires[index].defs.manque_tension := false;
		end_if;
		
		if (not(armoires[index].entrees.porte) and armoires[index].defs.porte) then
			armoires[index].defs.porte := false;
		end_if;

		if (not(armoires[index].entrees.au) and armoires[index].defs.au) then
			armoires[index].defs.au := false;
		end_if;

		if (armoires[index].entrees.span_N2 and armoires[index].defs.span) then
			armoires[index].defs.span := false;
		end_if;

		if ((generalites.nb_departs < 1 or not(armoires[index].entrees.D1[0])) and armoires[index].defs.env1) then
			armoires[index].defs.env1 := false;
		end_if;		
		
		if ((generalites.nb_departs < 1 or not(armoires[index].entrees.D1[1])) and armoires[index].defs.env1B) then
			armoires[index].defs.env1B := false;
		end_if;

		if ((generalites.nb_departs < 2 or not(armoires[index].entrees.D2[0])) and armoires[index].defs.env2) then
			armoires[index].defs.env2 := false;
		end_if;		
		
		if ((generalites.nb_departs < 2 or not(armoires[index].entrees.D2[1])) and armoires[index].defs.env2B) then
			armoires[index].defs.env2B := false;
		end_if;

	end_if;
	
	if (armoires[index].entrees.fuite_gazbox) then armoires[index].defs.fuite_gazbox := true; end_if;
	if (armoires[index].entrees.fuite_gazroom) then armoires[index].defs.fuite_gazroom := true; end_if;
	if (armoires[index].entrees.extraction) then armoires[index].defs.manque_extraction := true; end_if;
	if (armoires[index].entrees.manque_tension) then armoires[index].defs.manque_tension := true; end_if;
	if (armoires[index].entrees.au) then armoires[index].defs.au := true; end_if;
	if (armoires[index].entrees.D1[0] and generalites.nb_departs >= 1) then armoires[index].defs.env1 := true; end_if;
	if (armoires[index].entrees.D1[1] and generalites.nb_departs >= 1) then armoires[index].defs.env1B := true; end_if;
	if (armoires[index].entrees.D2[0] and generalites.nb_departs >= 2) then armoires[index].defs.env2 := true; end_if;
	if (armoires[index].entrees.D2[1] and generalites.nb_departs >= 2) then armoires[index].defs.env2B := true; end_if;
	if (not(armoires[index].entrees.span_N2)) then armoires[index].defs.span := true; end_if;
	if (armoires[index].entrees.porte) then armoires[index].defs.porte := true; end_if;
	
	(* basculement *)
	if (armoires[index].defs.surpression and not(armoires[index].en_cours_de_chgt) or
		armoires[index].defs.fuite_gazbox or 
		armoires[index].defs.fuite_gazroom or 
		armoires[index].defs.manque_extraction and not(armoires[index].en_cours_de_chgt) or
		armoires[index].defs.manque_tension or
		armoires[index].defs.porte and not(armoires[index].en_cours_de_chgt) or
		armoires[index].defs.env1 and generalites.nb_departs < 2 or
		armoires[index].defs.env1 and armoires[index].defs.env2 and generalites.nb_departs >= 2) then
			armoires[index].en_declenchement := true;
			if (armoires[index].en_declenchement and not(armoires[index].PrevDecl)) then
				if (armoires[index].en_cours_de_chgt) then
					armoires[index].en_cours_de_chgt := false;
					armoires[index].en_cours_de_chgt_avant := false;
					armoires[index].en_cours_de_chgt_apres := false;
					armoires[index].retour_sur_chgt_ap_decl := true;
				end_if;
			end_if;
	end_if;

	if (armoires[index].PrevDecl and not(armoires[index].en_declenchement)) then
		if (armoires[index].retour_sur_chgt_ap_decl) then
			armoires[index].en_cours_de_chgt := true;
			armoires[index].en_cours_de_chgt_avant := true;
			armoires[index].init_chgt := true;
		end_if;
	end_if;

	armoires[index].PrevDecl := armoires[index].en_declenchement; 
end_for;
