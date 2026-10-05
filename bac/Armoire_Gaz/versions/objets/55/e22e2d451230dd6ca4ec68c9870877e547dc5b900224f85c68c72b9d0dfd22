taille_memoire_automate := 10000;
taille_memoire_config := 150;
position_depart_memoire := 6000;
nombre_de_config := 20;

ConfigurationMemoireOk := position_depart_memoire + taille_memoire_config * nombre_de_config < taille_memoire_automate;  
ConfigurationMemoireOk := ConfigurationMemoireOk and position_depart_memoire - taille_memoire_config * 3 >= 0;

(* reset des configs avant réécriture des configurations brutes *)
if (ConfigurationMemoireOk) then
	for i := 0 to nombre_de_config - 1 do
		ConfigsGaz[i].Utilise := false;
		ConfigsGaz[i].Nom_gaz := '';
		ConfigsGaz[i].Unite := '';

		(* basculement *)
		ConfigsGaz[i].basc.SPBASC1 := 0;
		ConfigsGaz[i].basc.SPBASC2 := 0;
		ConfigsGaz[i].basc.SEUILPREAL1 := 0;
		ConfigsGaz[i].basc.SEUILPREAL2 := 0;
		ConfigsGaz[i].basc.DLBASC1 := 0;
		ConfigsGaz[i].basc.DLBASC2 := 0;

		(* changement de bouteille *)
		ConfigsGaz[i].chgt.SPV := 0;

		(* purge *)
		for j := 0 to 2 do
			ConfigsGaz[i].purge[j].NBPUR := 0;
			ConfigsGaz[i].purge[j].SPUR4 := 0;
			ConfigsGaz[i].purge[j].SPUR5 := 0;
			ConfigsGaz[i].purge[j].SPUR6 := 0;
			ConfigsGaz[i].purge[j].DL4 := 0;
			ConfigsGaz[i].purge[j].DL5 := 0;
			ConfigsGaz[i].purge[j].DL6 := 0;
			ConfigsGaz[i].purge[j].DL7 := 0;
			ConfigsGaz[i].purge[j].DL8 := 0;
			ConfigsGaz[i].purge[j].DL9 := 0;
			ConfigsGaz[i].purge[j].DL10 := 0;
			ConfigsGaz[i].purge[j].DL11 := 0;
			ConfigsGaz[i].purge[j].DL12 := 0;
		end_for;

		(* pompage *)
		for j := 0 to 2 do
			ConfigsGaz[i].pompage[j].NBPPE := 0;
			ConfigsGaz[i].pompage[j].SPPE5 := 0;
			ConfigsGaz[i].pompage[j].SPPE6 := 0;
			ConfigsGaz[i].pompage[j].DLP1 := 0;
			ConfigsGaz[i].pompage[j].DLP2 := 0;
			ConfigsGaz[i].pompage[j].DLP3 := 0;
			ConfigsGaz[i].pompage[j].DLP4 := 0;
			ConfigsGaz[i].pompage[j].DLP5 := 0;
			ConfigsGaz[i].pompage[j].DLP6 := 0;
			ConfigsGaz[i].pompage[j].DLP7 := 0;
			ConfigsGaz[i].pompage[j].DLP8 := 0;
			ConfigsGaz[i].pompage[j].DLP9 := 0;
			ConfigsGaz[i].pompage[j].DLP10 := 0;
			ConfigsGaz[i].pompage[j].DLP11 := 0;
		end_for;

		(* surpression *)
		ConfigsGaz[i].surpression.SP3H := 0;
		ConfigsGaz[i].surpression.SP3 := 0;
		ConfigsGaz[i].surpression.SP3B := 0;
		ConfigsGaz[i].surpression.NBS := 0;
		ConfigsGaz[i].surpression.DLSURV := 0;
		ConfigsGaz[i].surpression.DLSURP1 := 0;
		ConfigsGaz[i].surpression.DLSURP2 := 0;
		ConfigsGaz[i].surpression.DLSURP3 := 0;
		ConfigsGaz[i].surpression.DLSURP4 := 0;
		ConfigsGaz[i].surpression.DLSURP5 := 0;

		(* detoxal *)
		ConfigsGaz[i].detoxal.SPDETOX := 0;
		ConfigsGaz[i].detoxal.DLDETOX := 0;

		(* test helium *)
		ConfigsGaz[i].helium.DLHE1 := 10;
		ConfigsGaz[i].helium.DLHE2 := 10;
		ConfigsGaz[i].helium.DLHE3 := 10;
		ConfigsGaz[i].helium.DLHE4 := 10;
		ConfigsGaz[i].helium.DLHE5 := 10;
	end_for;

	reset_all_configs := reset_all_operateur;
	if reset_all_configs then
		resets := resets + 1;
	end_if;
end_if;
