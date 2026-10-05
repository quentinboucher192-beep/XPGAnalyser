(* ____________________________________________________________________ *)
(* SURP_X0 *)
if Acts_SurpA[0].Out then
	timeout_atteintA := FALSE;
	armoires[0].CycleSurpressionBit := FALSE;
end_if;
(* ____________________________________________________________________ *)


(* ____________________________________________________________________ *)
(* SURP_X1 *)
if Acts_SurpA[1].Out then
	armoires[0].CycleSurpressionBit := TRUE;
end_if;

(* ____________________________________________________________________ *)
(* SURP_X2 *)
if Acts_SurpA[2].Out then
	timeout_atteintA := true;
end_if;

if Acts_SurpA[3].Out then
	armoires[0].sorties.V5.F1 := FALSE;
	armoires[0].sorties.V5.F0 := TRUE;
end_if;

if Acts_SurpA[4].Out then
	armoires[0].sorties.V7.F0 := FALSE;
	armoires[0].sorties.V7.F1 := TRUE;
end_if;

if Acts_SurpA[5].Out then
	armoires[0].sorties.VS.F0 := FALSE;
	armoires[0].sorties.VS.F1 := TRUE;
end_if;

(* ____________________________________________________________________ *)
(* SURP_X3 *)
if Acts_SurpA[6].Out then
	if (armoires[1].prete) then
		armoires[0].demande_basculement := TRUE;
	end_if;
	armoires[0].defs.surpression := TRUE;
end_if;

if Acts_SurpA[7].Out then
	armoires[0].compteur_cycle_surpression := armoires[0].compteur_cycle_surpression + 1;
end_if;

(* ____________________________________________________________________ *)
(* SURP_X4 *)

if Acts_SurpA[8].Out then
	armoires[0].sorties.VS.F0 := TRUE;
	armoires[0].sorties.VS.F1 := FALSE;
end_if;

if Acts_SurpA[9].Out then
	armoires[0].sorties.V7.F0 := TRUE;
	armoires[0].sorties.V7.F1 := FALSE;
end_if;

if Acts_SurpA[10].Out then
	armoires[0].sorties.V5.F0 := FALSE;
	armoires[0].sorties.V5.F1 := TRUE;
end_if;

(* ____________________________________________________________________ *)
(* SURP_X5 *)


(* ____________________________________________________________________ *)
(* SURP_X10 *)
if Acts_SurpA[12].Out  then
	armoires[0].demande_basculement := TRUE;
	armoires[0].defs.surpression := TRUE;
end_if;
