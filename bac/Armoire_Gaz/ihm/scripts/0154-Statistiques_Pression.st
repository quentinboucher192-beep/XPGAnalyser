(* Mini et maxi de la pression A, depuis le lancement de l'IHM *)
VAR
    Mini : REAL := 1000.0;   (* VAR : gardée d'une exécution à l'autre *)
    Maxi : REAL;
    Tours : DINT;
    Ecart : REAL;
END_VAR
VAR_TEMP
    P : REAL;                (* VAR_TEMP : repart à chaque exécution *)
END_VAR

P := Armoires[0].ana.PT1.mes;
Tours := Tours + 1;
IF P < Mini THEN
    Mini := P;
END_IF;
IF P > Maxi THEN
    Maxi := P;
END_IF;
IF Tours MOD 5 = 0 THEN
    IHM_JOURNAL('Pression A : mini {Mini:0.0}, maxi {Maxi:0.0} bar ({Tours} mesures)');
END_IF;
IF Moyenne_Pression(P, Armoires[1].ana.PT1.mes) > 30.0 THEN
    Tracer_Evenement('Moyenne haute', 2);
END_IF;