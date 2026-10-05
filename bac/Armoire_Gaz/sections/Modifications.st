if (ConfigurationMemoireOk) then
	if (modifier) then

		(* creation d'une config avec la zone mémoire *)
		for i := 0 to taille_memoire_config - 1 do
			tempon[i] := %MW0[position_depart_memoire + index_config_utilisee * taille_memoire_config + i]; 
		end_for;
		LECTURE_MEMOIRE (config := tempon);
		ConfigGazModifications := LECTURE_MEMOIRE.stockage_public;

		(* copie de la config créée dans la zone mémoire de l'automate *)
		ECRITURE_MEMOIRE (config := ConfigGazModifications);
		for i := 0 to taille_memoire_config - 1 do
			%MW0[position_depart_memoire - (1 * taille_memoire_config) + i] := ECRITURE_MEMOIRE.stockage_public[i];
		end_for;
	end_if;

	if (sauvegarder_config_utilisee) then
	
		(* creation d'une config avec la zone mémoire *)
		for i := 0 to taille_memoire_config - 1 do
			tempon[i] := %MW0[position_depart_memoire - (1 * taille_memoire_config) + i]; 
		end_for;
		LECTURE_MEMOIRE (config := tempon);
		ConfigGazModifications := LECTURE_MEMOIRE.stockage_public;

		(* copie de la config créée dans la zone mémoire de l'automate *)
		ECRITURE_MEMOIRE (config := ConfigGazModifications);
		for i := 0 to taille_memoire_config - 1 do
			%MW0[position_depart_memoire + index_config_utilisee * taille_memoire_config + i] := ECRITURE_MEMOIRE.stockage_public[i];
		end_for;
	end_if;
end_if;
