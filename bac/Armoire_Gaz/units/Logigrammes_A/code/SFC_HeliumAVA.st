Gc_HeliumAVA.Runtime.GC_MAX_STEPS := 28;
Gc_HeliumAVA.Runtime.GC_MAX_TRANSITIONS := 28;
Gc_HeliumAVA.Runtime.GC_MAX_ACTIONS := 28;
Gc_HeliumAVA.Runtime.GC_MAX_SRC_PER_TRANS := 3;
Gc_HeliumAVA.Runtime.GC_MAX_DST_PER_TRANS := 3;
Gc_HeliumAVA.Runtime.GC_MAX_HIST := 8;

(* =========================
STEPS
========================= *)

Builder(T:=1,Text:='id=0|n=X0|i=1', 					_Step:=Steps_HeliumAVA[0]);
Builder(T:=1,Text:='id=1|n=X1', 						_Step:=Steps_HeliumAVA[1]); 
Builder(T:=1,Text:='id=2|n=X2', 						_Step:=Steps_HeliumAVA[2]); 
Builder(T:=1,Text:='id=3|n=X3', 						_Step:=Steps_HeliumAVA[3]); 
Builder(T:=1,Text:='id=4|n=X4', 						_Step:=Steps_HeliumAVA[4]); 
Builder(T:=1,Text:='id=5|n=X5', 						_Step:=Steps_HeliumAVA[5]);
Builder(T:=1,Text:='id=6|n=X6|f=1', 					_Step:=Steps_HeliumAVA[6]);

(* =========================
TRANSITIONS CHGT
========================= *)

