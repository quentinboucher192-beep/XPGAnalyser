Gc_SurpressionA.Runtime.GC_MAX_STEPS := 28;
Gc_SurpressionA.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_SurpressionA.Runtime.GC_MAX_ACTIONS := 28;
Gc_SurpressionA.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_SurpressionA.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_SurpressionA.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_SurpA[0]);
Builder(T:=1,Text:='id=1|n=X1', 						_Step:=Steps_SurpA[1]); 
Builder(T:=1,Text:='id=2|n=X2', 						_Step:=Steps_SurpA[2]);
Builder(T:=1,Text:='id=3|n=X3', 						_Step:=Steps_SurpA[3]); 
Builder(T:=1,Text:='id=4|n=X4', 						_Step:=Steps_SurpA[4]);
Builder(T:=1,Text:='id=5|n=X5', 						_Step:=Steps_SurpA[5]); 
Builder(T:=1,Text:='id=6|n=X10', 						_Step:=Steps_SurpA[6]);

(* =========================
TRANSITIONS SURP
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=PT3<=SP3', 				_Trans:=Trans_SurpA[0]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpA[0]);
Builder(T:=2,Text:='id=1|s=1|d=2|c=ARMOIRE EN DEFAUT', 			_Trans:=Trans_SurpA[1]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpA[1]);
Builder(T:=2,Text:='id=7|s=1|d=6|c=ARMOIRE PRETE', 				_Trans:=Trans_SurpA[7]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpA[7]);
Builder(T:=2,Text:='id=2|s=2|d=3|c=PT3<=SP3B OU TIMEOUT', 			_Trans:=Trans_SurpA[2]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpA[2]);
Builder(T:=2,Text:='id=3|s=3|d=4|c=PT3<=SP3B', 				_Trans:=Trans_SurpA[3]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpA[3]);
Builder(T:=2,Text:='id=4|s=3|d=5|c=TIMEOUT', 				_Trans:=Trans_SurpA[4]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpA[4]);
Builder(T:=2,Text:='id=5|s=4|d=0|c=TRUE', 					_Trans:=Trans_SurpA[5]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpA[5]);
Builder(T:=2,Text:='id=6|s=5|d=0|c=TRUE', 					_Trans:=Trans_SurpA[6]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpA[6]);
Builder(T:=2,Text:='id=8|s=6|d=0|c=FIN BASCULEMENT', 			_Trans:=Trans_SurpA[8]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpA[8]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=ATTENTE|k=0|s=0', 						_Act:=Acts_SurpA[0]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpA[0]);
Builder(T:=3,Text:='id=1|n=ATTENTE|k=0|s=1', 						_Act:=Acts_SurpA[1]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpA[1]);
Builder(T:=3,Text:='id=2|n=SET_TIMEOUT|k=2|s=2', 						_Act:=Acts_SurpA[2]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.surpression.DLSURP3)*1000.0), 	_Act:=Acts_SurpA[2]);
Builder(T:=3,Text:='id=3|n=FV5|k=0|s=2', 							_Act:=Acts_SurpA[3]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpA[3]);
Builder(T:=3,Text:='id=4|n=OV7|k=11|s=2', 							_Act:=Acts_SurpA[4]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.surpression.DLSURP1)*1000.0), 	_Act:=Acts_SurpA[4]);
Builder(T:=3,Text:='id=5|n=OVS|k=11|s=2', 							_Act:=Acts_SurpA[5]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.surpression.DLSURP2)*1000.0), 	_Act:=Acts_SurpA[5]);
Builder(T:=3,Text:='id=6|n=DEFAUT|k=5|s=3', 						_Act:=Acts_SurpA[6]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpA[6]);
Builder(T:=3,Text:='id=7|n=PAS DE DEFAUT|k=5|s=3', 						_Act:=Acts_SurpA[7]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpA[7]);
Builder(T:=3,Text:='id=8|n=DEFORCAGE VS|k=11|s=4', 						_Act:=Acts_SurpA[8]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.surpression.DLSURP4)*1000.0), 	_Act:=Acts_SurpA[8]);
Builder(T:=3,Text:='id=9|n=DEFORCAGE V7|k=11|s=4', 						_Act:=Acts_SurpA[9]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.surpression.DLSURP5)*1000.0), 	_Act:=Acts_SurpA[9]);
Builder(T:=3,Text:='id=10|n=DEFORCAGE V5|k=5|s=4', 						_Act:=Acts_SurpA[10]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpA[10]);
Builder(T:=3,Text:='id=11|n=ATTENTE|k=0|s=5', 						_Act:=Acts_SurpA[11]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpA[11]);
Builder(T:=3,Text:='id=12|n=BASCULEMENT|k=1|s=6', 						_Act:=Acts_SurpA[12]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpA[12]);

