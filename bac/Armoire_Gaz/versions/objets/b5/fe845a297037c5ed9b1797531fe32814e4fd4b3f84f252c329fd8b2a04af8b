for index := 0 to 1 do

	armoires[index].sorties.Sorties_in[0] := FALSE;
	armoires[index].sorties.Sorties_in[1]:= armoires[index].sorties.V2.F1;
	armoires[index].sorties.Sorties_in[2]:= armoires[index].sorties.V3.F1;
	armoires[index].sorties.Sorties_in[3]:= armoires[index].sorties.V4.F1;
	armoires[index].sorties.Sorties_in[4]:= armoires[index].sorties.V5.F1;
	armoires[index].sorties.Sorties_in[5]:= armoires[index].sorties.V6.F1;
	armoires[index].sorties.Sorties_in[6]:= armoires[index].sorties.V7.F1;
	armoires[index].sorties.Sorties_in[7]:= armoires[index].sorties.VS.F1;
	armoires[index].sorties.Sorties_in[8]:= armoires[index].sorties.V9.F1;
	armoires[index].sorties.Sorties_in[9]:= FALSE;
	armoires[index].sorties.Sorties_in[10] := FALSE;
	armoires[index].sorties.Sorties_in[11] := FALSE;
	armoires[index].sorties.Sorties_in[12] := armoires[index].sorties.PPE.F1;
	armoires[index].sorties.Sorties_in[13] := armoires[index].sorties.VBTE.F1;
	armoires[index].sorties.Sorties_in[14] := armoires[index].sorties.V16.F1;
	armoires[index].sorties.Sorties_in[15] := armoires[index].sorties.VHE.F1;

	armoires[index].sorties.Sorties_out := armoires[index].sorties.Sorties_in;
	
	for index_out := 0 to 15 do
		if armoires[index].sorties.Sorties_reset[index_out] then
			armoires[index].sorties.Sorties_cpt_rising[index_out] := 0;
			armoires[index].sorties.Sorties_cpt_falling[index_out] := 0;
		end_if;
	end_for;	

	for index_out := 0 to 15 do
		if armoires[index].sorties.Sorties_in[index_out] and not(armoires[index].sorties.Sorties_prev[index_out]) then
			armoires[index].sorties.Sorties_cpt_rising[index_out] := armoires[index].sorties.Sorties_cpt_rising[index_out] + 1;
		end_if; 
		if not(armoires[index].sorties.Sorties_in[index_out]) and armoires[index].sorties.Sorties_prev[index_out] then
			armoires[index].sorties.Sorties_cpt_falling[index_out] := armoires[index].sorties.Sorties_cpt_falling[index_out] + 1;
		end_if;
		armoires[index].sorties.Sorties_prev[index_out] := armoires[index].sorties.Sorties_in[index_out];
	end_for;

	Sorties_TOR16_STATES[index] := DWORD#0;

	FOR index_out := 0 TO 15 DO
	    IF armoires[index].sorties.Sorties_out[index_out] THEN
	        Sorties_TOR16_STATES[index] :=
	            Sorties_TOR16_STATES[index] OR SHL(DWORD#1, index_out);
	    END_IF;
	END_FOR;
end_for;
