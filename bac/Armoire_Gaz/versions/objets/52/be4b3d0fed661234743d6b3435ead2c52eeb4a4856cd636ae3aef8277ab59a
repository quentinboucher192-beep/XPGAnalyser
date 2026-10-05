TonMajeure (IN := modification_majeure, PT := t#5s);
IF (TonMajeure.Q) THEN modification_majeure := false; END_IF;

TonRCHGT_A (IN := armoires[0].retour_sur_chgt and not(armoires[0].manu), PT := t#5s);
IF (TonRCHGT_A.Q) THEN armoires[0].retour_sur_chgt := false; END_IF;

TonRCHGT_B (IN := armoires[1].retour_sur_chgt and not(armoires[1].manu), PT := t#5s);
IF (TonRCHGT_B.Q) THEN armoires[1].retour_sur_chgt := false; END_IF;

TonRCHGT_A_DECL (IN := armoires[0].retour_sur_chgt_ap_decl and not(armoires[0].en_declenchement), PT := t#5s);
IF (TonRCHGT_A_DECL.Q) THEN armoires[0].retour_sur_chgt_ap_decl := false; END_IF;

TonRCHGT_B_DECL (IN := armoires[1].retour_sur_chgt_ap_decl and not(armoires[1].en_declenchement), PT := t#5s);
IF (TonRCHGT_B_DECL.Q) THEN armoires[1].retour_sur_chgt_ap_decl := false; END_IF;

%M1000 := false;
%M1001 := false;
%M1004 := false;
%M1005 := false;
%M1006 := false;
%M1007 := false;
%M1008 := false;
%M1009 := false;
%M1015 := false;
%M1016 := false;
%M1017 := false;
%M1018 := false;
%M1019 := false;
%M1020 := false;
%M1021 := false;
%M1022 := false;
%M1023 := false;
%M1024 := false;
%M1025 := false;
%M1026 := false;
%M1027 := false;
%M1028 := false;
%M1029 := false;
%M1030 := false;
%M1031 := false;
%M1032 := false;
%M1033 := false;
%M1034 := false;
%M1035 := false;
%M1036 := false;
%M1037 := false;
%M1038 := false;
%M1039 := false;
%M1040 := false;
%M1041 := false;
%M1042 := false;
%M1043 := false;
%M1044 := false;
%M1045 := false;
%M1046 := false;
%M1047 := false;
%M1048 := false;
%M1049 := false;
%M1050 := false;
%M1051 := false;
%M1052 := false;
%M1053 := false;
%M1054 := false;
%M1055 := false;
%M1056 := false;
%M1057 := false;
%M1120 := false;
%M1121 := false;
%M1122 := false;

for i := 0 to nombre_de_config - 1 do
	reset_config[i] := false;
end_for;

%MW3062 := 0;
%MW3064 := 0;
%MW3072 := 0;
%MW3074 := 0;
