(* =====================================================================
    DFB_SYS_CLOCK - l'heure de l'automate, et le temps qui passe vraiment.

    DtMs REMPLACE LA CONSTANTE CycleMs. Tous les blocs de la bibliotheque
    accumulent leurs temps depuis CycleMs ; y cabler 20, c'est supposer que
    la tache tient sa periode. Un cycle qui deborde, une tache cyclique, une
    periode changee dans la configuration : les chronos derivent sans que
    rien ne le dise.

    DtMs vaut la periode nominale tant que la tache la tient, et RATTRAPE
    quand le compteur libre Tick montre qu'elle a derive de plus d'une unite.
    La somme des DtMs ne s'ecarte jamais du temps reel de plus d'une unite de
    Tick - 100 ms avec %SD18, 1 ms avec un compteur en millisecondes.

    UN ARRET N'EST PAS UN CYCLE. Plus de dix periodes d'un coup, c'est une
    reprise apres un STOP : ce temps-la n'est pas rendu, sinon chaque
    compteur d'heures de marche compterait l'arret comme de la marche.
   ===================================================================== *)

(* ---- le temps ecoule ------------------------------------------------ *)
nominal := PeriodMs;
IF nominal <= 0 THEN nominal := LastCycleMs; END_IF;
IF nominal < 0 THEN nominal := 0; END_IF;

IF NOT started THEN
    started := TRUE;
    prevTick := Tick;
    lag := 0;
    DtMs := nominal;
