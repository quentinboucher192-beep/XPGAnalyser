(* LE TEXTE DECRIT TOUT L'ELEMENT : ce qu'il ne dit pas repart de zero *)
IF T = 1 THEN
    _Step.IsInitial := FALSE;
    _Step.IsFinal := FALSE;
    _Step.Name := '';
ELSIF T = 2 THEN
    _Trans.Enabled := TRUE;
    _Trans.JoinKind := 1;
    _Trans.SplitKind := 0;
    _Trans.ConditionTime := d;
    _Trans.SourceCount := 0;
    _Trans.DestCount := 0;
    _Trans.CondText := '';
ELSIF T = 3 THEN
    _Act.EnableCond := TRUE;
    _Act.Kind := 0;
    _Act.Delay := d;
    _Act.BoundStepId := -1;      (* sans s= : liee a aucune etape *)
    _Act.Name := '';
END_IF;

Work := Text;

WHILE LEN_INT(Work) > 0 DO

    (* le jeton suivant ; FIND_INT rend 0 quand il ne trouve pas *)
    Pos := FIND_INT(Work, '|');
    IF Pos <= 0 THEN
        Token := Work;
        Work := '';
    ELSE
        Token := LEFT_INT(Work, Pos - 1);
        Work := RIGHT_INT(Work, LEN_INT(Work) - Pos);
    END_IF;

    (* cle = valeur, au premier '=' : 'c=NB==NBSURP' garde 'NB==NBSURP' *)
    Pos := FIND_INT(Token, '=');
    Spaced := FALSE;
    IF Pos > 1 THEN
        Key := LEFT_INT(Token, Pos - 1);
        Value := RIGHT_INT(Token, LEN_INT(Token) - Pos);
        (* les espaces autour : retires seulement s'il y en a, pour ne pas
           payer quatre boucles par jeton dans le cas courant *)
        Spaced := FIND_INT(Token, ' ') > 0;
        IF Spaced THEN
            WHILE (LEN_INT(Key) > 0) AND (LEFT_INT(Key, 1) = ' ') DO
                Key := RIGHT_INT(Key, LEN_INT(Key) - 1);
            END_WHILE;
            WHILE (LEN_INT(Key) > 0) AND (RIGHT_INT(Key, 1) = ' ') DO
                Key := LEFT_INT(Key, LEN_INT(Key) - 1);
            END_WHILE;
            WHILE (LEN_INT(Value) > 0) AND (LEFT_INT(Value, 1) = ' ') DO
                Value := RIGHT_INT(Value, LEN_INT(Value) - 1);
            END_WHILE;
            WHILE (LEN_INT(Value) > 0) AND (RIGHT_INT(Value, 1) = ' ') DO
                Value := LEFT_INT(Value, LEN_INT(Value) - 1);
            END_WHILE;
        END_IF;
    ELSE
        Key := '';      (* pas de '=' : jeton ignore *)
    END_IF;

    (* rangement par genre d'element, puis par cle : le cas le plus
       frequent (s= et d= d'une transition) passe en premier *)
    IF T = 2 THEN

        IF (Key = 's') OR (Key = 'd') THEN
            (* ---------------- SOURCES, DESTINATIONS : 3 places ---------------- *)
            WorkList := Value;
            WHILE LEN_INT(WorkList) > 0 DO
                Pos := FIND_INT(WorkList, ',');
                IF Pos <= 0 THEN
                    Item := WorkList;
                    WorkList := '';
                ELSE
                    Item := LEFT_INT(WorkList, Pos - 1);
                    WorkList := RIGHT_INT(WorkList, LEN_INT(WorkList) - Pos);
                END_IF;
                IF Spaced THEN
                    WHILE (LEN_INT(Item) > 0) AND (LEFT_INT(Item, 1) = ' ') DO
                        Item := RIGHT_INT(Item, LEN_INT(Item) - 1);
                    END_WHILE;
                END_IF;

                IF LEN_INT(Item) > 0 THEN
                    IF Key = 's' THEN
                        IF _Trans.SourceCount < 3 THEN
                            _Trans.Sources[_Trans.SourceCount] := STRING_TO_INT(Item);
                        END_IF;
                        _Trans.SourceCount := _Trans.SourceCount + 1;   (* au-dela de 3 : ConfigFault *)
                    ELSE
                        IF _Trans.DestCount < 3 THEN
                            _Trans.Destinations[_Trans.DestCount] := STRING_TO_INT(Item);
                        END_IF;
                        _Trans.DestCount := _Trans.DestCount + 1;
                    END_IF;
                END_IF;
            END_WHILE;

        ELSIF Key = 'c' THEN
            _Trans.CondText := LEFT_INT(Value, 8);     (* coupe a 8 *)

        ELSIF Key = 'id' THEN
            IF LEN_INT(Value) > 0 THEN _Trans.Id := STRING_TO_INT(Value); END_IF;
        END_IF;

    ELSIF T = 3 THEN

        IF Key = 's' THEN
            IF LEN_INT(Value) > 0 THEN _Act.BoundStepId := STRING_TO_INT(Value); END_IF;
        ELSIF Key = 'k' THEN
            IF LEN_INT(Value) > 0 THEN _Act.Kind := STRING_TO_INT(Value); END_IF;
        ELSIF Key = 'n' THEN
            _Act.Name := LEFT_INT(Value, 8);           (* coupe a 8 *)
        ELSIF Key = 'id' THEN
            IF LEN_INT(Value) > 0 THEN _Act.Id := STRING_TO_INT(Value); END_IF;
        END_IF;

    ELSIF T = 1 THEN

        IF Key = 'n' THEN
            _Step.Name := LEFT_INT(Value, 4);          (* coupe a 4 *)
        ELSIF Key = 'id' THEN
            IF LEN_INT(Value) > 0 THEN _Step.Id := STRING_TO_INT(Value); END_IF;
        ELSIF Key = 'i' THEN
            IF LEN_INT(Value) > 0 THEN _Step.IsInitial := STRING_TO_INT(Value) = 1; END_IF;
        ELSIF Key = 'f' THEN
            IF LEN_INT(Value) > 0 THEN _Step.IsFinal := STRING_TO_INT(Value) = 1; END_IF;
        END_IF;

    END_IF;

END_WHILE;

(* plusieurs destinations : divergence en ET, toutes s'activent *)
IF (T = 2) AND (_Trans.DestCount > 1) THEN
    _Trans.SplitKind := 1;
END_IF;
