(* ===================================================== *)
(* ACQUISITION / SIMULATION *)
(* ===================================================== *)

IF SimEnable THEN

    RawInput := SimValue;

ELSE

    RawInput := IW_Raw;

END_IF;

(* ===================================================== *)
(* MISE A L'ECHELLE *)
(* ===================================================== *)

IF Raw_Max <> Raw_Min THEN

    WorkingValue :=
        ((RawInput - Raw_Min)
        * (Eng_Max - Eng_Min)
        / (Raw_Max - Raw_Min))
        + Eng_Min;

ELSE

    WorkingValue := 0.0;

END_IF;

ScaledValue := WorkingValue;

(* ===================================================== *)
(* INITIALISATION *)
(* ===================================================== *)

IF FirstScan THEN

    FOR i := 0 TO 9 DO

        Buffer[i] := WorkingValue;

    END_FOR;

    Sum := WorkingValue * 10.0;

    StableValue := WorkingValue;

    FirstScan := FALSE;

END_IF;

(* ===================================================== *)
(* MOYENNE GLISSANTE *)
(* ===================================================== *)

(* Retire ancienne valeur *)
Sum := Sum - Buffer[Index];

(* Ajoute nouvelle valeur *)
Buffer[Index] := WorkingValue;

Sum := Sum + Buffer[Index];

(* Calcul moyenne *)
AverageValue := Sum / 10.0;

(* Incrémente index *)
Index := Index + 1;

IF Index > 9 THEN
    Index := 0;
END_IF;

(* ===================================================== *)
(* DEADBAND *)
(* ===================================================== *)

IF ABS(AverageValue - StableValue) >= DeadBand THEN

    StableValue := AverageValue;

END_IF;
