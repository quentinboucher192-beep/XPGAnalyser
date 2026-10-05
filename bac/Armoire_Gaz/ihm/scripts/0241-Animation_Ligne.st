(* Les mesures de la ligne de gaz, simulees toutes les 100 ms *)
Temps_Ligne := Temps_Ligne + 0.1;
(* L'essai de surpression : la pression monte tant qu'on appuie *)
IF Essai_Surpression AND Surpression < 6.0 THEN
    Surpression := Surpression + 0.3;
ELSIF NOT Essai_Surpression AND Surpression > 0.0 THEN
    Surpression := Surpression - 0.3;
END_IF;
Pression_Ligne := 8.0 + 2.0 * SIN(Temps_Ligne * 0.3) + Surpression;
Temperature_Local := 21.0 + 3.0 * SIN(Temps_Ligne * 0.05);
Niveau_Cuve := 60.0 + 30.0 * SIN(Temps_Ligne * 0.15);
IF Pompe_Marche THEN
    Debit_Ligne := 120.0 + 20.0 * SIN(Temps_Ligne * 0.4);
    Pieces_Produites := Pieces_Produites + 1.0;
ELSE
    Debit_Ligne := Debit_Ligne * 0.9;
END_IF;
(* L'etat de la pompe : en defaut au-dessus de 13 bar *)
IF Pression_Ligne > 13.0 THEN
    Etat_Pompe := 2;
ELSIF Pompe_Marche THEN
    Etat_Pompe := 1;
ELSE
    Etat_Pompe := 0;
END_IF;