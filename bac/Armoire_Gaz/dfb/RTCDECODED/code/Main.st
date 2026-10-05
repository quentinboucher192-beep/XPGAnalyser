IF NOT Enable THEN
RETURN;
END_IF;

RRTC_DT_MS(Display, Calcntpc, Out.Status);

lowOk := ((Out.Status AND 16#00FF) = 1);
highOk := (((Out.Status AND 16#FF00) / 256) = 1);
Out.ValidUtc := lowOk AND highOk;

(* ========================= *)
(* TEMPS DE BASE SAFE        *)
(* ========================= *)

Out.Millisecondes := Calcntpc.Fraction_Second;

IF Out.Millisecondes < 0 THEN
Out.Millisecondes := 0;
ELSIF Out.Millisecondes > 999 THEN
Out.Millisecondes := 999;
END_IF;

secOfDay := Calcntpc.Seconds MOD 86400;

msOfDay :=
(secOfDay * 1000) +
INT_TO_UDINT(Out.Millisecondes);

(* ========================= *)
(* HEURE / MIN / SEC         *)
(* ========================= *)

Out.Heures := UDINT_TO_INT(secOfDay / 3600);
Out.Minutes := UDINT_TO_INT((secOfDay MOD 3600) / 60);
Out.Secondes := UDINT_TO_INT(secOfDay MOD 60);

(* ========================= *)
(* DATE                      *)
(* ========================= *)

daysSince1980 := Calcntpc.Seconds / 86400;
remDays := daysSince1980;
y := 1980;

finished := FALSE;
WHILE NOT finished DO
isLeap := FALSE;


IF (y MOD 4 = 0) THEN
    IF (y MOD 100 <> 0) OR (y MOD 400 = 0) THEN
        isLeap := TRUE;
    END_IF;
END_IF;

IF remDays >= INT_TO_UDINT(365 + BOOL_TO_INT(isLeap)) THEN
    remDays := remDays - INT_TO_UDINT(365 + BOOL_TO_INT(isLeap));
    y := y + 1;
ELSE
    finished := TRUE;
END_IF;


END_WHILE;

m := 1;
finished := FALSE;

WHILE NOT finished DO


dim := 31;

IF (m = 4) OR (m = 6) OR (m = 9) OR (m = 11) THEN
    dim := 30;

ELSIF m = 2 THEN

    isLeap := FALSE;

    IF (y MOD 4 = 0) THEN
        IF (y MOD 100 <> 0) OR (y MOD 400 = 0) THEN
            isLeap := TRUE;
        END_IF;
    END_IF;

    IF isLeap THEN
        dim := 29;
    ELSE
        dim := 28;
    END_IF;

END_IF;

IF remDays >= INT_TO_UDINT(dim) THEN
    remDays := remDays - INT_TO_UDINT(dim);
    m := m + 1;
ELSE
    finished := TRUE;
END_IF;


END_WHILE;

Out.Annee := y;
Out.Mois := m;
Out.Jour := UDINT_TO_INT(remDays) + 1;

(* ========================= *)
(* CYCLE TIME                *)
(* ========================= *)

IF msOfDay >= msOfDayPrev THEN
Out.CycleMs := msOfDay - msOfDayPrev;
ELSE
Out.CycleMs := (86400000 - msOfDayPrev) + msOfDay;
END_IF;

msOfDayPrev := msOfDay;

