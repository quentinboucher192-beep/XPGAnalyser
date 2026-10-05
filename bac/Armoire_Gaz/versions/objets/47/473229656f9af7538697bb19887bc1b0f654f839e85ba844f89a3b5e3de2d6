(* Moyenne pondérée des pressions de deux armoires, en bar *)
VAR_INPUT
    A : REAL;
    B : REAL;
    Poids_A : REAL := 0.5;   (* 0.5 : la moyenne simple *)
END_VAR
VAR_TEMP
    Somme : REAL;
END_VAR

Somme := A * Poids_A + B * (1.0 - Poids_A);
Moyenne_Pression := Somme;