Ctrl_SurpA.Cmd.ForceStepFromId := -1;
Ctrl_SurpA.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_SurpA[0].EnableCond := TRUE;
Acts_SurpA[1].EnableCond := TRUE;
Acts_SurpA[2].EnableCond := not(timeout_atteintA);
Acts_SurpA[3].EnableCond := TRUE;
Acts_SurpA[4].EnableCond := armoires[0].ana.PT3.mes >= INT_TO_REAL(config_utilisee.surpression.SP3H);
Acts_SurpA[5].EnableCond := Acts_SurpA[4].Started AND Acts_SurpA[4].Finished;
Acts_SurpA[6].EnableCond := timeout_atteintA;
Acts_SurpA[7].EnableCond := armoires[0].ana.PT3.mes <= INT_TO_REAL(config_utilisee.surpression.SP3B);
Acts_SurpA[8].EnableCond := TRUE;
Acts_SurpA[9].EnableCond := Acts_SurpA[8].Started AND Acts_SurpA[8].Finished;
Acts_SurpA[10].EnableCond := Acts_SurpA[9].Started AND Acts_SurpA[9].Finished;
Acts_SurpA[11].EnableCond := TRUE;
Acts_SurpA[12].EnableCond := armoires[1].prete;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_SurpA[0].Condition  := armoires[0].prete and armoires[0].ana.PT3.mes >= INT_TO_REAL(config_utilisee.surpression.SP3);
Trans_SurpA[1].Condition  := not(armoires[1].prete) or armoires[1].manu;     				
Trans_SurpA[7].Condition  := armoires[1].prete and not(armoires[1].manu);
Trans_SurpA[2].Condition  := Steps_SurpA[2].ActiveTime >= t#1s and ((armoires[0].ana.PT3.mes <= INT_TO_REAL(config_utilisee.surpression.SP3B) or timeout_atteintA) and (not(Acts_SurpA[4].Started) or (Acts_SurpA[4].Started and Acts_SurpA[4].Finished and Acts_SurpA[5].Finished)));
Trans_SurpA[3].Condition  := armoires[0].ana.PT3.mes <= INT_TO_REAL(config_utilisee.surpression.SP3B);
Trans_SurpA[4].Condition  := timeout_atteintA;
Trans_SurpA[5].Condition  := Steps_SurpA[4].ActiveTime >= t#1s and (not(Acts_SurpA[8].Started) or (Acts_SurpA[8].Started and Acts_SurpA[8].Finished and Acts_SurpA[9].Finished));
Trans_SurpA[6].Condition  := Steps_SurpA[5].ActiveTime >= t#1s and not(armoires[0].en_cours_de_basculement);
Trans_SurpA[8].Condition  := Steps_SurpA[6].ActiveTime >= t#1s and not(armoires[0].en_cours_de_basculement);

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

Ctrl_SurpA.Cmd.Enable := configuree and not(armoires[0].manu) and not(armoires[0].en_cours_de_chgt) and not(armoires[0].retour_sur_chgt) and not(armoires[0].retour_sur_chgt_ap_decl);
Ctrl_SurpA.Cmd.ModeReq := 1;
IF NOT(Ctrl_SurpA.Cmd.Enable) THEN armoires[0].CycleSurpressionBit := FALSE; END_IF;

Gc_SurpressionA(
    Enable := Ctrl_SurpA.Cmd.Enable,
    ModeReq := Ctrl_SurpA.Cmd.ModeReq,
    FreezeReq := Ctrl_SurpA.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_SurpA.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_SurpA.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_SurpA.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_SurpA.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_SurpA.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_SurpA.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_SurpA.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_SurpA.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_SurpA.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_SurpA,
    Transitions := Trans_SurpA,
    Actions := Acts_SurpA
);

(* Retour statut *)
Ctrl_SurpA.Stat.Cycle := Gc_SurpressionA.Runtime.Cycle;
Ctrl_SurpA.Stat.Phase := Gc_SurpressionA.Runtime.Phase;
Ctrl_SurpA.Stat.Mode := Gc_SurpressionA.Runtime.Mode;
Ctrl_SurpA.Stat.ActiveStepCount := Gc_SurpressionA.Debug.ActiveStepCount;
Ctrl_SurpA.Stat.LastTransitionFiredId := Gc_SurpressionA.Debug.LastTransitionFiredId;
Ctrl_SurpA.Stat.ConfigFault := Gc_SurpressionA.Debug.ConfigFault;
Ctrl_SurpA.Stat.WatchdogDetected := Gc_SurpressionA.Debug.WatchdogDetected;
Ctrl_SurpA.Stat.NoActiveStepFault := Gc_SurpressionA.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_SurpA.Cmd.StepReq := FALSE;
Ctrl_SurpA.Cmd.InitReq := FALSE;
Ctrl_SurpA.Cmd.ResetInitialReq := FALSE;
Ctrl_SurpA.Cmd.ResetAllReq := FALSE;
Ctrl_SurpA.Cmd.ForceBackReq := FALSE;
Ctrl_SurpA.Cmd.ForceNextReq := FALSE;
