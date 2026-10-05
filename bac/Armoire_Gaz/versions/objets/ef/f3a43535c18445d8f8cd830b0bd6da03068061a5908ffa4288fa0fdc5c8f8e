IF armoires[1].cmd_toggle_manu THEN
	armoires[1].manu := NOT(armoires[1].manu);
END_IF;
IF armoires[1].entrees.Auto_manu THEN 
	armoires[1].manu := TRUE;
END_IF;

armoires[1].sorties.Sorties_in[0] := armoires[1].sorties.V2.F1;
armoires[1].sorties.Sorties_in[1]:= armoires[1].sorties.V3.F1;
armoires[1].sorties.Sorties_in[2]:= armoires[1].sorties.V4.F1;
armoires[1].sorties.Sorties_in[3]:= armoires[1].sorties.V5.F1;
armoires[1].sorties.Sorties_in[4]:= armoires[1].sorties.VBTE.F1;
armoires[1].sorties.Sorties_in[5]:= armoires[1].sorties.V6.F1;
armoires[1].sorties.Sorties_in[6]:= armoires[1].sorties.V16.F1;
armoires[1].sorties.Sorties_in[7]:= armoires[1].sorties.V7.F1;
armoires[1].sorties.Sorties_in[8]:= armoires[1].sorties.VS.F1;
armoires[1].sorties.Sorties_in[9]:= armoires[1].sorties.V9.F1;
armoires[1].sorties.Sorties_in[10] := armoires[1].sorties.PPE.F1;
armoires[1].sorties.Sorties_in[11] := armoires[1].sorties.VHE.F1;
armoires[1].sorties.Sorties_in[12] := FALSE;
armoires[1].sorties.Sorties_in[13] := FALSE;
armoires[1].sorties.Sorties_in[14] := FALSE;
armoires[1].sorties.Sorties_in[15] := FALSE;

(* AFFECTATION DE LA MATRICE *)
WordConverter (In := 2#1011111110001111, Out => armoires[1].sorties.V2.Mat);
WordConverter (In := 2#0111111110011111, Out => armoires[1].sorties.V3.Mat);
WordConverter (In := 2#1111111111111111, Out => armoires[1].sorties.V4.Mat);
WordConverter (In := 2#1111111111111111, Out => armoires[1].sorties.V5.Mat);
WordConverter (In := 2#1111111111111111, Out => armoires[1].sorties.VBTE.Mat);
WordConverter (In := 2#1111111111111111, Out => armoires[1].sorties.V6.Mat);
WordConverter (In := 2#1111111111111111, Out => armoires[1].sorties.V16.Mat);
WordConverter (In := 2#1111111110011111, Out => armoires[1].sorties.V7.Mat);
WordConverter (In := 2#1111111111111111, Out => armoires[1].sorties.VS.Mat);
WordConverter (In := 2#0011111011101111, Out => armoires[1].sorties.V9.Mat);
WordConverter (In := 2#0011111011101111, Out => armoires[1].sorties.PPE.Mat);
WordConverter (In := 2#0111111110011111, Out => armoires[1].sorties.VHE.Mat);

armoires[1].sorties.Sorties_out := armoires[1].sorties.Sorties_in;

if (armoires[1].manu) then
	FOR i := 0 TO 15 DO
	
		Autorise := TRUE;
		CASE i OF
			0: Tempon := armoires[1].sorties.V2.Mat;
			1: Tempon := armoires[1].sorties.V3.Mat;
			2: Tempon := armoires[1].sorties.V4.Mat;
			3: Tempon := armoires[1].sorties.V5.Mat;
			4: Tempon := armoires[1].sorties.VBTE.Mat;
			5: Tempon := armoires[1].sorties.V6.Mat;
			6: Tempon := armoires[1].sorties.V16.Mat;
			7: Tempon := armoires[1].sorties.V7.Mat;
			8: Tempon := armoires[1].sorties.VS.Mat;
			9: Tempon := armoires[1].sorties.V9.Mat;
			10: Tempon := armoires[1].sorties.PPE.Mat;
			11: Tempon := armoires[1].sorties.VHE.Mat;
			ELSE WordConverter (In := 16#FFFF, Out => Tempon);
		END_CASE;
		
		FOR j := 0 TO 15 DO
			IF NOT(Tempon[j]) THEN
	        		(* bit 0_i = 0 *)
				(* bloquant *)
				IF armoires[1].sorties.Sorties_in[j] THEN
					(* bit 0_i = 1 *)
					(* vanne deja ouverte *)
					Autorise := FALSE;
				END_IF;
	    		END_IF;
		END_FOR;

		CASE i OF
			0: armoires[1].sorties.V2.Autorisee := Autorise;
			1: armoires[1].sorties.V3.Autorisee := Autorise;
			2: armoires[1].sorties.V4.Autorisee := Autorise;
			3: armoires[1].sorties.V5.Autorisee := Autorise;
			4: armoires[1].sorties.VBTE.Autorisee := Autorise;
			5: armoires[1].sorties.V6.Autorisee := Autorise;
			6: armoires[1].sorties.V16.Autorisee := Autorise;
			7: armoires[1].sorties.V7.Autorisee := Autorise;
			8: armoires[1].sorties.VS.Autorisee := Autorise;
			9: armoires[1].sorties.V9.Autorisee := Autorise;
			10: armoires[1].sorties.PPE.Autorisee := Autorise;
			11: armoires[1].sorties.VHE.Autorisee := Autorise;
		END_CASE;

	END_FOR;
end_if;

