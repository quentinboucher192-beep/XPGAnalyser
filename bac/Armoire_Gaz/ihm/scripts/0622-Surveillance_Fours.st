VAR
    i : INT;
    f : INT;
    somme : REAL;
END_VAR
(* Surveillance des fours : la moyenne des consignes, les fours trop chauds *)
somme := 0.0;
FOR i := Consignes.Low TO Consignes.High DO
    somme := somme + Consignes[i];
END_FOR;
FOR f := 1 TO Fours.High DO
    IF Fours[f].Temperature > Fours[f].Consigne + 20.0 THEN
        Fours[f].Vannes[1].Position := 0;
        IHM_JOURNAL('Four {f} trop chaud');
    END_IF;
END_FOR;