Builder(T:=2,Text:='id=0|s=0|d=1|c=TRUE', 					_Trans:=Trans_HeliumAVA[0]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVA[0]);
Builder(T:=2,Text:='id=1|s=1|d=2|c=IHM', 					_Trans:=Trans_HeliumAVA[1]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVA[1]);
Builder(T:=2,Text:='id=2|s=2|d=3|c=IHM', 					_Trans:=Trans_HeliumAVA[2]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVA[2]);
Builder(T:=2,Text:='id=3|s=3|d=4|c=FPURGE', 				_Trans:=Trans_HeliumAVA[3]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVA[3]); 
Builder(T:=2,Text:='id=4|s=4|d=5|c=FPOMPAGE', 				_Trans:=Trans_HeliumAVA[4]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVA[4]);
Builder(T:=2,Text:='id=5|s=5|d=6|c=IHM', 					_Trans:=Trans_HeliumAVA[5]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVA[5]);
Builder(T:=2,Text:='id=6|s=6|d=0|c=TRUE', 					_Trans:=Trans_HeliumAVA[6]); 
Builder(T:=2,d:=t#0s, 							_Trans:=Trans_HeliumAVA[6]);  

(* =========================
ACTIONS
========================= *)

Builder(T:=3,Text:='id=0|n=FTTOV4VHE|k=0|s=1', 				_Act:=Acts_HeliumAVA[0]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVA[0]);
Builder(T:=3,Text:='id=1|n=FTT|k=0|s=2', 					_Act:=Acts_HeliumAVA[1]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVA[1]);
Builder(T:=3,Text:='id=2|n=IPURGE|k=1|s=3', 				_Act:=Acts_HeliumAVA[2]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVA[2]);
Builder(T:=3,Text:='id=3|n=RPURGE|k=5|s=3', 				_Act:=Acts_HeliumAVA[3]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVA[3]);
Builder(T:=3,Text:='id=4|n=IPOMPAGE|k=1|s=4', 				_Act:=Acts_HeliumAVA[4]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVA[4]);
Builder(T:=3,Text:='id=5|n=RPOMPAGE|k=5|s=4', 				_Act:=Acts_HeliumAVA[5]); 
Builder(T:=3,d:=t#0s, 							_Act:=Acts_HeliumAVA[5]);

Ctrl_HeliumAVA.Cmd.ForceStepFromId := -1;
Ctrl_HeliumAVA.Cmd.ForceStepToId := -1;

(* =========================================================
   CONDITIONS ACTIONS
   ========================================================= *)

Acts_HeliumAVA[0].EnableCond := TRUE;
Acts_HeliumAVA[1].EnableCond := TRUE;
Acts_HeliumAVA[2].EnableCond := TRUE;
Acts_HeliumAVA[3].EnableCond := TRUE;
Acts_HeliumAVA[4].EnableCond := TRUE;
Acts_HeliumAVA[5].EnableCond := TRUE;

(* =========================================================
   CONDITIONS TRANSITIONS
   ========================================================= *)

Trans_HeliumAVA[0].Condition  := TRUE;
Trans_HeliumAVA[1].Condition  := chgt_valider[0];
Trans_HeliumAVA[2].Condition  := chgt_valider[0];
Trans_HeliumAVA[3].Condition  := Steps_HeliumAVA[3].ActiveTime >= t#1s AND Gc_PurgeA.Finished;
Trans_HeliumAVA[4].Condition  := Steps_HeliumAVA[4].ActiveTime >= t#1s AND Gc_PompageA.Finished;
Trans_HeliumAVA[5].Condition  := chgt_valider[0];
Trans_HeliumAVA[6].Condition  := TRUE;

(* =========================================================
   EXECUTION MOTEUR
   ========================================================= *)

IF Ctrl_HeliumAVA.Cmd.ModeReq = 0 THEN Ctrl_HeliumAVA.Cmd.ModeReq := 2; END_IF;
Ctrl_HeliumAVA.Cmd.Enable := TRUE and not(armoires[0].manu) and not(modification_majeure);

Gc_HeliumAVA(
    Enable := Ctrl_HeliumAVA.Cmd.Enable,
    ModeReq := Ctrl_HeliumAVA.Cmd.ModeReq,
    FreezeReq := Ctrl_HeliumAVA.Cmd.FreezeReq OR FALSE,
    PauseReq := Ctrl_HeliumAVA.Cmd.PauseReq OR FALSE,
    StepReq := Ctrl_HeliumAVA.Cmd.StepReq OR FALSE,
    InitReq := Ctrl_HeliumAVA.Cmd.InitReq OR FALSE,
    ResetInitialReq := Ctrl_HeliumAVA.Cmd.ResetInitialReq OR FALSE,
    ResetAllReq := Ctrl_HeliumAVA.Cmd.ResetAllReq OR FALSE,
    ForceBackReq := Ctrl_HeliumAVA.Cmd.ForceBackReq OR FALSE,
    ForceNextReq := Ctrl_HeliumAVA.Cmd.ForceNextReq OR FALSE,
    ForceStepFromId := Ctrl_HeliumAVA.Cmd.ForceStepFromId,
    ForceStepToId := Ctrl_HeliumAVA.Cmd.ForceStepToId,
    TsNow := TsSys,
    NtpcNow := NtpcNow,
    Steps := Steps_HeliumAVA,
    Transitions := Trans_HeliumAVA,
    Actions := Acts_HeliumAVA
);

(* Retour statut *)
Ctrl_HeliumAVA.Stat.Cycle := Gc_HeliumAVA.Runtime.Cycle;
Ctrl_HeliumAVA.Stat.Phase := Gc_HeliumAVA.Runtime.Phase;
Ctrl_HeliumAVA.Stat.Mode := Gc_HeliumAVA.Runtime.Mode;
Ctrl_HeliumAVA.Stat.ActiveStepCount := Gc_HeliumAVA.Debug.ActiveStepCount;
Ctrl_HeliumAVA.Stat.LastTransitionFiredId := Gc_HeliumAVA.Debug.LastTransitionFiredId;
Ctrl_HeliumAVA.Stat.ConfigFault := Gc_HeliumAVA.Debug.ConfigFault;
Ctrl_HeliumAVA.Stat.WatchdogDetected := Gc_HeliumAVA.Debug.WatchdogDetected;
Ctrl_HeliumAVA.Stat.NoActiveStepFault := Gc_HeliumAVA.Debug.NoActiveStepFault;

(* Reset des commandes impulsionnelles après l'appel *)
Ctrl_HeliumAVA.Cmd.StepReq := FALSE;
Ctrl_HeliumAVA.Cmd.InitReq := FALSE;
Ctrl_HeliumAVA.Cmd.ResetInitialReq := FALSE;
Ctrl_HeliumAVA.Cmd.ResetAllReq := FALSE;
Ctrl_HeliumAVA.Cmd.ForceBackReq := FALSE;
Ctrl_HeliumAVA.Cmd.ForceNextReq := FALSE;