ELSE
    IF TickUnitMs > 0 AND Tick <> prevTick THEN
        tickSeen := TRUE;
    END_IF;
    IF tickSeen THEN
        real := Tick - prevTick;
        IF real < 0 AND TickWrap > 0 THEN
            real := real + TickWrap;
        END_IF;
        real := real * TickUnitMs;
        IF real < 0 OR real > 10 * nominal + TickUnitMs THEN
            (* Une reprise, pas un cycle : le temps n'est pas rendu. *)
            Jumps := Jumps + 1;
            lag := 0;
        ELSE
            lag := lag + real;
        END_IF;
        DtMs := nominal;
        IF lag - DtMs > TickUnitMs THEN
            DtMs := lag - TickUnitMs;
        ELSIF lag - DtMs < -TickUnitMs THEN
            DtMs := lag + TickUnitMs;
            IF DtMs < 0 THEN DtMs := 0; END_IF;
        END_IF;
        lag := lag - DtMs;
    ELSE
        DtMs := nominal;
    END_IF;
    prevTick := Tick;
END_IF;
LagMs := lag;

(* Les secondes depuis le demarrage, et l'impulsion de la seconde. *)
NewSecond := FALSE;
FracMs := FracMs + DtMs;
IF FracMs >= 1000 THEN
    secs := DINT_TO_UDINT(FracMs / 1000);
    FracMs := FracMs MOD 1000;
    UpS := UpS + secs;
    NewSecond := TRUE;
END_IF;

(* ---- l'heure, depuis les mots BCD ------------------------------------ *)
bad := FALSE;
IF RtcSec < 0 THEN
    bad := TRUE;
ELSE
    hi := RtcSec / 256;
    lo := RtcSec MOD 256;
    IF (hi / 16) > 9 OR (hi MOD 16) > 9 OR (lo / 16) > 9 OR (lo MOD 16) > 9 THEN
        bad := TRUE;
    END_IF;
    s := (hi / 16) * 10 + hi MOD 16;
    lo := (lo / 16) * 10 + lo MOD 16;
END_IF;
IF RtcHourMin < 0 THEN
    bad := TRUE;
ELSE
    hi := RtcHourMin / 256;
    lo := RtcHourMin MOD 256;
    IF (hi / 16) > 9 OR (hi MOD 16) > 9 OR (lo / 16) > 9 OR (lo MOD 16) > 9 THEN
        bad := TRUE;
    END_IF;
    h := (hi / 16) * 10 + hi MOD 16;
    mi := (lo / 16) * 10 + lo MOD 16;
END_IF;
IF RtcMonthDay < 0 THEN
    bad := TRUE;
ELSE
    hi := RtcMonthDay / 256;
    lo := RtcMonthDay MOD 256;
    IF (hi / 16) > 9 OR (hi MOD 16) > 9 OR (lo / 16) > 9 OR (lo MOD 16) > 9 THEN
        bad := TRUE;
    END_IF;
    mo := (hi / 16) * 10 + hi MOD 16;
    d := (lo / 16) * 10 + lo MOD 16;
END_IF;
IF RtcYear < 0 THEN
    bad := TRUE;
ELSE
    hi := RtcYear / 256;
    lo := RtcYear MOD 256;
    IF (hi / 16) > 9 OR (hi MOD 16) > 9 OR (lo / 16) > 9 OR (lo MOD 16) > 9 THEN
        bad := TRUE;
    END_IF;
    hi := (hi / 16) * 10 + hi MOD 16;
    y := (lo / 16) * 10 + lo MOD 16;
END_IF;
(* hi porte le siecle : 20 pour 2026. *)
y := hi * 100 + y;

(* Les jours du mois, et ceux de l'annee avant lui. *)
CASE mo OF
    1:  cum := 0;   dim := 31;
    2:  cum := 31;  dim := 28;
    3:  cum := 59;  dim := 31;
    4:  cum := 90;  dim := 30;
    5:  cum := 120; dim := 31;
    6:  cum := 151; dim := 30;
    7:  cum := 181; dim := 31;
    8:  cum := 212; dim := 31;
    9:  cum := 243; dim := 30;
    10: cum := 273; dim := 31;
    11: cum := 304; dim := 30;
    12: cum := 334; dim := 31;
ELSE
    cum := 0; dim := 0;
END_CASE;
(* 2000 a 2099 : une annee sur quatre est bissextile, sans exception. *)
IF (y MOD 4) = 0 THEN
    IF mo = 2 THEN dim := 29; END_IF;
    IF mo > 2 THEN cum := cum + 1; END_IF;
END_IF;

ok := NOT RtcLost AND NOT bad
      AND y >= 2000 AND y <= 2099
      AND mo >= 1 AND mo <= 12
      AND d >= 1 AND d <= dim
      AND h <= 23 AND mi <= 59 AND s <= 59;

Valid := ok;
NewMinute := FALSE;
NewHour := FALSE;
NewDay := FALSE;

IF ok THEN
    Year := y;
    Month := mo;
    Day := d;
    Hour := h;
    Minute := mi;
    Second := s;
    MinuteOfDay := h * 60 + mi;
    IF RtcDow >= 1 AND RtcDow <= 7 THEN
        DayOfWeek := RtcDow;
    ELSE
        DayOfWeek := 0;
    END_IF;

    (* Les jours depuis le 01/01/2000 : 365 par an, plus un par annee
       bissextile ecoulee - 2000 en est une. *)
    days := INT_TO_DINT(y - 2000) * 365 + INT_TO_DINT((y - 2000 + 3) / 4)
          + INT_TO_DINT(cum + d - 1);
    EpochS := DINT_TO_UDINT(days) * 86400
            + DINT_TO_UDINT(INT_TO_DINT(h) * 3600 + INT_TO_DINT(mi) * 60 + INT_TO_DINT(s));

    (* Les impulsions, a partir de la deuxieme lecture valide : la premiere
       n'est pas un changement. *)
    IF haveRtc THEN
        NewMinute := mi <> prevMin;
        NewHour := h <> prevHour;
        NewDay := d <> prevDay;
    END_IF;
    prevMin := mi;
    prevHour := h;
    prevDay := d;
    haveRtc := TRUE;
ELSE
    EpochS := 0;
END_IF;
