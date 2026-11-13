(* Le procédé simulé de la cuve d'essai (fictif) *)
Depart := NOT Arret;
IF Init.x THEN
	Niveau := 0;
END_IF;
IF Pompe AND Niveau < 200 THEN
	Niveau := Niveau + 1;
END_IF;
IF Vider.x AND Niveau > 0 THEN
	Niveau := Niveau - 2;
END_IF;
Plein := Niveau >= 150;
Chaud := Chauffer.t >= t#3s;
Go_Par := NOT Arret;
