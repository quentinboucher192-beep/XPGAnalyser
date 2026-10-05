(* ____________________________________________________________________ *)
(* SURP_X0 *)
if Acts_SurpB[0].Out then
	timeout_atteintB := FALSE;
	armoires[1].CycleSurpressionBit := FALSE;
end_if;
(* ____________________________________________________________________ *)


(* ____________________________________________________________________ *)
(* SURP_X1 *)
if Acts_SurpB[1].Out then
	armoires[1].CycleSurpressionBit := TRUE;
end_if;

(* ____________________________________________________________________ *)
(* SURP_X2 *)
if Acts_SurpB[2].Out then
	timeout_atteintB := true;
end_if;

if Acts_SurpB[3].Out then
	armoires[1].sorties.V5.F1 := FALSE;
	armoires[1].sorties.V5.F0 := TRUE;
end_if;

if Acts_SurpB[4].Out then
	armoires[1].sorties.V7.F0 := FALSE;
	armoires[1].sorties.V7.F1 := TRUE;
end_if;

if Acts_SurpB[5].Out then
	armoires[1].sorties.VS.F0 := FALSE;
	armoires[1].sorties.VS.F1 := TRUE;
end_if;

(* ____________________________________________________________________ *)
(* SURP_X3 *)
if Acts_SurpB[6].Out then
	if (armoires[0].prete) then
		armoires[1].demande_basculement := TRUE;
	end_if;
	armoires[1].defs.surpression := TRUE;
end_if;

if Acts_SurpB[7].Out then
	armoires[1].compteur_cycle_surpression := armoires[1].compteur_cycle_surpression + 1;
end_if;

(* ____________________________________________________________________ *)
(* SURP_X4 *)

if Acts_SurpB[8].Out then
	armoires[1].sorties.VS.F0 := TRUE;
	armoires[1].sorties.VS.F1 := FALSE;
end_if;

if Acts_SurpB[9].Out then
	armoires[1].sorties.V7.F0 := TRUE;
	armoires[1].sorties.V7.F1 := FALSE;
end_if;

if Acts_SurpB[10].Out then
	armoires[1].sorties.V5.F0 := FALSE;
	armoires[1].sorties.V5.F1 := TRUE;
end_if;

(* ____________________________________________________________________ *)
(* SURP_X5 *)


(* ____________________________________________________________________ *)
(* SURP_X10 *)
if Acts_SurpB[12].Out  then
	armoires[1].demande_basculement := TRUE;
	armoires[1].defs.surpression := TRUE;
end_if;
