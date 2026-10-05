Gc_DetoxalB.Runtime.GC_MAX_STEPS := 28;
Gc_DetoxalB.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_DetoxalB.Runtime.GC_MAX_ACTIONS := 28;
Gc_DetoxalB.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_DetoxalB.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_DetoxalB.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_DetoxalB[0]);
Builder(T:=1,Text:='id=1|n=X1', 						_Step:=Steps_DetoxalB[1]); 
Builder(T:=1,Text:='id=2|n=X2|f=1', 					_Step:=Steps_DetoxalB[2]); 
Builder(T:=1,Text:='id=3|n=X3', 						_Step:=Steps_DetoxalB[3]);

(* =========================
TRANSITIONS DETOX
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=DEBIT', 					_Trans:=Trans_DetoxalB[0]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalB[0]); 
Builder(T:=2,Text:='id=1|s=0|d=3|c=!DEBIT', 				_Trans:=Trans_DetoxalB[1]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalB[1]);
Builder(T:=2,Text:='id=2|s=1|d=2|c=PT1<=SP', 				_Trans:=Trans_DetoxalB[2]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalB[2]);
Builder(T:=2,Text:='id=3|s=1|d=3|c=PT1>SP', 				_Trans:=Trans_DetoxalB[3]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalB[3]);
Builder(T:=2,Text:='id=4|s=2|d=0|c=TRUE', 					_Trans:=Trans_DetoxalB[4]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalB[4]);
Builder(T:=2,Text:='id=5|s=3|d=0|c=IHM', 					_Trans:=Trans_DetoxalB[5]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalB[5]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=OV3V4|k=4|s=1', 							_Act:=Acts_DetoxalB[0]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.detoxal.DLDETOX)*1000.0), 		_Act:=Acts_DetoxalB[0]);
Builder(T:=3,Text:='id=1|n=FV3|k=1|s=2', 							_Act:=Acts_DetoxalB[1]);
Builder(T:=3,d:=t#0s,								 	_Act:=Acts_DetoxalB[1]);
Builder(T:=3,Text:='id=2|n=FV3V4|k=1|s=3', 							_Act:=Acts_DetoxalB[2]);
Builder(T:=3,d:=t#0s,								 	_Act:=Acts_DetoxalB[2]);  
	
Ctrl_DetoxalB.Cmd.ForceStepFromId := -1;
Ctrl_DetoxalB.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_DetoxalB[0].EnableCond := TRUE;
Acts_DetoxalB[1].EnableCond := TRUE;
Acts_DetoxalB[2].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_DetoxalB[0].Condition := armoires[1].entrees.detoxal;
Trans_DetoxalB[1].Condition := NOT(armoires[1].entrees.detoxal);     				
Trans_DetoxalB[2].Condition := Steps_DetoxalB[1].ActiveTime >= t#1s AND Acts_DetoxalB[0].Started AND Acts_DetoxalB[0].Finished AND armoires[1].ana.PT1.mes <= INT_TO_REAL(config_utilisee.detoxal.SPDETOX);
Trans_DetoxalB[3].Condition := Steps_DetoxalB[1].ActiveTime >= t#1s AND Acts_DetoxalB[0].Started AND Acts_DetoxalB[0].Finished AND armoires[1].ana.PT1.mes > INT_TO_REAL(config_utilisee.detoxal.SPDETOX);
Trans_DetoxalB[4].Condition := TRUE;
Trans_DetoxalB[5].Condition := chgt_valider[0];

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

IF Ctrl_DetoxalB.Cmd.ModeReq = 0 THEN Ctrl_DetoxalB.Cmd.ModeReq := 2; END_IF;
Ctrl_DetoxalB.Cmd.Enable := generalites.pres_detoxal and not(armoires[1].manu);

Gc_DetoxalB(
    Enable := Ctrl_DetoxalB.Cmd.Enable,
    ModeReq := Ctrl_DetoxalB.Cmd.ModeReq,
    FreezeReq := Ctrl_DetoxalB.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_DetoxalB.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_DetoxalB.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_DetoxalB.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_DetoxalB.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_DetoxalB.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_DetoxalB.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_DetoxalB.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_DetoxalB.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_DetoxalB.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_DetoxalB,
    Transitions := Trans_DetoxalB,
    Actions := Acts_DetoxalB
);

(* Retour statut *)
Ctrl_DetoxalB.Stat.Cycle := Gc_DetoxalB.Runtime.Cycle;
Ctrl_DetoxalB.Stat.Phase := Gc_DetoxalB.Runtime.Phase;
Ctrl_DetoxalB.Stat.Mode := Gc_DetoxalB.Runtime.Mode;
Ctrl_DetoxalB.Stat.ActiveStepCount := Gc_DetoxalB.Debug.ActiveStepCount;
Ctrl_DetoxalB.Stat.LastTransitionFiredId := Gc_DetoxalB.Debug.LastTransitionFiredId;
Ctrl_DetoxalB.Stat.ConfigFault := Gc_DetoxalB.Debug.ConfigFault;
Ctrl_DetoxalB.Stat.WatchdogDetected := Gc_DetoxalB.Debug.WatchdogDetected;
Ctrl_DetoxalB.Stat.NoActiveStepFault := Gc_DetoxalB.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_DetoxalB.Cmd.StepReq := FALSE;
Ctrl_DetoxalB.Cmd.InitReq := FALSE;
Ctrl_DetoxalB.Cmd.ResetInitialReq := FALSE;
Ctrl_DetoxalB.Cmd.ResetAllReq := FALSE;
Ctrl_DetoxalB.Cmd.ForceBackReq := FALSE;
Ctrl_DetoxalB.Cmd.ForceNextReq := FALSE;
