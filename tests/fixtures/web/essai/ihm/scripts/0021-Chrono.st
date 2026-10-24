(* script detache : compte les secondes tant que Marche *)
WHILE TRUE DO
    IF Marche THEN
        Secondes := Secondes + 1;
    END_IF;
    WAIT(T#100ms);
END_WHILE;
