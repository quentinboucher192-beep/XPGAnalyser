(* Une ligne au journal, et le compte des traces *)
VAR_INPUT
    Quoi : STRING;
    Niveau : INT := 1;
END_VAR

Compteur_Clics := Compteur_Clics + 1;
IF Niveau > 1 THEN
    IHM_JOURNAL('[ALERTE] {Quoi}');
ELSE
    IHM_JOURNAL('{Quoi}');
END_IF;
