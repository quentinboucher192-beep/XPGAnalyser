(* La vue synoptique : le verin va et vient, le silo se vide dans la tremie *)
Temps_Synoptique := Temps_Synoptique + 0.1;
Position_Verin := 50.0 - 50.0 * COS(Temps_Synoptique * 0.8);
IF Marche_Ligne THEN
    Niveau_Silo := Niveau_Silo - 0.25;
    Charge_Tremie := Charge_Tremie + 0.35;
END_IF;
IF Niveau_Silo < 15.0 THEN
    Niveau_Silo := 90.0;
END_IF;
IF Charge_Tremie > 90.0 THEN
    Charge_Tremie := 20.0;
END_IF;