(* _________________________________________________________________________________________ *)

(* creation d'une config avec la zone mémoire *)

if (ConfigurationMemoireOk) then
	for i := 0 to taille_memoire_config - 1 do
		tempon[i] := %MW0[position_depart_memoire + index_affichage * taille_memoire_config + i]; 
	end_for;
	LECTURE_MEMOIRE (config := tempon);
	ConfigGazAffichage := LECTURE_MEMOIRE.stockage_public;

	(* copie de la config créée dans la zone mémoire de l'automate *)
	ECRITURE_MEMOIRE (config := ConfigGazAffichage);
	for i := 0 to taille_memoire_config - 1 do
		if (ConfigArmoireUtilisee.configuree) then
			%MW0[position_depart_memoire - (taille_memoire_config * 2) + i] := ECRITURE_MEMOIRE.stockage_public[i];
		else
			%MW0[position_depart_memoire - (taille_memoire_config * 2) + i] := -1;
		end_if;
	end_for;
end_if;

(* _________________________________________________________________________________________ *)
(* _________________________________________________________________________________________ *)
(* _________________________________________________________________________________________ *)


(* copie de la config utilisée dans la zone mémoire de l'automate *)
ECRITURE_MEMOIRE (config := ConfigGazUtilisee);
for i := 0 to taille_memoire_config - 1 do
	if (ConfigArmoireUtilisee.configuree) then
		%MW0[position_depart_memoire - (taille_memoire_config * 3) + i] := ECRITURE_MEMOIRE.stockage_public[i];
	else
		%MW0[position_depart_memoire - (taille_memoire_config * 3) + i] := -1;
	end_if;
end_for;

(* _________________________________________________________________________________________ *)
