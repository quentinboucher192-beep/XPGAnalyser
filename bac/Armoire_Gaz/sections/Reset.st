if (ConfigurationMemoireOk) then

	for i := 0 to nombre_de_config - 1 do
		reset_config[i] := false;
		if (reset_operateur) then
			if (index_config_utilisee >= 0 and index_config_utilisee < nombre_de_config) then
				if (i = index_config_utilisee) then
					reset_config[i] := true;
				end_if;
			end_if;
		end_if;
	end_for;

	for i := 0 to nombre_de_config - 1 do
		if reset_config[i] then

			config_a_reutiliser := false;
			if (ConfigsGazModifications[i].Utilise) then
				config_a_reutiliser := true;
			end_if;		

			ECRITURE_MEMOIRE (config := ConfigsGaz[i]);
			for j := 0 to taille_memoire_config - 1 do
				%MW0[position_depart_memoire + i * taille_memoire_config + j] := ECRITURE_MEMOIRE.stockage_public[j];
			end_for;
		
			ConfigsGazModifications[i] := ConfigsGaz[i];
			if (config_a_reutiliser) then
				%MW0[position_depart_memoire + i * taille_memoire_config + (taille_memoire_config - 1)].0 := true;
				ConfigsGazModifications[i].Utilise := true;
			end_if;
		end_if;
	end_for;
end_if;
