(* Genere par PreparerGrafcets. EN TETE DE TACHE : le temps reel du cycle,
   que suivent les grafcets (GC_Temps) et les autres blocs. *)
Horloge(PeriodMs := %SW0, LastCycleMs := %SW30, Tick := %SD18, TickUnitMs := 100,
        RtcSec := %SW50, RtcHourMin := %SW51, RtcMonthDay := %SW52, RtcYear := %SW53,
        RtcDow := %SW49, RtcLost := %S51);
