Gc_SurpressionB.Runtime.GC_MAX_STEPS := 28;
Gc_SurpressionB.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_SurpressionB.Runtime.GC_MAX_ACTIONS := 28;
Gc_SurpressionB.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_SurpressionB.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_SurpressionB.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_SurpB[0]);
Builder(T:=1,Text:='id=1|n=X1', 						_Step:=Steps_SurpB[1]); 
Builder(T:=1,Text:='id=2|n=X2', 						_Step:=Steps_SurpB[2]);
Builder(T:=1,Text:='id=3|n=X3', 						_Step:=Steps_SurpB[3]); 
Builder(T:=1,Text:='id=4|n=X4', 						_Step:=Steps_SurpB[4]);
Builder(T:=1,Text:='id=5|n=X5', 						_Step:=Steps_SurpB[5]); 
Builder(T:=1,Text:='id=6|n=X10', 						_Step:=Steps_SurpB[6]);

(* =========================
TRANSITIONS SURP
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=PT3<=SP3', 				_Trans:=Trans_SurpB[0]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpB[0]);
Builder(T:=2,Text:='id=1|s=1|d=2|c=ARMOIRE EN DEFAUT', 			_Trans:=Trans_SurpB[1]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpB[1]);
Builder(T:=2,Text:='id=7|s=1|d=6|c=ARMOIRE PRETE', 				_Trans:=Trans_SurpB[7]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpB[7]);
Builder(T:=2,Text:='id=2|s=2|d=3|c=PT3<=SP3B OU TIMEOUT', 			_Trans:=Trans_SurpB[2]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpB[2]);
Builder(T:=2,Text:='id=3|s=3|d=4|c=PT3<=SP3B', 				_Trans:=Trans_SurpB[3]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpB[3]);
Builder(T:=2,Text:='id=4|s=3|d=5|c=TIMEOUT', 				_Trans:=Trans_SurpB[4]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpB[4]);
Builder(T:=2,Text:='id=5|s=4|d=0|c=TRUE', 					_Trans:=Trans_SurpB[5]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpB[5]);
Builder(T:=2,Text:='id=6|s=5|d=0|c=TRUE', 					_Trans:=Trans_SurpB[6]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpB[6]);
Builder(T:=2,Text:='id=8|s=6|d=0|c=FIN BASCULEMENT', 			_Trans:=Trans_SurpB[8]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_SurpB[8]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=ATTENTE|k=0|s=0', 						_Act:=Acts_SurpB[0]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpB[0]);
Builder(T:=3,Text:='id=1|n=ATTENTE|k=0|s=1', 						_Act:=Acts_SurpB[1]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpB[1]);
Builder(T:=3,Text:='id=2|n=SET_TIMEOUT|k=2|s=2', 						_Act:=Acts_SurpB[2]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.surpression.DLSURP3)*1000.0), 	_Act:=Acts_SurpB[2]);
Builder(T:=3,Text:='id=3|n=FV5|k=0|s=2', 							_Act:=Acts_SurpB[3]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpB[3]);
Builder(T:=3,Text:='id=4|n=OV7|k=11|s=2', 							_Act:=Acts_SurpB[4]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.surpression.DLSURP1)*1000.0), 	_Act:=Acts_SurpB[4]);
Builder(T:=3,Text:='id=5|n=OVS|k=11|s=2', 							_Act:=Acts_SurpB[5]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.surpression.DLSURP2)*1000.0), 	_Act:=Acts_SurpB[5]);
Builder(T:=3,Text:='id=6|n=DEFAUT|k=5|s=3', 						_Act:=Acts_SurpB[6]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpB[6]);
Builder(T:=3,Text:='id=7|n=PAS DE DEFAUT|k=5|s=3', 						_Act:=Acts_SurpB[7]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpB[7]);
Builder(T:=3,Text:='id=8|n=DEFORCAGE VS|k=11|s=4', 						_Act:=Acts_SurpB[8]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.surpression.DLSURP4)*1000.0), 	_Act:=Acts_SurpB[8]);
Builder(T:=3,Text:='id=9|n=DEFORCAGE V7|k=11|s=4', 						_Act:=Acts_SurpB[9]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.surpression.DLSURP5)*1000.0), 	_Act:=Acts_SurpB[9]);
Builder(T:=3,Text:='id=10|n=DEFORCAGE V5|k=5|s=4', 						_Act:=Acts_SurpB[10]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpB[10]);
Builder(T:=3,Text:='id=11|n=ATTENTE|k=0|s=5', 						_Act:=Acts_SurpB[11]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpB[11]);
Builder(T:=3,Text:='id=12|n=BASCULEMENT|k=1|s=6', 						_Act:=Acts_SurpB[12]); 
Builder(T:=3,d:=t#0s, 									_Act:=Acts_SurpB[12]);

Ctrl_SurpB.Cmd.ForceStepFromId := -1;
Ctrl_SurpB.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_SurpB[0].EnableCond := TRUE;
Acts_SurpB[1].EnableCond := TRUE;
Acts_SurpB[2].EnableCond := not(timeout_atteintB);
Acts_SurpB[3].EnableCond := TRUE;
Acts_SurpB[4].EnableCond := armoires[1].ana.PT3.mes >= INT_TO_REAL(config_utilisee.surpression.SP3H);
Acts_SurpB[5].EnableCond := Acts_SurpB[4].Started AND Acts_SurpB[4].Finished;
Acts_SurpB[6].EnableCond := timeout_atteintB;
Acts_SurpB[7].EnableCond := armoires[1].ana.PT3.mes <= INT_TO_REAL(config_utilisee.surpression.SP3B);
Acts_SurpB[8].EnableCond := TRUE;
Acts_SurpB[9].EnableCond := Acts_SurpB[8].Started AND Acts_SurpB[8].Finished;
Acts_SurpB[10].EnableCond := Acts_SurpB[9].Started AND Acts_SurpB[9].Finished;
Acts_SurpB[11].EnableCond := TRUE;
Acts_SurpB[12].EnableCond := armoires[0].prete;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_SurpB[0].Condition  := armoires[1].prete and armoires[1].ana.PT3.mes >= INT_TO_REAL(config_utilisee.surpression.SP3);
Trans_SurpB[1].Condition  := not(armoires[0].prete) or armoires[0].manu;     				
Trans_SurpB[7].Condition  := armoires[0].prete and not(armoires[0].manu);
Trans_SurpB[2].Condition  := Steps_SurpB[2].ActiveTime >= t#1s and ((armoires[1].ana.PT3.mes <= INT_TO_REAL(config_utilisee.surpression.SP3B) or timeout_atteintB) and (not(Acts_SurpB[4].Started) or (Acts_SurpB[4].Started and Acts_SurpB[4].Finished and Acts_SurpB[5].Finished)));
Trans_SurpB[3].Condition  := armoires[1].ana.PT3.mes <= INT_TO_REAL(config_utilisee.surpression.SP3B);
Trans_SurpB[4].Condition  := timeout_atteintB;
Trans_SurpB[5].Condition  := Steps_SurpB[4].ActiveTime >= t#1s and (not(Acts_SurpB[8].Started) or (Acts_SurpB[8].Started and Acts_SurpB[8].Finished and Acts_SurpB[9].Finished));
Trans_SurpB[6].Condition  := Steps_SurpB[5].ActiveTime >= t#1s and not(armoires[1].en_cours_de_basculement);
Trans_SurpB[8].Condition  := Steps_SurpB[6].ActiveTime >= t#1s and not(armoires[1].en_cours_de_basculement);

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

Ctrl_SurpB.Cmd.Enable := configuree and not(armoires[1].manu) and not(armoires[1].en_cours_de_chgt) and not(armoires[1].retour_sur_chgt) and not(armoires[1].retour_sur_chgt_ap_decl);
Ctrl_SurpB.Cmd.ModeReq := 1;
IF NOT(Ctrl_SurpB.Cmd.Enable) THEN armoires[1].CycleSurpressionBit := FALSE; END_IF;

Gc_SurpressionB(
    Enable := Ctrl_SurpB.Cmd.Enable,
    ModeReq := Ctrl_SurpB.Cmd.ModeReq,
    FreezeReq := Ctrl_SurpB.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_SurpB.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_SurpB.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_SurpB.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_SurpB.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_SurpB.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_SurpB.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_SurpB.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_SurpB.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_SurpB.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_SurpB,
    Transitions := Trans_SurpB,
    Actions := Acts_SurpB
);

(* Retour statut *)
Ctrl_SurpB.Stat.Cycle := Gc_SurpressionB.Runtime.Cycle;
Ctrl_SurpB.Stat.Phase := Gc_SurpressionB.Runtime.Phase;
Ctrl_SurpB.Stat.Mode := Gc_SurpressionB.Runtime.Mode;
Ctrl_SurpB.Stat.ActiveStepCount := Gc_SurpressionB.Debug.ActiveStepCount;
Ctrl_SurpB.Stat.LastTransitionFiredId := Gc_SurpressionB.Debug.LastTransitionFiredId;
Ctrl_SurpB.Stat.ConfigFault := Gc_SurpressionB.Debug.ConfigFault;
Ctrl_SurpB.Stat.WatchdogDetected := Gc_SurpressionB.Debug.WatchdogDetected;
Ctrl_SurpB.Stat.NoActiveStepFault := Gc_SurpressionB.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_SurpB.Cmd.StepReq := FALSE;
Ctrl_SurpB.Cmd.InitReq := FALSE;
Ctrl_SurpB.Cmd.ResetInitialReq := FALSE;
Ctrl_SurpB.Cmd.ResetAllReq := FALSE;
Ctrl_SurpB.Cmd.ForceBackReq := FALSE;
Ctrl_SurpB.Cmd.ForceNextReq := FALSE;
