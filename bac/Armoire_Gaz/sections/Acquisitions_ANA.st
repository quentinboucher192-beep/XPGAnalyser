(* COTE A *)

PT1_A (IW_Raw := INT_TO_REAL(%IW0.10.0),
       SimEnable := simulations[0],
       Raw_Min := 0.0,
       Raw_Max := 10000.0,
       Eng_Min := Armoires[0].ana.PT1.ech_basse,
       Eng_Max := Armoires[0].ana.PT1.ech_haute,
       SimValue := INT_TO_REAL(simulations_valeurs[0]),
       DeadBand := 0.05);

Armoires[0].ana.PT1.mes := PT1_A.StableValue;
Armoires[0].ana.PT1.raw := PT1_A.RawInput;

PT3_A (IW_Raw := INT_TO_REAL(%IW0.10.1),
       SimEnable := simulations[1],
       Raw_Min := 0.0,
       Raw_Max := 10000.0,
       Eng_Min := Armoires[0].ana.PT3.ech_basse,
       Eng_Max := Armoires[0].ana.PT3.ech_haute,
       SimValue := INT_TO_REAL(simulations_valeurs[1]),
       DeadBand := 0.05);

Armoires[0].ana.PT3.mes := PT3_A.StableValue;
Armoires[0].ana.PT3.raw := PT3_A.RawInput;

BALANCE_A (IW_Raw := INT_TO_REAL(%IW0.10.2),
       SimEnable := simulations[2],
       Raw_Min := 0.0,
       Raw_Max := 10000.0,
       Eng_Min := Armoires[0].ana.Balance.ech_basse,
       Eng_Max := Armoires[0].ana.Balance.ech_haute,
       SimValue := INT_TO_REAL(simulations_valeurs[2]),
       DeadBand := 0.05);

Armoires[0].ana.Balance.mes := BALANCE_A.StableValue;
Armoires[0].ana.Balance.raw := BALANCE_A.RawInput;

(* COTE B *)

PT1_B (IW_Raw := INT_TO_REAL(%IW0.11.0),
       SimEnable := simulations[8],
       Raw_Min := 0.0,
       Raw_Max := 10000.0,
       Eng_Min := Armoires[1].ana.PT1.ech_basse,
       Eng_Max := Armoires[1].ana.PT1.ech_haute,
       SimValue := INT_TO_REAL(simulations_valeurs[8]),
       DeadBand := 0.05);

Armoires[1].ana.PT1.mes := PT1_B.StableValue;
Armoires[1].ana.PT1.raw := PT1_B.RawInput;

PT3_B (IW_Raw := INT_TO_REAL(%IW0.11.1),
       SimEnable := simulations[9],
       Raw_Min := 0.0,
       Raw_Max := 10000.0,
       Eng_Min := Armoires[1].ana.PT3.ech_basse,
       Eng_Max := Armoires[1].ana.PT3.ech_haute,
       SimValue := INT_TO_REAL(simulations_valeurs[9]),
       DeadBand := 0.05);

Armoires[1].ana.PT3.mes := PT3_B.StableValue;
Armoires[1].ana.PT3.raw := PT3_B.RawInput;

BALANCE_B (IW_Raw := INT_TO_REAL(%IW0.11.2),
       SimEnable := simulations[10],
       Raw_Min := 0.0,
       Raw_Max := 10000.0,
       Eng_Min := Armoires[1].ana.Balance.ech_basse,
       Eng_Max := Armoires[1].ana.Balance.ech_haute,
       SimValue := INT_TO_REAL(simulations_valeurs[10]),
       DeadBand := 0.05);

Armoires[1].ana.Balance.mes := BALANCE_B.StableValue;
Armoires[1].ana.Balance.raw := BALANCE_B.RawInput;
