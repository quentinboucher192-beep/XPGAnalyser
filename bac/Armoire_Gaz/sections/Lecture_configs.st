(* configurations modifiees et stockees *)
if (ConfigurationMemoireOk) then
	for i := 0 to nombre_de_config - 1 do
		for j := 0 to taille_memoire_config - 1 do
			tempon[j] := %MW0[position_depart_memoire + i * taille_memoire_config + j]; 
		end_for;

		LECTURE_MEMOIRE (config := tempon);
		ConfigsGazModifications[i] := LECTURE_MEMOIRE.stockage_public;
	end_for;
end_if;
