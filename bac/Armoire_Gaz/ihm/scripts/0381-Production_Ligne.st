(* La ligne 1 : 2,5 pieces par seconde quand la pompe tourne, un rebut sur treize *)
IF Pompe_Marche THEN
    Fraction_Piece := Fraction_Piece + 0.25;
END_IF;
IF Fraction_Piece >= 1.0 THEN
    Fraction_Piece := Fraction_Piece - 1.0;
    IF ((Pieces_Bonnes + Rebuts_Ligne) MOD 13) = 12 THEN
        Rebuts_Ligne := Rebuts_Ligne + 1;
    ELSE
        Pieces_Bonnes := Pieces_Bonnes + 1;
    END_IF;
END_IF;