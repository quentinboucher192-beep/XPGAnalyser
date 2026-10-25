(* 1.13.0 (lot J3) : JSON et regex DANS la page (WebAssembly), au demarrage.
   Prouve que l'analyseur JSON et std::regex tournent en WASM, et qu'un motif
   invalide leve E_REGEX (rattrape) sans planter la page. *)
VAR j : ANY; END_VAR
j := JSON_LIRE('{"gaz":"Helium","pression":7.5}');
Gaz := JSON_TEXTE(j['gaz'], '?');
Pression := JSON_NOMBRE(j['pression'], 0.0);
RegexOk := REGEX_TESTE('PRES=7.5bar', 'PRES=([0-9.]+)bar');
TRY
    RegexOk := RegexOk AND REGEX_TESTE('x', '[0-9');   (* un motif invalide *)
    CodeErr := 'pas d erreur';
CATCH e
    CodeErr := e.Code;                                 (* E_REGEX, sans planter la page *)
END_TRY;
