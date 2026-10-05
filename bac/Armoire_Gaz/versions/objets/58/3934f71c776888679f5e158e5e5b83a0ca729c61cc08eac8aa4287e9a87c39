for index := 0 to 1 do

	flag_bouteille_vide[index] := false;
	flag_bouteille_presque_vide[index] := false;	
	
	(* si basculement sur pression *)
	if not(armoires[index].en_cours_de_chgt) then
		if (generalites.conf_pression) then
		
			if (armoires[index].ana.PT1.mes <= INT_TO_REAL(config_utilisee.basc.SEUILPREAL1)) then
				flag_bouteille_presque_vide[index] := true;
			end_if;

			if (armoires[index].ana.PT1.mes <= INT_TO_REAL(config_utilisee.basc.SPBASC1)) then
				flag_bouteille_vide[index] := true;
			end_if;
		end_if;
	
		(* si basculement sur poids *)
		if (generalites.conf_poids) then
		
			if (armoires[index].PoidsBouteille <= INT_TO_REAL(config_utilisee.basc.SEUILPREAL2)) then
				flag_bouteille_presque_vide[index] := true;
			end_if;

			if (armoires[index].PoidsBouteille <= INT_TO_REAL(config_utilisee.basc.SPBASC2)) then
				flag_bouteille_vide[index] := true;
			end_if;
		end_if;
	end_if;

end_for;

TON_PRESQUE_VIDE_0 (IN := flag_bouteille_presque_vide[0] and not(armoires[0].Manu) and not(armoires[0].en_cours_de_chgt), PT := UDINT_TO_TIME(INT_TO_UDINT(config_utilisee.basc.DLBASC2) * 1000));
TON_PRESQUE_VIDE_1 (IN := flag_bouteille_presque_vide[1] and not(armoires[1].Manu) and not(armoires[1].en_cours_de_chgt), PT := UDINT_TO_TIME(INT_TO_UDINT(config_utilisee.basc.DLBASC2) * 1000));
TON_VIDE_0 (IN := flag_bouteille_vide[0] and not(armoires[0].Manu) and not(armoires[0].en_cours_de_chgt), PT := UDINT_TO_TIME(INT_TO_UDINT(config_utilisee.basc.DLBASC1) * 1000));
TON_VIDE_1 (IN := flag_bouteille_vide[1] and not(armoires[1].Manu) and not(armoires[1].en_cours_de_chgt), PT := UDINT_TO_TIME(INT_TO_UDINT(config_utilisee.basc.DLBASC1) * 1000));

if Acquit or acquittements[0] or acquittements[1] then
	if Acquit or acquittements[0] then
		armoires[0].bouteille_presque_vide := false; armoires[0].bouteille_vide := false;
	end_if;
	if Acquit or acquittements[1] then
		armoires[1].bouteille_presque_vide := false; armoires[1].bouteille_vide := false;
	end_if;
end_if;

if TON_PRESQUE_VIDE_0.Q then armoires[0].bouteille_presque_vide := true; end_if;
if TON_PRESQUE_VIDE_1.Q then armoires[1].bouteille_presque_vide := true; end_if;
if TON_VIDE_0.Q then armoires[0].bouteille_vide := true; end_if;
if TON_VIDE_1.Q then armoires[1].bouteille_vide := true; end_if;
