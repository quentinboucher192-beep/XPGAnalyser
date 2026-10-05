Gc_DetoxalA.Runtime.GC_MAX_STEPS := 28;
Gc_DetoxalA.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_DetoxalA.Runtime.GC_MAX_ACTIONS := 28;
Gc_DetoxalA.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_DetoxalA.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_DetoxalA.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_DetoxalA[0]);
Builder(T:=1,Text:='id=1|n=X1', 						_Step:=Steps_DetoxalA[1]); 
Builder(T:=1,Text:='id=2|n=X2|f=1', 					_Step:=Steps_DetoxalA[2]); 
Builder(T:=1,Text:='id=3|n=X3', 						_Step:=Steps_DetoxalA[3]);

(* =========================
TRANSITIONS DETOX
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=DEBIT', 					_Trans:=Trans_DetoxalA[0]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalA[0]); 
Builder(T:=2,Text:='id=1|s=0|d=3|c=!DEBIT', 				_Trans:=Trans_DetoxalA[1]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalA[1]);
Builder(T:=2,Text:='id=2|s=1|d=2|c=PT1<=SP', 				_Trans:=Trans_DetoxalA[2]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalA[2]);
Builder(T:=2,Text:='id=3|s=1|d=3|c=PT1>SP', 				_Trans:=Trans_DetoxalA[3]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalA[3]);
Builder(T:=2,Text:='id=4|s=2|d=0|c=TRUE', 					_Trans:=Trans_DetoxalA[4]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalA[4]);
Builder(T:=2,Text:='id=5|s=3|d=0|c=IHM', 					_Trans:=Trans_DetoxalA[5]);
Builder(T:=2,d:=t#0s,							_Trans:=Trans_DetoxalA[5]);

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=OV3V4|k=4|s=1', 							_Act:=Acts_DetoxalA[0]);
Builder(T:=3,d:=REAL_TO_TIME(INT_TO_REAL(config_utilisee.detoxal.DLDETOX)*1000.0), 		_Act:=Acts_DetoxalA[0]);
Builder(T:=3,Text:='id=1|n=FV3|k=1|s=2', 							_Act:=Acts_DetoxalA[1]);
Builder(T:=3,d:=t#0s,								 	_Act:=Acts_DetoxalA[1]);
Builder(T:=3,Text:='id=2|n=FV3V4|k=1|s=3', 							_Act:=Acts_DetoxalA[2]);
Builder(T:=3,d:=t#0s,								 	_Act:=Acts_DetoxalA[2]);  
	
Ctrl_DetoxalA.Cmd.ForceStepFromId := -1;
Ctrl_DetoxalA.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_DetoxalA[0].EnableCond := TRUE;
Acts_DetoxalA[1].EnableCond := TRUE;
Acts_DetoxalA[2].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_DetoxalA[0].Condition := armoires[0].entrees.detoxal;
Trans_DetoxalA[1].Condition := NOT(armoires[0].entrees.detoxal);     				
Trans_DetoxalA[2].Condition := Steps_DetoxalA[1].ActiveTime >= t#1s AND Acts_DetoxalA[0].Started AND Acts_DetoxalA[0].Finished AND armoires[0].ana.PT1.mes <= INT_TO_REAL(config_utilisee.detoxal.SPDETOX);
Trans_DetoxalA[3].Condition := Steps_DetoxalA[1].ActiveTime >= t#1s AND Acts_DetoxalA[0].Started AND Acts_DetoxalA[0].Finished AND armoires[0].ana.PT1.mes > INT_TO_REAL(config_utilisee.detoxal.SPDETOX);
Trans_DetoxalA[4].Condition := TRUE;
Trans_DetoxalA[5].Condition := chgt_valider[0];

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

IF Ctrl_DetoxalA.Cmd.ModeReq = 0 THEN Ctrl_DetoxalA.Cmd.ModeReq := 2; END_IF;
Ctrl_DetoxalA.Cmd.Enable := generalites.pres_detoxal and not(armoires[0].manu) and not(modification_majeure);

Gc_DetoxalA(
    Enable := Ctrl_DetoxalA.Cmd.Enable,
    ModeReq := Ctrl_DetoxalA.Cmd.ModeReq,
    FreezeReq := Ctrl_DetoxalA.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_DetoxalA.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_DetoxalA.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_DetoxalA.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_DetoxalA.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_DetoxalA.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_DetoxalA.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_DetoxalA.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_DetoxalA.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_DetoxalA.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_DetoxalA,
    Transitions := Trans_DetoxalA,
    Actions := Acts_DetoxalA
);

(* Retour statut *)
Ctrl_DetoxalA.Stat.Cycle := Gc_DetoxalA.Runtime.Cycle;
Ctrl_DetoxalA.Stat.Phase := Gc_DetoxalA.Runtime.Phase;
Ctrl_DetoxalA.Stat.Mode := Gc_DetoxalA.Runtime.Mode;
Ctrl_DetoxalA.Stat.ActiveStepCount := Gc_DetoxalA.Debug.ActiveStepCount;
Ctrl_DetoxalA.Stat.LastTransitionFiredId := Gc_DetoxalA.Debug.LastTransitionFiredId;
Ctrl_DetoxalA.Stat.ConfigFault := Gc_DetoxalA.Debug.ConfigFault;
Ctrl_DetoxalA.Stat.WatchdogDetected := Gc_DetoxalA.Debug.WatchdogDetected;
Ctrl_DetoxalA.Stat.NoActiveStepFault := Gc_DetoxalA.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_DetoxalA.Cmd.StepReq := FALSE;
Ctrl_DetoxalA.Cmd.InitReq := FALSE;
Ctrl_DetoxalA.Cmd.ResetInitialReq := FALSE;
Ctrl_DetoxalA.Cmd.ResetAllReq := FALSE;
Ctrl_DetoxalA.Cmd.ForceBackReq := FALSE;
Ctrl_DetoxalA.Cmd.ForceNextReq := FALSE;
