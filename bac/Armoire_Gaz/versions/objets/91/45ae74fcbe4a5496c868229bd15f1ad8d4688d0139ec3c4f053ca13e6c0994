(* Les graphiques du lot 11 : un essai de pompe, des pesees, une pompe qui s'arrete *)
Temps_Graphiques := Temps_Graphiques + 0.1;
Debit_Essai := 50.0 - 50.0 * COS(Temps_Graphiques * 0.21);
Pression_Essai := 9.2 - 0.00066 * Debit_Essai * Debit_Essai + 0.18 * SIN(Temps_Graphiques * 3.1);
Poids_Sachet := 500.4 + 1.5 * SIN(Temps_Graphiques * 7.3) + 1.0 * SIN(Temps_Graphiques * 2.9 + 1.0) + 0.6 * SIN(Temps_Graphiques * 13.7);
Vibration_Pompe := 55.0 + 30.0 * SIN(Temps_Graphiques * 0.4);
Pompe_Marche := SIN(Temps_Graphiques * 0.33) > -0.35;