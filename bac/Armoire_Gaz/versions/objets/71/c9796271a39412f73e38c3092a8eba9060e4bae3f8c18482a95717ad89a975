IF T = 1 THEN
	_Step.IsInitial := FALSE;
	_Step.IsFinal := FALSE;
	
	Work := Text;

	WHILE LEN_INT(Work) > 0 DO

	    Pos := FIND_INT(Work, '|');

	    IF Pos = -1 THEN
	        Token := Work;
	        Work := '';
	    ELSE
	        Token := LEFT_INT(Work, Pos - 1);
	        Work := RIGHT_INT(Work, LEN_INT(Work) - Pos);
	    END_IF;

	    (* ---------------- ID ---------------- *)
	    IF LEFT_INT(Token,3) = 'id=' THEN

	        Value := RIGHT_INT(Token, LEN_INT(Token) - 3);
	        _Step.Id := STRING_TO_INT(Value);

	    (* ---------------- NAME ---------------- *)
	    ELSIF LEFT_INT(Token,2) = 'n=' THEN

	        Value := RIGHT_INT(Token, LEN_INT(Token) - 2);
		IF LEN_INT(Value) <= 4 THEN
	        	_Step.Name := Value;
		END_IF;

	    (* ---------------- INITIAL ---------------- *)
	    ELSIF LEFT_INT(Token,2) = 'i=' THEN

	        Value := RIGHT_INT(Token, LEN_INT(Token) - 2);
	        _Step.isInitial := STRING_TO_INT(Value) = 1;

	    (* ---------------- FINAL ---------------- *)
	    ELSIF LEFT_INT(Token,2) = 'f=' THEN

	        Value := RIGHT_INT(Token, LEN_INT(Token) - 2);
	        _Step.isFinal := STRING_TO_INT(Value) = 1;

	    END_IF;

	END_WHILE;
ELSIF (T = 2) THEN
	_Trans.Enabled := TRUE;
	_Trans.JoinKind := 1;
	_Trans.SplitKind := 0;
	_Trans.ConditionTime := d;

	_Trans.SourceCount := 0;
	_Trans.DestCount := 0;

	Work := Text;

	WHILE LEN_INT(Work) > 0 DO

	    Pos := FIND_INT(Work, '|');

	    IF Pos = -1 THEN
	        Token := Work;
	        Work := '';
	    ELSE
	        Token := LEFT_INT(Work, Pos - 1);
	        Work := RIGHT_INT(Work, LEN_INT(Work) - Pos);
	    END_IF;

	    (* ---------------- ID ---------------- *)
	    IF LEFT_INT(Token,3) = 'id=' THEN

	        Value := RIGHT_INT(Token, LEN_INT(Token) - 3);
	        _Trans.Id := STRING_TO_INT(Value);

	    (* ---------------- SOURCES ---------------- *)
	    ELSIF LEFT_INT(Token,2) = 's=' THEN

	        Value := RIGHT_INT(Token, LEN_INT(Token) - 2);

	        WorkList := Value;

	        WHILE LEN_INT(WorkList) > 0 DO

	            Pos := FIND_INT(WorkList, ',');

	            IF Pos = -1 THEN
	                Item := WorkList;
	                WorkList := '';
	            ELSE
	                Item := LEFT_INT(WorkList, Pos - 1);
	                WorkList := RIGHT_INT(WorkList, LEN_INT(WorkList) - Pos);
	            END_IF;

	            _Trans.Sources[_Trans.SourceCount] := STRING_TO_INT(Item);
	            _Trans.SourceCount := _Trans.SourceCount + 1;

	        END_WHILE;

	    (* ---------------- DESTINATIONS ---------------- *)
	    ELSIF LEFT_INT(Token,2) = 'd=' THEN

	        Value := RIGHT_INT(Token, LEN_INT(Token) - 2);

	        WorkList := Value;

	        WHILE LEN_INT(WorkList) > 0 DO

	            Pos := FIND_INT(WorkList, ',');

	            IF Pos = -1 THEN
	                Item := WorkList;
	                WorkList := '';
	            ELSE
	                Item := LEFT_INT(WorkList, Pos - 1);
	                WorkList := RIGHT_INT(WorkList, LEN_INT(WorkList) - Pos);
	            END_IF;
            
		    _Trans.Destinations[_Trans.DestCount] := STRING_TO_INT(Item);
	            _Trans.DestCount := _Trans.DestCount + 1;

	        END_WHILE;

	    (* ---------------- CONDITION ---------------- *)
	    ELSIF LEFT_INT(Token,2) = 'c=' THEN
		
	        IF LEN_INT(RIGHT_INT(Token, LEN_INT(Token) - 2)) <= 8 THEN
			_Trans.CondText := RIGHT_INT(Token, LEN_INT(Token) - 2);
		END_IF;

	    END_IF;

	END_WHILE;
ELSIF T = 3 THEN
	_Act.EnableCond := TRUE;
	_Act.Kind := 0;
	_Act.Delay := d;
	
	Work := Text;

	WHILE LEN_INT(Work) > 0 DO

	    Pos := FIND_INT(Work, '|');

	    IF Pos = -1 THEN
	        Token := Work;
	        Work := '';
	    ELSE
	        Token := LEFT_INT(Work, Pos - 1);
	        Work := RIGHT_INT(Work, LEN_INT(Work) - Pos);
	    END_IF;

	    (* ---------------- ID ---------------- *)
	    IF LEFT_INT(Token,3) = 'id=' THEN

	        Value := RIGHT_INT(Token, LEN_INT(Token) - 3);
	        _Act.Id := STRING_TO_INT(Value);

	    (* ---------------- NAME ---------------- *)
	    ELSIF LEFT_INT(Token,2) = 'n=' THEN

	        Value := RIGHT_INT(Token, LEN_INT(Token) - 2);
		IF LEN_INT(Value) <= 8 THEN
	        	_Act.Name := Value;
		END_IF;

	    (* ---------------- KIND ---------------- *)
	    ELSIF LEFT_INT(Token,2) = 'k=' THEN

	        Value := RIGHT_INT(Token, LEN_INT(Token) - 2);
	        _Act.Kind := STRING_TO_INT(Value);

	    (* ---------------- STEP ---------------- *)
	    ELSIF LEFT_INT(Token,2) = 's=' THEN

	        Value := RIGHT_INT(Token, LEN_INT(Token) - 2);
	        _Act.BoundStepId := STRING_TO_INT(Value);

	    END_IF;

	END_WHILE;
END_IF;
