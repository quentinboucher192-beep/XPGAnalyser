Gc_HeliumAVB.Runtime.GC_MAX_STEPS := 28;
Gc_HeliumAVB.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_HeliumAVB.Runtime.GC_MAX_ACTIONS := 28;
Gc_HeliumAVB.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_HeliumAVB.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_HeliumAVB.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_HeliumAVB[0]);
Builder(T:=1,Text:='id=1|n=X1', 						_Step:=Steps_HeliumAVB[1]); 
Builder(T:=1,Text:='id=2|n=X2', 						_Step:=Steps_HeliumAVB[2]); 
Builder(T:=1,Text:='id=3|n=X3', 						_Step:=Steps_HeliumAVB[3]); 
Builder(T:=1,Text:='id=4|n=X4', 						_Step:=Steps_HeliumAVB[4]); 
Builder(T:=1,Text:='id=5|n=X5', 						_Step:=Steps_HeliumAVB[5]);
Builder(T:=1,Text:='id=6|n=X6|f=1', 					_Step:=Steps_HeliumAVB[6]);

(* =========================
TRANSITIONS CHGT
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=TRUE', 					_Trans:=Trans_HeliumAVB[0]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVB[0]);
Builder(T:=2,Text:='id=1|s=1|d=2|c=IHM', 					_Trans:=Trans_HeliumAVB[1]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVB[1]);
Builder(T:=2,Text:='id=2|s=2|d=3|c=IHM', 					_Trans:=Trans_HeliumAVB[2]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVB[2]);
Builder(T:=2,Text:='id=3|s=3|d=4|c=FPURGE', 				_Trans:=Trans_HeliumAVB[3]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVB[3]); 
Builder(T:=2,Text:='id=4|s=4|d=5|c=FPOMPAGE', 				_Trans:=Trans_HeliumAVB[4]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVB[4]);
Builder(T:=2,Text:='id=5|s=5|d=6|c=IHM', 					_Trans:=Trans_HeliumAVB[5]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVB[5]);
Builder(T:=2,Text:='id=6|s=6|d=0|c=TRUE', 					_Trans:=Trans_HeliumAVB[6]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVB[6]);  

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=FTTOV4VHE|k=0|s=1', 				_Act:=Acts_HeliumAVB[0]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVB[0]);
Builder(T:=3,Text:='id=1|n=FTT|k=0|s=2', 					_Act:=Acts_HeliumAVB[1]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVB[1]);
Builder(T:=3,Text:='id=2|n=IPURGE|k=1|s=3', 				_Act:=Acts_HeliumAVB[2]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVB[2]);
Builder(T:=3,Text:='id=3|n=RPURGE|k=5|s=3', 				_Act:=Acts_HeliumAVB[3]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVB[3]);
Builder(T:=3,Text:='id=4|n=IPOMPAGE|k=1|s=4', 				_Act:=Acts_HeliumAVB[4]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVB[4]);
Builder(T:=3,Text:='id=5|n=RPOMPAGE|k=5|s=4', 				_Act:=Acts_HeliumAVB[5]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVB[5]);

Ctrl_HeliumAVB.Cmd.ForceStepFromId := -1;
Ctrl_HeliumAVB.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_HeliumAVB[0].EnableCond := TRUE;
Acts_HeliumAVB[1].EnableCond := TRUE;
Acts_HeliumAVB[2].EnableCond := TRUE;
Acts_HeliumAVB[3].EnableCond := TRUE;
Acts_HeliumAVB[4].EnableCond := TRUE;
Acts_HeliumAVB[5].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_HeliumAVB[0].Condition  := TRUE;
Trans_HeliumAVB[1].Condition  := chgt_valider[0];
Trans_HeliumAVB[2].Condition  := chgt_valider[0];
Trans_HeliumAVB[3].Condition  := Steps_HeliumAVB[3].ActiveTime >= t#1s AND Gc_PurgeB.Finished;
Trans_HeliumAVB[4].Condition  := Steps_HeliumAVB[4].ActiveTime >= t#1s AND Gc_PompageB.Finished;
Trans_HeliumAVB[5].Condition  := chgt_valider[0];
Trans_HeliumAVB[6].Condition  := TRUE;

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

IF Ctrl_HeliumAVB.Cmd.ModeReq = 0 THEN Ctrl_HeliumAVB.Cmd.ModeReq := 2; END_IF;
Ctrl_HeliumAVB.Cmd.Enable := TRUE and not(armoires[1].manu);

Gc_HeliumAVB(
    Enable := Ctrl_HeliumAVB.Cmd.Enable,
    ModeReq := Ctrl_HeliumAVB.Cmd.ModeReq,
    FreezeReq := Ctrl_HeliumAVB.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_HeliumAVB.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_HeliumAVB.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_HeliumAVB.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_HeliumAVB.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_HeliumAVB.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_HeliumAVB.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_HeliumAVB.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_HeliumAVB.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_HeliumAVB.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_HeliumAVB,
    Transitions := Trans_HeliumAVB,
    Actions := Acts_HeliumAVB
);

(* Retour statut *)
Ctrl_HeliumAVB.Stat.Cycle := Gc_HeliumAVB.Runtime.Cycle;
Ctrl_HeliumAVB.Stat.Phase := Gc_HeliumAVB.Runtime.Phase;
Ctrl_HeliumAVB.Stat.Mode := Gc_HeliumAVB.Runtime.Mode;
Ctrl_HeliumAVB.Stat.ActiveStepCount := Gc_HeliumAVB.Debug.ActiveStepCount;
Ctrl_HeliumAVB.Stat.LastTransitionFiredId := Gc_HeliumAVB.Debug.LastTransitionFiredId;
Ctrl_HeliumAVB.Stat.ConfigFault := Gc_HeliumAVB.Debug.ConfigFault;
Ctrl_HeliumAVB.Stat.WatchdogDetected := Gc_HeliumAVB.Debug.WatchdogDetected;
Ctrl_HeliumAVB.Stat.NoActiveStepFault := Gc_HeliumAVB.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_HeliumAVB.Cmd.StepReq := FALSE;
Ctrl_HeliumAVB.Cmd.InitReq := FALSE;
Ctrl_HeliumAVB.Cmd.ResetInitialReq := FALSE;
Ctrl_HeliumAVB.Cmd.ResetAllReq := FALSE;
Ctrl_HeliumAVB.Cmd.ForceBackReq := FALSE;
Ctrl_HeliumAVB.Cmd.ForceNextReq := FALSE;
