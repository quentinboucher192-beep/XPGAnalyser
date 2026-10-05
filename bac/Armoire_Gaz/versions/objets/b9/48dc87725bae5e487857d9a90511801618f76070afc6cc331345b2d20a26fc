if (ConfigurationMemoireOk) then
	if (initialisation_valider) then
		initialisation_code_erreur := 0;
		initialisation_erreur_active := false;
		
		StrVide := TRUE;
		FOR IndexCodeErreur := 0 TO 15 DO
			IF NomArmoireModifs[IndexCodeErreur] <> WORD#0000 THEN
				StrVide := FALSE;
			END_IF;
		END_FOR;
		
		(* pas de nom saisi *)
		if (StrVide) then
			initialisation_code_erreur := 5;
		end_if;		

		(* aucun basculement configure *)
		if (not(ConfigArmoireModifications.conf_poids) and not(ConfigArmoireModifications.conf_pression)) then
			initialisation_code_erreur := 10;
		end_if;

		(* nombre de departs illogique *)
		if (ConfigArmoireModifications.nb_departs < 1 or ConfigArmoireModifications.nb_departs >= 3) then
			initialisation_code_erreur := 11;
		end_if;

		(* index de depart illogique *)
		if (ConfigArmoireModifications.index_depart < 1 or ConfigArmoireModifications.index_depart >= 3) then
			initialisation_code_erreur := 12;
		end_if;
	
		(* pt1 armoire A mal configuré *)
		if (IWAModifications.PT1.ech_haute <= IWAModifications.PT1.ech_basse) then
			initialisation_code_erreur := 20;
		end_if;
	
		(* pt3 armoire A mal configuré *)
		if (IWAModifications.PT3.ech_haute <= IWAModifications.PT3.ech_basse) then
			initialisation_code_erreur := 21;
		end_if;

		(* balance armoire A mal configurée *)
		if (ConfigArmoireModifications.conf_poids and IWAModifications.Balance.ech_haute <= IWAModifications.Balance.ech_basse) then
			initialisation_code_erreur := 22;
		end_if;

		(* pt1 armoire B mal configuré *)
		if (IWBModifications.PT1.ech_haute <= IWBModifications.PT1.ech_basse) then
			initialisation_code_erreur := 30;
		end_if;
	
		(* pt3 armoire B mal configuré *)
		if (IWBModifications.PT3.ech_haute <= IWBModifications.PT3.ech_basse) then
			initialisation_code_erreur := 31;
		end_if;

		(* balance armoire B mal configurée *)
		if (ConfigArmoireModifications.conf_poids and IWBModifications.Balance.ech_haute <= IWBModifications.Balance.ech_basse) then
			initialisation_code_erreur := 32;
		end_if;
	
		(* config existe ou pas *)
		indice := -1;
		nom_gaz := '';

		case initialisation_index of
			1: nom_gaz := 'HBR';
			2: nom_gaz := 'CL2';
			3: nom_gaz := 'NF3';
			4: nom_gaz := 'HCL';
			5: nom_gaz := 'ND3';
			6: nom_gaz := 'F2/KRNE';
			7: nom_gaz := 'CH3F';
			8: nom_gaz := 'CH4';
			9: nom_gaz := 'CH2F2';
			10: nom_gaz := 'D2';
			11: nom_gaz := 'PH3/N2';
		end_case;

		for i := 0 to nombre_de_config - 1 do
			if ConfigsGazModifications[i].nom_gaz = nom_gaz then
				ConfigArmoireModifications.gaz := ConfigsGazModifications[i].nom_gaz; 
				indice := i;
			end_if;
		end_for;
	
		(* str vide *)
		if (LEN_INT(ConfigArmoireModifications.gaz) = 0) then
			initialisation_code_erreur := 13;
		end_if;

		(* pas de gaz trouvé *)
		if (nom_gaz = '') then
			initialisation_code_erreur := 40;
		end_if;

		(* pas d'indice trouvé *)
		if (indice = -1) then
			initialisation_code_erreur := 41;
		end_if;
		
		(* si erreur *)
		if initialisation_code_erreur <> 0 then
			initialisation_erreur_active := true;
		end_if;

		(* si pas d'erreur on vient écrire dans les vraies configs *)
		if (not(initialisation_erreur_active)) then
			
			ancienne_config_configuree := ConfigArmoireUtilisee.configuree;
			ancien_gaz := ConfigArmoireUtilisee.gaz;
			modification_majeure := false;

			if (not ancienne_config_configuree) then
				modification_majeure := true;
			end_if;

			if (ancien_gaz <> ConfigArmoireModifications.gaz) then
				modification_majeure := true;
			end_if;

			armoires[0].ana := IWAModifications;
			armoires[1].ana := IWBModifications;
			ConfigArmoireUtilisee := ConfigArmoireModifications;
			(* application des paramètres dans la config utilisee *)
			(*
			1  HBR
			2  CL2
			3  NF3
			4  HCL
			5  ND3
			6  F2/KRNE
			7  CH3F
			8  CH4
			9  CH2F2
			10 D2
			11 PH3/N2
			*)
		
			(* choix de l'indice *)
			for i := 0 to nombre_de_config - 1 do
				%MW0[position_depart_memoire + i * taille_memoire_config + (taille_memoire_config - 1)].0 := false;
			end_for;
			%MW0[position_depart_memoire + indice * taille_memoire_config + (taille_memoire_config - 1)].0 := true;

			index_config_utilisee := indice;
			ConfigArmoireUtilisee.configuree := true;
			
			(* à la fin d'une config on force le changement de bouteille des 2 côtés *)
			if (modification_majeure) then
				armoires[0].en_cours_de_chgt := true;
				armoires[0].en_cours_de_chgt_avant := true;
				armoires[0].en_cours_de_chgt_apres := false;
				armoires[1].en_cours_de_chgt := true;
				armoires[1].en_cours_de_chgt_avant := true;
				armoires[1].en_cours_de_chgt_apres := false;
			end_if;
		end_if;

		initialisation_valider := false;	
	end_if;



	TPErreurActive (IN := initialisation_erreur_active, PT := t#5s);
	if (not TPErreurActive.Q) and initialisation_erreur_active then
		initialisation_erreur_active := false;
		initialisation_code_erreur := 0;
	end_if;
end_if;
