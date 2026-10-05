(* =========================================================
   CONFIGURATION - sélection des armoires
   ========================================================= *)
(* Si le système n'est pas configuré, on force l'index par défaut *)
IF NOT generalites.configuree THEN
    index_armoire_utilisee := generalites.index_depart;
END_IF;

(* Sauvegarde de l'ancien index utilisé (debug / traçabilité) *)
ancien_index_utilise := index_armoire_utilisee;

(* =========================================================
   ARMOIRE ACTIVE / INACTIVE
   ========================================================= *)
(* Détermination de l'armoire non utilisée (basculement binaire) *)
IF index_armoire_utilisee = 0 THEN
    index_armoire_non_utilisee := 1;
ELSE
    index_armoire_non_utilisee := 0;
END_IF;

(* =========================================================
   ETAT GLOBAL CONFIGURATION
   ========================================================= *)
configuree := generalites.configuree;
acquittements[0] := Acquit_A;
acquittements[1] := Acquit_B;
