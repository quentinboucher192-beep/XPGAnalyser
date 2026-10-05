for index := 0 to 1 do

	(* informations spécifiques à chaque armoire *)
	armoires[index].prete := 	
				not(armoires[index].entrees.porte) and
				not(armoires[index].entrees.AU) and
			        (generalites.nb_departs < 1 or not(armoires[index].entrees.D1[0])) and
			        (generalites.nb_departs < 2 or (not(armoires[index].entrees.D1[0]) or not(armoires[index].entrees.D2[0]))) and
			        not(armoires[index].entrees.fuite_gazbox) and
				not(armoires[index].entrees.fuite_gazroom) and
				not(armoires[index].entrees.extraction) and
			        not(armoires[index].entrees.Manque_tension) and
				not(armoires[index].en_declenchement) and
				not(armoires[index].en_cours_de_chgt) and
				not(armoires[index].bouteille_vide) and
				generalites.configuree;

	armoires[index].physiquement_prete := 	
				not(armoires[index].entrees.porte) and
				not(armoires[index].entrees.AU) and
			        (generalites.nb_departs < 1 or not(armoires[index].entrees.D1[0])) and
			        (generalites.nb_departs < 2 or (not(armoires[index].entrees.D1[0]) or not(armoires[index].entrees.D2[0]))) and
			        not(armoires[index].entrees.fuite_gazbox) and
				not(armoires[index].entrees.fuite_gazroom) and
				not(armoires[index].entrees.extraction) and
			        not(armoires[index].entrees.Manque_tension) and
				not(armoires[index].en_declenchement) and
				generalites.configuree;

end_for;

PasDeBasculementEnCours := not(armoires[0].en_cours_de_basculement) and not(armoires[1].en_cours_de_basculement);
PasDeChangementEnCours := not(armoires[0].en_cours_de_chgt) and not(armoires[1].en_cours_de_chgt);
(* basculement A vers B *)
if (index_armoire_utilisee = 0) then
	if (not(armoires[0].prete) or armoires[0].manu or (armoires[0].demande_basculement and PasDeBasculementEnCours and PasDeChangementEnCours)) then
		if (armoires[1].prete and not(armoires[1].manu)) then
			if (armoires[0].demande_basculement) then
				armoires[0].demande_basculement := false;
			end_if;
			index_armoire_utilisee := 1;
		end_if;
	end_if;
end_if;

(* basculement B vers A *)
if (index_armoire_utilisee = 1) then
	if (not(armoires[1].prete) or armoires[1].manu or (armoires[1].demande_basculement and PasDeBasculementEnCours and PasDeChangementEnCours)) then
		if (armoires[0].prete and not(armoires[0].manu)) then
			if (armoires[1].demande_basculement) then
				armoires[1].demande_basculement := false;
			end_if;
			index_armoire_utilisee := 0;
		end_if;
	end_if;
end_if;

(* basculement sur bouteille vide *)
if (ancien_index_utilise <> index_armoire_utilisee) then
	armoires[ancien_index_utilise].en_cours_de_basculement := true;
end_if;

(* détection pour début changement de bouteille *)
for index := 0 to 1 do
	if PasDeChangementEnCours then
		if (not(armoires[index].prete) and armoires[index].physiquement_prete) and not(armoires[index].en_cours_de_chgt) and not(armoires[index].en_cours_de_chgt_avant) then
			armoires[index].en_cours_de_chgt := true;
			armoires[index].en_cours_de_chgt_avant := true;
		end_if;
	end_if;
end_for;

(* durée d'un basculement *) 
TON_BASCULEMENT (IN := not(PasDeBasculementEnCours), PT := t#5s);
if (TON_BASCULEMENT.Q) then
	IF armoires[0].en_cours_de_basculement THEN
		armoires[0].en_cours_de_basculement := false;
		Close(arm := armoires[0]);
	END_IF;
	IF armoires[1].en_cours_de_basculement THEN
		armoires[1].en_cours_de_basculement := false;
		Close(arm := armoires[1]);
	END_IF;
end_if;
